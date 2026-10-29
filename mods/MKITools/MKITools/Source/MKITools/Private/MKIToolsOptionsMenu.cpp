#include "MKIToolsOptionsMenu.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "FGCharacterPlayer.h"
#include "FGGameUserSettings.h"
#include "FGInventoryComponent.h"
#include "GameFramework/PlayerController.h"
#include "MKIInventorySlotsOption.h"
#include "MKIPlayerSubsystem.h"
#include "MKITools.h"
#include "MKIToolsConfiguration.h"
#include "Settings/FGUserSetting.h"
#include "Settings/FGUserSettingApplyType.h"

#define LOCTEXT_NAMESPACE "MKITools"

UMKIToolsSettingsCategory::UMKIToolsSettingsCategory()
{
	mDisplayName = LOCTEXT("OptionsTab", "MKI Tools");
	// After the game's own tabs.
	mMenuPriority = 100.f;
}

UMKIToolsSettingsCategory_Player::UMKIToolsSettingsCategory_Player()
{
	mDisplayName = LOCTEXT("OptionsPlayer", "Player");
	mMenuPriority = 0.f;
}

UMKIToolsSettingsCategory_Items::UMKIToolsSettingsCategory_Items()
{
	mDisplayName = LOCTEXT("OptionsItems", "Items & Inventory");
	mMenuPriority = 1.f;
}

UMKIToolsSettingsCategory_Building::UMKIToolsSettingsCategory_Building()
{
	mDisplayName = LOCTEXT("OptionsBuilding", "Building");
	mMenuPriority = 2.f;
}

UMKIToolsSettingsCategory_Equipment::UMKIToolsSettingsCategory_Equipment()
{
	mDisplayName = LOCTEXT("OptionsEquipment", "Equipment");
	mMenuPriority = 3.f;
}

UMKIToolsSettingsCategory_World::UMKIToolsSettingsCategory_World()
{
	mDisplayName = LOCTEXT("OptionsWorld", "World");
	mMenuPriority = 4.f;
}

namespace
{
	const TCHAR* const StrIdPrefix = TEXT("MKITools.");
}

void UMKIToolsOptionsMenu::Start(UGameInstance* InGameInstance)
{
	GameInstance = InGameInstance;
	using M = UMKIToolsConfiguration;
	// The maintainer (2026-10-29): grouped like the game's own options, names in the game's title case. The order here is
	// the order in the menu.
	Entries = {
		{ M::HandCraftingSpeedKey, UMKIToolsSettingsCategory_Player::StaticClass(), LOCTEXT("OptHandCrafting", "Hand Crafting Multiplier"), M::MinimumHandCraftingSpeed, M::MaximumHandCraftingSpeed },
		{ M::HandMiningSpeedKey, UMKIToolsSettingsCategory_Player::StaticClass(), LOCTEXT("OptHandMining", "Hand Mining Multiplier"), 1.f, M::MaximumHandMiningSpeed },
		{ M::HealthRegenerationSpeedKey, UMKIToolsSettingsCategory_Player::StaticClass(), LOCTEXT("OptHealthRegeneration", "Health Regeneration Speed"), 1.f, M::MaximumHealthRegenerationSpeed },
		{ M::MovementSpeedKey, UMKIToolsSettingsCategory_Player::StaticClass(), LOCTEXT("OptMovementSpeed", "Movement Speed Bonus (x 10%)"), 0.f, M::MaximumMovementSpeedSteps, true },
		{ M::FlightModeKey, UMKIToolsSettingsCategory_Player::StaticClass(), LOCTEXT("OptFlightMode", "Flight Mode") },
		{ M::InventorySlotsKey, UMKIToolsSettingsCategory_Items::StaticClass(), LOCTEXT("OptInventorySlots", "Inventory Slots"), 0.f, 0.f, false, true },
		{ M::CollectNearbyKey, UMKIToolsSettingsCategory_Items::StaticClass(), LOCTEXT("OptCollectNearby", "Collect Nearby Items") },
		{ M::CollectRadiusMultiplierKey, UMKIToolsSettingsCategory_Items::StaticClass(), LOCTEXT("OptCollectRadius", "Collect Radius (x 5 m)"), 1.f, M::MaximumCollectRadiusMultiplier },
		{ M::FillDepotFromInventoryKey, UMKIToolsSettingsCategory_Items::StaticClass(), LOCTEXT("OptFillDepot", "Fill Depot from Inventory") },
		{ M::ShortConveyorLiftsKey, UMKIToolsSettingsCategory_Building::StaticClass(), LOCTEXT("OptShortLifts", "Short Conveyor Lifts") },
		{ M::HudReadoutsKey, UMKIToolsSettingsCategory_Building::StaticClass(), LOCTEXT("OptHudReadouts", "Elevation and Length Readouts") },
		{ M::DismantleUnusedSupportsKey, UMKIToolsSettingsCategory_Building::StaticClass(), LOCTEXT("OptUnusedSupports", "Dismantle Unused Supports") },
		{ M::ChainsawCutsBouldersKey, UMKIToolsSettingsCategory_Equipment::StaticClass(), LOCTEXT("OptChainsawBoulders", "Chainsaw Cuts Boulders") },
		{ M::ChainsawClearanceMultiplierKey, UMKIToolsSettingsCategory_Equipment::StaticClass(), LOCTEXT("OptChainsawRadius", "Chainsaw Clearance Radius (x 5 m)"), 1.f, M::MaximumChainsawClearanceMultiplier },
		{ M::ObjectScannerRangeKey, UMKIToolsSettingsCategory_Equipment::StaticClass(), LOCTEXT("OptScannerRange", "Object Scanner Range Bonus (x 10%)"), 0.f, M::MaximumObjectScannerRangeSteps, true },
		{ M::VegetationRegrowthKey, UMKIToolsSettingsCategory_World::StaticClass(), LOCTEXT("OptRegrowth", "Vegetation Regrowth") },
		{ M::RegrowthHoursKey, UMKIToolsSettingsCategory_World::StaticClass(), LOCTEXT("OptRegrowthHours", "Regrowth Time (hours of play)"), 1.f, M::MaximumRegrowthHours },
	};
	if (!TickHandle.IsValid()) TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMKIToolsOptionsMenu::Tick), 1.f);
}

void UMKIToolsOptionsMenu::Stop()
{
	if (TickHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	TickHandle.Reset();
}

UConfigProperty* UMKIToolsOptionsMenu::FindOption(const TCHAR* Key) const
{
	const UConfigManager* Config = GameInstance.IsValid() ? GameInstance->GetSubsystem<UConfigManager>() : nullptr;
	FConfigId Id; Id.ModReference = TEXT("MKITools");
	const UConfigPropertySection* Root = Config ? Config->GetConfigurationRootSection(Id) : nullptr;
	const TObjectPtr<UConfigProperty>* Property = Root ? Root->SectionProperties.Find(Key) : nullptr;
	return Property ? Property->Get() : nullptr;
}

FVariant UMKIToolsOptionsMenu::OptionValue(const UConfigProperty* Option)
{
	if (const UConfigPropertyInteger* Integer = Cast<UConfigPropertyInteger>(Option)) return FVariant(static_cast<float>(Integer->Value));
	if (const UConfigPropertyBool* Bool = Cast<UConfigPropertyBool>(Option)) return FVariant(Bool->Value);
	return FVariant();
}

namespace
{
	/** A setting's value as a number (bools 0 or 1), for comparing the game's value with SML's. */
	bool VariantNumber(const FVariant& Value, double& OutNumber)
	{
		switch (Value.GetType())
		{
		case EVariantTypes::Bool: OutNumber = Value.GetValue<bool>() ? 1.0 : 0.0; return true;
		case EVariantTypes::Float: OutNumber = Value.GetValue<float>(); return true;
		case EVariantTypes::Double: OutNumber = Value.GetValue<double>(); return true;
		case EVariantTypes::Int32: OutNumber = Value.GetValue<int32>(); return true;
		default: return false;
		}
	}
}

UFGUserSetting* UMKIToolsOptionsMenu::BuildSetting(const FEntry& Entry, const UConfigProperty* Option, int32 Index)
{
	UFGUserSetting* Setting = NewObject<UFGUserSetting>(this, *FString::Printf(TEXT("MKITools_%s"), Entry.Key));
	Setting->StrId = FString(StrIdPrefix) + Entry.Key;
	Setting->DisplayName = Entry.DisplayName;
	Setting->ToolTip = Option ? Option->Tooltip : FText::GetEmpty();
	Setting->WidgetsToCreate.Add(FSettingsWidgetLocationDescriptor(UMKIToolsSettingsCategory::StaticClass(), Entry.Section, nullptr, static_cast<float>(Index)));
	// The game's normal apply cycle: a change waits for Apply, as the game's own options do.
	Setting->ApplyType = TSoftClassPtr<UFGUserSettingApplyType>(UFGUserSettingApplyType::StaticClass());
	Setting->ManagerTypeAvailability = UFGGameUserSettings::StaticClass();
	// Left at its default (Development), a setting is hidden in the shipped game.
	Setting->ShowInBuilds = EIncludeInBuilds::IIB_PublicBuilds;
	if (Entry.Maximum > Entry.Minimum)
	{
		UFGUserSetting_Slider* Slider = NewObject<UFGUserSetting_Slider>(Setting);
		// The value and the number shown are the same whole number (as the game's Autosave Interval).
		Slider->MinValue = Slider->MinDisplayValue = Entry.Minimum;
		Slider->MaxValue = Slider->MaxDisplayValue = Entry.Maximum;
		Slider->MaxFractionalDigits = 0;
		Slider->ShowZeroAsOff = Entry.bZeroIsOff;
		const UConfigPropertyInteger* Integer = Cast<UConfigPropertyInteger>(Option);
		Slider->DefaultSliderValue = Integer ? static_cast<float>(Integer->DefaultValue) : Entry.Minimum;
		Setting->ValueSelector = Slider;
	}
	else
	{
		UFGUserSetting_CheckBox* CheckBox = NewObject<UFGUserSetting_CheckBox>(Setting);
		const UConfigPropertyBool* Bool = Cast<UConfigPropertyBool>(Option);
		CheckBox->DefaultCheckBoxValue = Bool && !Entry.bInventorySlots ? Bool->DefaultValue : false;
		Setting->ValueSelector = CheckBox;
		if (Entry.bInventorySlots) Setting->CustomValueSelectorWidget = TSoftClassPtr<UFGOptionsValueController>(UMKIInventorySlotsController::StaticClass());
	}
	return Setting;
}

bool UMKIToolsOptionsMenu::Install(UFGGameUserSettings* UserSettings)
{
	if (!UserSettings || !UserSettings->HasInitialized()) return false;
	if (Settings.IsEmpty())
	{
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			const UConfigProperty* Option = FindOption(Entries[Index].Key);
			// SML registers the options before the game sets up its settings; without them there is nothing to show.
			if (!Option) return false;
		}
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			const FEntry& Entry = Entries[Index];
			const UConfigProperty* Option = FindOption(Entry.Key);
			UFGUserSetting* Setting = BuildSetting(Entry, Option, Index);
			UFGUserSettingApplyType* ApplyType = UFGUserSettingApplyType::GetUserSettingApplyType(UserSettings, Setting);
			if (!ApplyType)
			{
				UE_LOG(LogMKITools, Warning, TEXT("Options: the game made no setting for %s; it stays under Mods > MKI Tools"), *Setting->StrId);
				Settings.Add(Setting); ApplyTypes.Add(nullptr);
				continue;
			}
			if (!Entry.bInventorySlots) ApplyType->ForceSetValue(OptionValue(Option));
			ApplyType->AddSubscriber(FOnOptionUpdated::CreateUObject(this, &UMKIToolsOptionsMenu::OnOptionUpdated));
			Settings.Add(Setting); ApplyTypes.Add(ApplyType);
		}
		UE_LOG(LogMKITools, Log, TEXT("Options: %d settings built for Options > MKI Tools"), ApplyTypes.Num());
	}
	int32 Added = 0;
	for (int32 Index = 0; Index < Settings.Num(); ++Index)
	{
		UFGUserSettingApplyType* ApplyType = ApplyTypes[Index];
		if (!ApplyType) continue;
		TObjectPtr<UFGUserSettingApplyType>& Slot = UserSettings->mUserSettings.FindOrAdd(Settings[Index]->StrId);
		if (Slot != ApplyType) { Slot = ApplyType; ++Added; }
	}
	if (Added) UE_LOG(LogMKITools, Log, TEXT("Options: %d settings added to the game's Options menu"), Added);
	return true;
}

void UMKIToolsOptionsMenu::FollowConfig()
{
	for (int32 Index = 0; Index < ApplyTypes.Num(); ++Index)
	{
		UFGUserSettingApplyType* ApplyType = ApplyTypes[Index];
		if (!ApplyType || Entries[Index].bInventorySlots || ApplyType->HasPendingChanges()) continue;
		const FVariant Wanted = OptionValue(FindOption(Entries[Index].Key));
		double Shown = 0.0, Config = 0.0;
		if (!VariantNumber(Wanted, Config)) continue;
		if (VariantNumber(ApplyType->GetAppliedValue(), Shown) && FMath::RoundToInt(Shown) == FMath::RoundToInt(Config)) continue;
		ApplyType->ForceSetValue(Wanted);
	}
}

void UMKIToolsOptionsMenu::OnOptionUpdated(FString StrId, FVariant Value)
{
	if (!StrId.StartsWith(StrIdPrefix)) return;
	UConfigProperty* Option = FindOption(*StrId.RightChop(FCString::Strlen(StrIdPrefix)));
	double Number = 0.0;
	if (!Option || Option->IsA<UMKIInventorySlotsOption>() || !VariantNumber(Value, Number)) return;
	if (UConfigPropertyInteger* Integer = Cast<UConfigPropertyInteger>(Option))
	{
		const int32 Whole = FMath::RoundToInt(Number);
		if (Integer->Value == Whole) return;
		Integer->Value = Whole;
	}
	else if (UConfigPropertyBool* Bool = Cast<UConfigPropertyBool>(Option))
	{
		const bool bOn = Number >= 0.5;
		if (Bool->Value == bOn) return;
		Bool->Value = bOn;
	}
	else return;
	// SML saves a property marked dirty (its root section hands it to the config manager).
	Option->MarkDirty();
	UE_LOG(LogMKITools, Log, TEXT("Options: %s set to %g"), *StrId, Number);
}

void UMKIToolsOptionsMenu::SelfTest(UFGGameUserSettings* UserSettings)
{
	APlayerController* Controller = GameInstance.IsValid() ? GameInstance->GetFirstLocalPlayerController() : nullptr;
	if (!Controller || !Controller->GetWorld()) return;
	bSelfTested = true;
	UUserWidget* Owner = CreateWidget<UMKIInventorySlotsController>(Controller, UMKIInventorySlotsController::StaticClass());
	const TArray<FUserSettingCategoryMapping> Tabs = UserSettings->GetCategorizedSettingWidgets(Controller->GetWorld(), Owner);
	for (const FUserSettingCategoryMapping& Tab : Tabs)
	{
		if (Tab.Category != UMKIToolsSettingsCategory::StaticClass()) continue;
		int32 Rows = 0;
		TArray<FString> Sections;
		for (const FUserSettingSubCategoryMapping& Section : Tab.SubCategories)
		{
			Rows += Section.SettingWidgets.Num();
			Sections.Add(FString::Printf(TEXT("%s %d"), *UFGCategory::GetCategoryName(Section.SubCategory).ToString(), Section.SettingWidgets.Num()));
		}
		UE_LOG(LogMKITools, Log, TEXT("Options self-test: Options > MKI Tools has %d rows of %d (%s)"), Rows, Entries.Num(), *FString::Join(Sections, TEXT(", ")));
		return;
	}
	UE_LOG(LogMKITools, Warning, TEXT("Options self-test: no MKI Tools tab among the game's %d option tabs"), Tabs.Num());
}

bool UMKIToolsOptionsMenu::Tick(float DeltaTime)
{
	UFGGameUserSettings* UserSettings = UFGGameUserSettings::GetFGGameUserSettings();
	if (!GameInstance.IsValid() || !Install(UserSettings)) return true;
	FollowConfig();
	if (!bSelfTested) SelfTest(UserSettings);
	return true;
}

namespace MKIInventorySlotsStyle
{
	/** The user widgets around a widget, nearest first: the row it sits in, then the panels and menu above it. */
	TArray<UUserWidget*> EnclosingUserWidgets(UWidget* Widget, int32 Max)
	{
		TArray<UUserWidget*> Result;
		UWidget* Current = Widget;
		while (Current && Result.Num() < Max)
		{
			if (UWidget* Parent = Current->GetParent()) { Current = Parent; continue; }
			// The root of a widget tree: its user widget owns the tree.
			UUserWidget* Owner = Current->GetTypedOuter<UUserWidget>();
			if (!Owner || Owner == Current || Result.Contains(Owner)) break;
			Result.Add(Owner);
			Current = Owner;
		}
		return Result;
	}

	/** The first widget in a user widget's tree (and the trees of user widgets in it, Depth levels down) that matches. */
	UWidget* FindWidget(UUserWidget* Root, TFunctionRef<bool(UWidget*)> Matches, int32 Depth, const UUserWidget* Skip = nullptr)
	{
		if (!Root || !Root->WidgetTree || Root == Skip) return nullptr;
		UWidget* Found = nullptr;
		Root->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (Found || !Widget) return;
			if (Matches(Widget)) { Found = Widget; return; }
			if (UUserWidget* Nested = Cast<UUserWidget>(Widget); Nested && Depth > 0) Found = FindWidget(Nested, Matches, Depth - 1, Skip);
		});
		return Found;
	}

	/** Whether a brush draws something visible (a vanilla hit-area button draws nothing). */
	bool Draws(const FSlateBrush& Brush)
	{
		return Brush.DrawAs != ESlateBrushDrawType::NoDrawType && Brush.TintColor.GetSpecifiedColor().A > 0.05f;
	}

	/** A plain dark button with the game's orange on hover, until (or when no) vanilla style is found. */
	void SetFlatStyle(UButton* Button)
	{
		FButtonStyle Style = Button->GetStyle();
		Style.SetNormal(FSlateColorBrush(FLinearColor(0.08f, 0.08f, 0.08f, 0.85f)));
		Style.SetHovered(FSlateColorBrush(FLinearColor(0.98f, 0.58f, 0.29f, 1.f)));
		Style.SetPressed(FSlateColorBrush(FLinearColor(0.80f, 0.42f, 0.18f, 1.f)));
		Style.SetDisabled(FSlateColorBrush(FLinearColor(0.08f, 0.08f, 0.08f, 0.4f)));
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		Button->SetStyle(Style);
	}
}

TSharedRef<SWidget> UMKIInventorySlotsController::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		// Live (2026-10-29): the row first showed only a thin line (the texts asked for the game's Noto Sans, a font face
		// Slate cannot draw on its own, and the control was squeezed to a checkbox's width), then a count three times the
		// size of the row labels and a "+10" overflowing a small grey box. The texts start small here and take the row
		// label's own font and colour, and the button a vanilla options button's style, once the control sits in its row
		// (ApplyVanillaStyle).
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Box"));
		Box->SetMinDesiredWidth(320.f);
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row"));
		Box->AddChild(Row);
		Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Label"));
		FSlateFontInfo Small = Label->GetFont();
		Small.Size = 10;
		Label->SetFont(Small);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Label->SetJustification(ETextJustify::Right);
		Label->SetText(LOCTEXT("OptInventorySlotsNoGame", "Load a save to add slots"));
		if (UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Label))
		{
			LabelSlot->SetVerticalAlignment(VAlign_Center);
			LabelSlot->SetHorizontalAlignment(HAlign_Right);
			LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			LabelSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
		}
		AddButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("AddButton"));
		ButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ButtonText"));
		ButtonText->SetText(FText::Format(LOCTEXT("OptInventorySlotsAdd", "+{0}"), AMKIPlayerSubsystem::InventorySlotsPerPress));
		ButtonText->SetFont(Small);
		ButtonText->SetJustification(ETextJustify::Center);
		ButtonText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		if (UButtonSlot* TextSlot = Cast<UButtonSlot>(AddButton->AddChild(ButtonText)))
		{
			TextSlot->SetPadding(FMargin(14.f, 3.f));
			TextSlot->SetHorizontalAlignment(HAlign_Center);
			TextSlot->SetVerticalAlignment(VAlign_Center);
		}
		AddButton->OnClicked.AddDynamic(this, &UMKIInventorySlotsController::OnAddClicked);
		USizeBox* ButtonBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ButtonBox"));
		ButtonBox->SetMinDesiredWidth(64.f);
		ButtonBox->AddChild(AddButton);
		if (UHorizontalBoxSlot* ButtonSlot = Row->AddChildToHorizontalBox(ButtonBox)) ButtonSlot->SetVerticalAlignment(VAlign_Center);
		MKIInventorySlotsStyle::SetFlatStyle(AddButton);
		WidgetTree->RootWidget = Box;
	}
	return Super::RebuildWidget();
}

bool UMKIInventorySlotsController::ApplyVanillaStyle(bool bFallback)
{
	const TArray<UUserWidget*> Around = MKIInventorySlotsStyle::EnclosingUserWidgets(this, 12);
	// The row's own label: the first text with text in the user widget the control sits in (its row).
	UTextBlock* RowLabel = nullptr;
	for (UUserWidget* Owner : Around)
	{
		RowLabel = Cast<UTextBlock>(MKIInventorySlotsStyle::FindWidget(Owner, [](UWidget* Widget)
		{
			const UTextBlock* Text = Cast<UTextBlock>(Widget);
			return Text && !Text->GetText().IsEmpty();
		}, 1, this));
		if (RowLabel) break;
	}
	// A vanilla options button that draws something (not a transparent hit area), from the menu around the row.
	UButton* Vanilla = nullptr;
	for (UUserWidget* Owner : Around)
	{
		Vanilla = Cast<UButton>(MKIInventorySlotsStyle::FindWidget(Owner, [this](UWidget* Widget)
		{
			const UButton* Button = Cast<UButton>(Widget);
			return Button && Button != AddButton && MKIInventorySlotsStyle::Draws(Button->GetStyle().Normal);
		}, 4, this));
		if (Vanilla) break;
	}
	if (!bFallback && (!RowLabel || !Vanilla)) return false;
	if (RowLabel)
	{
		Label->SetFont(RowLabel->GetFont());
		Label->SetColorAndOpacity(RowLabel->GetColorAndOpacity());
		ButtonText->SetFont(RowLabel->GetFont());
	}
	if (Vanilla)
	{
		AddButton->SetStyle(Vanilla->GetStyle());
		// Text on the button: dark on a light button, light on a dark one.
		const FLinearColor Fill = Vanilla->GetStyle().Normal.TintColor.GetSpecifiedColor();
		ButtonText->SetColorAndOpacity(FSlateColor(Fill.GetLuminance() > 0.5f ? FLinearColor::Black : FLinearColor::White));
	}
	const FSlateFontInfo Font = Label->GetFont();
	UE_LOG(LogMKITools, Log, TEXT("Options: Inventory Slots control font %s size %.0f (%s); button style %s"),
		Font.FontObject ? *Font.FontObject->GetName() : TEXT("default"), Font.Size, RowLabel ? *FString::Printf(TEXT("from row label %s"), *RowLabel->GetName()) : TEXT("no row label found"),
		Vanilla ? *FString::Printf(TEXT("from %s in %s"), *Vanilla->GetName(), *GetNameSafe(Vanilla->GetTypedOuter<UUserWidget>() ? Vanilla->GetTypedOuter<UUserWidget>()->GetClass() : nullptr)) : TEXT("flat (no vanilla button found)"));
	return true;
}

void UMKIInventorySlotsController::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!Label || !AddButton) return;
	// The row and menu exist once the control is in place; after a second of looking, what was found is used.
	if (!bStyled) bStyled = ApplyVanillaStyle(++StyleTicks > 60);
	const AFGCharacterPlayer* Character = Cast<AFGCharacterPlayer>(GetOwningPlayerPawn());
	const UFGInventoryComponent* Inventory = Character ? Character->GetInventory() : nullptr;
	if (!Inventory)
	{
		Label->SetText(LOCTEXT("OptInventorySlotsNoGame", "Load a save to add slots"));
		AddButton->SetIsEnabled(false);
		return;
	}
	Label->SetText(FText::Format(LOCTEXT("OptInventorySlotsCount", "{0} slots ({1} of {2} added)"), Inventory->GetSizeLinear(), AMKIPlayerSubsystem::GetExtraInventorySlots(this), AMKIPlayerSubsystem::MaximumExtraInventorySlots));
	AddButton->SetIsEnabled(AMKIPlayerSubsystem::CanAddInventorySlots(this));
}

void UMKIInventorySlotsController::OnAddClicked()
{
	AMKIPlayerSubsystem::AddInventorySlots(this);
}

#undef LOCTEXT_NAMESPACE
