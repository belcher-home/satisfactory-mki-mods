#include "MKIAreaDismantle.h"

#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildableFactory.h"
#include "Buildables/FGBuildablePoleBase.h"
#include "Buildables/FGBuildableWire.h"
#include "Components/LineBatchComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunDismantle.h"
#include "FGCharacterPlayer.h"
#include "FGCircuitConnectionComponent.h"
#include "FGDismantleInterface.h"
#include "FGFactoryConnectionComponent.h"
#include "FGInventoryComponent.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGOutlineComponent.h"
#include "FGRecipe.h"
#include "InputCoreTypes.h"
#include "MKIAreaDismantleMode.h"
#include "MKIAreaRules.h"
#include "MKITools.h"
#include "Resources/FGItemDescriptor.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MKITools"

// A named namespace: the unity build joins this file with Terrain mode's, whose anonymous namespace has the same names.
namespace MKIAreaDismantlePrivate
{
	/** How long fire is held before the area is dismantled: a whole second, for a mass dismantle. */
	constexpr float HoldToDismantleSeconds = 1.0f;
	/** The selection is gathered again this often while the cube stands still (buildings come and go). */
	constexpr double ReselectSeconds = 1.0;
	/** At most this many actors are outlined (outlining thousands would cost more than it shows). */
	constexpr int32 MaxOutlined = 300;
	/** Actors whose location is farther than this beyond the cube's corner are not tested (splines test their bounds). */
	constexpr double PrefilterSlackCm = 5000.0;
	/** A belt support goes with a taken belt when one of its connections lies this close to the belt's end. */
	constexpr double SupportJoinCm = 20.0;
	/** A hit this close outside a lightweight instance's measured shape still lands on it. */
	constexpr double AimSlackCm = 5.0;
	/** The cube's lines: their thickness and colour (the dismantle orange-red). */
	constexpr float CubeLineThickness = 12.f;
	const FLinearColor CubeColor(1.f, 0.25f, 0.05f);

	struct FLightweightPick
	{
		TSubclassOf<AFGBuildable> Class;
		int32 Index = INDEX_NONE;
		FTransform Transform;
	};

	/** What the cube holds: actors and lightweight instances, each with its top (for the top-down order), and the refund. */
	struct FAreaSelection
	{
		TArray<TWeakObjectPtr<AFGBuildable>> Actors;
		TArray<double> ActorTops;
		TArray<FLightweightPick> Lightweights;
		TArray<double> LightweightTops;
		TMap<TSubclassOf<UFGItemDescriptor>, int32> Refund;
		bool bNoBuildCost = false;

		int32 Num() const { return Actors.Num() + Lightweights.Num(); }
	};

	struct FAreaAim
	{
		/** The cube's size in 16 m steps (1-6); kept while the mode is used. */
		int32 Step = 1;
		bool bHasCube = false;
		FBox Cube = FBox(ForceInit);
		FAreaSelection Selection;
		FBox SelectedCube = FBox(ForceInit);
		double SelectedAt = -1.0;
		TArray<TWeakObjectPtr<AActor>> Outlined;
		/** Mesh stand-ins outlining lightweight instances, which are no actors the outline can take. */
		TArray<TWeakObjectPtr<UStaticMeshComponent>> Proxies;
		TWeakObjectPtr<UFGOutlineComponent> Outline;
		bool bHolding = false;
		float HeldSeconds = 0.f;
		/** After a dismantle (or a press with nothing selected), nothing more happens until the button is released. */
		bool bWaitForRelease = false;
	};
	TMap<TWeakObjectPtr<UFGBuildGunStateDismantle>, FAreaAim> Aims;

	/** A queued dismantle: an actor, or a lightweight instance by class and index. */
	struct FQueued
	{
		TWeakObjectPtr<AFGBuildable> Actor;
		TSubclassOf<AFGBuildable> Class;
		int32 Index = INDEX_NONE;
	};
	struct FAreaQueue
	{
		TWeakObjectPtr<UFGBuildGunStateDismantle> State;
		TArray<FQueued> Items;
		int32 Next = 0;
	};
	/** Dismantles under way, sent a batch per tick from the core ticker, so they run on after the mode is left. */
	TArray<FAreaQueue> Queues;
	FTSTicker::FDelegateHandle QueueTicker;

	/** The line under the crosshair: the cube's size, the count and the refund, or the progress of a dismantle. */
	TSharedPtr<SWidget> Line;
	FText LineText;
	bool bShowLine = false;

	void EnsureLine(UWorld* World)
	{
		if (Line.IsValid() || !World) return;
		UGameViewportClient* Viewport = World->GetGameViewport();
		if (!Viewport) return;
		Line = SNew(SOverlay).Visibility(EVisibility::HitTestInvisible)
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMargin(0.f, 200.f, 0.f, 0.f))
			[
				SNew(STextBlock)
				.Text_Lambda([]() { return LineText; })
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(FLinearColor(1.f, 0.62f, 0.2f))
				.ShadowOffset(FVector2D(1.f, 1.f))
				.Visibility_Lambda([]() { return bShowLine ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			];
		Viewport->AddViewportWidgetContent(Line.ToSharedRef(), 5);
	}

	AFGCharacterPlayer* LocalCharacter(const UFGBuildGunStateDismantle* State)
	{
		const AFGBuildGun* Gun = State ? State->GetBuildGun() : nullptr;
		AFGCharacterPlayer* Character = Gun ? Gun->GetInstigatorCharacter() : nullptr;
		return Character && Character->IsLocallyControlled() ? Character : nullptr;
	}

	/** The fire button, held: the left mouse button or a gamepad's right trigger, while no menu has the cursor. */
	bool FireHeld(const APlayerController* Controller)
	{
		return Controller && !Controller->bShowMouseCursor && (Controller->IsInputKeyDown(EKeys::LeftMouseButton) || Controller->IsInputKeyDown(EKeys::Gamepad_RightTrigger));
	}

	/** The scroll wheel (or the gamepad's d-pad) this tick: +1 grows the cube, -1 shrinks it. */
	int32 ScrollSteps(const APlayerController* Controller)
	{
		if (!Controller || Controller->bShowMouseCursor) return 0;
		int32 Steps = 0;
		if (Controller->WasInputKeyJustPressed(EKeys::MouseScrollUp) || Controller->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Up)) ++Steps;
		if (Controller->WasInputKeyJustPressed(EKeys::MouseScrollDown) || Controller->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Down)) --Steps;
		return Steps;
	}

	/**
	 * A buildable class's shape in its own space: the meshes it is drawn with (its lightweight instance data, which
	 * foundations, walls and most machines carry), else its clearance box. Cached per class.
	 */
	/** A class's instance meshes and their transforms in the building's own space (its lightweight instance data), cached. */
	const TArray<TPair<TWeakObjectPtr<UStaticMesh>, FTransform>>& ClassMeshes(UClass* Class)
	{
		static TMap<TWeakObjectPtr<UClass>, TArray<TPair<TWeakObjectPtr<UStaticMesh>, FTransform>>> Cache;
		if (const auto* Found = Cache.Find(Class)) return *Found;
		TArray<TPair<TWeakObjectPtr<UStaticMesh>, FTransform>>& Meshes = Cache.Add(Class);
		const AFGBuildable* Defaults = Class ? Cast<AFGBuildable>(Class->GetDefaultObject()) : nullptr;
		if (!Defaults) return Meshes;
		// By reflection: the SDK does not export the AbstractInstance types (FInstanceData, GetInstanceData) to link against.
		static FObjectProperty* DataProperty = FindFProperty<FObjectProperty>(AFGBuildable::StaticClass(), TEXT("mInstanceDataCDO"));
		const UObject* Data = DataProperty ? DataProperty->GetObjectPropertyValue_InContainer(Defaults) : nullptr;
		const FArrayProperty* Instances = Data ? FindFProperty<FArrayProperty>(Data->GetClass(), TEXT("Instances")) : nullptr;
		const FStructProperty* Inner = Instances ? CastField<FStructProperty>(Instances->Inner) : nullptr;
		const FObjectProperty* MeshProperty = Inner ? FindFProperty<FObjectProperty>(Inner->Struct, TEXT("StaticMesh")) : nullptr;
		const FStructProperty* TransformProperty = Inner ? FindFProperty<FStructProperty>(Inner->Struct, TEXT("RelativeTransform")) : nullptr;
		if (!MeshProperty || !TransformProperty || TransformProperty->Struct != TBaseStructure<FTransform>::Get()) return Meshes;
		FScriptArrayHelper Array(Instances, Instances->ContainerPtrToValuePtr<void>(Data));
		for (int32 Index = 0; Index < Array.Num(); ++Index)
		{
			const uint8* Element = Array.GetRawPtr(Index);
			if (UStaticMesh* Mesh = Cast<UStaticMesh>(MeshProperty->GetObjectPropertyValue_InContainer(Element))) Meshes.Emplace(Mesh, *TransformProperty->ContainerPtrToValuePtr<FTransform>(Element));
		}
		return Meshes;
	}

	/**
	 * A buildable class's shape in its own space: the meshes it is drawn with (its lightweight instance data, which
	 * foundations, walls and most machines carry), else its clearance box. Cached per class.
	 */
	FBox ClassLocalBox(UClass* Class)
	{
		static TMap<TWeakObjectPtr<UClass>, FBox> Cache;
		if (const FBox* Found = Cache.Find(Class)) return *Found;
		FBox Box(ForceInit);
		if (const AFGBuildable* Defaults = Class ? Cast<AFGBuildable>(Class->GetDefaultObject()) : nullptr)
		{
			for (const auto& Mesh : ClassMeshes(Class)) if (const UStaticMesh* Asset = Mesh.Key.Get()) Box += Asset->GetBoundingBox().TransformBy(Mesh.Value);
			const bool bMeshes = Box.IsValid != 0;
			if (!Box.IsValid) Box = Defaults->GetCombinedClearanceBox();
			// Once per class: what each building measures as, so a live run shows where it really stands.
			UE_LOG(LogMKITools, Log, TEXT("Area: %s measures %s from its %s"), *Class->GetName(), *Box.ToString(), bMeshes ? TEXT("instance meshes") : TEXT("clearance"));
		}
		Cache.Add(Class, Box);
		return Box;
	}

	/** An actor's shape in its own space: its mesh components and its class's instance meshes, else its components' bounds. */
	FBox ActorLocalBox(const AFGBuildable* Actor)
	{
		FBox Box = ClassLocalBox(Actor->GetClass());
		const FTransform& ActorTransform = Actor->GetActorTransform();
		TInlineComponentArray<UStaticMeshComponent*> Meshes(Actor);
		for (const UStaticMeshComponent* Mesh : Meshes)
		{
			if (const UStaticMesh* Asset = Mesh ? Mesh->GetStaticMesh() : nullptr) Box += Asset->GetBoundingBox().TransformBy(Mesh->GetComponentTransform().GetRelativeTransform(ActorTransform));
		}
		// Live (2026-10-24): lifts, passthroughs and road pieces measured nothing from their class; their own clearance, their
		// components, and at last a small box at their place keep them from being left out silently.
		if (!Box.IsValid) Box = Actor->GetCombinedClearanceBox();
		if (!Box.IsValid) Box = Actor->CalculateComponentsBoundingBoxInLocalSpace(true);
		if (!Box.IsValid) Box = FBox(FVector(-50.0), FVector(50.0));
		return Box;
	}

	/**
	 * A spline buildable's path in world space. Live (2026-10-24): no belt was ever taken; a belt's spline component is not
	 * registered, so its bounds and world transform are not kept. It is sampled in its own space and placed by its relative
	 * transform and the actor's.
	 */
	void SplinePoints(const AFGBuildable* Actor, const USplineComponent* Spline, TArray<FVector, TInlineAllocator<64>>& OutPoints)
	{
		const double Length = Spline->GetSplineLength();
		const FTransform ToWorld = Spline->GetRelativeTransform() * Actor->GetActorTransform();
		const int32 Samples = FMath::Max(1, FMath::CeilToInt(Length / FMKIAreaRules::SplineSampleCm));
		for (int32 Sample = 0; Sample <= Samples; ++Sample) OutPoints.Add(ToWorld.TransformPosition(Spline->GetLocationAtDistanceAlongSpline(Length * Sample / Samples, ESplineCoordinateSpace::Local)));
	}

	/** Whether an actor reaches into the interior, and its top. Splines and wires are tested along their length. */
	bool ActorReaches(const AFGBuildable* Actor, const FBox& Cube, const FBox& Interior, double& OutTop)
	{
		const double Corner = Cube.GetExtent().Size();
		if (const AFGBuildableWire* Wire = Cast<AFGBuildableWire>(Actor))
		{
			const UFGCircuitConnectionComponent* A = Wire->GetConnection(0);
			const UFGCircuitConnectionComponent* B = Wire->GetConnection(1);
			if (!A || !B) return false;
			const FVector Ends[2] = { A->GetComponentLocation(), B->GetComponentLocation() };
			OutTop = FMath::Max(Ends[0].Z, Ends[1].Z);
			return FMKIAreaRules::PathReaches(Ends, Interior);
		}
		if (const USplineComponent* Spline = Actor->FindComponentByClass<USplineComponent>())
		{
			if (FVector::Dist(Actor->GetActorLocation(), Cube.GetCenter()) > Corner + Spline->GetSplineLength() + PrefilterSlackCm) return false;
			TArray<FVector, TInlineAllocator<64>> Points;
			SplinePoints(Actor, Spline, Points);
			OutTop = -UE_BIG_NUMBER;
			for (const FVector& Point : Points) OutTop = FMath::Max(OutTop, Point.Z);
			return FMKIAreaRules::PathReaches(Points, FMKIAreaRules::SplineInterior(Interior));
		}
		if (FVector::Dist(Actor->GetActorLocation(), Cube.GetCenter()) > Corner + PrefilterSlackCm) return false;
		const FBox Local = ActorLocalBox(Actor);
		OutTop = Local.TransformBy(Actor->GetActorTransform()).Max.Z;
		return FMKIAreaRules::OrientedBoxReaches(Local, Actor->GetActorTransform(), Interior);
	}

	void AddRefund(FAreaSelection& Selection, TSubclassOf<UFGItemDescriptor> Item, int32 Amount)
	{
		if (Item && Amount > 0) Selection.Refund.FindOrAdd(Item) += Amount;
	}

	/** Takes an actor into the selection, with its top and the refund the game would give for it. */
	void AddActor(FAreaSelection& Selection, AFGBuildable* Actor, double Top)
	{
		Selection.Actors.Add(Actor);
		Selection.ActorTops.Add(Top);
		if (Selection.bNoBuildCost) return;
		TArray<FInventoryStack> Stacks;
		IFGDismantleInterface::Execute_GetDismantleRefund(Actor, Stacks, false);
		for (const FInventoryStack& Stack : Stacks) AddRefund(Selection, Stack.Item.GetItemClass(), Stack.NumItems);
	}

	/** Everything that reaches into the cube and that the game would dismantle, with the refund it would give. */
	FAreaSelection Gather(UWorld* World, const FBox& Cube, const AFGCharacterPlayer* Character)
	{
		FAreaSelection Selection;
		const UFGInventoryComponent* Inventory = Character ? Character->GetInventory() : nullptr;
		Selection.bNoBuildCost = Inventory && Inventory->GetNoBuildCost();
		const FBox Interior = FMKIAreaRules::Interior(Cube);
		for (TActorIterator<AFGBuildable> It(World); It; ++It)
		{
			AFGBuildable* Actor = *It;
			// Lightweight temporaries stand in for instances, which are gathered below; road tiles go with their piece.
			if (!IsValid(Actor) || Actor->GetIsLightweightTemporary() || Actor->IsAboutToBeDismantled() || Actor->GetIsDismantled()) continue;
			if (Actor->GetClass()->GetName().StartsWith(TEXT("MKIRoadTile"))) continue;
			double Top = 0.0;
			if (!ActorReaches(Actor, Cube, Interior, Top) || !IFGDismantleInterface::Execute_CanDismantle(Actor)) continue;
			AddActor(Selection, Actor, Top);
		}
		// The maintainer (2026-10-27): belt supports go with their belts, also where a support stands below the cube.
		TArray<FVector> BeltEnds;
		for (const TWeakObjectPtr<AFGBuildable>& Taken : Selection.Actors)
		{
			const AFGBuildableConveyorBase* Belt = Cast<AFGBuildableConveyorBase>(Taken.Get());
			if (!Belt) continue;
			for (const UFGFactoryConnectionComponent* End : { Belt->GetConnection0(), Belt->GetConnection1() }) if (End) BeltEnds.Add(End->GetComponentLocation());
		}
		if (BeltEnds.Num() > 0)
		{
			const double Reach = Cube.GetExtent().Size() + PrefilterSlackCm;
			for (TActorIterator<AFGBuildablePoleBase> It(World); It; ++It)
			{
				AFGBuildablePoleBase* Support = *It;
				if (!IsValid(Support) || Support->GetIsLightweightTemporary() || Support->IsAboutToBeDismantled() || Support->GetIsDismantled()) continue;
				if (FVector::Dist(Support->GetActorLocation(), Cube.GetCenter()) > Reach || Selection.Actors.Contains(Support)) continue;
				TInlineComponentArray<UFGFactoryConnectionComponent*> Connections(Support);
				const bool bHoldsBelt = Connections.ContainsByPredicate([&](const UFGFactoryConnectionComponent* Connection)
				{
					return Connection && BeltEnds.ContainsByPredicate([&](const FVector& End) { return FVector::Dist(End, Connection->GetComponentLocation()) <= SupportJoinCm; });
				});
				if (!bHoldsBelt || !IFGDismantleInterface::Execute_CanDismantle(Support)) continue;
				AddActor(Selection, Support, ActorLocalBox(Support).TransformBy(Support->GetActorTransform()).Max.Z);
			}
		}
		if (AFGLightweightBuildableSubsystem* Lightweights = AFGLightweightBuildableSubsystem::Get(World))
		{
			const double Reach = Cube.GetExtent().Size() + PrefilterSlackCm;
			const FVector Center = Cube.GetCenter();
			for (const auto& Entry : Lightweights->GetAllLightweightBuildableInstances())
			{
				const FBox Local = ClassLocalBox(Entry.Key);
				if (!Local.IsValid) continue;
				for (int32 Index = 0; Index < Entry.Value.Num(); ++Index)
				{
					const FRuntimeBuildableInstanceData& Instance = Entry.Value[Index];
					// Removed instances lose their recipe and their render handles.
					if (!Instance.BuiltWithRecipe || Instance.Handles.Num() == 0) continue;
					if (FVector::Dist(Instance.Transform.GetLocation(), Center) > Reach) continue;
					if (!FMKIAreaRules::OrientedBoxReaches(Local, Instance.Transform, Interior)) continue;
					Selection.Lightweights.Add({ Entry.Key, Index, Instance.Transform });
					Selection.LightweightTops.Add(Local.TransformBy(Instance.Transform).Max.Z);
					if (!Selection.bNoBuildCost)
					{
						for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(World, Instance.BuiltWithRecipe)) AddRefund(Selection, Ingredient.ItemClass, Ingredient.Amount);
					}
				}
			}
		}
		return Selection;
	}

	/**
	 * An aimed building's world bounds: a wire by its ends, a spline by its path, anything else by its shape. An item (a
	 * machine, a container, a belt support) stands on its own base: its bounds start at its base or higher, so the cube's
	 * bottom sits above the floor it stands on (FMKIAreaRules::ItemFootCm).
	 */
	bool BuildingBounds(const AFGBuildable* Actor, FBox& OutBounds)
	{
		if (const AFGBuildableWire* Wire = Cast<AFGBuildableWire>(Actor))
		{
			const UFGCircuitConnectionComponent* A = Wire->GetConnection(0);
			const UFGCircuitConnectionComponent* B = Wire->GetConnection(1);
			if (!A || !B) return false;
			OutBounds = FBox(ForceInit) + A->GetComponentLocation() + B->GetComponentLocation();
			return true;
		}
		if (const USplineComponent* Spline = Actor->FindComponentByClass<USplineComponent>())
		{
			TArray<FVector, TInlineAllocator<64>> Points;
			SplinePoints(Actor, Spline, Points);
			OutBounds = FBox(Points.GetData(), Points.Num());
			return OutBounds.IsValid != 0;
		}
		OutBounds = ActorLocalBox(Actor).TransformBy(Actor->GetActorTransform());
		// Live (2026-10-27, the maintainer): aiming at a storage container took the foundation under it. A container's
		// measured shape reaches below the floor it stands on; its base is its own place.
		if (OutBounds.IsValid && (Actor->IsA<AFGBuildableFactory>() || Actor->IsA<AFGBuildablePoleBase>()))
		{
			OutBounds.Min.Z = FMath::Min(FMath::Max(OutBounds.Min.Z, Actor->GetActorLocation().Z) + FMKIAreaRules::ItemFootCm, OutBounds.Max.Z);
		}
		return OutBounds.IsValid != 0;
	}

	/**
	 * The world bounds of the building a hit lands on: an actor (BuildingBounds), or an instance of the shared instance
	 * manager by the building that owns it (the hit's handle), or a lightweight foundation or wall (an instance owned by no building) by the
	 * instance whose shape holds the hit point. False for the ground and everything that is no building.
	 */
	bool AimedBounds(UWorld* World, const FHitResult& Hit, FBox& OutBounds)
	{
		// Live (2026-10-27, the maintainer): aiming at a belt took nothing. Belts and their supports are drawn by the shared
		// instance manager, so the hit's actor is the manager; the cube stood on the belt's top and the belt lay below it. The
		// hit's handle resolves the instance to the building that owns it.
		AActor* HitActor = Hit.HitObjectHandle.FetchActor();
		if (!HitActor) HitActor = Hit.GetActor();
		if (const AFGBuildable* Actor = Cast<AFGBuildable>(HitActor)) return BuildingBounds(Actor, OutBounds);
		AFGLightweightBuildableSubsystem* Lightweights = AFGLightweightBuildableSubsystem::Get(World);
		if (!Lightweights) return false;
		// A lightweight instance's handle is owned by the subsystem, so the instance is found by its shape.
		const FVector Point = Hit.ImpactPoint;
		double Best = TNumericLimits<double>::Max();
		for (const auto& Entry : Lightweights->GetAllLightweightBuildableInstances())
		{
			const FBox Local = ClassLocalBox(Entry.Key);
			if (!Local.IsValid) continue;
			const double Reach = Local.GetExtent().Size() + 100.0;
			for (const FRuntimeBuildableInstanceData& Instance : Entry.Value)
			{
				if (!Instance.BuiltWithRecipe || Instance.Handles.Num() == 0) continue;
				const double Distance = FVector::Dist(Instance.Transform.TransformPosition(Local.GetCenter()), Point);
				if (Distance > Reach || Distance >= Best) continue;
				if (!Local.ExpandBy(AimSlackCm).IsInside(Instance.Transform.InverseTransformPosition(Point))) continue;
				Best = Distance;
				OutBounds = Local.TransformBy(Instance.Transform);
			}
		}
		return Best < TNumericLimits<double>::Max();
	}

	void HideOutlines(FAreaAim& Aim)
	{
		if (UFGOutlineComponent* Outline = Aim.Outline.Get())
		{
			for (const TWeakObjectPtr<AActor>& Actor : Aim.Outlined) if (AActor* Outlined = Actor.Get()) Outline->HideOutline(Outlined);
		}
		for (const TWeakObjectPtr<UStaticMeshComponent>& Weak : Aim.Proxies)
		{
			UStaticMeshComponent* Proxy = Weak.Get();
			if (!Proxy) continue;
			if (UFGOutlineComponent* Outline = Aim.Outline.Get()) Outline->HideOutlineForStaticMeshComponent(Proxy->GetOwner(), Proxy);
			Proxy->DestroyComponent();
		}
		Aim.Outlined.Reset();
		Aim.Proxies.Reset();
	}

	void ShowOutlines(AFGCharacterPlayer* Character, FAreaAim& Aim)
	{
		HideOutlines(Aim);
		Aim.Outline = Character->GetOutline();
		UFGOutlineComponent* Outline = Aim.Outline.Get();
		if (!Outline) return;
		for (const TWeakObjectPtr<AFGBuildable>& Actor : Aim.Selection.Actors)
		{
			if (Aim.Outlined.Num() >= MaxOutlined) break;
			if (AFGBuildable* Buildable = Actor.Get()) { Outline->ShowOutline(Buildable, EOutlineColor::OC_DISMANTLE); Aim.Outlined.Add(Buildable); }
		}
		// Live (2026-10-25, the maintainer): the foundations did not highlight. Foundations and walls are lightweight instances,
		// no actors; each gets a stand-in of its meshes, drawn only by the outline, as Terrain mode outlines a boulder.
		for (const FLightweightPick& Pick : Aim.Selection.Lightweights)
		{
			for (const auto& Mesh : ClassMeshes(Pick.Class))
			{
				if (Aim.Outlined.Num() + Aim.Proxies.Num() >= MaxOutlined) return;
				UStaticMesh* Asset = Mesh.Key.Get();
				if (!Asset) continue;
				UStaticMeshComponent* Proxy = NewObject<UStaticMeshComponent>(Character, NAME_None, RF_Transient);
				Proxy->SetStaticMesh(Asset);
				Proxy->SetMobility(EComponentMobility::Movable);
				Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Proxy->SetCastShadow(false);
				Proxy->SetRenderInMainPass(false);
				Proxy->RegisterComponent();
				Proxy->SetWorldTransform(Mesh.Value * Pick.Transform);
				Outline->ShowOutlineForStaticMeshComponent(Character, Proxy, EOutlineColor::OC_DISMANTLE);
				Aim.Proxies.Add(Proxy);
			}
		}
	}

	FText RefundText(const FAreaSelection& Selection)
	{
		if (Selection.bNoBuildCost) return LOCTEXT("AreaNoBuildCost", "No Build Cost: nothing is refunded");
		TArray<TPair<TSubclassOf<UFGItemDescriptor>, int32>> Items;
		for (const auto& Entry : Selection.Refund) Items.Emplace(Entry.Key, Entry.Value);
		Items.Sort([](const auto& A, const auto& B) { return A.Value > B.Value; });
		TArray<FString> Parts;
		for (int32 Index = 0; Index < Items.Num() && Index < 6; ++Index) Parts.Add(FString::Printf(TEXT("%d %s"), Items[Index].Value, *UFGItemDescriptor::GetItemName(Items[Index].Key).ToString()));
		if (Items.Num() > 6) Parts.Add(FString::Printf(TEXT("and %d more"), Items.Num() - 6));
		return FText::Format(LOCTEXT("AreaRefund", "Refund: {0}"), FText::FromString(FString::Join(Parts, TEXT(", "))));
	}

	void UpdateLine(const FAreaAim& Aim)
	{
		bShowLine = true;
		const int32 Meters = FMath::RoundToInt(FMKIAreaRules::EdgeCm(Aim.Step) / 100.0);
		if (!Aim.bHasCube) { LineText = FText::Format(LOCTEXT("AreaNoSurface", "Area {0} m: aim at a building or the ground (scroll to resize)"), Meters); return; }
		if (Aim.Selection.Num() == 0) { LineText = FText::Format(LOCTEXT("AreaEmpty", "Area {0} m: nothing to dismantle (scroll to resize)"), Meters); return; }
		LineText = FText::Format(LOCTEXT("AreaSelection", "Area {0} m: {1} buildings\n{2}\nHold fire to dismantle"), Meters, Aim.Selection.Num(), RefundText(Aim.Selection));
	}

	/** Sends the next batch of each queued dismantle to the game's own dismantle; the queue ends with its last batch. */
	bool TickQueues(float DeltaTime)
	{
		for (int32 QueueIndex = Queues.Num() - 1; QueueIndex >= 0; --QueueIndex)
		{
			FAreaQueue& Queue = Queues[QueueIndex];
			UFGBuildGunStateDismantle* State = Queue.State.Get();
			if (!State || Queue.Next >= Queue.Items.Num())
			{
				if (!State) { UE_LOG(LogMKITools, Warning, TEXT("Area: the dismantle gun went away; %d of %d buildings not sent"), Queue.Items.Num() - Queue.Next, Queue.Items.Num()); }
				else { UE_LOG(LogMKITools, Log, TEXT("Area: dismantle of %d buildings sent"), Queue.Items.Num()); }
				Queues.RemoveAt(QueueIndex);
				continue;
			}
			TArray<AActor*> Actors;
			TMap<UClass*, FDismantleLightweightBundle> Bundles;
			const int32 End = FMath::Min(Queue.Next + UMKIAreaDismantleMode::BatchSize, Queue.Items.Num());
			for (; Queue.Next < End; ++Queue.Next)
			{
				const FQueued& Item = Queue.Items[Queue.Next];
				if (Item.Class) Bundles.FindOrAdd(Item.Class, FDismantleLightweightBundle(Item.Class)).RemovalIndices.Add(Item.Index);
				else if (AFGBuildable* Actor = Item.Actor.Get()) { if (!Actor->IsAboutToBeDismantled() && !Actor->GetIsDismantled()) Actors.Add(Actor); }
			}
			TArray<FDismantleLightweightBundle> BundleList;
			Bundles.GenerateValueArray(BundleList);
			if (Actors.Num() > 0 || BundleList.Num() > 0) FMKIAreaDismantle::SendBatch(State, Actors, BundleList);
			LineText = FText::Format(LOCTEXT("AreaProgress", "Dismantling: {0} / {1}"), Queue.Next, Queue.Items.Num());
		}
		if (Queues.Num() == 0) { QueueTicker.Reset(); return false; }
		return true;
	}

	/** Queues the cube's selection, highest top first, and starts sending it. */
	void StartDismantle(UFGBuildGunStateDismantle* State, FAreaAim& Aim)
	{
		const FAreaSelection& Selection = Aim.Selection;
		TArray<TPair<double, int32>> Order;
		for (int32 Index = 0; Index < Selection.Actors.Num(); ++Index) Order.Emplace(Selection.ActorTops[Index], Index);
		for (int32 Index = 0; Index < Selection.Lightweights.Num(); ++Index) Order.Emplace(Selection.LightweightTops[Index], Selection.Actors.Num() + Index);
		FMKIAreaRules::SortTopDown(Order);
		FAreaQueue Queue;
		Queue.State = State;
		for (const TPair<double, int32>& Entry : Order)
		{
			FQueued Item;
			if (Entry.Value < Selection.Actors.Num()) Item.Actor = Selection.Actors[Entry.Value];
			else { const FLightweightPick& Pick = Selection.Lightweights[Entry.Value - Selection.Actors.Num()]; Item.Class = Pick.Class; Item.Index = Pick.Index; }
			Queue.Items.Add(Item);
		}
		UE_LOG(LogMKITools, Log, TEXT("Area: dismantling %d actors and %d lightweight instances in %s, %d per batch"), Selection.Actors.Num(), Selection.Lightweights.Num(), *Aim.Cube.ToString(), UMKIAreaDismantleMode::BatchSize);
		Queues.Add(MoveTemp(Queue));
		if (!QueueTicker.IsValid()) QueueTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickQueues));
		HideOutlines(Aim);
		Aim.Selection = FAreaSelection();
		Aim.SelectedAt = -1.0;
		Aim.bWaitForRelease = true;
	}
}

void FMKIAreaDismantle::SendBatch(UFGBuildGunStateDismantle* State, const TArray<AActor*>& Actors, const TArray<FDismantleLightweightBundle>& Bundles)
{
	// The game's own dismantle: on the host it runs at once; from a client it is the state's server call.
	State->Server_DismantleActors(Actors, Bundles);
}

bool FMKIAreaDismantle::IsAreaMode(const UFGBuildGunStateDismantle* State)
{
	return State && State->IsCurrentBuildGunMode(UMKIAreaDismantleMode::StaticClass());
}

void FMKIAreaDismantle::Tick(UFGBuildGunStateDismantle* State, float DeltaTime)
{
	using namespace MKIAreaDismantlePrivate;
	// Nothing vanilla stays aimed at or queued while Area mode has the gun.
	if (State->mCurrentlySelectedActor || State->mPendingDismantleActors.Num() > 0)
	{
		State->ClearPendingSelectedActors();
		State->SetAimedAtActor(nullptr);
	}
	AFGCharacterPlayer* Character = LocalCharacter(State);
	FAreaAim& Aim = Aims.FindOrAdd(State);
	APlayerController* Controller = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!Controller || !World) { HideOutlines(Aim); bShowLine = false; return; }
	EnsureLine(World);
	// A dismantle under way shows its progress (TickQueues writes the line) until it ends.
	for (const FAreaQueue& Queue : Queues) if (Queue.State.Get() == State) { bShowLine = true; return; }

	Aim.Step = FMKIAreaRules::ClampStep(Aim.Step + ScrollSteps(Controller));
	FVector ViewLocation; FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MKIAreaAim), true, Character);
	Query.AddIgnoredActor(State->GetBuildGun());
	FHitResult Hit;
	// The maintainer (2026-10-25): an aimed building sets the cube's bottom centre at its own centre and lowest point, and is
	// inside; the ground sets it at the point aimed at (only ground facing up).
	FBox Bounds(ForceInit);
	Aim.bHasCube = false;
	if (World->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + ViewRotation.Vector() * FMKIAreaRules::AimRangeCm, ECC_Visibility, Query))
	{
		if (AimedBounds(World, Hit, Bounds)) { Aim.Cube = FMKIAreaRules::Cube(FMKIAreaRules::BottomCenter(Bounds), Aim.Step); Aim.bHasCube = true; }
		else if (Hit.ImpactNormal.Z >= FMKIAreaRules::MinimumSurfaceNormalZ) { Aim.Cube = FMKIAreaRules::Cube(Hit.ImpactPoint, Aim.Step); Aim.bHasCube = true; }
	}

	// The selection follows the cube, and is gathered again now and then while it stands still.
	const double Now = World->GetTimeSeconds();
	if (!Aim.bHasCube)
	{
		if (Aim.Selection.Num() > 0 || Aim.Outlined.Num() > 0) { HideOutlines(Aim); Aim.Selection = FAreaSelection(); }
		Aim.SelectedAt = -1.0;
	}
	else if (!Aim.SelectedCube.Equals(Aim.Cube) || Aim.SelectedAt < 0.0 || Now - Aim.SelectedAt >= ReselectSeconds)
	{
		Aim.Selection = Gather(World, Aim.Cube, Character);
		Aim.SelectedCube = Aim.Cube;
		Aim.SelectedAt = Now;
		ShowOutlines(Character, Aim);
	}
	if (Aim.bHasCube)
	{
		if (ULineBatchComponent* Lines = World->GetLineBatcher(UWorld::ELineBatcherType::Foreground))
		{
			Lines->DrawBox(Aim.Cube.GetCenter(), Aim.Cube.GetExtent(), FQuat::Identity, CubeColor, 0.f, SDPG_Foreground, CubeLineThickness);
		}
	}
	UpdateLine(Aim);

	// Hold to dismantle, timed here as Terrain mode times its hold (the vanilla delay needs a vanilla target).
	if (!FireHeld(Controller)) { Aim.bHolding = false; Aim.HeldSeconds = 0.f; Aim.bWaitForRelease = false; return; }
	if (Aim.bWaitForRelease) return;
	if (!Aim.bHasCube || Aim.Selection.Num() == 0) { Aim.bHolding = false; Aim.HeldSeconds = 0.f; Aim.bWaitForRelease = true; return; }
	Aim.bHolding = true;
	Aim.HeldSeconds += DeltaTime;
	if (Aim.HeldSeconds >= HoldToDismantleSeconds)
	{
		Aim.bHolding = false;
		Aim.HeldSeconds = 0.f;
		// The latest state of the cube, not the one gathered up to a second ago.
		Aim.Selection = Gather(World, Aim.Cube, Character);
		StartDismantle(State, Aim);
	}
}

float FMKIAreaDismantle::HoldProgress(const UFGBuildGunState* State)
{
	using namespace MKIAreaDismantlePrivate;
	const FAreaAim* Aim = Aims.Find(const_cast<UFGBuildGunStateDismantle*>(Cast<UFGBuildGunStateDismantle>(State)));
	return Aim && Aim->bHolding ? FMath::Clamp(Aim->HeldSeconds / HoldToDismantleSeconds, 0.f, 1.f) : -1.f;
}

void FMKIAreaDismantle::PrimaryFire(UFGBuildGunStateDismantle* State)
{
	// The hold is timed by Tick; a primary fire from the gun itself does nothing more.
}

void FMKIAreaDismantle::Clear(UFGBuildGunStateDismantle* State)
{
	using namespace MKIAreaDismantlePrivate;
	if (FAreaAim* Aim = Aims.Find(State)) HideOutlines(*Aim);
	Aims.Remove(State);
	bShowLine = false;
	// Weak keys of states that are gone.
	for (auto It = Aims.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
}

#undef LOCTEXT_NAMESPACE
