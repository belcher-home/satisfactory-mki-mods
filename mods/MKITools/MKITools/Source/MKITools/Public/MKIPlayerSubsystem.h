#pragma once

#include "CoreMinimal.h"
#include "FGSaveInterface.h"
#include "Subsystem/ModSubsystem.h"
#include "MKIPlayerSubsystem.generated.h"

/**
 * Extra inventory slots (Mods > MKI Tools, the maintainer 2026-10-21): a "+10 slots" button, up to 500 added, that can only
 * grow, not subtract. The slots added in this save are kept here (saved, server side). The module adds them to the game's
 * own slot count (AFGCharacterPlayer::GetTotalPlayerInventorySlots), and once a second every player's inventory that is
 * smaller than that count is grown to it.
 *
 * Flight mode (Mods > MKI Tools, the maintainer 2026-10-23): turning the option off while a player flies ends the flight
 * (the player falls), checked with the slots once a second.
 *
 * Fill Depot from inventory (the maintainer 2026-10-28): for each player who turned it on, once a second, items in the
 * player's inventory that the Dimensional Depot lists (has a row for, even at 0) move into the player's depot upload queue (the game's own
 * upload inventory, which the game uploads at its own pace), no more than the depot still has room for. Queued items of
 * a kind the depot is full of go back to the inventory where they fit.
 */
UCLASS()
class MKITOOLS_API AMKIPlayerSubsystem : public AModSubsystem, public IFGSaveInterface
{
	GENERATED_BODY()

public:
	AMKIPlayerSubsystem();

	/** Slots one press of the button adds, and the most a save can have added. */
	static constexpr int32 InventorySlotsPerPress = 10;
	static constexpr int32 MaximumExtraInventorySlots = 500;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// IFGSaveInterface
	virtual bool ShouldSave_Implementation() const override { return true; }

	/** The extra inventory slots added in this save; 0 without the subsystem. */
	static int32 GetExtraInventorySlots(const UObject* WorldContext);
	/** Whether the button can add slots now: on the host, in a game, below the maximum. */
	static bool CanAddInventorySlots(const UObject* WorldContext);
	/** Adds InventorySlotsPerPress slots (up to the maximum) and grows every player's inventory now. */
	static void AddInventorySlots(const UObject* WorldContext);

	/** How long Fill Depot leaves an item alone after the depot's count of it drops (taken out, or built or crafted from). */
	static constexpr float DepotCooldownSeconds = 60.f;

	/** Turns "Fill Depot from inventory" on or off for a player (server side; the player's own option, sent by its client). */
	static void SetFillDepotFromInventory(class APlayerController* Controller, bool bFill);

private:
	static AMKIPlayerSubsystem* Get(const UObject* WorldContext);
	void Apply();
	/** One "Fill Depot from inventory" pass for a player: returns queued items the depot is full of, then queues more. */
	void FillDepot(class AFGCharacterPlayer* Character, class AFGCentralStorageSubsystem* Depot);
	/** Follows the depot's counts (its update delegate, and each pass's counts) and starts an item's cooldown when its count drops. */
	void TrackDepot(class AFGCentralStorageSubsystem* Depot);
	/** A depot count seen: an item whose count dropped since the last one is left alone for DepotCooldownSeconds. */
	void NoteDepotAmount(TSubclassOf<class UFGItemDescriptor> Item, int32 Amount);
	UFUNCTION()
	void OnDepotAmountUpdated(TSubclassOf<class UFGItemDescriptor> Item, int32 Amount);

	/** The slots added in this save; only ever grows. */
	UPROPERTY(SaveGame)
	int32 ExtraInventorySlots = 0;

	FTimerHandle ApplyTimer;

	/** The players (by player state) with "Fill Depot from inventory" on; not saved, each client sends its option on joining. */
	TSet<TWeakObjectPtr<class APlayerState>> FillDepotPlayers;
	/** The depot's count of each item as last seen, and when each item's cooldown ends (game time). */
	TMap<TSubclassOf<class UFGItemDescriptor>, int32> DepotAmounts;
	TMap<TSubclassOf<class UFGItemDescriptor>, float> DepotCooldowns;
	/** The depot whose update delegate is bound. */
	TWeakObjectPtr<class AFGCentralStorageSubsystem> TrackedDepot;
	/** The last Fill Depot pass's summary, logged when it changes. */
	FString LastFillSummary;

	/** "Flight mode" as last seen, so turning it off ends a flight once (Apply). */
	bool bFlightModeWas = false;
};
