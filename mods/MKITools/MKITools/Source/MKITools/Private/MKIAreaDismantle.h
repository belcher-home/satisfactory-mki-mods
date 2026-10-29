#pragma once

#include "CoreMinimal.h"

class AActor;
class UFGBuildGunState;
class UFGBuildGunStateDismantle;
struct FDismantleLightweightBundle;

/**
 * The dismantle gun in Area mode (UMKIAreaDismantleMode, #402), driven from hooks on the vanilla dismantle state. Each tick
 * it traces from the player's view to an upward-facing surface and draws the cube standing on it (FMKIAreaRules); the
 * scroll wheel sizes it. The selection (actors, and lightweight foundations and walls by class and index) is gathered
 * when the cube moves, outlined, and counted with its refund in a line under the crosshair. Holding fire queues the
 * selection top down and sends it, a batch per tick, to the game's own Server_DismantleActors, which checks each piece,
 * refunds it to the inventory (then a dismantle crate) and removes it. A friend of the vanilla state
 * (Config/AccessTransformers.ini) so it can call that RPC and clear the vanilla aim.
 */
class FMKIAreaDismantle
{
public:
	static bool IsAreaMode(const UFGBuildGunStateDismantle* State);
	/** Replaces the vanilla tick in Area mode: the cube, its size, the selection and holding fire to dismantle it. */
	static void Tick(UFGBuildGunStateDismantle* State, float DeltaTime);
	/** Replaces the vanilla primary fire in Area mode (the hold is timed by Tick). */
	static void PrimaryFire(UFGBuildGunStateDismantle* State);
	/** How far the Area hold has run (0-1), for the HUD's progress bar; negative when no Area hold runs on this state. */
	static float HoldProgress(const UFGBuildGunState* State);
	/** Drops the cube, the outlines and the line (leaving the mode or the dismantle state); a queued dismantle runs on. */
	static void Clear(UFGBuildGunStateDismantle* State);
	/** One batch to the game's own dismantle (Server_DismantleActors, which only this friend class may call). */
	static void SendBatch(UFGBuildGunStateDismantle* State, const TArray<AActor*>& Actors, const TArray<FDismantleLightweightBundle>& Bundles);
};
