#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Misc/Variant.h"
#include "Settings/FGUserSettingCategory.h"
#include "UI/FGOptionsValueController.h"
#include "MKIToolsOptionsMenu.generated.h"

/** The "MKI Tools" tab in the game's Options menu, next to the game's own tabs (Gameplay, Video, ... World). */
UCLASS()
class MKITOOLS_API UMKIToolsSettingsCategory : public UFGUserSettingCategory
{
	GENERATED_BODY()

public:
	UMKIToolsSettingsCategory();
};

/** The tab's section headers, as the game's own ("Gameplay", "General", "Language" under Options > Gameplay). */
UCLASS()
class MKITOOLS_API UMKIToolsSettingsCategory_Player : public UFGUserSettingCategory
{
	GENERATED_BODY()

public:
	UMKIToolsSettingsCategory_Player();
};

UCLASS()
class MKITOOLS_API UMKIToolsSettingsCategory_Items : public UFGUserSettingCategory
{
	GENERATED_BODY()

public:
	UMKIToolsSettingsCategory_Items();
};

UCLASS()
class MKITOOLS_API UMKIToolsSettingsCategory_Building : public UFGUserSettingCategory
{
	GENERATED_BODY()

public:
	UMKIToolsSettingsCategory_Building();
};

UCLASS()
class MKITOOLS_API UMKIToolsSettingsCategory_Equipment : public UFGUserSettingCategory
{
	GENERATED_BODY()

public:
	UMKIToolsSettingsCategory_Equipment();
};

UCLASS()
class MKITOOLS_API UMKIToolsSettingsCategory_World : public UFGUserSettingCategory
{
	GENERATED_BODY()

public:
	UMKIToolsSettingsCategory_World();
};

/**
 * Shows MKI Tools' options under Options > MKI Tools with the game's own settings system (the maintainer, 2026-10-29: the
 * options look and work as the game's own, grouped under section headers, the description at the bottom on hover, Apply and
 * Reset to Default). The values stay in SML's configuration (UMKIToolsConfiguration, saved in the game's config folder), so
 * every getter, the depot panel's checkbox and the values already saved keep working: each setting starts from its SML value,
 * and an applied change is written back to it. The game gathers its settings from cooked assets only, so MKI Tools builds its
 * settings in code and adds them to the game's user settings (UFGGameUserSettings::mUserSettings) once those are set up.
 */
UCLASS()
class MKITOOLS_API UMKIToolsOptionsMenu : public UObject
{
	GENERATED_BODY()

public:
	/** Builds the settings and starts the once-a-second check that adds them to the game's (and follows SML's values). */
	void Start(class UGameInstance* GameInstance);
	void Stop();

private:
	struct FEntry
	{
		const TCHAR* Key;
		TSubclassOf<UFGUserSettingCategory> Section;
		FText DisplayName;
		/** 0 for a checkbox; otherwise a whole-number slider from Minimum to Maximum. */
		float Minimum = 0.f;
		float Maximum = 0.f;
		bool bZeroIsOff = false;
		/** The inventory slots button (no value of its own). */
		bool bInventorySlots = false;
	};

	bool Tick(float DeltaTime);
	/** Builds one game setting from an MKI Tools option (its description is the option's tooltip). */
	class UFGUserSetting* BuildSetting(const FEntry& Entry, const class UConfigProperty* Option, int32 Index);
	/** Adds the settings to the game's user settings (again, if the game rebuilt them); false while the game has none yet. */
	bool Install(class UFGGameUserSettings* UserSettings);
	/** Sets each setting's shown value to its SML value when that changed elsewhere (the depot panel's checkbox) and nothing is pending. */
	void FollowConfig();
	/** Writes an applied (or reset) setting back to its SML option. */
	void OnOptionUpdated(FString StrId, FVariant Value);
	/** Once, with a player controller: builds the Options menu's rows for the MKI Tools tab and logs them (checked by the native gate). */
	void SelfTest(class UFGGameUserSettings* UserSettings);

	class UConfigProperty* FindOption(const TCHAR* Key) const;
	static FVariant OptionValue(const class UConfigProperty* Option);

	TArray<FEntry> Entries;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UFGUserSetting>> Settings;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UFGUserSettingApplyType>> ApplyTypes;

	TWeakObjectPtr<class UGameInstance> GameInstance;
	FTSTicker::FDelegateHandle TickHandle;
	bool bSelfTested = false;
};

/**
 * The "Inventory Slots" row's control in the Options menu: the current inventory size and a "+10" button, up to
 * AMKIPlayerSubsystem::MaximumExtraInventorySlots added per save (grow-only). The game's settings have no button, so this
 * control is built in code; its setting keeps no value.
 */
UCLASS()
class MKITOOLS_API UMKIInventorySlotsController : public UFGOptionsValueController
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UFUNCTION()
	void OnAddClicked();

	/** Takes the row label's font and colour and a vanilla options button's style, once the control sits in its row. */
	bool ApplyVanillaStyle(bool bFallback);

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> ButtonText;

	bool bStyled = false;
	int32 StyleTicks = 0;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> Label;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> AddButton;
};
