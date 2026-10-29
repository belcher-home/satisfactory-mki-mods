#include "MKIHudReadouts.h"

#include "Components/CapsuleComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "FGCharacterPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Hologram/FGConveyorLiftHologram.h"
#include "Hologram/FGHologram.h"
#include "Hologram/FGSplineHologram.h"
#include "Hologram/FGWireHologram.h"
#include "MKIToolsConfiguration.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MKITools"

bool UMKIHudReadoutsSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	// The HUD belongs to a local player: never on a dedicated server.
	return World && World->IsGameWorld() && !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UMKIHudReadoutsSubsystem::Deinitialize()
{
	RemoveOverlay();
	Super::Deinitialize();
}

TStatId UMKIHudReadoutsSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMKIHudReadoutsSubsystem, STATGROUP_Tickables);
}

FText UMKIHudReadoutsSubsystem::FormatMetres(double Centimetres)
{
	FNumberFormattingOptions Options;
	Options.MinimumFractionalDigits = 1;
	Options.MaximumFractionalDigits = 1;
	return FText::Format(LOCTEXT("Metres", "{0} m"), FText::AsNumber(Centimetres / 100.0, &Options));
}

void UMKIHudReadoutsSubsystem::Tick(float DeltaTime)
{
	PlayerText = FText::GetEmpty();
	HologramText = FText::GetEmpty();
	APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	AFGCharacterPlayer* Character = Controller ? Cast<AFGCharacterPlayer>(Controller->GetPawn()) : nullptr;
	// A menu, the map or a machine's window shows the cursor: the readouts make way for it.
	if (!Character || !Character->IsLocallyControlled() || Controller->bShowMouseCursor || !UMKIToolsConfiguration::GetHudReadouts(this)) return;
	EnsureOverlay();
	// Where the player stands: the bottom of the capsule, their feet.
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const double FeetZ = Character->GetActorLocation().Z - (Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.f);
	PlayerText = FText::Format(LOCTEXT("PlayerElevation", "Elevation {0}"), FormatMetres(FeetZ));
	AFGBuildGun* Gun = Character->GetBuildGun();
	if (!Gun || !Gun->IsEquipped() || !Gun->IsInState(EBuildGunState::BGS_BUILD)) return;
	const UFGBuildGunStateBuild* Build = Cast<UFGBuildGunStateBuild>(Gun->GetBuildGunStateFor(EBuildGunState::BGS_BUILD));
	HologramText = DescribeHologram(Build ? Build->GetHologram() : nullptr);
}

FText UMKIHudReadoutsSubsystem::DescribeHologram(const AFGHologram* Hologram)
{
	// A hologram aimed at nothing (the sky) is hidden: nothing to measure.
	if (!IsValid(Hologram) || Hologram->IsHidden()) return FText::GetEmpty();
	double ElevationCm = Hologram->GetActorLocation().Z;
	TOptional<FText> Length;
	if (const AFGConveyorLiftHologram* Lift = Cast<AFGConveyorLiftHologram>(Hologram))
	{
		// A lift stands on its bottom (the hologram's location) and rises by its height, as the game steps it.
		Length = FText::Format(LOCTEXT("LiftHeight", "Height {0}"), FormatMetres(Lift->GetHeight()));
	}
	else if (const AFGWireHologram* Wire = Cast<AFGWireHologram>(Hologram))
	{
		// The game's own length of the wire being placed (what its maximum length is checked against), once its start is set.
		const float WireLength = Wire->mCurrentConnection > 0 ? Wire->GetLength() : 0.f;
		if (WireLength >= MinimumLengthCm) Length = FText::Format(LOCTEXT("WireLength", "Length {0}"), FormatMetres(WireLength));
	}
	else if (Hologram->IsA<AFGSplineHologram>())
	{
		// Belts, pipes, rails, hypertubes and MKI roads: the spline's length, and the end being placed, which follows the aim.
		if (const USplineComponent* Spline = Hologram->FindComponentByClass<USplineComponent>())
		{
			const float SplineLength = Spline->GetSplineLength();
			if (SplineLength >= MinimumLengthCm && Spline->GetNumberOfSplinePoints() > 1)
			{
				ElevationCm = Spline->GetLocationAtSplinePoint(Spline->GetNumberOfSplinePoints() - 1, ESplineCoordinateSpace::World).Z;
				Length = FText::Format(LOCTEXT("SplineLength", "Length {0}"), FormatMetres(SplineLength));
			}
		}
	}
	const FText Elevation = FText::Format(LOCTEXT("HologramElevation", "Elevation {0}"), FormatMetres(ElevationCm));
	return Length.IsSet() ? FText::Format(LOCTEXT("HologramElevationAndLength", "{0}    {1}"), Elevation, Length.GetValue()) : Elevation;
}

void UMKIHudReadoutsSubsystem::EnsureOverlay()
{
	if (Overlay.IsValid()) return;
	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	if (!Viewport) return;
	TWeakObjectPtr<UMKIHudReadoutsSubsystem> Weak(this);
	// The game's HUD style: white text with a dark drop shadow.
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 13);
	const auto Line = [Weak, Font](bool bPlayer)
	{
		return SNew(STextBlock)
			.Font(Font)
			.Justification(ETextJustify::Center)
			.ColorAndOpacity(FLinearColor::White)
			.ShadowOffset(FVector2D(1.f, 1.f))
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f))
			.Text_Lambda([Weak, bPlayer]() { return Weak.IsValid() ? (bPlayer ? Weak->PlayerText : Weak->HologramText) : FText::GetEmpty(); })
			.Visibility_Lambda([Weak, bPlayer]() { return Weak.IsValid() && !(bPlayer ? Weak->PlayerText : Weak->HologramText).IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; });
	};
	Overlay = SNew(SOverlay).Visibility(EVisibility::HitTestInvisible)
		// The player's elevation, top centre under the compass.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0.f, 96.f, 0.f, 0.f))
		[
			Line(true)
		]
		// The hologram's, under the crosshair (below the game's own placement messages and MKI Tools' hold bars).
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMargin(0.f, 240.f, 0.f, 0.f))
		[
			Line(false)
		];
	Viewport->AddViewportWidgetContent(Overlay.ToSharedRef(), 5);
}

void UMKIHudReadoutsSubsystem::RemoveOverlay()
{
	if (Overlay.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr) Viewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
		Overlay.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
