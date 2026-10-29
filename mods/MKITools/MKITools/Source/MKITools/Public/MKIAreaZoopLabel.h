#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MKIAreaZoopLabel.generated.h"

class UWidget;

/**
 * Foundation area zoop's count on the HUD (the maintainer, 2026-10-29): the game's "Currently Building" label counts a
 * zoop as one line ("(x10)"); while a foundation zoops in two directions it reads the rectangle instead ("(10x10)"),
 * along by across. Local player only. The label is the game's own text widget, found by its text and rewritten while the
 * zoop is a rectangle; the game writes its own count back when the zoop changes. The first one found is logged.
 */
UCLASS()
class MKITOOLS_API UMKIAreaZoopLabelSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** How often the game's widgets are searched for the label while it is not found yet. */
	static constexpr float SearchIntervalSeconds = 0.25f;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** The text with its line count "(xN)" (with bOwnCount also an earlier "(AxB)") replaced by "(AxB)"; unset when it has none. */
	static TOptional<FString> Relabel(const FString& Text, int32 Along, int32 Across, bool bOwnCount);

private:
	/** Rewrites the widget's text if it shows a count (see Relabel); false when it has none. */
	static bool Apply(UWidget* Widget, int32 Along, int32 Across, bool bOwnCount);

	TWeakObjectPtr<UWidget> Label;
	float SearchCooldown = 0.f;
	bool bLogged = false;
};
