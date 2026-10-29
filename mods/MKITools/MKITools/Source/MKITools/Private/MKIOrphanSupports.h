#pragma once

#include "CoreMinimal.h"

class AActor;
class UFGBuildGunStateDismantle;
class UObject;
class UWorld;

/**
 * Unused supports go with their belts and pipes (the maintainer, 2026-10-29). Dismantling a belt, lift, pipe or hypertube
 * also dismantles each support one of its ends rests on (conveyor poles, wall and ceiling mounts, pipe and hypertube
 * supports, stacked or not) when no other belt or pipe rests on that support; wall and floor holes stay. The server adds
 * them to what the game's own dismantle is sent (Server_DismantleActors) and to its refund preview, so they are checked
 * and refunded as the rest. In the Default mode the dismantle gun outlines them red with the belt or pipe it aims at or
 * has selected. A friend of the vanilla dismantle state (Config/AccessTransformers.ini) for its aim and mode.
 */
class FMKIOrphanSupports
{
public:
	/** The supports that only the belts and pipes among Actors rest on: dismantling Actors leaves them holding nothing. */
	static void Find(UWorld* World, const TArray<AActor*>& Actors, TArray<AActor*>& OutSupports);
	/** Actors with the supports they leave unused added, when the option is on; Actors unchanged otherwise. */
	static bool AddUnused(const UObject* WorldContext, const TArray<AActor*>& Actors, TArray<AActor*>& OutActors);
	/** After the vanilla tick: in the Default mode, outlines the supports the aimed or selected belts and pipes leave unused. */
	static void Tick(UFGBuildGunStateDismantle* State);
	/** Drops the outlines (leaving the Default mode or the dismantle state). */
	static void Clear(UFGBuildGunStateDismantle* State);
};
