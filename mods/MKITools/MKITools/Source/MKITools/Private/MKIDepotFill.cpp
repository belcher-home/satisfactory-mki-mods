#include "MKIDepotFill.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/ListView.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "FGCharacterPlayer.h"
#include "FGInventoryComponent.h"
#include "FGPlayerState.h"
#include "ItemAmount.h"
#include "Resources/FGItemDescriptor.h"
#include "FGPlayerController.h"
#include "MKITools.h"
#include "MKIToolsConfiguration.h"
#include "MKIToolsRemoteCallObject.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "MKITools"

namespace
{
	/** The game's Dimensional Depot widgets are named BPW_CentralStorage* (its item rows too). */
	bool IsDepotWidget(const UUserWidget* Widget)
	{
		return Widget && Widget->GetClass()->GetName().StartsWith(TEXT("BPW_CentralStorage"));
	}

	/** The inventory's Dimensional Depot panel, the one with "Take from inventory before Depot". */
	bool IsDepotPanel(const UUserWidget* Widget)
	{
		return Widget && Widget->GetClass()->GetName() == TEXT("BPW_CentralStorage_Inventory_C");
	}

	/** Every widget in a widget's tree, the trees of the user widgets in it included (a user widget's tree is its own). */
	void CollectWidgets(UUserWidget* Owner, TArray<UWidget*>& Out, int32 Depth = 0)
	{
		if (!Owner || !Owner->WidgetTree || Depth > 8) return;
		Owner->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			Out.Add(Widget);
			if (UUserWidget* Nested = Cast<UUserWidget>(Widget)) CollectWidgets(Nested, Out, Depth + 1);
		});
	}

	/** The widget tree, one line per widget (class, name, parent), for the log when the panel isn't laid out as expected. */
	void LogTree(UUserWidget* Panel)
	{
		TArray<UWidget*> Widgets;
		CollectWidgets(Panel, Widgets);
		UE_LOG(LogMKITools, Log, TEXT("Depot fill: %s has %d widgets:"), *Panel->GetClass()->GetName(), Widgets.Num());
		for (const UWidget* Widget : Widgets)
		{
			const UTextBlock* Text = Cast<UTextBlock>(Widget);
			UE_LOG(LogMKITools, Log, TEXT("  %s %s in %s%s%s"), *Widget->GetClass()->GetName(), *Widget->GetName(), *GetNameSafe(Widget->GetParent()),
				Text ? TEXT(" text: ") : TEXT(""), Text ? *Text->GetText().ToString() : TEXT(""));
		}
	}

	/** Calls a Blueprint function whose one parameter is a bool; false when the object has no such function. */
	bool CallWithBool(UObject* Object, FName Name, bool bValue)
	{
		UFunction* Function = Object ? Object->FindFunction(Name) : nullptr;
		if (!Function || Function->NumParms != 1) return false;
		FBoolProperty* Param = CastField<FBoolProperty>(Function->PropertyLink);
		if (!Param || !Param->HasAnyPropertyFlags(CPF_Parm) || Param->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm)) return false;
		uint8* Params = static_cast<uint8*>(FMemory_Alloca(FMath::Max<int32>(Function->ParmsSize, 1)));
		FMemory::Memzero(Params, FMath::Max<int32>(Function->ParmsSize, 1));
		Param->SetPropertyValue_InContainer(Params, bValue);
		Object->ProcessEvent(Function, Params);
		return true;
	}

	/** A Blueprint bool variable of an object; false when it has none. */
	bool GetBool(const UObject* Object, FName Name, bool& bOut)
	{
		const FBoolProperty* Property = Object ? FindFProperty<FBoolProperty>(Object->GetClass(), Name) : nullptr;
		if (!Property) return false;
		bOut = Property->GetPropertyValue_InContainer(Object);
		return true;
	}

	/** A class's own functions and their parameters, for the log (the panel and checkbox are the game's Blueprints). */
	void LogFunctions(const UClass* Class)
	{
		UE_LOG(LogMKITools, Log, TEXT("Depot fill: %s functions:"), *Class->GetName());
		for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			FString Params;
			for (TFieldIterator<FProperty> Param(*It); Param && Param->HasAnyPropertyFlags(CPF_Parm); ++Param)
			{
				Params += FString::Printf(TEXT("%s%s %s%s"), Params.IsEmpty() ? TEXT("") : TEXT(", "), *Param->GetCPPType(), *Param->GetName(),
					Param->HasAnyPropertyFlags(CPF_ReturnParm) ? TEXT(" (return)") : Param->HasAnyPropertyFlags(CPF_OutParm) ? TEXT(" (out)") : TEXT(""));
			}
			UE_LOG(LogMKITools, Log, TEXT("  %s(%s)"), *It->GetName(), *Params);
		}
	}

	/** The item a depot list entry stands for: the entry itself (an item class), its item class variable, or the item of its
	 * item amount (the game's entries are BP_CentralStorage_EntryWrapper_C, with an ItemAmount). */
	TSubclassOf<UFGItemDescriptor> ItemOf(const UObject* Entry)
	{
		if (const UClass* Class = Cast<UClass>(Entry)) return Class->IsChildOf(UFGItemDescriptor::StaticClass()) ? const_cast<UClass*>(Class) : nullptr;
		if (!Entry) return nullptr;
		for (TFieldIterator<FClassProperty> It(Entry->GetClass()); It; ++It)
		{
			if (!It->MetaClass || !It->MetaClass->IsChildOf(UFGItemDescriptor::StaticClass())) continue;
			if (UClass* Class = Cast<UClass>(It->GetObjectPropertyValue_InContainer(Entry))) return Class;
		}
		for (TFieldIterator<FStructProperty> It(Entry->GetClass()); It; ++It)
		{
			if (It->Struct != FItemAmount::StaticStruct()) continue;
			if (const FItemAmount* Amount = It->ContainerPtrToValuePtr<FItemAmount>(Entry); Amount && Amount->ItemClass) return Amount->ItemClass;
		}
		return nullptr;
	}

	/** The user widget whose own tree holds a widget (its WidgetTree's outer). */
	UUserWidget* TreeOwner(const UWidget* Widget)
	{
		const UWidgetTree* Tree = Widget ? Cast<UWidgetTree>(Widget->GetOuter()) : nullptr;
		return Tree ? Cast<UUserWidget>(Tree->GetOuter()) : nullptr;
	}
}

bool UMKIDepotFillSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	// A checkbox for a local player: never on a dedicated server.
	return World && World->IsGameWorld() && !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

TStatId UMKIDepotFillSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMKIDepotFillSubsystem, STATGROUP_Tickables);
}

void UMKIDepotFillSubsystem::Tick(float DeltaTime)
{
	SincePoll += DeltaTime;
	if (SincePoll < PollSeconds) return;
	SincePoll = 0.f;
	const bool bFill = UMKIToolsConfiguration::GetFillDepotFromInventory(this);
	SendOption(bFill);
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Panel = *It;
		if (Panel->GetWorld() != GetWorld() || !IsDepotWidget(Panel)) continue;
		// The depot widgets seen, once each, so a renamed panel after a game update shows in the log.
		bool bSeen = false;
		SeenClasses.Add(Panel->GetClass()->GetFName(), &bSeen);
		if (!bSeen) UE_LOG(LogMKITools, Log, TEXT("Depot fill: depot widget %s seen"), *Panel->GetClass()->GetName());
		// Once its Slate widgets are built: our row goes in beside them, the game's UMG widgets untouched.
		if (!IsDepotPanel(Panel) || Handled.Contains(Panel) || !Panel->WidgetTree || !Panel->WidgetTree->RootWidget || !Panel->GetCachedWidget()) continue;
		Handled.Add(Panel);
		// The game's "Take from inventory before Depot" is the player's setting, and adding our row must leave it alone.
		bool bTake = false;
		const bool bHasTake = GetTakeSetting(bTake);
		AddCheckBox(Panel);
		bool bTakeAfter = bTake;
		GetTakeSetting(bTakeAfter);
		UE_LOG(LogMKITools, Log, TEXT("Depot fill: depot panel opened; the game's Take from inventory before Depot is %s, Fill Depot from inventory is %s"),
			bHasTake ? (bTake ? TEXT("on") : TEXT("off")) : TEXT("unknown"), bFill ? TEXT("on") : TEXT("off"));
		if (bHasTake && bTakeAfter != bTake)
		{
			UE_LOG(LogMKITools, Warning, TEXT("Depot fill: the game's Take from inventory before Depot changed to %s while our row was added; set back to %s"),
				bTakeAfter ? TEXT("on") : TEXT("off"), bTake ? TEXT("on") : TEXT("off"));
			if (AFGPlayerState* Player = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPlayerState<AFGPlayerState>() : nullptr)
			{
				Player->SetTakeFromInventoryBeforeCentralStorage(bTake);
			}
		}
	}
	// Any later change of the game's setting while a panel is open, for the log (the player's own click included).
	bool bTake = false;
	if (Handled.Num() > 0 && GetTakeSetting(bTake))
	{
		if (bTakeLogged && bTakeSeen != bTake) UE_LOG(LogMKITools, Log, TEXT("Depot fill: the game's Take from inventory before Depot is now %s"), bTake ? TEXT("on") : TEXT("off"));
		bTakeSeen = bTake;
		bTakeLogged = true;
	}
	// The option can also change under Mods > MKI Tools: the checkboxes follow it.
	ShowOption(bFill);
	ShowUploadQueue();
	SortLists();
	for (auto HandledIt = Handled.CreateIterator(); HandledIt; ++HandledIt) if (!HandledIt->IsValid()) HandledIt.RemoveCurrent();
	for (auto RowIt = Rows.CreateIterator(); RowIt; ++RowIt) if (!RowIt->Key.IsValid()) RowIt.RemoveCurrent();
}

bool UMKIDepotFillSubsystem::GetTakeSetting(bool& bOut) const
{
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	const AFGPlayerState* Player = Controller ? Controller->GetPlayerState<AFGPlayerState>() : nullptr;
	if (!Player) return false;
	bOut = Player->GetTakeFromInventoryBeforeCentralStorage();
	return true;
}

bool UMKIDepotFillSubsystem::AddCheckBox(UUserWidget* Panel)
{
	TArray<UWidget*> Widgets;
	CollectWidgets(Panel, Widgets);
	// The game's row: a button (mGrabFromCentralStorageButton) holding its checkbox widget (BPW_OptionsCheckbox_C, not a
	// UCheckBox) and the label "Take from inventory before Depot", in a SizeBox in the panel's list.
	UButton* Vanilla = nullptr;
	for (UWidget* Widget : Widgets)
	{
		if (Widget->GetFName() == TEXT("mGrabFromCentralStorageButton")) { Vanilla = Cast<UButton>(Widget); break; }
	}
	UUserWidget* VanillaCheck = nullptr;
	for (UWidget* Widget : Widgets)
	{
		UUserWidget* Candidate = Cast<UUserWidget>(Widget);
		if (!Candidate || !Candidate->GetClass()->GetName().Contains(TEXT("OptionsCheckbox"))) continue;
		// Failing the name, the button the checkbox widget sits in.
		UButton* Holder = nullptr;
		for (UWidget* Up = Candidate->GetParent(); Up && !Holder; Up = Up->GetParent()) Holder = Cast<UButton>(Up);
		if (Vanilla && Holder != Vanilla) continue;
		if (!Vanilla) Vanilla = Holder;
		VanillaCheck = Candidate;
		break;
	}
	UPanelWidget* Content = Vanilla ? Cast<UPanelWidget>(Vanilla->GetContent()) : nullptr;
	if (!Vanilla || !VanillaCheck || !Content)
	{
		UE_LOG(LogMKITools, Warning, TEXT("Depot fill: %s has no checkbox row of the game's (button %s, checkbox %s); nothing added"),
			*Panel->GetClass()->GetName(), *GetNameSafe(Vanilla), *GetNameSafe(VanillaCheck));
		LogTree(Panel);
		return false;
	}
	const UTextBlock* Label = nullptr;
	for (UWidget* Widget : Content->GetAllChildren()) if ((Label = Cast<UTextBlock>(Widget)) != nullptr) break;
	// The row to put ours under: the button's SizeBox, in the panel's list.
	USizeBox* VanillaSize = Cast<USizeBox>(Vanilla->GetParent());
	UWidget* Row = VanillaSize && VanillaSize->GetParent() ? static_cast<UWidget*>(VanillaSize) : static_cast<UWidget*>(Vanilla);
	UPanelWidget* Parent = Row->GetParent();
	if (!Parent)
	{
		UE_LOG(LogMKITools, Warning, TEXT("Depot fill: the checkbox row in %s has no parent; nothing added"), *Panel->GetClass()->GetName());
		LogTree(Panel);
		return false;
	}
	// The game's checkbox widget, for its look only: our button takes the clicks, and the tick is set from the option.
	UUserWidget* Check = CreateWidget<UUserWidget>(Panel->GetOwningPlayer(), VanillaCheck->GetClass());
	if (!Check)
	{
		UE_LOG(LogMKITools, Warning, TEXT("Depot fill: could not create %s; nothing added"), *VanillaCheck->GetClass()->GetName());
		return false;
	}
	Check->SetVisibility(ESlateVisibility::HitTestInvisible);

	UWidgetTree* Tree = Cast<UWidgetTree>(Parent->GetOuter());
	if (!Tree) Tree = Panel->WidgetTree;
	UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("MKIFillDepotButton"));
	Button->SetStyle(Vanilla->GetStyle());
	Button->SetColorAndOpacity(Vanilla->GetColorAndOpacity());
	Button->SetBackgroundColor(Vanilla->GetBackgroundColor());
	Button->SetToolTipText(LOCTEXT("FillDepotFromInventoryTip", "Every second, items in your inventory that the Dimensional Depot lists (even at 0) go into your upload queue, up to the depot's limit. Queued items the depot has no room for come back to your inventory. An item taken out of the depot is left alone for a minute, to be used."));
	Button->OnClicked.AddDynamic(this, &UMKIDepotFillSubsystem::OnRowClicked);
	UHorizontalBox* Ours = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("MKIFillDepotRow"));
	UButtonSlot* OursSlot = Cast<UButtonSlot>(Button->AddChild(Ours));
	if (const UButtonSlot* From = Cast<UButtonSlot>(Content->Slot); OursSlot && From)
	{
		OursSlot->SetPadding(From->GetPadding());
		OursSlot->SetHorizontalAlignment(From->GetHorizontalAlignment());
		OursSlot->SetVerticalAlignment(From->GetVerticalAlignment());
	}
	UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MKIFillDepotLabel"));
	Text->SetText(LOCTEXT("FillDepotFromInventoryLabel", "Fill Depot from inventory"));
	if (Label)
	{
		Text->SetFont(Label->GetFont());
		Text->SetColorAndOpacity(Label->GetColorAndOpacity());
		Text->SetShadowColorAndOpacity(Label->GetShadowColorAndOpacity());
		Text->SetShadowOffset(Label->GetShadowOffset());
	}
	// Each in the slot its counterpart has in the game's row (the game's gamepad hint is left out).
	auto AddLike = [Ours](UWidget* Widget, const UWidget* Like)
	{
		UHorizontalBoxSlot* To = Ours->AddChildToHorizontalBox(Widget);
		const UHorizontalBoxSlot* From = Like ? Cast<UHorizontalBoxSlot>(Like->Slot) : nullptr;
		if (!To) return;
		if (!From) { To->SetVerticalAlignment(VAlign_Center); return; }
		To->SetPadding(From->GetPadding());
		To->SetSize(From->GetSize());
		To->SetHorizontalAlignment(From->GetHorizontalAlignment());
		To->SetVerticalAlignment(From->GetVerticalAlignment());
	};
	AddLike(Check, VanillaCheck);
	AddLike(Text, Label);

	// Under the game's row, inside the game's SizeBox, in Slate only: the game's SizeBox's Slate box gets a list of the
	// game's button's Slate widget and ours, each in a box sized as the game's was. The game's widgets are never moved in
	// UMG: taking a UMG widget out of its parent releases its Slate widgets, and rebuilding them ran the game's checkbox's
	// Construct again after the panel had set it, which turned "Take from inventory before Depot" off on every open (the
	// maintainer 2026-10-28). The panel's own list (the item list and the upload queue after it) is left alone too: a
	// built list puts a new row's Slate slot at its end whatever the index.
	if (!VanillaSize || !VanillaSize->GetParent())
	{
		UE_LOG(LogMKITools, Warning, TEXT("Depot fill: the checkbox row in %s is not in a SizeBox; nothing added"), *Panel->GetClass()->GetName());
		LogTree(Panel);
		return false;
	}
	const TSharedPtr<SBox> SizeSlate = StaticCastSharedPtr<SBox>(VanillaSize->GetCachedWidget());
	const TSharedPtr<SWidget> GameSlate = Vanilla->GetCachedWidget();
	if (!SizeSlate || !GameSlate)
	{
		UE_LOG(LogMKITools, Warning, TEXT("Depot fill: the checkbox row in %s has no Slate widgets; nothing added"), *Panel->GetClass()->GetName());
		return false;
	}
	auto SizedLike = [VanillaSize, Vanilla](const TSharedRef<SWidget>& Widget)
	{
		const USizeBoxSlot* From = Cast<USizeBoxSlot>(Vanilla->Slot);
		TSharedRef<SBox> Size = SNew(SBox)
			.Padding(From ? From->GetPadding() : FMargin(0.f))
			.HAlign(From ? From->GetHorizontalAlignment() : HAlign_Fill)
			.VAlign(From ? From->GetVerticalAlignment() : VAlign_Fill)
			[Widget];
		if (VanillaSize->IsWidthOverride()) Size->SetWidthOverride(VanillaSize->GetWidthOverride());
		if (VanillaSize->IsHeightOverride()) Size->SetHeightOverride(VanillaSize->GetHeightOverride());
		if (VanillaSize->IsMinDesiredWidthOverride()) Size->SetMinDesiredWidth(VanillaSize->GetMinDesiredWidth());
		if (VanillaSize->IsMinDesiredHeightOverride()) Size->SetMinDesiredHeight(VanillaSize->GetMinDesiredHeight());
		if (VanillaSize->IsMaxDesiredWidthOverride()) Size->SetMaxDesiredWidth(VanillaSize->GetMaxDesiredWidth());
		if (VanillaSize->IsMaxDesiredHeightOverride()) Size->SetMaxDesiredHeight(VanillaSize->GetMaxDesiredHeight());
		return Size;
	};
	// Our button has no UMG parent: it is kept here while the panel lives.
	Rows.Add(Panel, Button);
	SizeSlate->SetContent(SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[SizedLike(GameSlate.ToSharedRef())]
		+ SVerticalBox::Slot().AutoHeight()[SizedLike(Button->TakeWidget())]);
	// The game's box now holds two rows: its height limits and padding go (each row keeps them).
	SizeSlate->SetHeightOverride(FOptionalSize());
	SizeSlate->SetMinDesiredHeight(FOptionalSize());
	SizeSlate->SetMaxDesiredHeight(FOptionalSize());
	SizeSlate->SetPadding(FMargin(0.f));
	SizeSlate->SetHAlign(HAlign_Fill);
	SizeSlate->SetVAlign(VAlign_Fill);
	Checks.Add(Check);
	// Once per game run, the Blueprint functions of the panel and the checkbox, for the log.
	static bool bLogged = false;
	if (!bLogged)
	{
		bLogged = true;
		LogFunctions(Panel->GetClass());
		LogFunctions(Check->GetClass());
	}
	ShowOption(UMKIToolsConfiguration::GetFillDepotFromInventory(this));
	UE_LOG(LogMKITools, Log, TEXT("Depot fill: checkbox added to %s under %s (row 2 in %s)"), *Panel->GetClass()->GetName(), *Vanilla->GetName(), *VanillaSize->GetName());
	return true;
}

void UMKIDepotFillSubsystem::ShowOption(bool bFill)
{
	Checks.RemoveAll([](const TWeakObjectPtr<UUserWidget>& Check) { return !Check.IsValid(); });
	for (const TWeakObjectPtr<UUserWidget>& Check : Checks)
	{
		// The game's checkbox draws its state itself (its button style and tick, as the game's own row does), through its
		// SetChecked_NoNotify, which does not fire its OnCheckChanged.
		bool bChecked = false;
		if (GetBool(Check.Get(), TEXT("mIsChecked"), bChecked) && bChecked == bFill) continue;
		if (CallWithBool(Check.Get(), TEXT("SetChecked_NoNotify"), bFill)) continue;
		// Failing that, its tick alone.
		UWidget* TickImage = Check->WidgetTree ? Check->WidgetTree->FindWidget(TEXT("mCheck")) : nullptr;
		const ESlateVisibility Visibility = bFill ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden;
		if (TickImage && TickImage->GetVisibility() != Visibility) TickImage->SetVisibility(Visibility);
	}
}

void UMKIDepotFillSubsystem::ShowUploadQueue()
{
	// The panel shows the upload queue (mShowUploadInfo) when items are dropped on it; items our fill queued went in
	// without a drop, so the queue is shown when it holds items. The game hides it again once the queue is empty.
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	const AFGCharacterPlayer* Character = Controller ? Cast<AFGCharacterPlayer>(Controller->GetPawn()) : nullptr;
	const UFGInventoryComponent* Upload = Character ? Character->GetUploadInventory() : nullptr;
	if (!Upload || Upload->IsEmpty()) return;
	for (const TWeakObjectPtr<UUserWidget>& Panel : Handled)
	{
		bool bShown = true;
		if (!Panel.IsValid() || !GetBool(Panel.Get(), TEXT("mShowUploadInfo"), bShown) || bShown) continue;
		if (CallWithBool(Panel.Get(), TEXT("SetShowUploadInfo"), true)) UE_LOG(LogMKITools, Log, TEXT("Depot fill: upload queue shown in %s"), *Panel->GetName());
	}
}

void UMKIDepotFillSubsystem::SortLists()
{
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	const AFGPlayerState* Player = Controller ? Controller->GetPlayerState<AFGPlayerState>() : nullptr;
	for (auto It = SortedLists.CreateIterator(); It; ++It) if (!It->Key.IsValid()) It.RemoveCurrent();
	for (const TWeakObjectPtr<UUserWidget>& Panel : Handled)
	{
		UListView* List = Panel.IsValid() && Panel->WidgetTree ? Cast<UListView>(Panel->WidgetTree->FindWidget(TEXT("mStorageList"))) : nullptr;
		if (!List) continue;
		const TArray<UObject*>& Items = List->GetListItems();
		// Only when the game has filled the list since (a new item, a pin, a search): our own order is left as it is.
		const TArray<TWeakObjectPtr<UObject>>* Last = SortedLists.Find(List);
		bool bSame = Last && Last->Num() == Items.Num();
		for (int32 Index = 0; bSame && Index < Items.Num(); ++Index) bSame = (*Last)[Index].Get() == Items[Index];
		if (bSame || Items.Num() < 2) continue;
		struct FEntry { UObject* Item; FString Name; bool bPinned; };
		TArray<FEntry> Entries;
		for (UObject* Item : Items)
		{
			const TSubclassOf<UFGItemDescriptor> Class = ItemOf(Item);
			if (!Class)
			{
				if (!bSortWarned) UE_LOG(LogMKITools, Warning, TEXT("Depot fill: a depot list entry (%s) has no item; the list is left unsorted"), *GetNameSafe(Item ? Item->GetClass() : nullptr));
				bSortWarned = true;
				Entries.Reset();
				break;
			}
			Entries.Add({ Item, UFGItemDescriptor::GetItemName(Class).ToString(), Player && Player->IsCentralStorageItemPinned(Class) });
		}
		if (Entries.Num() != Items.Num()) continue;
		// Pinned items stay first when the game put them first (each group A-Z); otherwise the whole list is A-Z.
		bool bPinnedFirst = true, bSeenUnpinned = false;
		for (const FEntry& Entry : Entries)
		{
			if (!Entry.bPinned) bSeenUnpinned = true;
			else if (bSeenUnpinned) bPinnedFirst = false;
		}
		Entries.StableSort([bPinnedFirst](const FEntry& A, const FEntry& B)
		{
			if (bPinnedFirst && A.bPinned != B.bPinned) return A.bPinned;
			return A.Name.Compare(B.Name, ESearchCase::IgnoreCase) < 0;
		});
		TArray<UObject*> Sorted;
		TArray<TWeakObjectPtr<UObject>>& Kept = SortedLists.FindOrAdd(List);
		Kept.Reset();
		for (const FEntry& Entry : Entries)
		{
			Sorted.Add(Entry.Item);
			Kept.Add(Entry.Item);
		}
		bool bChanged = false;
		for (int32 Index = 0; !bChanged && Index < Sorted.Num(); ++Index) bChanged = Sorted[Index] != Items[Index];
		if (!bChanged) continue;
		List->SetListItems(Sorted);
		FString First;
		for (int32 Index = 0; Index < FMath::Min(5, Entries.Num()); ++Index) First += (Index ? TEXT(", ") : TEXT("")) + Entries[Index].Name;
		UE_LOG(LogMKITools, Log, TEXT("Depot fill: depot list of %d items sorted A-Z%s: %s..."), Entries.Num(), bPinnedFirst ? TEXT(" (pinned first)") : TEXT(""), *First);
	}
}

void UMKIDepotFillSubsystem::OnRowClicked()
{
	const bool bFill = !UMKIToolsConfiguration::GetFillDepotFromInventory(this);
	UMKIToolsConfiguration::SetFillDepotFromInventory(this, bFill);
	ShowOption(bFill);
	SendOption(bFill);
}

void UMKIDepotFillSubsystem::SendOption(bool bFill)
{
	AFGPlayerController* Controller = Cast<AFGPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!Controller || !Controller->PlayerState) return;
	if (bSent == bFill && SentTo.Get() == Controller) return;
	UMKIToolsRemoteCallObject* Remote = Controller->GetRemoteCallObjectOfClass<UMKIToolsRemoteCallObject>();
	if (!Remote) return;
	// On the host this runs right away; a client's call goes to the server.
	Remote->Server_SetFillDepotFromInventory(bFill);
	bSent = bFill;
	SentTo = Controller;
}

#undef LOCTEXT_NAMESPACE
