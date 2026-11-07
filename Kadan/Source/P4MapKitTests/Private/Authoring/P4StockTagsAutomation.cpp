#if P4_MAPKIT_TESTS
#include "P4StockTags.h"
#include "Animation/AnimMontage.h"
#include "Serialization/MemoryReader.h"
#include "P4StockTagTree.h"
#include "P4GameNativeTails.h"
#include "P4StockEngineFieldContent.h"
#include "P4DiagnosticCookPackageGuard.h"
#include "P4MapKitCore/cooked_export_rebuild.h"
#include "P4MapKitCore/property_decoder.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "UObject/StrongObjectPtr.h"
#include "VectorField/VectorFieldStatic.h"

namespace P4StockTagsTest
{
using P4MapKit::FDecodedValue;
using P4StockTags::FNode;
using P4StockTags::FList;
using P4StockTags::Bytes;
struct FCooked
{
    std::vector<uint8> Header, Body;
    P4MapKit::FPackageInventory Native;
};

// The actual native cook of one asset's package, as the export and the import's comparison make it.
bool Cook(FAutomationTestBase& Test, UPackage* Package, UObject* Asset, const FString& Name, FCooked& Out)
{
    ITargetPlatform* Target = GetTargetPlatformManagerRef().FindTargetPlatform(TEXT("WindowsNoEditor"));
    if (!Test.TestNotNull(TEXT("The native cook target exists"), Target))
        return false;
    const FString Base = FPaths::ProjectSavedDir() / TEXT("P4MapKit/StockTags") / Name;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Base), true);
    FString Error;
    {
        FP4DiagnosticCookPackageGuard Guard(Package);
        if (!Test.TestTrue(TEXT("The cook state is guarded"), Guard.IsReady()))
            return false;
        Package->SetPackageFlags(PKG_FilterEditorOnly);
        const bool Saved = GEditor->SavePackage(Package, Asset, RF_Public | RF_Standalone, *(Base + TEXT(".uasset")),
            GError, nullptr, false, true, SAVE_KeepGUID | SAVE_KeepDirty | SAVE_Unversioned | SAVE_NoError | SAVE_Async,
            Target, FDateTime::MinValue(), false);
        if (!Test.TestTrue(TEXT("The actual asset cooks: ") + Name, Saved) ||
            !Test.TestTrue(TEXT("The cook restores its live state"), Guard.AwaitAndRestore(Error)))
            return false;
    }
    TArray<uint8> HeaderBytes, BodyBytes;
    if (!FFileHelper::LoadFileToArray(HeaderBytes, *(Base + TEXT(".uasset"))) ||
        !FFileHelper::LoadFileToArray(BodyBytes, *(Base + TEXT(".uexp"))))
        return false;
    Out.Header.assign(HeaderBytes.GetData(), HeaderBytes.GetData() + HeaderBytes.Num());
    Out.Body.assign(BodyBytes.GetData(), BodyBytes.GetData() + BodyBytes.Num());
    Out.Native = P4MapKit::ReadPackage(Out.Header, Out.Body);
    return Test.TestTrue(TEXT("The cooked package reads"), Out.Native.Error.Code.empty());
}
int32 ExportOf(const P4MapKit::FPackageInventory& Native, const std::string& Name)
{
    for (std::size_t At = 0; At < Native.Exports.size(); ++At)
        if (Native.Exports[At].Name == Name)
            return int32(At);
    return INDEX_NONE;
}
bool Values(const P4MapKit::FPackageInventory& Native, const std::vector<uint8>& Body, int32 Export,
    const std::string& Owner, FDecodedValue& Out)
{
    const std::vector<P4MapKit::FArchivedFloatCodec> Floats{{"AnimNotifyEvent", "MiddleDuration", true, false, false}};
    P4MapKit::FPropertyDecoder Decoder(Native, Body, 1000000, 64 * 1024 * 1024, {}, Floats);
    Out = FDecodedValue();
    std::size_t Position = Native.Exports[Export].Offset;
    return Decoder.Properties(Position, Native.Exports[Export].Offset + Native.Exports[Export].Size, Out, 0, Owner);
}
FString Json(const FDecodedValue* Value)
{
    return Value ? FString(UTF8_TO_TCHAR(P4MapKit::DecodedJson(*Value).c_str())) : FString();
}
FString Json(const FDecodedValue& Value)
{
    return Json(&Value);
}
Bytes Word(uint32 Value)
{
    Bytes Out;
    for (int N = 0; N < 4; ++N)
        Out.push_back(uint8(Value >> (N * 8)));
    return Out;
}
Bytes Float(float Value)
{
    uint32 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, 4);
    return Word(Bits);
}
FNode Tag(const char* Name, const char* Type)
{
    FNode Node;
    Node.Name = {Name, 0};
    Node.Type = {Type, 0};
    return Node;
}

// Makes "the game's file" of a cooked asset: the same package with the members and choices the game's
// engine adds written into the given export, as the game's own cook writes them.
bool GameFile(const FCooked& Cooked, int32 Export,
    TFunctionRef<bool(FList&, std::vector<P4MapKit::FPackageImport>&, std::vector<std::string>&)> Edit, FCooked& Out)
{
    const auto& Item = Cooked.Native.Exports[Export];
    P4StockTags::FReader Reader{Cooked.Body, Cooked.Native.Names, true};
    FList Root;
    std::size_t Position = Item.Offset;
    if (!Reader.List(Position, Item.Offset + Item.Size, Root, 0))
        return false;
    auto Names = Cooked.Native.Names;
    auto Imports = Cooked.Native.Imports;
    if (!Edit(Root, Imports, Names))
        return false;
    for (const auto& Import : Imports)
        for (const std::string* Name : {&Import.Name, &Import.Class, &Import.ClassPackage})
            if (std::find(Names.begin(), Names.end(), *Name) == Names.end())
                Names.push_back(*Name);
    P4StockTags::FWriter Writer{Names};
    Bytes Rebuilt;
    if (!Writer.List(Rebuilt, Root))
        return false;
    Rebuilt.insert(Rebuilt.end(), Cooked.Body.begin() + Position, Cooked.Body.begin() + Item.Offset + Item.Size);
    P4MapKit::Export::RewrittenPackage Candidate;
    std::string Why;
    FString Error;
    if (!P4MapKit::Export::RebuildCookedExports(
            Cooked.Header, Cooked.Body, Names, {{std::size_t(Export), Rebuilt}}, Candidate, Why))
        return false;
    if (Imports.size() != Cooked.Native.Imports.size() &&
        !P4AppendCookedImports(Candidate.Header, Candidate.Body, Imports, Error))
        return false;
    Out.Header = Candidate.Header;
    Out.Body = Candidate.Body;
    Out.Native = P4MapKit::ReadPackage(Out.Header, Out.Body);
    return Out.Native.Error.Code.empty();
}
FCollectionScalarParameter Parameter(const TCHAR* Name, float Value)
{
    FCollectionScalarParameter One;
    One.ParameterName = FName(Name);
    One.DefaultValue = Value;
    One.Id = FGuid::NewGuid();
    return One;
}
} //namespace P4StockTagsTest

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameStockTagUnsupportedContainersTest,
    "P4MapKit.EngineFields.GameStockTagUnsupportedContainers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameStockTagUnsupportedContainersTest::RunTest(const FString&)
{
    using namespace P4StockTagsTest;
    for (const char* Type : {"MapProperty", "SetProperty"})
    {
        FNode Node = Tag("Unsupported", Type);
        Node.Detail = {{"IntProperty", 0}};
        if (std::string(Type) == "MapProperty")
            Node.Detail.push_back({"IntProperty", 0});
        Node.Raw = Word(0);
        std::vector<std::string> Names;
        Bytes Body;
        P4StockTags::FWriter Writer{Names};
        if (!TestTrue(TEXT("The same-package envelope writes"), Writer.One(Body, Node)))
            return false;
        FNode Read;
        std::size_t At = 0;
        P4StockTags::FReader Reader{Body, Names, false};
        TestFalse(TEXT("An unsupported strict container refuses: ") + FString(UTF8_TO_TCHAR(Type)),
            Reader.One(At, Body.size(), Read, 0));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameStockTagMembersTest, "P4MapKit.EngineFields.GameStockTagMembers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameStockTagMembersTest::RunTest(const FString&)
{
    using namespace P4StockTagsTest;
    const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    // The game's asset: a collection whose second parameter carries members the game's engine adds.
    UPackage* GamePackage = CreatePackage(*(TEXT("/Game/Mods/StockTagsGame_") + Token));
    TStrongObjectPtr<UMaterialParameterCollection> Game(
        NewObject<UMaterialParameterCollection>(GamePackage, TEXT("Collection"), RF_Public | RF_Standalone));
    ON_SCOPE_EXIT
    {
        Game->ClearFlags(RF_Public | RF_Standalone);
        Game->MarkPendingKill();
        GamePackage->SetDirtyFlag(false);
    };
    Game->ScalarParameters = {Parameter(TEXT("First"), 1.f), Parameter(TEXT("Second"), 2.f)};
    FCooked Plain, File;
    if (!Cook(*this, GamePackage, Game.Get(), TEXT("game-plain"), Plain))
        return false;
    const int32 Export = ExportOf(Plain.Native, "Collection");
    if (!TestTrue(TEXT("The collection is an export"), Export != INDEX_NONE))
        return false;
    const UMaterial* External = LoadObject<UMaterial>(
        nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
    if (!TestNotNull(TEXT("The engine's default material exists"), External))
        return false;
    const bool Made = GameFile(
        Plain, Export,
        [&](FList& Root, std::vector<P4MapKit::FPackageImport>& Imports, std::vector<std::string>&)
        {
            FNode* Parameters = P4StockTags::Find(Root, "ScalarParameters");
            if (!Parameters || !Parameters->StructArray || Parameters->Elements.size() != 2)
                return false;
            Imports.push_back({"/Script/CoreUObject", "Package", "/Engine/EngineMaterials/DefaultMaterial", 0});
            Imports.push_back({"/Script/Engine", "Material", "DefaultMaterial", -int32(Imports.size())});
            const int32 Other = -int32(Imports.size()), Self = Export + 1;
            FList& Element = Parameters->Elements[1];
            const int Value = P4StockTags::FindAt(Element, "DefaultValue");
            if (Value < 0)
                return false;
            FNode Flag = Tag("C4Flag", "BoolProperty");
            Flag.BoolValue = true;
            FNode Distance = Tag("C4Distance", "StructProperty");
            Distance.Detail = {{"PerPlatformFloat", 0}};
            Distance.Raw = Word(1);
            const Bytes Default = Float(1000.f);
            Distance.Raw.insert(Distance.Raw.end(), Default.begin(), Default.end());
            FNode Mode = Tag("C4Mode", "ByteProperty");
            Mode.Detail = {{"EComponentMobility", 0}};
            Mode.Form = FNode::EForm::Name;
            Mode.Value = {"EComponentMobility::C4Special", 0};
            FNode Mine = Tag("C4Self", "ObjectProperty");
            Mine.Raw = Word(uint32(Self));
            FNode Theirs = Tag("C4Other", "ObjectProperty");
            Theirs.Raw = Word(uint32(Other));
            FNode Link = Tag("C4Link", "StructProperty");
            Link.Detail = {{"C4LinkElement", 0}};
            Link.Form = FNode::EForm::Tagged;
            FNode LinkValue = Tag("LinkValue", "FloatProperty");
            LinkValue.Raw = Float(0.25f);
            FNode Linked = Tag("LinkedSequence", "ObjectProperty");
            Linked.Raw = Word(uint32(Self));
            Link.Children = {LinkValue, Linked};
            Element.insert(Element.begin() + Value + 1, {Flag, Distance, Mode, Mine, Theirs, Link});
            FNode Enabled = Tag("bC4Enabled", "BoolProperty");
            Enabled.BoolValue = false;
            Root.insert(Root.begin(), Enabled);
            return true;
        },
        File);
    if (!TestTrue(TEXT("The game's file is made"), Made))
        return false;
    FDecodedValue GameValues;
    if (!TestTrue(TEXT("The game's file decodes"),
            Values(File.Native, File.Body, Export, "MaterialParameterCollection", GameValues)))
        return false;
    const FDecodedValue* GameParameters = GameValues.Field("ScalarParameters");
    if (!TestTrue(TEXT("The game's parameters decode"), GameParameters && GameParameters->Items.size() == 2))
        return false;
    const FDecodedValue& GameSecond = GameParameters->Items[1];

    // The import: a new collection holds what this engine has, and the game's additions are kept as tags.
    UPackage* Package = CreatePackage(*(TEXT("/Game/Mods/StockTags_") + Token));
    TStrongObjectPtr<UMaterialParameterCollection> Asset(
        NewObject<UMaterialParameterCollection>(Package, TEXT("Collection"), RF_Public | RF_Standalone));
    ON_SCOPE_EXIT
    {
        Asset->ClearFlags(RF_Public | RF_Standalone);
        Asset->MarkPendingKill();
        Package->SetDirtyFlag(false);
    };
    Asset->ScalarParameters = Game->ScalarParameters;
    TArray<FP4GameStockTagWanted> Wanted;
    for (const char* Member : {"C4Flag", "C4Distance", "C4Mode", "C4Self", "C4Other", "C4Link"})
    {
        const FString Path = TEXT("ScalarParameters[1].") + FString(UTF8_TO_TCHAR(Member));
        const FString Said = Json(GameSecond.Field(Member));
        TestEqual(TEXT("A member the game's engine adds to an engine struct in an array is a member: ") + Path,
            int32(P4GameStockTagKind(Asset.Get(), Path, Said)), int32(EP4GameStockTag::Member));
        Wanted.Add({Path, Said, EP4GameStockTag::Member});
    }
    Wanted.Add({TEXT("bC4Enabled"), Json(GameValues.Field("bC4Enabled")),
        P4GameStockTagKind(Asset.Get(), TEXT("bC4Enabled"), Json(GameValues.Field("bC4Enabled")))});
    TestEqual(TEXT("A property the game's engine adds to an engine class is a member"), int32(Wanted.Last().Kind),
        int32(EP4GameStockTag::Member));
    TestEqual(TEXT("A value this engine has a place for is not kept as a tag"),
        int32(P4GameStockTagKind(Asset.Get(), TEXT("ScalarParameters[1].DefaultValue"), TEXT("2.0"))),
        int32(EP4GameStockTag::None));
    TestEqual(TEXT("A path into an element that is not there is not kept"),
        int32(P4GameStockTagKind(Asset.Get(), TEXT("ScalarParameters[7].C4Flag"), TEXT("true"))),
        int32(EP4GameStockTag::None));
    TArray<FString> Kept, Problems;
    const auto& Item = File.Native.Exports[Export];
    TestTrue(TEXT("The game's tags are kept"),
        P4StoreGameStockTags(Asset.Get(), File.Body, File.Native, Item.Offset, Item.Offset + Item.Size,
            TEXT("MaterialParameterCollection"), Wanted, Kept, Problems));
    if (!TestEqual(TEXT("Every addition is kept"), Kept.Num(), Wanted.Num()))
    {
        for (const FString& Problem : Problems)
            AddError(Problem);
        return false;
    }
    TArray<FString> NoKept, Refused;
    TArray<FP4GameStockTagWanted> WrongValue{
        {TEXT("ScalarParameters[1].C4Flag"), TEXT("false"), EP4GameStockTag::Member}};
    P4StoreGameStockTags(Asset.Get(), File.Body, File.Native, Item.Offset, Item.Offset + Item.Size,
        TEXT("MaterialParameterCollection"), WrongValue, NoKept, Refused);
    TestTrue(TEXT("A tag that does not decode to the value the import was told is refused"),
        NoKept.Num() == 0 && Refused.Num() == 1);

    // The cook writes them where the game's file has them and reads them back.
    FCooked Out;
    FString Error;
    if (!Cook(*this, Package, Asset.Get(), TEXT("made"), Out))
        return false;
    TestFalse(TEXT("Editor records do not ship"),
        std::find(Out.Native.Names.begin(), Out.Native.Names.end(), "P4GameStockTags") != Out.Native.Names.end());
    if (!TestTrue(
            TEXT("The cook writes the kept tags"), P4TranslateGameStockTagCook(Package, Out.Header, Out.Body, Error)))
    {
        AddError(Error);
        return false;
    }
    Out.Native = P4MapKit::ReadPackage(Out.Header, Out.Body);
    FDecodedValue Written;
    if (!TestTrue(TEXT("The written package decodes"),
            Values(Out.Native, Out.Body, ExportOf(Out.Native, "Collection"), "MaterialParameterCollection", Written)))
        return false;
    const FDecodedValue* Parameters = Written.Field("ScalarParameters");
    if (!TestTrue(TEXT("The written parameters decode"), Parameters && Parameters->Items.size() == 2))
        return false;
    // Ids differ (each collection made its own), so the element is compared without them.
    const auto WithoutId = [](FDecodedValue Value)
    {
        Value.Fields.erase(std::remove_if(Value.Fields.begin(), Value.Fields.end(),
                               [](const std::pair<std::string, FDecodedValue>& One) { return One.first == "Id"; }),
            Value.Fields.end());
        return Value;
    };
    TestEqual(
        TEXT(
            "The element says what the game's says, in the game's order (bool, PerPlatformFloat, enum byte, references, struct)"),
        Json(WithoutId(Parameters->Items[1])), Json(WithoutId(GameSecond)));
    TestEqual(TEXT("The added class property is written"), Json(Written.Field("bC4Enabled")), FString(TEXT("false")));
    TestEqual(TEXT("The own-object reference names the cooked asset"), Json(Parameters->Items[1].Field("C4Self")),
        FString(TEXT("\"Collection\"")));
    bool Imported = false;
    std::string Path;
    for (std::size_t At = 0; At < Out.Native.Imports.size(); ++At)
        Imported = Imported ||
            (Out.Native.Reference(-int32(At + 1), Path) &&
                Path == "/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial");
    TestTrue(TEXT("A reference to an asset the cook does not import is added as an import"), Imported);
    TestEqual(TEXT("The added reference decodes to the game's"), Json(Parameters->Items[1].Field("C4Other")),
        Json(GameSecond.Field("C4Other")));
    const auto Before = Out;
    TestFalse(TEXT("A second pass refuses to shadow a written tag"),
        P4TranslateGameStockTagCook(Package, Out.Header, Out.Body, Error));
    TestTrue(TEXT("A refused pass keeps both buffers"), Out.Header == Before.Header && Out.Body == Before.Body);

    // Sealed, the values follow their element when the creator removes another one, and are left out of
    // an element the creator changed.
    P4SealGameStockTags(Package);
    Asset->ScalarParameters.RemoveAt(0);
    if (!Cook(*this, Package, Asset.Get(), TEXT("shifted"), Out))
        return false;
    if (!TestTrue(TEXT("The cook writes the kept tags after an element was removed"),
            P4TranslateGameStockTagCook(Package, Out.Header, Out.Body, Error)))
    {
        AddError(Error);
        return false;
    }
    Out.Native = P4MapKit::ReadPackage(Out.Header, Out.Body);
    Values(Out.Native, Out.Body, ExportOf(Out.Native, "Collection"), "MaterialParameterCollection", Written);
    Parameters = Written.Field("ScalarParameters");
    TestTrue(TEXT("The kept members moved with their element"),
        Parameters && Parameters->Items.size() == 1 &&
            Json(WithoutId(Parameters->Items[0])) == Json(WithoutId(GameSecond)));
    Asset->ScalarParameters[0].DefaultValue = 5.f;
    if (!Cook(*this, Package, Asset.Get(), TEXT("edited"), Out))
        return false;
    if (!TestTrue(TEXT("The cook of an edited element succeeds"),
            P4TranslateGameStockTagCook(Package, Out.Header, Out.Body, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("The members of the changed element are left out, told"), P4GameStockTagsLeftOut().Num(), 6);
    Out.Native = P4MapKit::ReadPackage(Out.Header, Out.Body);
    Values(Out.Native, Out.Body, ExportOf(Out.Native, "Collection"), "MaterialParameterCollection", Written);
    Parameters = Written.Field("ScalarParameters");
    TestTrue(TEXT("The changed element has none of the game's members"),
        Parameters && Parameters->Items.size() == 1 && !Parameters->Items[0].Field("C4Flag"));
    TestEqual(TEXT("The class property is still written"), Json(Written.Field("bC4Enabled")), FString(TEXT("false")));
    P4ClearGameStockTags(Package);
    TestFalse(TEXT("Clearing removes every kept tag"), P4PackageHasGameStockTags(Package));
    Package->GetMetaData()->SetValue(Asset.Get(), TEXT("P4GameStockTags"), TEXT("broken"));
    TestTrue(TEXT("Malformed custody still routes through translation"), P4PackageHasGameStockTags(Package));
    const auto Unchanged = Out;
    TestFalse(TEXT("Malformed custody refuses translation"),
        P4TranslateGameStockTagCook(Package, Out.Header, Out.Body, Error));
    TestTrue(
        TEXT("Malformed custody keeps both buffers"), Out.Header == Unchanged.Header && Out.Body == Unchanged.Body);
    P4ClearGameStockTags(Package);
    TestFalse(TEXT("Clearing also removes malformed records"), P4PackageHasGameStockTags(Package));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameStockTagChoiceTest, "P4MapKit.EngineFields.GameStockTagEnumChoice",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameStockTagChoiceTest::RunTest(const FString&)
{
    using namespace P4StockTagsTest;
    const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* GamePackage = CreatePackage(*(TEXT("/Game/Mods/StockChoiceGame_") + Token));
    TStrongObjectPtr<UStaticMeshComponent> Game(
        NewObject<UStaticMeshComponent>(GamePackage, TEXT("Component"), RF_Public | RF_Standalone));
    ON_SCOPE_EXIT
    {
        Game->ClearFlags(RF_Public | RF_Standalone);
        Game->MarkPendingKill();
        GamePackage->SetDirtyFlag(false);
    };
    Game->Mobility = EComponentMobility::Static;
    FCooked Plain, File;
    if (!Cook(*this, GamePackage, Game.Get(), TEXT("choice-plain"), Plain))
        return false;
    const int32 Export = ExportOf(Plain.Native, "Component");
    // The game's file chooses a mobility this engine's enum does not have.
    const bool Made = GameFile(
        Plain, Export,
        [](FList& Root, std::vector<P4MapKit::FPackageImport>&, std::vector<std::string>&)
        {
            FNode* Mobility = P4StockTags::Find(Root, "Mobility");
            if (!Mobility || Mobility->Form != FNode::EForm::Raw || Mobility->Detail.size() != 1)
                return false;
            Mobility->Form = FNode::EForm::Name;
            Mobility->Value = {"EComponentMobility::C4Special", 0};
            Mobility->Raw.clear();
            return true;
        },
        File);
    if (!TestTrue(TEXT("The game's file is made"), Made))
        return false;
    FDecodedValue GameValues;
    Values(File.Native, File.Body, Export, "StaticMeshComponent", GameValues);
    const FString Said = Json(GameValues.Field("Mobility"));
    TestEqual(TEXT("The game's choice decodes"), Said, FString(TEXT("\"EComponentMobility::C4Special\"")));
    UPackage* Package = CreatePackage(*(TEXT("/Game/Mods/StockChoice_") + Token));
    TStrongObjectPtr<UStaticMeshComponent> Asset(
        NewObject<UStaticMeshComponent>(Package, TEXT("Component"), RF_Public | RF_Standalone));
    ON_SCOPE_EXIT
    {
        Asset->ClearFlags(RF_Public | RF_Standalone);
        Asset->MarkPendingKill();
        Package->SetDirtyFlag(false);
    };
    Asset->Mobility = EComponentMobility::Static;
    const EP4GameStockTag Kind = P4GameStockTagKind(Asset.Get(), TEXT("Mobility"), Said);
    TestEqual(TEXT("A choice an engine enum lacks is a value"), int32(Kind), int32(EP4GameStockTag::Value));
    TestEqual(TEXT("A choice the enum has is not kept"),
        int32(P4GameStockTagKind(Asset.Get(), TEXT("Mobility"), TEXT("\"EComponentMobility::Movable\""))),
        int32(EP4GameStockTag::None));
    TArray<FString> Kept, Problems;
    const auto& Item = File.Native.Exports[Export];
    P4StoreGameStockTags(Asset.Get(), File.Body, File.Native, Item.Offset, Item.Offset + Item.Size,
        TEXT("StaticMeshComponent"), {{TEXT("Mobility"), Said, Kind}}, Kept, Problems);
    if (!TestEqual(TEXT("The choice is kept"), Kept.Num(), 1))
    {
        for (const FString& Problem : Problems)
            AddError(Problem);
        return false;
    }
    FCooked Out;
    FString Error;
    FDecodedValue Written;
    if (!Cook(*this, Package, Asset.Get(), TEXT("choice-made"), Out))
        return false;
    if (!TestTrue(TEXT("The cook writes the game's choice over the engine's"),
            P4TranslateGameStockTagCook(Package, Out.Header, Out.Body, Error)))
    {
        AddError(Error);
        return false;
    }
    Out.Native = P4MapKit::ReadPackage(Out.Header, Out.Body);
    Values(Out.Native, Out.Body, ExportOf(Out.Native, "Component"), "StaticMeshComponent", Written);
    TestEqual(TEXT("The written choice is the game's"), Json(Written.Field("Mobility")), Said);
    P4SealGameStockTags(Package);
    Asset->Mobility = EComponentMobility::Movable;
    if (!Cook(*this, Package, Asset.Get(), TEXT("choice-edited"), Out))
        return false;
    if (!TestTrue(TEXT("The cook of a creator's choice succeeds"),
            P4TranslateGameStockTagCook(Package, Out.Header, Out.Body, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("The creator's choice wins and the game's is told as left out"), P4GameStockTagsLeftOut().Num(), 1);
    Out.Native = P4MapKit::ReadPackage(Out.Header, Out.Body);
    Values(Out.Native, Out.Body, ExportOf(Out.Native, "Component"), "StaticMeshComponent", Written);
    TestNotEqual(TEXT("The game's choice is not written over the creator's"), Json(Written.Field("Mobility")), Said);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameStockTagNotifyIdentityTest, "P4MapKit.EngineFields.GameStockTagNotifyIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameStockTagNotifyIdentityTest::RunTest(const FString&)
{
    using namespace P4StockTagsTest;
    const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package = CreatePackage(*(TEXT("/Game/Mods/NotifyIdentity_") + Token));
    TStrongObjectPtr<UAnimMontage> Asset(NewObject<UAnimMontage>(Package, TEXT("Montage"), RF_Public | RF_Standalone));
    ON_SCOPE_EXIT
    {
        Asset->ClearFlags(RF_Public | RF_Standalone);
        Asset->MarkPendingKill();
        Package->SetDirtyFlag(false);
    };
    Asset->SequenceLength = 1.f;
    Asset->Notifies.AddDefaulted();
    FCooked Plain, File;
    if (!Cook(*this, Package, Asset.Get(), TEXT("notify-plain"), Plain))
        return false;
    const int32 Export = ExportOf(Plain.Native, "Montage");
    if (!TestTrue(TEXT("The actual montage has an export"), Export != INDEX_NONE))
        return false;
    const bool Made = GameFile(
        Plain, Export,
        [](FList& Root, std::vector<P4MapKit::FPackageImport>&, std::vector<std::string>&)
        {
            FNode* Notifies = P4StockTags::Find(Root, "Notifies");
            if (!Notifies || Notifies->Elements.size() != 1)
                return false;
            FNode Middle = Tag("MiddleTriggerTimeOffset", "FloatProperty");
            Middle.Raw = Float(0.125f);
            Notifies->Elements[0].insert(Notifies->Elements[0].begin(), Middle);
            return true;
        },
        File);
    if (!TestTrue(TEXT("The game's notify addition is made"), Made))
        return false;
    FDecodedValue GameValues;
    if (!Values(File.Native, File.Body, Export, "AnimMontage", GameValues))
        return false;
    const auto* Notifies = GameValues.Field("Notifies");
    if (!TestTrue(TEXT("The game's notify decodes"), Notifies && Notifies->Items.size() == 1))
        return false;
    const FString Said = Json(Notifies->Items[0].Field("MiddleTriggerTimeOffset"));
    const auto& Item = File.Native.Exports[Export];
    TArray<FString> Kept, Problems;
    P4StoreGameStockTags(Asset.Get(), File.Body, File.Native, Item.Offset, Item.Offset + Item.Size, TEXT("AnimMontage"),
        {{TEXT("Notifies[0].MiddleTriggerTimeOffset"), Said, EP4GameStockTag::Member}}, Kept, Problems);
    if (!TestEqual(TEXT("The notify addition is kept"), Kept.Num(), 1))
        return false;
    Asset->Notifies[0].Guid.Invalidate();
    P4SealGameStockTags(Package);
    const FGuid Id = Asset->Notifies[0].Guid;
    if (!TestTrue(TEXT("The editor-only notify identity is initialized before sealing"), Id.IsValid()))
        return false;
    TArray<uint8> NoBytes;
    FMemoryReader Loading(NoBytes);
    Asset->Notifies[0].PostSerialize(Loading);
    TestEqual(TEXT("The real loading callback preserves that identity"), Asset->Notifies[0].Guid, Id);
    FCooked Out;
    FString Error;
    FDecodedValue Written;
    if (!Cook(*this, Package, Asset.Get(), TEXT("notify-loaded"), Out))
        return false;
    if (!TestTrue(TEXT("The cook after the real loading callback writes the kept notify value"),
            P4TranslateGameStockTagCook(Package, Out.Header, Out.Body, Error)))
    {
        AddError(Error);
        return false;
    }
    Out.Native = P4MapKit::ReadPackage(Out.Header, Out.Body);
    if (!Values(Out.Native, Out.Body, ExportOf(Out.Native, "Montage"), "AnimMontage", Written))
        return false;
    Notifies = Written.Field("Notifies");
    TestTrue(TEXT("Decoded readback retains the exact middle timing"),
        Notifies && Notifies->Items.size() == 1 && Json(Notifies->Items[0].Field("MiddleTriggerTimeOffset")) == Said);
    Asset->Notifies[0].NotifyTriggerChance = 0.5f;
    if (!Cook(*this, Package, Asset.Get(), TEXT("notify-edited"), Out))
        return false;
    TestTrue(
        TEXT("A creator's notify edit still cooks"), P4TranslateGameStockTagCook(Package, Out.Header, Out.Body, Error));
    TestEqual(TEXT("A creator's notify edit leaves the kept value out"), P4GameStockTagsLeftOut().Num(), 1);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameNativeTailsTest, "P4MapKit.EngineFields.GameNativeTails",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameNativeTailsTest::RunTest(const FString&)
{
    using namespace P4StockTagsTest;
    FString Error;
    // Foliage grid cells: the game's version 2 layout, written back exactly; other layouts are refused.
    FP4FoliageGridTail Grid;
    Grid.Version = 2;
    Grid.Cells = {1, 200, 0x34, 0x12, 14, 0, 0xff, 0xff, 0, 0, 0, 0};
    TArray<uint8> Bytes_;
    P4WriteFoliageGridTail(Grid, Bytes_);
    TestEqual(TEXT("A grid tail is the id flag, the version, the count and the cells"), Bytes_.Num(), 4 + 1 + 4 + 12);
    FP4FoliageGridTail Again;
    TestTrue(TEXT("The grid tail reads back exactly"),
        P4ReadFoliageGridTail(Bytes_.GetData(), Bytes_.Num(), Again, Error) && Again.Version == 2 &&
            Again.Cells == Grid.Cells);
    TArray<uint8> Short = Bytes_;
    Short.Pop();
    TestFalse(TEXT("A cell count that disagrees with the data is refused"),
        P4ReadFoliageGridTail(Short.GetData(), Short.Num(), Again, Error));
    TArray<uint8> Old = Bytes_;
    Old[4] = 1;
    TestFalse(TEXT("A version this reader does not know is refused"),
        P4ReadFoliageGridTail(Old.GetData(), Old.Num(), Again, Error));
    // A vector field's source: inline bulk data, read whole.
    TArray<uint8> Field = {0, 0, 0, 0, 0x48, 0, 0, 0, 64, 0, 0, 0, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (int32 At = 0; At < 64; ++At)
        Field.Add(uint8(At * 7));
    FP4VectorFieldTail Source;
    if (!TestTrue(TEXT("Inline vector field source reads"),
            P4ReadVectorFieldTail(Field.GetData(), Field.Num(), Source, Error)))
    {
        AddError(Error);
        return false;
    }
    TArray<uint8> AtEnd = Field;
    AtEnd[4] |= 0x1;
    FP4VectorFieldTail Refused;
    TestFalse(TEXT("A source stored elsewhere is refused"),
        P4ReadVectorFieldTail(AtEnd.GetData(), AtEnd.Num(), Refused, Error));
    const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package = CreatePackage(*(TEXT("/Game/Mods/VectorField_") + Token));
    TStrongObjectPtr<UVectorFieldStatic> Vectors(
        NewObject<UVectorFieldStatic>(Package, TEXT("Field"), RF_Public | RF_Standalone));
    ON_SCOPE_EXIT
    {
        Vectors->ClearFlags(RF_Public | RF_Standalone);
        Vectors->MarkPendingKill();
        Package->SetDirtyFlag(false);
    };
    Vectors->SizeX = Vectors->SizeY = 2;
    Vectors->SizeZ = 2;
    if (!TestTrue(
            TEXT("The engine's vector field takes the source"), P4FillVectorFieldSource(Vectors.Get(), Source, Error)))
    {
        AddError(Error);
        return false;
    }
    FCooked Out;
    if (!Cook(*this, Package, Vectors.Get(), TEXT("vector-field"), Out))
        return false;
    const int32 Export = ExportOf(Out.Native, "Field");
    const auto Tags = P4MapKit::ReadPropertyEnvelopes(Out.Body, Out.Native.Names, Out.Native.Exports[Export].Offset,
        Out.Native.Exports[Export].Offset + Out.Native.Exports[Export].Size);
    FP4VectorFieldTail Cooked;
    TestTrue(TEXT("The cook writes the same source inline with the game's flags"),
        Tags.Error.Code.empty() &&
            P4ReadVectorFieldTail(Out.Body.data() + Tags.End,
                int64(Out.Native.Exports[Export].Offset + Out.Native.Exports[Export].Size - Tags.End), Cooked, Error) &&
            Cooked.Payload == Source.Payload && P4VectorFieldTailLine(Cooked) == P4VectorFieldTailLine(Source));
    Package->GetMetaData()->SetValue(Vectors.Get(), TEXT("P4GameNativeTail"), TEXT("broken"));
    TestTrue(TEXT("Malformed native custody still routes through translation"), P4PackageHasGameNativeTails(Package));
    const auto Unchanged = Out;
    TestFalse(TEXT("Malformed native custody refuses translation"),
        P4TranslateGameNativeTailCook(Package, Out.Header, Out.Body, Error));
    TestTrue(TEXT("Malformed native custody keeps both buffers"),
        Out.Header == Unchanged.Header && Out.Body == Unchanged.Body);
    Package->GetMetaData()->RemoveValue(Vectors.Get(), TEXT("P4GameNativeTail"));
    // The game's grid asset class, when the project has it, gets its cells back from the cook.
    UClass* GridClass = LoadObject<UClass>(nullptr, TEXT("/Script/Carrot4.FoliageGridDataAsset"));
    if (!GridClass)
    {
        AddInfo(TEXT("P4MapKit test not run. The project has no FoliageGridDataAsset class for the cook part."));
        return !HasAnyErrors();
    }
    UPackage* GridPackage = CreatePackage(*(TEXT("/Game/Mods/FoliageGrid_") + Token));
    TStrongObjectPtr<UObject> Asset(
        NewObject<UObject>(GridPackage, GridClass, TEXT("Grid"), RF_Public | RF_Standalone));
    ON_SCOPE_EXIT
    {
        Asset->ClearFlags(RF_Public | RF_Standalone);
        Asset->MarkPendingKill();
        GridPackage->SetDirtyFlag(false);
    };
    if (!TestTrue(TEXT("The grid cells are kept with the asset"), P4StoreFoliageGridTail(Asset.Get(), Grid, Error)))
    {
        AddError(Error);
        return false;
    }
    if (!Cook(*this, GridPackage, Asset.Get(), TEXT("foliage-grid"), Out))
        return false;
    if (!TestTrue(TEXT("The cook writes the grid cells"),
            P4TranslateGameNativeTailCook(GridPackage, Out.Header, Out.Body, Error)))
    {
        AddError(Error);
        return false;
    }
    Out.Native = P4MapKit::ReadPackage(Out.Header, Out.Body);
    const int32 GridExport = ExportOf(Out.Native, "Grid");
    const auto GridTags = P4MapKit::ReadPropertyEnvelopes(Out.Body, Out.Native.Names,
        Out.Native.Exports[GridExport].Offset,
        Out.Native.Exports[GridExport].Offset + Out.Native.Exports[GridExport].Size);
    TestTrue(TEXT("The cooked grid holds the game's cells exactly"),
        GridTags.Error.Code.empty() &&
            P4ReadFoliageGridTail(Out.Body.data() + GridTags.End,
                int64(Out.Native.Exports[GridExport].Offset + Out.Native.Exports[GridExport].Size - GridTags.End),
                Again, Error) &&
            Again.Version == 2 && Again.Cells == Grid.Cells);
    const auto Before = Out;
    TestFalse(TEXT("A second pass refuses to write the cells twice"),
        P4TranslateGameNativeTailCook(GridPackage, Out.Header, Out.Body, Error));
    TestTrue(TEXT("A refused pass keeps both buffers"), Out.Header == Before.Header && Out.Body == Before.Body);
    return !HasAnyErrors();
}
#endif
