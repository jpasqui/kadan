#include "P4StrictNativeClassSources.h"
#include "P4KadanImport.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Class.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/package_reader.h"
THIRD_PARTY_INCLUDES_END

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StrictNativeClassSourcesTest, "P4MapKit.Kadan.StrictClassSourceIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StrictNativeClassSourcesTest::RunTest(const FString&)
{
    const FString Package = TEXT("/Game/Carrot4/Placeables/Teki/GBaby");
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    auto Original = MakeShared<FP4KadanSourceAsset>();
    Original->Package = Package;
    Original->Header = Root /
        TEXT("host/Saved/P4MapKit/FullNativeCave000/game/Carrot4/Content/Carrot4/Placeables/Teki/GBaby.uasset");
    Original->Body = FPaths::ChangeExtension(Original->Header, TEXT("uexp"));
    FP4KadanImportSourceScope OriginalScope({Original});
    FP4KadanSourceBinding Source;
    UClass* LiveClass = nullptr;
    FString Error;
    if (!TestTrue(TEXT("Retained original Blueprint source and live ancestry qualify"),
            P4ReadStrictNativeClassSource(Package, Source, LiveClass, Error)))
    {
        AddError(Error);
        return false;
    }
    UObject* OriginalCDO = LiveClass->GetDefaultObject(false);
    const auto Inventory = P4MapKit::ReadPackage(
        std::vector<uint8>(Source.Header.GetData(), Source.Header.GetData() + Source.Header.Num()),
        std::vector<uint8>(Source.Body.GetData(), Source.Body.GetData() + Source.Body.Num()));
    if (!TestTrue(TEXT("Original package inventory is readable"), Inventory.Error.Code.empty()))
        return false;
    int32 GeneratedIndex = INDEX_NONE, OtherRootIndex = INDEX_NONE, OtherScriptIndex = INDEX_NONE;
    size_t ExportAt = 0, ImportAt = 0;
    for (const auto& Span : Inventory.HeaderSpans)
    {
        if (Span.Kind == "exports")
            ExportAt = Span.Offset;
        if (Span.Kind == "imports")
            ImportAt = Span.Offset;
    }
    for (size_t Index = 0; Index < Inventory.Exports.size(); ++Index)
    {
        const auto& Export = Inventory.Exports[Index];
        if (Export.Name == "GBaby_C")
            GeneratedIndex = int32(Index);
        else if (!Export.Outer)
            OtherRootIndex = int32(Index);
    }
    if (!TestTrue(TEXT("Actual generated export and another top-level export are present"),
            GeneratedIndex != INDEX_NONE && OtherRootIndex != INDEX_NONE && ExportAt && ImportAt))
        return false;
    const auto& Generated = Inventory.Exports[size_t(GeneratedIndex)];
    if (!TestTrue(TEXT("Actual generated metaclass is an imported class distinct from its parent"),
            Generated.Class < 0 && Generated.Class != Generated.Super))
        return false;
    const size_t MetaIndex = size_t(-int64(Generated.Class) - 1);
    if (!TestTrue(TEXT("Actual generated metaclass import is bounded"), MetaIndex < Inventory.Imports.size()))
        return false;
    for (size_t Index = 0; Index < Inventory.Imports.size(); ++Index)
    {
        const auto& Import = Inventory.Imports[Index];
        if (Import.Class == "Package" && Import.Name.compare(0, 8, "/Script/") == 0 &&
            -int64(Index) - 1 != Inventory.Imports[MetaIndex].Outer)
        {
            OtherScriptIndex = int32(Index);
            break;
        }
    }
    if (!TestTrue(
            TEXT("Actual source has another script package for the metaclass control"), OtherScriptIndex != INDEX_NONE))
        return false;
    const FString ScratchDirectory = FPaths::ProjectSavedDir() / TEXT("P4MapKit/StrictClassSourceControls");
    auto Scratch = MakeShared<FP4KadanSourceAsset>();
    Scratch->Package = Package;
    Scratch->Header = ScratchDirectory / TEXT("GBaby.uasset");
    Scratch->Body = FPaths::ChangeExtension(Scratch->Header, TEXT("uexp"));
    if (!TestTrue(TEXT("One bounded source-control directory is available"),
            IFileManager::Get().MakeDirectory(*ScratchDirectory, true)))
        return false;
    if (!TestTrue(
            TEXT("Write only the single control body"), FFileHelper::SaveArrayToFile(Source.Body, *Scratch->Body)))
        return false;
    struct FStrictSourceControlCleanup
    {
        FString Header, Body;
        ~FStrictSourceControlCleanup()
        {
            IFileManager::Get().Delete(*Header, false, true);
            IFileManager::Get().Delete(*Body, false, true);
        }
    } Cleanup{Scratch->Header, Scratch->Body};
    const auto WriteIndex = [](TArray<uint8>& Header, size_t At, int32 Value)
    {
        if (At > size_t(Header.Num()) || size_t(Header.Num()) - At < 4)
            return false;
        const uint32 Bits = uint32(Value);
        for (size_t Byte = 0; Byte < 4; ++Byte)
            Header[int32(At + Byte)] = uint8(Bits >> (Byte * 8));
        return true;
    };
    const auto CheckRejected = [&](const TArray<uint8>& Header, const FString& ExpectedReason)
    {
        if (!TestTrue(TEXT("Write one bounded header control"), FFileHelper::SaveArrayToFile(Header, *Scratch->Header)))
            return;
        FP4KadanImportSourceScope ScratchScope({Scratch});
        FP4KadanSourceBinding Bound;
        UClass* Class = nullptr;
        FString Why;
        if (!TestTrue(TEXT("Forged identity remains a valid canonical bounded source pair"),
                P4KadanReadBoundSource(Package, Bound, Why)))
        {
            AddError(Why);
            return;
        }
        TestFalse(TEXT("Production class source reader refuses the forged source identity"),
            P4ReadStrictNativeClassSource(Package, Bound, Class, Why));
        TestTrue(TEXT("Refusal identifies the exact source identity problem"),
            Why.StartsWith(ExpectedReason, ESearchCase::CaseSensitive));
        if (!Why.StartsWith(ExpectedReason, ESearchCase::CaseSensitive))
            AddError(Why);
        TestNull(TEXT("Rejected source never exposes an admitted live class"), Class);
    };
    auto Header = Source.Header;
    if (!TestTrue(TEXT("Parent mutation is inside the decoded export record"),
            WriteIndex(Header, ExportAt + size_t(GeneratedIndex) * 104 + 4, Generated.Class)))
        return false;
    CheckRejected(Header, TEXT("strict-class-source-live-parent-differs:"));
    Header = Source.Header;
    if (!TestTrue(TEXT("Path mutation is inside the decoded export record"),
            WriteIndex(Header, ExportAt + size_t(GeneratedIndex) * 104 + 12, OtherRootIndex + 1)))
        return false;
    CheckRejected(Header, TEXT("strict-class-source-generated-class-path-differs"));
    Header = Source.Header;
    if (!TestTrue(TEXT("Metaclass mutation is inside its decoded import record"),
            WriteIndex(Header, ImportAt + MetaIndex * 28 + 16, -OtherScriptIndex - 1)))
        return false;
    CheckRejected(Header, TEXT("strict-class-source-live-metaclass-differs:"));
    FP4KadanSourceBinding After;
    UClass* AfterClass = nullptr;
    TestTrue(TEXT("Real original source and class still qualify after controls"),
        P4ReadStrictNativeClassSource(Package, After, AfterClass, Error));
    TestTrue(TEXT("Original source bytes remain unchanged"), P4KadanSourceBindingCurrent(Source, Error));
    TestTrue(TEXT("Actual Blueprint class and CDO were never replaced"),
        AfterClass == LiveClass && AfterClass && AfterClass->GetDefaultObject(false) == OriginalCDO);
    return !HasAnyErrors();
}
#endif
