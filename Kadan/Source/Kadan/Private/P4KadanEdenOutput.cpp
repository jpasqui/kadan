#include "P4KadanEdenOutput.h"
#include "EditorStyleSet.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

FString P4KadanFindEdenOutput(const FString& RoamingDirectory)
{
    if (RoamingDirectory.IsEmpty() || FPaths::IsRelative(RoamingDirectory))
    {
        return FString();
    }
    const FString Profile = RoamingDirectory / TEXT("eden");
    const FString TitleId = TEXT("0100B7C00933A000");
    TArray<FString> Candidates;
    FConfigFile Config;
    const FString ConfigFile = Profile / TEXT("config/qt-config.ini");
    if (IFileManager::Get().FileExists(*ConfigFile))
    {
        Config.Read(ConfigFile);
        FString LoadDirectory;
        if (Config.GetString(TEXT("Paths"), TEXT("load_directory"), LoadDirectory))
        {
            LoadDirectory.TrimStartAndEndInline();
            if (!LoadDirectory.IsEmpty() && !FPaths::IsRelative(LoadDirectory))
            {
                Candidates.Add(LoadDirectory / TitleId);
            }
        }
    }
    Candidates.Add(Profile / TEXT("load") / TitleId);
    for (const FString& Candidate : Candidates)
    {
        if (IFileManager::Get().DirectoryExists(*Candidate))
        {
            FString Full = FPaths::ConvertRelativePathToFull(Candidate);
            FPaths::NormalizeDirectoryName(Full);
            return Full;
        }
    }
    return FString();
}

TSharedRef<SWidget> P4KadanEdenOutputSuggestion(const FString& Directory, TFunction<void()> Choose)
{
    return SNew(SVerticalBox)
        .Tag(TEXT("KadanEdenSuggestion"))
        .Visibility(Directory.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
        + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
            .Text(FText::FromString(TEXT("Eden")))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))]
        + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
            .AutoWrapText(true)
            .Text(FText::FromString(TEXT("Eden's Pikmin 4 mod folder is available.")))]
        + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
            .Tag(TEXT("KadanEdenFolder"))
            .AutoWrapText(true)
            .Text(FText::FromString(Directory))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4).HAlign(HAlign_Left)[SNew(SButton)
            .ButtonStyle(FEditorStyle::Get(), "Button")
            .ButtonColorAndOpacity(FLinearColor(0.12f, 0.14f, 0.18f))
            .TextStyle(FEditorStyle::Get(), "NormalText")
            .ForegroundColor(FLinearColor::White)
            .ContentPadding(FMargin(8, 4))
            .Text(FText::FromString(TEXT("Use Eden folder")))
            .OnClicked_Lambda([Choose]()
            {
                Choose();
                return FReply::Handled();
            })];
}
