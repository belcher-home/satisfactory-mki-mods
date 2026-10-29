#pragma once

#include "CoreMinimal.h"
#include "FGDismantleModeDescriptor.h"
#include "MKIAreaDismantleMode.generated.h"

/**
 * The Area dismantle mode (#402): listed after Default, Blueprint and Terrain (the module adds it to
 * UFGBuildGunStateDismantle::GetSupportedBuildModes). The gun shows a cube standing on the aimed surface (FMKIAreaRules);
 * holding fire dismantles everything that reaches into it, through the game's own dismantle (FMKIAreaDismantle).
 */
UCLASS()
class MKITOOLS_API UMKIAreaDismantleMode : public UFGDismantleModeDescriptor
{
	GENERATED_BODY()

public:
	/** How many buildings one batch sends to the game's dismantle; the mode's multi-dismantle limit is set to it. */
	static constexpr int32 BatchSize = 200;

	UMKIAreaDismantleMode();
};
