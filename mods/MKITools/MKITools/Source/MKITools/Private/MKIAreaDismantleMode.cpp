#include "MKIAreaDismantleMode.h"

#define LOCTEXT_NAMESPACE "MKITools"

UMKIAreaDismantleMode::UMKIAreaDismantleMode()
{
	mDisplayName = LOCTEXT("AreaDismantleMode", "Area");
	// The game caches this as the state's multi-dismantle limit while the mode is selected; each batch stays within it
	// (Config/AccessTransformers.ini makes this class a friend of the descriptor).
	mMultiDismantleLimit = BatchSize;
}

#undef LOCTEXT_NAMESPACE
