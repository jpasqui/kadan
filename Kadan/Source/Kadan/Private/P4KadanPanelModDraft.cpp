#include "P4KadanPanelModsPage.h"

namespace P4KadanEditorPrivate
{

bool FKadanModsPage::HasUnsavedModIdentity() const
{
    if (ActiveMod)
    {
        return ModId != ActiveMod->Id || ModName != ActiveMod->Name || ModVersion != ActiveMod->Version;
    }
    return !ModId.IsEmpty() || !ModName.IsEmpty() || (!ModVersion.IsEmpty() && ModVersion != TEXT("1.0.0"));
}

void FKadanModsPage::RestoreModPicker()
{
    if (!ModPicker)
    {
        return;
    }
    TGuardValue<bool> Restoring(RestoringModPicker, true);
    if (ActiveMod)
    {
        ModPicker->SetSelectedItem(ActiveMod);
    }
    else
    {
        ModPicker->ClearSelection();
    }
}

FP4KadanDraftSaveResult FKadanModsPage::SaveModIdentity()
{
    FP4KadanDraftSaveResult Result;
    FP4ModDescriptor Value;
    Value.Id = ModId;
    Value.Name = ModName;
    Value.Version = ModVersion;
    Host.NoticeError = false;
    // Each mod has one folder, named after its ID. A new mod whose folder another mod of the project has is refused.
    FP4ModNames NewNames;
    FString Ignored;
    if (!ActiveMod && P4ModDerivedNames(Value, NewNames, Ignored))
        for (const TSharedPtr<FP4ModDescriptor>& Other : Mods)
        {
            FP4ModNames OtherNames;
            if (Other && P4ModDerivedNames(*Other, OtherNames, Ignored) &&
                OtherNames.PackageRoot.Equals(NewNames.PackageRoot, ESearchCase::IgnoreCase))
            {
                Result.Message = FString::Printf(
                    TEXT("The mod %s already has the folder %s. Choose an ID that differs by more than dots and underscores."),
                    *Other->Id, *NewNames.PackageRoot);
                Result.Detail = Result.Message;
                Host.Message = Result.Message;
                Host.NoticeError = true;
                return Result;
            }
        }
    if (!P4WriteModDescriptor(FPaths::ProjectDir(), Value, ActiveMod.Get(), Result.Detail))
    {
        if (Result.Detail == TEXT("Invalid mod version"))
        {
            Result.Message = TEXT("Enter a version with three numbers, such as 1.0.0.");
        }
        else if (Result.Detail == TEXT("Invalid mod name"))
        {
            Result.Message = TEXT("Enter a mod name. It can use up to 127 bytes of UTF-8 text.");
        }
        else if (Result.Detail == TEXT("Control character in mod name"))
        {
            Result.Message = TEXT("Remove control characters from the mod name.");
        }
        else
        {
            Result.Message = TEXT("The mod could not be saved. Check Details before trying again.");
        }
        Host.Message = Result.Message;
        ExportDetails = Result.Detail;
        Host.NoticeError = true;
        return Result;
    }
    const bool New = !ActiveMod;
    if (!New)
    {
        *ActiveMod = Value;
    }
    else
    {
        ActiveMod = MakeShared<FP4ModDescriptor>(Value);
        Mods.Add(ActiveMod);
        FP4ModNames Names;
        FString Why;
        if (P4ModDerivedNames(Value, Names, Why))
        {
            SelectionFolder = Names.PackageRoot;
        }
    }
    if (ModPicker)
    {
        ModPicker->RefreshOptions();
    }
    RestoreModPicker();
    P4KadanRememberModId(GEditorPerProjectIni, ActiveMod->Id);
    ExportReceipt->Select(ActiveMod->Id);
    ExportDetails.Empty();
    Host.Message = TEXT("Saved the mod's identity.");
    // A new mod's own folder is made and shown, so its assets start in the right place.
    if (New && FApp::CanEverRender() && !FApp::IsUnattended())
        OpenContentFolder();
    Result.Saved = true;
    return Result;
}

void FKadanModsPage::StartModDraft()
{
    ApplyModSelection(nullptr);
    ModId.Empty();
    ModName.Empty();
    ModVersion = TEXT("1.0.0");
    SelectionFolder.Empty();
    RestoreModPicker();
    Host.NoticeError = false;
    Host.Message =
        TEXT("Name the mod and check its suggested ID, then save it. Keep the ID the same when you update the mod.");
}

void FKadanModsPage::ContinueModChange(const FString &Id, bool NewDraft)
{
    if (NewDraft)
    {
        StartModDraft();
        return;
    }
    for (const auto &Mod : Mods)
    {
        if (Mod->Id == Id)
        {
            ApplyModSelection(Mod);
            RestoreModPicker();
            return;
        }
    }
    Host.Message =
        TEXT("The selected mod is no longer available. Close and reopen Kadan to load the project's mods again.");
    Host.NoticeError = true;
}

void FKadanModsPage::RequestModChange(TSharedPtr<FP4ModDescriptor> Mod, bool NewDraft)
{
    if (RestoringModPicker)
    {
        return;
    }
    if (!NewDraft && Mod == ActiveMod)
    {
        return;
    }
    const FString Id = Mod ? Mod->Id : FString();
    if (!HasUnsavedModIdentity())
    {
        if (NewDraft)
        {
            StartModDraft();
        }
        else
        {
            ApplyModSelection(Mod);
        }
        return;
    }
    RestoreModPicker();
    if (const auto Dialog = ModDraftWindow.Pin())
    {
        Dialog->BringToFront(true);
        return;
    }
    const TWeakPtr<SKadanPanel> Weak = Host.Self();
    FP4ModDescriptor Draft;
    Draft.Id = ModId;
    Draft.Name = ModName;
    Draft.Version = ModVersion;
    ModDraftWindow = P4KadanModDraftChoice(
        Host.Self(),
        Draft,
        [Weak]()
        {
            if (const auto Panel = Weak.Pin())
            {
                return Panel->Mods().SaveModIdentity();
            }
            return FP4KadanDraftSaveResult();
        },
        [Weak, Id, NewDraft]()
        {
            if (const auto Panel = Weak.Pin())
            {
                Panel->Mods().ContinueModChange(Id, NewDraft);
            }
        });
}
} // namespace P4KadanEditorPrivate
