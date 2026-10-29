#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MKIDepotFill.generated.h"

class UUserWidget;
class UWidget;
class UButton;

/**
 * "Fill Depot from inventory" (the maintainer 2026-10-28): a checkbox under the game's own "Take from inventory before
 * Depot" in the inventory's Dimensional Depot panel. The panel is the game's Blueprint widget (BPW_CentralStorage_*), so
 * the checkbox is added to each panel the game creates, a copy of the game's own row (its button style, its
 * BPW_OptionsCheckbox widget and its label style). It edits
 * MKI Tools' option of the same name (also under Mods > MKI Tools, saved per player), and the option is sent to the
 * server, which does the filling (AMKIPlayerSubsystem).
 */
UCLASS()
class MKITOOLS_API UMKIDepotFillSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** How often new depot panels are looked for and the option is sent to the server. */
	static constexpr float PollSeconds = 0.25f;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	/** Adds the checkbox to a depot panel; false when the panel has no checkbox of the game's to put it under. */
	bool AddCheckBox(UUserWidget* Panel);
	/** Sends the option to the server when it changed (or the controller did). */
	void SendOption(bool bFill);

	/** Shows the option on every checkbox added (through the game's checkbox widget's own SetChecked_NoNotify). */
	void ShowOption(bool bFill);
	/** Shows the upload queue in the depot panels when it holds items (our fill queues them without the panel's drop). */
	void ShowUploadQueue();
	/** Sorts each depot panel's item list A-Z by item name (pinned items first when the game puts them first). */
	void SortLists();

	/** The local player's "Take from inventory before Depot" (the game's setting); false when there is no player state. */
	bool GetTakeSetting(bool& bOut) const;

	UFUNCTION()
	void OnRowClicked();

	float SincePoll = PollSeconds;
	/** Panels already handled (with or without a checkbox added), so each is patched and logged once. */
	TSet<TWeakObjectPtr<UUserWidget>> Handled;
	/** Our button in each panel: it is in the panel's Slate only (no UMG parent), so it is kept alive here. */
	UPROPERTY()
	TMap<TWeakObjectPtr<UUserWidget>, TObjectPtr<UButton>> Rows;
	/** The game's setting last seen while a panel was open, to log its changes. */
	bool bTakeSeen = false;
	bool bTakeLogged = false;
	/** The game's checkbox widget in each row added, kept in step with the option (it can also change under Mods > MKI Tools). */
	TArray<TWeakObjectPtr<UUserWidget>> Checks;
	/** The order each depot list was last sorted to, so a list is sorted again only when the game fills it again. */
	TMap<TWeakObjectPtr<class UListView>, TArray<TWeakObjectPtr<UObject>>> SortedLists;
	/** Whether a list entry without an item was logged. */
	bool bSortWarned = false;
	/** The depot widget classes logged so far. */
	TSet<FName> SeenClasses;
	TWeakObjectPtr<class APlayerController> SentTo;
	bool bSent = false;
};
