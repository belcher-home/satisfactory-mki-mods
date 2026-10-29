#include "MKIAreaZoopLabel.h"

#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "FGCharacterPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Hologram/FGFoundationHologram.h"
#include "Internationalization/Regex.h"
#include "MKITools.h"
#include "UObject/UObjectIterator.h"

bool UMKIAreaZoopLabelSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	// The HUD belongs to a local player: never on a dedicated server.
	return World && World->IsGameWorld() && !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

TStatId UMKIAreaZoopLabelSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMKIAreaZoopLabelSubsystem, STATGROUP_Tickables);
}

TOptional<FString> UMKIAreaZoopLabelSubsystem::Relabel(const FString& Text, int32 Along, int32 Across, bool bOwnCount)
{
	// The game's line count, and on the label already found also our own rectangle count (the zoop may have changed size).
	const FRegexPattern Pattern(bOwnCount ? FString(TEXT("\\((\\d+)?x\\d+\\)")) : FString(TEXT("\\(x\\d+\\)")));
	FRegexMatcher Matcher(Pattern, Text);
	if (!Matcher.FindNext()) return {};
	const int32 Begin = Matcher.GetMatchBeginning(), End = Matcher.GetMatchEnding();
	return Text.Left(Begin) + FString::Printf(TEXT("(%dx%d)"), Along, Across) + Text.Mid(End);
}

bool UMKIAreaZoopLabelSubsystem::Apply(UWidget* Widget, int32 Along, int32 Across, bool bOwnCount)
{
	UTextBlock* Text = Cast<UTextBlock>(Widget);
	URichTextBlock* Rich = Cast<URichTextBlock>(Widget);
	if (!Text && !Rich) return false;
	const FString Old = Text ? Text->GetText().ToString() : Rich->GetText().ToString();
	const TOptional<FString> New = Relabel(Old, Along, Across, bOwnCount);
	if (!New.IsSet()) return false;
	if (New.GetValue() == Old) return true;
	if (Text) Text->SetText(FText::FromString(New.GetValue()));
	else Rich->SetText(FText::FromString(New.GetValue()));
	return true;
}

void UMKIAreaZoopLabelSubsystem::Tick(float DeltaTime)
{
	SearchCooldown -= DeltaTime;
	APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	AFGCharacterPlayer* Character = Controller ? Cast<AFGCharacterPlayer>(Controller->GetPawn()) : nullptr;
	if (!Character || !Character->IsLocallyControlled()) return;
	AFGBuildGun* Gun = Character->GetBuildGun();
	if (!Gun || !Gun->IsEquipped() || !Gun->IsInState(EBuildGunState::BGS_BUILD)) return;
	const UFGBuildGunStateBuild* Build = Cast<UFGBuildGunStateBuild>(Gun->GetBuildGunStateFor(EBuildGunState::BGS_BUILD));
	const AFGFoundationHologram* Hologram = Build ? Cast<AFGFoundationHologram>(Build->GetHologram()) : nullptr;
	if (!IsValid(Hologram)) return;
	const FIntVector Zoop = Hologram->mDesiredZoop;
	// A line (or no zoop) keeps the game's own count.
	if (Zoop.X == 0 || Zoop.Y == 0) return;
	const int32 Along = FMath::Abs(Zoop.X) + 1, Across = FMath::Abs(Zoop.Y) + 1;
	if (UWidget* Known = Label.Get())
	{
		if (Apply(Known, Along, Across, true) || Known->GetCachedWidget().IsValid()) return;
		Label.Reset();
	}
	if (SearchCooldown > 0.f) return;
	SearchCooldown = SearchIntervalSeconds;
	// The game's label: a text widget on screen (its Slate widget made) whose text holds a line count.
	for (TObjectIterator<UWidget> It; It; ++It)
	{
		UWidget* Widget = *It;
		if (!Widget->IsA<UTextBlock>() && !Widget->IsA<URichTextBlock>()) continue;
		if (Widget->GetWorld() != GetWorld() || !Widget->GetCachedWidget().IsValid() || !Widget->IsVisible()) continue;
		const FString Before = Widget->IsA<UTextBlock>() ? Cast<UTextBlock>(Widget)->GetText().ToString() : Cast<URichTextBlock>(Widget)->GetText().ToString();
		if (!Apply(Widget, Along, Across, false)) continue;
		Label = Widget;
		if (!bLogged)
		{
			bLogged = true;
			UE_LOG(LogMKITools, Log, TEXT("Area zoop label: %s in %s, \"%s\" -> %d x %d"), *Widget->GetName(), *GetNameSafe(Widget->GetOuter()), *Before, Along, Across);
		}
		return;
	}
}
