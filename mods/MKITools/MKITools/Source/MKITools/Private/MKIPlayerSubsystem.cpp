#include "MKIPlayerSubsystem.h"

#include "Engine/World.h"
#include "FGCentralStorageSubsystem.h"
#include "EngineUtils.h"
#include "FGCharacterMovementComponent.h"
#include "FGCharacterPlayer.h"
#include "FGInventoryComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "MKITools.h"
#include "MKIToolsConfiguration.h"
#include "Subsystem/SubsystemActorManager.h"
#include "TimerManager.h"

AMKIPlayerSubsystem::AMKIPlayerSubsystem()
{
	ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer;
	PrimaryActorTick.bCanEverTick = false;
}

void AMKIPlayerSubsystem::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority()) GetWorldTimerManager().SetTimer(ApplyTimer, this, &AMKIPlayerSubsystem::Apply, 1.f, true);
}

void AMKIPlayerSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ApplyTimer);
	Super::EndPlay(EndPlayReason);
}

AMKIPlayerSubsystem* AMKIPlayerSubsystem::Get(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	USubsystemActorManager* Manager = World ? World->GetSubsystem<USubsystemActorManager>() : nullptr;
	return Manager ? Manager->GetSubsystemActor<AMKIPlayerSubsystem>() : nullptr;
}

int32 AMKIPlayerSubsystem::GetExtraInventorySlots(const UObject* WorldContext)
{
	const AMKIPlayerSubsystem* Self = Get(WorldContext);
	return Self ? Self->ExtraInventorySlots : 0;
}

bool AMKIPlayerSubsystem::CanAddInventorySlots(const UObject* WorldContext)
{
	// The subsystem spawns on the host only, so a multiplayer client finds none here.
	const AMKIPlayerSubsystem* Self = Get(WorldContext);
	return Self && Self->HasAuthority() && Self->ExtraInventorySlots < MaximumExtraInventorySlots;
}

void AMKIPlayerSubsystem::AddInventorySlots(const UObject* WorldContext)
{
	AMKIPlayerSubsystem* Self = Get(WorldContext);
	if (!Self || !Self->HasAuthority() || Self->ExtraInventorySlots >= MaximumExtraInventorySlots) return;
	const int32 Before = Self->ExtraInventorySlots;
	Self->ExtraInventorySlots = FMath::Min(Before + InventorySlotsPerPress, MaximumExtraInventorySlots);
	UE_LOG(LogMKITools, Log, TEXT("Inventory: extra slots %d -> %d"), Before, Self->ExtraInventorySlots);
	Self->Apply();
}

void AMKIPlayerSubsystem::SetFillDepotFromInventory(APlayerController* Controller, bool bFill)
{
	AMKIPlayerSubsystem* Self = Get(Controller);
	APlayerState* Player = Controller ? Controller->PlayerState : nullptr;
	if (!Self || !Self->HasAuthority() || !Player) return;
	const bool bChanged = bFill ? !Self->FillDepotPlayers.Contains(Player) : Self->FillDepotPlayers.Contains(Player);
	if (bFill) Self->FillDepotPlayers.Add(Player); else Self->FillDepotPlayers.Remove(Player);
	if (bChanged) UE_LOG(LogMKITools, Log, TEXT("Depot fill: %s for %s"), bFill ? TEXT("on") : TEXT("off"), *Player->GetPlayerName());
}

void AMKIPlayerSubsystem::FillDepot(AFGCharacterPlayer* Character, AFGCentralStorageSubsystem* Depot)
{
	UFGInventoryComponent* Inventory = Character->GetInventory();
	UFGInventoryComponent* Upload = Character->GetUploadInventory();
	if (!Inventory || !Upload) return;
	int32 Matching = 0, AtLimit = 0, Moved = 0, Returned = 0, Cooling = 0;
	const float Now = GetWorld()->GetTimeSeconds();
	// The maintainer's rule: a queued item the depot won't hold any more of goes back to the inventory, if there is room.
	TMap<TSubclassOf<UFGItemDescriptor>, int32> Queued;
	// The items the depot lists (a row in its panel), those it holds none of included.
	TArray<FItemAmount> Listed;
	Depot->GetAllItemsFromCentralStorage(Listed);
	TSet<TSubclassOf<UFGItemDescriptor>> Rows;
	int32 Empty = 0;
	for (const FItemAmount& Row : Listed)
	{
		Rows.Add(Row.ItemClass);
		if (Row.Amount <= 0) ++Empty;
	}
	for (int32 Index = 0; Index < Upload->GetSizeLinear(); ++Index)
	{
		FInventoryStack Stack;
		if (!Upload->GetStackFromIndex(Index, Stack) || !Stack.HasItems()) continue;
		const TSubclassOf<UFGItemDescriptor> Item = Stack.Item.GetItemClass();
		if (Depot->GetNumItemsFromCentralStorage(Item) < Depot->GetCentralStorageItemLimit(Item))
		{
			Queued.FindOrAdd(Item) += Stack.NumItems;
			continue;
		}
		const int32 Back = Inventory->AddStack(Stack, true);
		if (Back > 0)
		{
			Upload->RemoveFromIndex(Index, Back);
			Returned += Back;
			UE_LOG(LogMKITools, Log, TEXT("Depot fill: depot full of %s, %d back to %s's inventory"), *GetNameSafe(Item), Back, *Character->GetName());
		}
		if (Back < Stack.NumItems) Queued.FindOrAdd(Item) += Stack.NumItems - Back;
	}
	// Items the depot lists go into the upload queue, no more than the depot still has room for (counting what is
	// queued already), so nothing queued here has to come back.
	for (int32 Index = 0; Index < Inventory->GetSizeLinear(); ++Index)
	{
		FInventoryStack Stack;
		if (!Inventory->GetStackFromIndex(Index, Stack) || !Stack.HasItems()) continue;
		const TSubclassOf<UFGItemDescriptor> Item = Stack.Item.GetItemClass();
		if (!Rows.Contains(Item) || !Depot->CanUploadInventoryItemToCentralStorage(Stack.Item)) continue;
		// Taken out of the depot lately: left in the inventory for a while, to be used.
		if (const float* Until = DepotCooldowns.Find(Item); Until && *Until > Now) { ++Cooling; continue; }
		const int32 Stored = Depot->GetNumItemsFromCentralStorage(Item);
		++Matching;
		int32& AlreadyQueued = Queued.FindOrAdd(Item);
		const int32 Room = Depot->GetCentralStorageItemLimit(Item) - Stored - AlreadyQueued;
		if (Room <= 0) { ++AtLimit; continue; }
		Stack.NumItems = FMath::Min(Stack.NumItems, Room);
		const int32 Added = Upload->AddStack(Stack, true);
		if (Added <= 0) continue;
		Inventory->RemoveFromIndex(Index, Added);
		AlreadyQueued += Added;
		Moved += Added;
		UE_LOG(LogMKITools, Log, TEXT("Depot fill: %d %s queued for upload from %s"), Added, *GetNameSafe(Item), *Character->GetName());
	}
	// What a pass found, when it differs from the last (so "nothing happens" can be told from "nothing qualifies").
	const FString Summary = FString::Printf(TEXT("depot lists %d items (%d of them at 0), %d inventory stacks of listed items, %d of them cooling down, %d at the depot's limit, %d queued, %d returned, depot built %s, upload slots %d"),
		Rows.Num(), Empty, Matching, Cooling, AtLimit, Moved, Returned, Depot->IsCentralStorageBuilt() ? TEXT("yes") : TEXT("no"), Upload->GetSizeLinear());
	if (Summary != LastFillSummary)
	{
		LastFillSummary = Summary;
		UE_LOG(LogMKITools, Log, TEXT("Depot fill: %s: %s"), *Character->GetName(), *Summary);
	}
}

void AMKIPlayerSubsystem::TrackDepot(AFGCentralStorageSubsystem* Depot)
{
	if (TrackedDepot.Get() != Depot)
	{
		if (TrackedDepot.IsValid()) TrackedDepot->mOnCentralStorageItemAmountUpdatedDelegate.RemoveDynamic(this, &AMKIPlayerSubsystem::OnDepotAmountUpdated);
		Depot->mOnCentralStorageItemAmountUpdatedDelegate.AddUniqueDynamic(this, &AMKIPlayerSubsystem::OnDepotAmountUpdated);
		TrackedDepot = Depot;
	}
	// The delegate gives each change as it happens; the counts of each pass catch a change it does not report.
	TArray<FItemAmount> Listed;
	Depot->GetAllItemsFromCentralStorage(Listed);
	for (const FItemAmount& Row : Listed) NoteDepotAmount(Row.ItemClass, Row.Amount);
	const float Now = GetWorld()->GetTimeSeconds();
	for (auto It = DepotCooldowns.CreateIterator(); It; ++It) if (It->Value <= Now) It.RemoveCurrent();
}

void AMKIPlayerSubsystem::OnDepotAmountUpdated(TSubclassOf<UFGItemDescriptor> Item, int32 Amount)
{
	NoteDepotAmount(Item, Amount);
}

void AMKIPlayerSubsystem::NoteDepotAmount(TSubclassOf<UFGItemDescriptor> Item, int32 Amount)
{
	if (!Item) return;
	int32& Last = DepotAmounts.FindOrAdd(Item, Amount);
	if (Amount < Last)
	{
		DepotCooldowns.Add(Item, GetWorld()->GetTimeSeconds() + DepotCooldownSeconds);
		UE_LOG(LogMKITools, Log, TEXT("Depot fill: %d %s taken out of the depot (%d to %d); left alone for %.0f s"), Last - Amount, *GetNameSafe(Item), Last, Amount, DepotCooldownSeconds);
	}
	Last = Amount;
}

void AMKIPlayerSubsystem::Apply()
{
	const bool bFlightMode = UMKIToolsConfiguration::GetFlightMode(this);
	const bool bFlightModeTurnedOff = bFlightModeWas && !bFlightMode;
	bFlightModeWas = bFlightMode;
	AFGCentralStorageSubsystem* Depot = FillDepotPlayers.Num() > 0 ? AFGCentralStorageSubsystem::Get(GetWorld()) : nullptr;
	if (Depot) TrackDepot(Depot);
	for (TActorIterator<AFGCharacterPlayer> It(GetWorld()); It; ++It)
	{
		UCharacterMovementComponent* Movement = It->GetCharacterMovement();
		if (bFlightModeTurnedOff && Movement && Movement->MovementMode == MOVE_Flying && Movement->bCheatFlying && !It->CheatToggleGhostFlyIsActive())
		{
			UE_LOG(LogMKITools, Log, TEXT("Flight mode: off, %s stops flying"), *It->GetName());
			Movement->bCheatFlying = false;
			Movement->SetMovementMode(MOVE_Falling);
		}
		if (Depot && FillDepotPlayers.Contains(It->GetPlayerState())) FillDepot(*It, Depot);
		UFGInventoryComponent* Inventory = It->GetInventory();
		// GetTotalPlayerInventorySlots includes the extra slots (the module's hook).
		const int32 Total = It->GetTotalPlayerInventorySlots();
		if (!Inventory || Inventory->GetSizeLinear() >= Total) continue;
		UE_LOG(LogMKITools, Log, TEXT("Inventory: %s grows from %d to %d slots"), *It->GetName(), Inventory->GetSizeLinear(), Total);
		Inventory->Resize(Total);
	}
}
