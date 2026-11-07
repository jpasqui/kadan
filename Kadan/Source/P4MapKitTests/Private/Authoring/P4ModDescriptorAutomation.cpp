#if P4_MAPKIT_TESTS
#include "P4ModDescriptor.h"
#include "P4AuthoringRows.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ModDescriptorTest, "P4MapKit.Export.ModDescriptor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4ModDescriptorTest::RunTest(const FString&)
{
    const FString Directory = FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("P4MapKit/DescriptorTest"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    struct FCleanup
    {
        FString Directory;
        ~FCleanup() { IFileManager::Get().DeleteDirectory(*Directory, false, true); }
    } Cleanup{Directory};
    FString Error;
    FP4ModDescriptor D;
    D.Id = TEXT("my.cave");
    D.Name = TEXT("Creator Cave");
    D.Version = TEXT("1.2.3");
    FP4ModNames Names, Other;
    TestTrue(TEXT("Canonical descriptor accepted"), P4ValidateModDescriptor(D, Error));
    TestTrue(TEXT("Derived namespace accepted"), P4ModDerivedNames(D, Names, Error));
    TestTrue(TEXT("Root is below own namespace"), Names.PackageRoot.StartsWith(TEXT("/Game/Mods/mycave_")));
    FP4ModDescriptor Collision = D;
    Collision.Id = TEXT("myc.ave");
    TestTrue(TEXT("Other dot placement accepted"), P4ModDerivedNames(Collision, Other, Error));
    TestNotEqual(TEXT("Full identity hash avoids dot-removal collision"), Names.PackageRoot, Other.PackageRoot);
    TArray<FP4ModDescriptor> Listed;
    TestTrue(TEXT("Missing descriptor folder lists empty"), P4ListModDescriptors(Directory, Listed, Error));
    TestFalse(TEXT("List does not create folder"), IFileManager::Get().DirectoryExists(*Directory));
    TestTrue(TEXT("Creates persistent identity"), P4WriteModDescriptor(Directory, D, nullptr, Error));
    FP4ModDescriptor Read;
    TestTrue(TEXT("Read persistent identity"), P4ReadModDescriptor(Directory, D.Id, Read, Error));
    TestEqual(TEXT("Creator name retained"), Read.Name, D.Name);
    TestEqual(TEXT("Creator version retained"), Read.Version, D.Version);
    TestFalse(TEXT("Create cannot silently replace identity"), P4WriteModDescriptor(Directory, D, nullptr, Error));
    FP4ModDescriptor Edited = D;
    Edited.Name = TEXT("Renamed Cave");
    Edited.Version = TEXT("2.0.0");
    TestTrue(TEXT("Explicit update preserves identity"), P4WriteModDescriptor(Directory, Edited, &Read, Error));
    TestFalse(TEXT("Stale editor update refused"), P4WriteModDescriptor(Directory, D, &Read, Error));
    TestTrue(TEXT("Updated descriptor readable"), P4ReadModDescriptor(Directory, D.Id, Read, Error));
    TestEqual(TEXT("Failed update retains latest name"), Read.Name, Edited.Name);
    Collision = Edited;
    Collision.Id = TEXT("another.mod");
    TestFalse(TEXT("Update cannot rename ID"), P4WriteModDescriptor(Directory, Collision, &Read, Error));
    TestTrue(TEXT("List finds persistent mod"), P4ListModDescriptors(Directory, Listed, Error));
    TestEqual(TEXT("Exactly one persistent mod"), Listed.Num(), 1);
    const FString File = FPaths::Combine(Directory, TEXT("Config/KadanMods/my.cave.json"));
    TestTrue(TEXT("Malformed descriptor fixture written"),
        FFileHelper::SaveStringToFile(
            TEXT("{\"format\":true,\"id\":\"my.cave\",\"name\":\"x\",\"version\":\"1.0.0\"}"), *File));
    TestFalse(TEXT("Boolean format is not numeric schema"), P4ReadModDescriptor(Directory, D.Id, Read, Error));
    TestEqual(TEXT("Failed read preserves caller result"), Read.Name, Edited.Name);
    for (const FString& Id :
        {FString(TEXT("tsuchi.reserved")), FString(TEXT("UPPER")), FString(TEXT("../outside")), FString(TEXT("bad."))})
    {
        Collision = D;
        Collision.Id = Id;
        TestFalse(TEXT("Invalid or reserved identity refused"), P4ValidateModDescriptor(Collision, Error));
    }
    Collision = D;
    Collision.Version = TEXT("01.0.0");
    TestFalse(TEXT("Noncanonical version refused"), P4ValidateModDescriptor(Collision, Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SaveModScriptTest, "P4MapKit.Kadan.SaveModScript",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4SaveModScriptTest::RunTest(const FString&)
{
    const FString Id = TEXT("save.") + FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
    const FString File = FPaths::ProjectDir() / TEXT("Config/KadanMods") / (Id + TEXT(".json"));
    ON_SCOPE_EXIT
    {
        IFileManager::Get().Delete(*File, false, true);
    };
    const auto Valid = [](const FString& Text)
    {
        TSharedPtr<FJsonObject> Object;
        bool Saved = false;
        return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) && Object.IsValid() &&
            Object->TryGetBoolField(TEXT("valid"), Saved) && Saved;
    };
    TestTrue(TEXT("Script creates the same descriptor as Save mod"),
        Valid(UP4AuthoringTools::NativeSaveMod(Id, TEXT("Script mod"), TEXT("1.0.0"), false)));
    FP4ModDescriptor Read;
    FString Error;
    TestTrue(TEXT("The panel reads the script's identity"), P4ReadModDescriptor(FPaths::ProjectDir(), Id, Read, Error));
    TestEqual(TEXT("The creator's name is saved"), Read.Name, FString(TEXT("Script mod")));
    TestFalse(TEXT("Creating again preserves the existing identity"),
        Valid(UP4AuthoringTools::NativeSaveMod(Id, TEXT("Other"), TEXT("2.0.0"), false)));
    TestTrue(TEXT("Explicit edit saves name and version"),
        Valid(UP4AuthoringTools::NativeSaveMod(Id, TEXT("Edited"), TEXT("2.0.0"), true)));
    TestTrue(TEXT("Updated identity reopens"), P4ReadModDescriptor(FPaths::ProjectDir(), Id, Read, Error));
    TestEqual(TEXT("The panel sees the edit"), Read.Name, FString(TEXT("Edited")));
    TestEqual(TEXT("The version is saved"), Read.Version, FString(TEXT("2.0.0")));
    TestFalse(TEXT("An invalid update cannot replace the saved identity"),
        Valid(UP4AuthoringTools::NativeSaveMod(Id, TEXT("Invalid"), TEXT("01.0.0"), true)));
    TestTrue(
        TEXT("The valid identity remains after refusal"), P4ReadModDescriptor(FPaths::ProjectDir(), Id, Read, Error));
    TestEqual(TEXT("Failed edit preserves the creator's name"), Read.Name, FString(TEXT("Edited")));
    return true;
}
#endif
