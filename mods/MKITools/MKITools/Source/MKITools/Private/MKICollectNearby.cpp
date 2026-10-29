#include "MKICollectNearby.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FGCharacterPlayer.h"
#include "FGCrate.h"
#include "FGFoliageIdentifier.h"
#include "FGFoliagePickup.h"
#include "FGFoliageRemovalSubsystem.h"
#include "FGFoliageResourceUserData.h"
#include "FGInventoryComponent.h"
#include "FGItemPickup.h"
#include "GameFramework/PlayerController.h"
#include "MKITools.h"
#include "MKIToolsConfiguration.h"

bool UMKICollectNearbySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

TStatId UMKICollectNearbySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMKICollectNearbySubsystem, STATGROUP_Tickables);
}

bool UMKICollectNearbySubsystem::IsCollectibleFoliage(UStaticMesh* Mesh)
{
	if (!Mesh) return false;
	if (const bool* Known = CollectibleMeshes.Find(Mesh)) return *Known;
	bool bCollectible = false;
	if (const UFGFoliageResourceUserData* Data = Mesh->GetAssetUserData<UFGFoliageResourceUserData>())
	{
		// The maintainer (2026-10-21): berries, nuts and bacon agaric (Desc_Berry, Desc_Nut, Desc_Shroom).
		for (const FItemDropWithChance& Drop : Data->GetPickupItems())
		{
			const FString Item = GetNameSafe(Drop.Drop.ItemClass.Get());
			if (Item.Contains(TEXT("Berry")) || Item.Contains(TEXT("Nut")) || Item.Contains(TEXT("Shroom"))) { bCollectible = true; break; }
		}
	}
	CollectibleMeshes.Add(Mesh, bCollectible);
	if (bCollectible) UE_LOG(LogMKITools, Log, TEXT("Collect: collects %s"), *Mesh->GetName());
	return bCollectible;
}

void UMKICollectNearbySubsystem::Tick(float DeltaTime)
{
	APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	AFGCharacterPlayer* Character = Controller ? Cast<AFGCharacterPlayer>(Controller->GetPawn()) : nullptr;
	if (!Character || !Character->IsLocallyControlled() || !UMKIToolsConfiguration::GetCollectNearby(Character)) return;
	AFGFoliagePickup* Proxy = Character->GetFoliagePickupProxy();
	const float RadiusCm = BaseRadiusCm * UMKIToolsConfiguration::GetCollectRadiusMultiplier(Character);
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextScanTime)
	{
		NextScanTime = Now + ScanIntervalSeconds;
		Scan(Character, RadiusCm);
		if (Character->HasAuthority()) EmptyCrates(Character, RadiusCm);
	}
	if (Proxy && Now >= NextPickupTime && PickUpNext(Character, Proxy, RadiusCm)) NextPickupTime = Now + PickupIntervalSeconds;
}

void UMKICollectNearbySubsystem::Scan(AFGCharacterPlayer* Character, float RadiusCm)
{
	NearItems.Reset();
	NearCrates.Reset();
	NearFoliage.Reset();
	const FVector Center = Character->GetActorLocation();
	const double RadiusSq = FMath::Square(static_cast<double>(RadiusCm));
	for (TActorIterator<AFGItemPickup> It(GetWorld()); It; ++It)
	{
		if (!It->IsPickedUp() && FVector::DistSquared(It->GetActorLocation(), Center) <= RadiusSq) NearItems.Add(*It);
	}
	for (TActorIterator<AFGCrate> It(GetWorld()); It; ++It)
	{
		// The maintainer (2026-10-21): dismantle crates and death crates.
		const EFGCrateType Type = It->GetCrateType();
		if ((Type == EFGCrateType::CT_DismantleCrate || Type == EFGCrateType::CT_DeathCrate) && FVector::DistSquared(It->GetActorLocation(), Center) <= RadiusSq) NearCrates.Add(*It);
	}
	// Collectible foliage, once per scan: at 100 m the foliage within the radius is far too much to ask for at every pickup.
	AFGFoliageRemovalSubsystem* Removal = AFGFoliageRemovalSubsystem::Get(GetWorld());
	if (!Removal) return;
	TMap<UHierarchicalInstancedStaticMeshComponent*, TArray<int32>> Near;
	Removal->GetFoliageWithinRadius(Center, RadiusCm, Near);
	TArray<TPair<double, FNearFoliage>> Found;
	for (const TPair<UHierarchicalInstancedStaticMeshComponent*, TArray<int32>>& Entry : Near)
	{
		if (!IsValid(Entry.Key) || !IsCollectibleFoliage(Entry.Key->GetStaticMesh())) continue;
		if (!AFGFoliageRemovalSubsystem::HasIdentifier(Entry.Key, UFGFoliageIdentifier_Pickupable::StaticClass())) continue;
		for (const int32 Instance : Entry.Value)
		{
			FTransform Transform;
			if (!Entry.Key->GetInstanceTransform(Instance, Transform, true)) continue;
			Found.Emplace(FVector::DistSquared(Transform.GetLocation(), Center), FNearFoliage{ Entry.Key, Instance, Transform.GetLocation() });
		}
	}
	Found.Sort([](const TPair<double, FNearFoliage>& A, const TPair<double, FNearFoliage>& B) { return A.Key > B.Key; });
	for (const TPair<double, FNearFoliage>& Entry : Found) NearFoliage.Add(Entry.Value);
}

bool UMKICollectNearbySubsystem::PickUpNext(AFGCharacterPlayer* Character, AFGFoliagePickup* Proxy, float RadiusCm)
{
	// Ground items first, one per pickup, through the proxy as a press of E on them.
	while (NearItems.Num() > 0)
	{
		AFGItemPickup* Item = NearItems.Pop().Get();
		if (!Item || Item->IsPickedUp() || !Item->CanBePickedUp(Character)) continue;
		Proxy->SetItemToPickup(Item);
		Proxy->DoPickup();
		return true;
	}
	// Then the nearest collectible foliage instance the last scan found, if it is still where it was (an index can move
	// when another instance goes) and still within the radius.
	const FVector Center = Character->GetActorLocation();
	const double RadiusSq = FMath::Square(static_cast<double>(RadiusCm));
	while (NearFoliage.Num() > 0)
	{
		const FNearFoliage Next = NearFoliage.Pop();
		UHierarchicalInstancedStaticMeshComponent* Component = Next.Component.Get();
		FTransform Transform;
		if (!IsValid(Component) || !Component->GetInstanceTransform(Next.Instance, Transform, true)) continue;
		if (!Transform.GetLocation().Equals(Next.Location, 1.0) || FVector::DistSquared(Next.Location, Center) > RadiusSq) continue;
		Proxy->SetPickupData(Component, Next.Instance, Character);
		Proxy->DoPickup();
		return true;
	}
	return false;
}

void UMKICollectNearbySubsystem::EmptyCrates(AFGCharacterPlayer* Character, float RadiusCm)
{
	UFGInventoryComponent* Inventory = Character->GetInventory();
	if (!Inventory) return;
	for (const TWeakObjectPtr<AFGCrate>& Weak : NearCrates)
	{
		AFGCrate* Crate = Weak.Get();
		UFGInventoryComponent* Contents = Crate ? Crate->GetInventory() : nullptr;
		if (!Contents) continue;
		int32 Moved = 0;
		for (int32 Index = 0; Index < Contents->GetSizeLinear(); ++Index)
		{
			FInventoryStack Stack;
			if (!Contents->GetStackFromIndex(Index, Stack) || !Stack.HasItems()) continue;
			const int32 Added = Inventory->AddStack(Stack, true);
			if (Added <= 0) continue;
			// Removing the items lets the crate notice it is empty and go, as when the player takes them.
			Contents->RemoveFromIndex(Index, Added);
			Moved += Added;
		}
		if (Moved > 0)
		{
			UE_LOG(LogMKITools, Log, TEXT("Collect: took %d items from %s"), Moved, *Crate->GetName());
			Character->OnPickUpItem();
		}
	}
}
