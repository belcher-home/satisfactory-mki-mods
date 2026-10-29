#include "MKIOrphanSupports.h"

#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildablePassthroughBase.h"
#include "Buildables/FGBuildablePipeBase.h"
#include "Buildables/FGBuildablePoleStackable.h"
#include "Buildables/FGBuildableWallPassthrough.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunDismantle.h"
#include "FGBuildableSubsystem.h"
#include "FGCharacterPlayer.h"
#include "FGConnectionComponent.h"
#include "FGDismantleInterface.h"
#include "FGFactoryConnectionComponent.h"
#include "FGOutlineComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGPipeConnectionComponentHyper.h"
#include "MKITools.h"
#include "MKIToolsConfiguration.h"

// A named namespace: the unity build joins this file with others whose anonymous namespaces may share names.
namespace MKIOrphanSupportsPrivate
{
	/** A belt or pipe end rests on a support when the support's snap point lies this close (as Area mode joins them). */
	constexpr double JoinCm = 20.0;
	/** Supports are looked for this far around the ends taken (a wall mount's snap point is 2 m out from it, a stacked one 3.75 m up). */
	constexpr double SupportReachCm = 1000.0;
	/** Other belts and pipes are looked for this far around: an actor sits at one end, and belts and pipes run up to 56 m. */
	constexpr double LineReachCm = 6000.0;
	/** A stackable support with another one on top within this of its stack height carries that one. */
	constexpr double StackSlackCm = 100.0;

	/** Which kind of line a connection belongs to: a support holds only its own kind (a pipe never rests on a belt pole). */
	enum class EFamily : uint8 { None, Conveyor, Fluid, Hyper };

	struct FEnd
	{
		FVector Location;
		EFamily Family;
	};

	EFamily FamilyOf(const UFGConnectionComponent* Connection)
	{
		if (Connection->IsA<UFGFactoryConnectionComponent>()) return EFamily::Conveyor;
		if (Connection->IsA<UFGPipeConnectionComponentHyper>()) return EFamily::Hyper;
		if (Connection->IsA<UFGPipeConnectionComponent>()) return EFamily::Fluid;
		return EFamily::None;
	}

	bool IsSnapOnly(const UFGConnectionComponent* Connection)
	{
		if (const UFGFactoryConnectionComponent* Factory = Cast<UFGFactoryConnectionComponent>(Connection)) return Factory->GetDirection() == EFactoryConnectionDirection::FCD_SNAP_ONLY;
		if (const UFGPipeConnectionComponentBase* Pipe = Cast<UFGPipeConnectionComponentBase>(Connection)) return Pipe->GetPipeConnectionType() == EPipeConnectionType::PCT_SNAP_ONLY;
		return false;
	}

	/** Belts, conveyor lifts, pipes and hypertubes. */
	bool IsLine(const AActor* Actor)
	{
		return Actor->IsA<AFGBuildableConveyorBase>() || Actor->IsA<AFGBuildablePipeBase>();
	}

	bool IsGone(const AFGBuildable* Buildable)
	{
		return !IsValid(Buildable) || Buildable->GetIsLightweightTemporary() || Buildable->IsAboutToBeDismantled() || Buildable->GetIsDismantled();
	}

	/** A line's two ends (its connections that carry items, fluid or pioneers). */
	void AddLineEnds(const AActor* Line, TArray<FEnd>& OutEnds)
	{
		TInlineComponentArray<UFGConnectionComponent*> Connections(Line);
		for (const UFGConnectionComponent* Connection : Connections)
		{
			const EFamily Family = Connection ? FamilyOf(Connection) : EFamily::None;
			if (Family != EFamily::None && !IsSnapOnly(Connection)) OutEnds.Add({ Connection->GetComponentLocation(), Family });
		}
	}

	/**
	 * A support's snap points: a building whose connections are all snap-only belt, pipe or hypertube points (poles, wall and
	 * ceiling mounts, pipe and hypertube supports). Wall and floor holes (also snap points) and anything with a working or
	 * power connection give none.
	 */
	bool SupportSnaps(const AFGBuildable* Buildable, TArray<FEnd>& OutSnaps)
	{
		OutSnaps.Reset();
		if (Buildable->IsA<AFGBuildablePassthroughBase>() || Buildable->IsA<AFGBuildableWallPassthrough>()) return false;
		if (Buildable->GetClass()->GetName().Contains(TEXT("Hole"))) return false;
		TInlineComponentArray<UFGConnectionComponent*> Connections(Buildable);
		for (const UFGConnectionComponent* Connection : Connections)
		{
			if (!Connection) continue;
			const EFamily Family = FamilyOf(Connection);
			if (Family == EFamily::None || !IsSnapOnly(Connection)) { OutSnaps.Reset(); return false; }
			OutSnaps.Add({ Connection->GetComponentLocation(), Family });
		}
		return OutSnaps.Num() > 0;
	}

	bool Touches(const TArray<FEnd>& Snaps, const TArray<FEnd>& Ends)
	{
		for (const FEnd& Snap : Snaps)
		{
			for (const FEnd& End : Ends)
			{
				if (End.Family == Snap.Family && FVector::DistSquared(End.Location, Snap.Location) <= JoinCm * JoinCm) return true;
			}
		}
		return false;
	}

	/** The supports outlined for a dismantle state, and the aim and selection they were found for. */
	struct FOutlined
	{
		TArray<TWeakObjectPtr<AActor>> Actors;
		TWeakObjectPtr<UFGOutlineComponent> Outline;
		uint32 Signature = 0;
	};
	TMap<TWeakObjectPtr<UFGBuildGunStateDismantle>, FOutlined> Outlined;

	/** Hides the supports outlined, except those now in Keep (the game outlines what it aims at and has selected itself). */
	void HideOutlines(FOutlined& Entry, const TArray<AActor*>& Keep = TArray<AActor*>())
	{
		if (UFGOutlineComponent* Outline = Entry.Outline.Get())
		{
			for (const TWeakObjectPtr<AActor>& Actor : Entry.Actors) if (AActor* Support = Actor.Get(); Support && !Keep.Contains(Support)) Outline->HideOutline(Support);
		}
		Entry.Actors.Reset();
	}
}

void FMKIOrphanSupports::Find(UWorld* World, const TArray<AActor*>& Actors, TArray<AActor*>& OutSupports)
{
	using namespace MKIOrphanSupportsPrivate;
	OutSupports.Reset();
	TSet<const AActor*> Taken;
	TArray<FEnd> TakenEnds;
	FBox Around(ForceInit);
	for (const AActor* Actor : Actors)
	{
		if (!IsValid(Actor)) continue;
		Taken.Add(Actor);
		if (!IsLine(Actor)) continue;
		const int32 First = TakenEnds.Num();
		AddLineEnds(Actor, TakenEnds);
		for (int32 Index = First; Index < TakenEnds.Num(); ++Index) Around += TakenEnds[Index].Location;
	}
	AFGBuildableSubsystem* Buildables = World ? AFGBuildableSubsystem::Get(World) : nullptr;
	if (TakenEnds.Num() == 0 || !Buildables) return;

	// One pass over the buildings near the ends taken: the supports one of them rests on, the ends of the belts and pipes
	// that stay, and the stackable supports (one may carry another).
	const FBox SupportBox = Around.ExpandBy(SupportReachCm);
	const FBox LineBox = Around.ExpandBy(LineReachCm);
	TArray<FEnd> StayingEnds;
	TArray<AFGBuildable*> Candidates;
	TArray<AFGBuildablePoleStackable*> Stackables;
	TArray<FEnd> Snaps;
	for (AFGBuildable* Buildable : Buildables->GetAllBuildablesRef())
	{
		if (IsGone(Buildable) || Taken.Contains(Buildable)) continue;
		const FVector Location = Buildable->GetActorLocation();
		if (IsLine(Buildable))
		{
			if (LineBox.IsInsideOrOn(Location)) AddLineEnds(Buildable, StayingEnds);
			continue;
		}
		if (!SupportBox.IsInsideOrOn(Location)) continue;
		if (AFGBuildablePoleStackable* Stackable = Cast<AFGBuildablePoleStackable>(Buildable)) Stackables.Add(Stackable);
		if (SupportSnaps(Buildable, Snaps) && Touches(Snaps, TakenEnds)) Candidates.Add(Buildable);
	}
	for (AFGBuildable* Support : Candidates)
	{
		SupportSnaps(Support, Snaps);
		if (Touches(Snaps, StayingEnds) || !IFGDismantleInterface::Execute_CanDismantle(Support)) continue;
		OutSupports.Add(Support);
	}

	// A stackable support carrying another that stays keeps standing (the one on top would float).
	bool bChanged = true;
	while (bChanged)
	{
		bChanged = false;
		for (int32 Index = OutSupports.Num() - 1; Index >= 0; --Index)
		{
			const AFGBuildablePoleStackable* Lower = Cast<AFGBuildablePoleStackable>(OutSupports[Index]);
			if (!Lower) continue;
			const FVector Base = Lower->GetActorLocation();
			const bool bCarries = Stackables.ContainsByPredicate([&](const AFGBuildablePoleStackable* Upper)
			{
				if (Upper == Lower || OutSupports.Contains(Upper)) return false;
				const FVector Offset = Upper->GetActorLocation() - Base;
				return Offset.Z > 1.0 && Offset.Z <= Lower->GetStackHeight() + StackSlackCm && FVector2D(Offset.X, Offset.Y).Size() <= JoinCm;
			});
			if (bCarries) { OutSupports.RemoveAt(Index); bChanged = true; }
		}
	}
}

bool FMKIOrphanSupports::AddUnused(const UObject* WorldContext, const TArray<AActor*>& Actors, TArray<AActor*>& OutActors)
{
	if (!WorldContext || !UMKIToolsConfiguration::GetDismantleUnusedSupports(WorldContext)) return false;
	TArray<AActor*> Supports;
	Find(WorldContext->GetWorld(), Actors, Supports);
	if (Supports.Num() == 0) return false;
	OutActors = Actors;
	for (AActor* Support : Supports) OutActors.AddUnique(Support);
	static int32 Logged = 0;
	if (Logged < 5) { ++Logged; UE_LOG(LogMKITools, Log, TEXT("Unused supports: %d go with %d dismantled (first %s)"), Supports.Num(), Actors.Num(), *Supports[0]->GetName()); }
	return true;
}

void FMKIOrphanSupports::Tick(UFGBuildGunStateDismantle* State)
{
	using namespace MKIOrphanSupportsPrivate;
	const AFGBuildGun* Gun = State ? State->GetBuildGun() : nullptr;
	AFGCharacterPlayer* Character = Gun ? Gun->GetInstigatorCharacter() : nullptr;
	const bool bDefaultMode = State && (!State->mDefaultDismantleMode || State->IsCurrentBuildGunMode(State->mDefaultDismantleMode));
	if (!Character || !Character->IsLocallyControlled() || !bDefaultMode || !UMKIToolsConfiguration::GetDismantleUnusedSupports(Character)) { Clear(State); return; }

	// The aimed building and the selection; found again only when they change.
	TArray<AActor*> Selection;
	for (AActor* Pending : State->mPendingDismantleActors) if (IsValid(Pending)) Selection.Add(Pending);
	if (IsValid(State->mCurrentlySelectedActor)) Selection.AddUnique(State->mCurrentlySelectedActor);
	uint32 Signature = ::GetTypeHash(Selection.Num());
	for (const AActor* Actor : Selection) Signature = HashCombine(Signature, ::GetTypeHash(Actor));
	FOutlined& Entry = Outlined.FindOrAdd(State);
	if (Signature == Entry.Signature) return;
	Entry.Signature = Signature;

	// The game outlines what it aims at and has selected again when they change (this runs after it): ours are shown again.
	HideOutlines(Entry, Selection);
	if (!Selection.ContainsByPredicate([](const AActor* Actor) { return MKIOrphanSupportsPrivate::IsLine(Actor); })) return;
	TArray<AActor*> Supports;
	Find(Character->GetWorld(), Selection, Supports);
	Entry.Outline = Character->GetOutline();
	UFGOutlineComponent* Outline = Entry.Outline.Get();
	if (!Outline) return;
	for (AActor* Support : Supports)
	{
		Outline->ShowOutline(Support, EOutlineColor::OC_DISMANTLE);
		Entry.Actors.Add(Support);
	}
}

void FMKIOrphanSupports::Clear(UFGBuildGunStateDismantle* State)
{
	using namespace MKIOrphanSupportsPrivate;
	if (FOutlined* Entry = Outlined.Find(State)) HideOutlines(*Entry);
	Outlined.Remove(State);
	// Weak keys of states that are gone.
	for (auto It = Outlined.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
}
