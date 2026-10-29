#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "MKIToolsConfiguration.generated.h"

/**
 * MKI Tools' options, under Mods > MKI Tools in the game's menus (SML mod configuration, saved per player in the game's
 * config folder). Registered with SML's config manager by UMKIToolsGameInstanceSubsystem.
 */
UCLASS()
class MKITOOLS_API UMKIToolsConfiguration : public UModConfiguration
{
	GENERATED_BODY()

public:
	UMKIToolsConfiguration();

	/** The key of the hand crafting multiplier option (the crafting work per Craft Bench tick). */
	static constexpr const TCHAR* HandCraftingSpeedKey = TEXT("HandCraftingSpeed");
	static constexpr float MinimumHandCraftingSpeed = 1.f;
	// The maintainer (2026-10-25): up to 20.
	static constexpr float MaximumHandCraftingSpeed = 20.f;
	/** The key of the hand mining multiplier option (the ore per swing), 1 to MaximumHandMiningSpeed. */
	static constexpr const TCHAR* HandMiningSpeedKey = TEXT("HandMiningSpeed");
	static constexpr float MaximumHandMiningSpeed = 20.f;
	/** The key of the health regeneration speed option: a multiple of the game's own rate, 1 to MaximumHealthRegenerationSpeed. */
	static constexpr const TCHAR* HealthRegenerationSpeedKey = TEXT("HealthRegenerationSpeed");
	static constexpr float MaximumHealthRegenerationSpeed = 20.f;
	/** The key of the movement speed bonus option: 0 to MaximumMovementSpeedSteps steps of MovementSpeedStep (10%) over the game's own player speed. */
	static constexpr const TCHAR* MovementSpeedKey = TEXT("MovementSpeedBonus");
	// The maintainer (2026-10-25): a 0-20 scale, up to three times the game's speed.
	static constexpr float MaximumMovementSpeedSteps = 20.f;
	static constexpr float MovementSpeedStep = 0.1f;
	/** The key of the "Flight mode" option (default off): whether a double jump can start and end flying. */
	static constexpr const TCHAR* FlightModeKey = TEXT("FlightMode");
	/** The keys of "Collect nearby items" (default off) and its radius, a multiple of 5 m, 1 to MaximumCollectRadiusMultiplier. */
	static constexpr const TCHAR* CollectNearbyKey = TEXT("CollectNearby");
	static constexpr const TCHAR* CollectRadiusMultiplierKey = TEXT("CollectRadiusMultiplier");
	static constexpr float MaximumCollectRadiusMultiplier = 20.f;
	/** The key of the "Short conveyor lifts" option (default on). */
	static constexpr const TCHAR* ShortConveyorLiftsKey = TEXT("ShortConveyorLifts");
	/** The key of the "Inventory slots" entry (a +10 button and the current size; the slots are AMKIPlayerSubsystem's). */
	static constexpr const TCHAR* InventorySlotsKey = TEXT("InventorySlots");
	/** The key of the "Elevation and length readouts" option (default on). */
	static constexpr const TCHAR* HudReadoutsKey = TEXT("HudReadouts");
	/** The key of the "Dismantle unused supports" option (default on). */
	static constexpr const TCHAR* DismantleUnusedSupportsKey = TEXT("DismantleUnusedSupports");
	/** The key of the "Chainsaw cuts boulders" option. */
	static constexpr const TCHAR* ChainsawCutsBouldersKey = TEXT("ChainsawCutsBoulders");
	/** The key of the "Chainsaw clearance radius" option: a multiple of the game's own radius, 1 to MaximumChainsawClearanceMultiplier. */
	static constexpr const TCHAR* ChainsawClearanceMultiplierKey = TEXT("ChainsawClearanceMultiplier");
	static constexpr float MaximumChainsawClearanceMultiplier = 20.f;
	/** The key of the object scanner range bonus option: 0 to MaximumObjectScannerRangeSteps steps of ObjectScannerRangeStep (10%) over the game's own range. */
	static constexpr const TCHAR* ObjectScannerRangeKey = TEXT("ObjectScannerRangeBonus");
	// The maintainer (2026-10-28): a 1-20 scale, up to 200% more range (0 is the game's own).
	static constexpr float MaximumObjectScannerRangeSteps = 20.f;
	static constexpr float ObjectScannerRangeStep = 0.1f;
	/** The key of the "Vegetation regrowth" option (default off). */
	static constexpr const TCHAR* VegetationRegrowthKey = TEXT("VegetationRegrowth");
	/** The key of the "Regrowth time" option: the mean time a cut plant takes to regrow, in hours of play, 1 to 10. */
	static constexpr const TCHAR* RegrowthHoursKey = TEXT("RegrowthHours");
	static constexpr int32 DefaultRegrowthHours = 3;
	static constexpr float MaximumRegrowthHours = 20.f;
	/** The key of "Fill Depot from inventory" (default off), also a checkbox in the inventory's Dimensional Depot panel. */
	static constexpr const TCHAR* FillDepotFromInventoryKey = TEXT("FillDepotFromInventory");

	/**
	 * Builds the options on the class defaults from SML's Blueprint property classes (only they draw in the menu), once,
	 * before the configuration is registered: SML's content is loaded by then, unlike when the class defaults are made.
	 */
	static void BuildOptions();
	/** After registration: moves the options into SML's live root section, so the menu can edit and save them in game. */
	static void AdoptOptions(class UConfigManager* Config);

	/** The hand crafting multiplier (work per Craft Bench tick), 1 (the game's own) to 10; 1 when the option is missing. */
	static float GetHandCraftingSpeed(const UObject* WorldContext);

	/** The hand mining multiplier (ore per swing on a resource node), 1 (the game's own) to 10; 1 when the option is missing. */
	static float GetHandMiningSpeed(const UObject* WorldContext);

	/** The health regeneration speed, a multiple of the game's own rate, 1 to 10; 1 when the option is missing. */
	static float GetHealthRegenerationSpeed(const UObject* WorldContext);

	/** The player movement speed multiplier, 1 (the game's own) to 2 in 10% steps; 1 when the option is missing. */
	static float GetMovementSpeed(const UObject* WorldContext);

	/** Whether a double jump can toggle flying (default off); off when the option is missing. */
	static bool GetFlightMode(const UObject* WorldContext);

	/** Whether "Collect nearby items" is on (default off); off when the option is missing. */
	static bool GetCollectNearby(const UObject* WorldContext);

	/** The collect radius as a multiple of 5 m, 1 to 20 (1 when the option is missing). */
	static float GetCollectRadiusMultiplier(const UObject* WorldContext);

	/** Whether conveyor lifts build short anywhere (default on); on when the option is missing. */
	static bool GetShortConveyorLifts(const UObject* WorldContext);

	/** Whether the HUD shows the player's elevation and the build hologram's elevation and length (default on); on when the option is missing. */
	static bool GetHudReadouts(const UObject* WorldContext);

	/** Whether dismantling a belt or pipe also dismantles the supports it leaves unused (default on); on when the option is missing. */
	static bool GetDismantleUnusedSupports(const UObject* WorldContext);

	/** Whether the chainsaw cuts the game's rock foliage (default on); on when the option is missing. */
	static bool GetChainsawCutsBoulders(const UObject* WorldContext);

	/** The chainsaw clearance radius as a multiple of the game's own, 1 to 10 (1 when the option is missing). */
	static float GetChainsawClearanceMultiplier(const UObject* WorldContext);

	/** The object scanner's range multiplier, 1 (the game's own) to 3 in 10% steps; 1 when the option is missing. */
	static float GetObjectScannerRange(const UObject* WorldContext);

	/** Whether cut vegetation regrows (default off); off when the option is missing. */
	static bool GetVegetationRegrowth(const UObject* WorldContext);

	/** The mean regrowth time in hours of play, 1 to 10 (DefaultRegrowthHours when the option is missing). */
	static float GetRegrowthHours(const UObject* WorldContext);

	/** Whether "Fill Depot from inventory" is on (default off); off when the option is missing. */
	static bool GetFillDepotFromInventory(const UObject* WorldContext);
	/** Sets "Fill Depot from inventory" (the depot panel's checkbox) and saves it. */
	static void SetFillDepotFromInventory(const UObject* WorldContext, bool bFill);
};
