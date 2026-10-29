#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MKIHudReadouts.generated.h"

class AFGCharacterPlayer;
class AFGHologram;
class SWidget;

/**
 * Elevation and length readouts on the HUD (the maintainer, 2026-10-27). Always: the elevation of the player's feet, top
 * centre under the compass. With the build gun building: under the crosshair, the elevation of the hologram (of the end
 * being placed, for belts, pipes, rails, hypertubes and roads) and, where the hologram has one, its length: the spline's
 * length for belts, pipes, rails, hypertubes and roads, the height for conveyor lifts, and the wire's length for power
 * lines. Elevations are the world height in metres (world Z / 100), as MKI Bridge reports positions. The option
 * "Elevation and length readouts" (default on) hides both. Local player only, read every frame; nothing is changed.
 */
UCLASS()
class MKITOOLS_API UMKIHudReadoutsSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** A spline shorter than this has not been dragged out yet (its first click), so it shows no length. */
	static constexpr double MinimumLengthCm = 1.0;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** "123.4 m": a world distance in centimetres as metres, one decimal. */
	static FText FormatMetres(double Centimetres);

private:
	/** The hologram's line: its elevation, and its length or height when it has one. Empty when there is nothing to show. */
	static FText DescribeHologram(const AFGHologram* Hologram);
	void EnsureOverlay();
	void RemoveOverlay();

	/** The overlay reads them; an empty text hides its line. */
	FText PlayerText;
	FText HologramText;
	TSharedPtr<SWidget> Overlay;
};
