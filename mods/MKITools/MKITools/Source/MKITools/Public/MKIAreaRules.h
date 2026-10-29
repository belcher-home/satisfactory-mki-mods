#pragma once

#include "CoreMinimal.h"

/**
 * The geometry of the Area dismantle mode (#402). As redesigned by the maintainer on 2026-10-25: the area is always a
 * world-aligned cube whose bottom centre is set by what is aimed at. On a building (an item or a foundation) it is that
 * building's centre at its lowest point, so the building is inside; on the ground it is the point aimed at. The edge is 16
 * to 96 m in 16 m steps; the height equals the edge. There are no filters: everything that reaches more than ReachCm into
 * the cube goes, the aimed building and its neighbours included, belts, pipes, rails and power lines that only cross it too.
 * What lies wholly below the bottom face (the foundation under a stacked one, the floor under an aimed machine) stays;
 * a spline may lie up to SplineBelowCm below it. Pure functions, no world.
 */
struct MKITOOLS_API FMKIAreaRules
{
	/** The edge grows by this much per scroll step, from one step (16 m) to MaxStep steps (96 m). */
	static constexpr double StepCm = 1600.0;
	static constexpr int32 MaxStep = 6;
	/** A building must reach this far into the cube to be dismantled; one that only touches a face stays. */
	static constexpr double ReachCm = 1.0;
	/** How far away the aimed building or ground may be. */
	static constexpr double AimRangeCm = 15000.0;
	/** Only ground facing up (its normal's Z at least this) carries a cube; a building carries one whichever face is aimed at. */
	static constexpr double MinimumSurfaceNormalZ = 0.7;
	/** Spline buildables (belts, pipes, rails, roads) are tested along their spline at this spacing. */
	static constexpr double SplineSampleCm = 50.0;
	/**
	 * A spline is the line its building is drawn along: a belt lying on a floor runs along the floor's top. One may lie
	 * this far below the cube's bottom face and still be in, so the aimed belt (the bottom face runs along its spline) and
	 * the belts beside it go. A belt on the floor under a stacked foundation lies a whole foundation lower and stays.
	 */
	static constexpr double SplineBelowCm = 50.0;
	/**
	 * An aimed item (a machine, a container, a belt support) puts the cube's bottom this far above its base, past the lip
	 * of the floor it stands on (a foundation's mesh rises about 6 cm above its top), so that floor stays.
	 */
	static constexpr double ItemFootCm = 10.0;

	/** The step clamped to 1..MaxStep. */
	static int32 ClampStep(int32 Step);
	/** The cube's edge (and height) for a step: 1600, 3200, ... 9600 cm. */
	static double EdgeCm(int32 Step);
	/** The cube's bottom centre for an aimed building: the centre of its bounds, at their lowest point. */
	static FVector BottomCenter(const FBox& AimedBounds);
	/** The world-aligned cube standing on a bottom centre. */
	static FBox Cube(const FVector& BottomCenter, int32 Step);
	/** The part of the cube a building must reach into: the cube shrunk by ReachCm on every side. */
	static FBox Interior(const FBox& Cube);
	/** Whether a box in an actor's local space, placed by Transform, overlaps the interior (separating axis test). */
	static bool OrientedBoxReaches(const FBox& LocalBox, const FTransform& Transform, const FBox& Interior);
	/** The interior a spline's path is tested against: the interior reaching SplineBelowCm below the bottom face. */
	static FBox SplineInterior(const FBox& Interior);
	/** Whether a path of points (a spline's samples, a wire's two ends) enters the interior anywhere along it. */
	static bool PathReaches(TConstArrayView<FVector> Points, const FBox& Interior);
	/** Dismantle order: the highest top first, so nothing is left standing on what went before it. */
	static void SortTopDown(TArray<TPair<double, int32>>& TopsAndIndices);
};
