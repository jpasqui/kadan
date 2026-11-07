#include "P4MaterialRecovery.h"
#include "P4KadanImport.h"
#include "P4OriginalTextureImport.h"
#include "P4OriginalAssetImport.h"
#include "P4KadanAssetPolicy.h"
#include "P4OriginalRecord.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Materials/Material.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialShared.h"
#include "Misc/AutomationTest.h"
#include "DynamicRHI.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "ShaderCompiler.h"
#include "HAL/FileManager.h"
#include "P4MapKitCore/package_reader.h"
#include "P4MapKitCore/material_cooked.h"
#include "P4MapKitCore/material_json.h"
#include "P4MapKitCore/material_game_reads.h"
#include "P4MapKitCore/custom_material_hash.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace P4FixtureRetirement
{
bool Package(UPackage* Made);
}
namespace P4MaterialRecoveryTest
{
namespace M = P4MapKit::Material;

std::string Utf8(const FString& Text)
{
    FTCHARToUTF8 Value(*Text);
    return std::string(Value.Get(), Value.Length());
}

bool ReadJson(const FString& File, M::Json& Out)
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *File))
        return false;
    Out = M::Json::Parse(Utf8(Text), 256u * 1024u * 1024u, 20000000);
    return true;
}

// The ordered topology of a graph's pins, independent of node names
// (tools/navigation/native_material_corpus.root_fingerprints).
struct FFingerprint
{
    const M::Json& Graph;
    std::map<std::string, std::string> Memo;
    std::string Node(const std::string& Id)
    {
        const auto Found = Memo.find(Id);
        if (Found != Memo.end())
            return Found->second;
        const M::Json& Node = Graph.At("nodes").At(Id);
        std::string Body = "[" + M::Json(Node.At("type").Text()).Dump() + ",{";
        bool First = true;
        if (Node.Has("inputs"))
            for (const auto& Input : Node.At("inputs").object)
            {
                Body += (First ? "" : ",") + M::Json(Input.first).Dump() + ":" + Link(Input.second);
                First = false;
            }
        Body += "}," + (Node.Has("props") ? Node.At("props") : M::Json::MakeObject()).Dump() + "]";
        const std::string Digest = P4MapKit::CustomMaterial::Hex(
            P4MapKit::CustomMaterial::Sha256(P4MapKit::CustomMaterial::ByteText(Body)));
        Memo[Id] = Digest;
        return Digest;
    }
    std::string Link(const M::Json& Value)
    {
        std::string Out = Node(Value.array.at(0).Text());
        for (std::size_t I = 1; I < Value.array.size(); ++I)
            Out += "|" + Value.array[I].Dump();
        return Out;
    }
};

// What differs between two graphs: their pins, each pin's formula, and their settings. A recovered master's
// render settings also hold the master's cooked true usage flags (cb2db2b5, after the decompiler's records
// were made): those are compared with the flags read from the master's own package, Usage.
FString Differences(const M::Json& Expected, const M::Json& Actual, const std::set<std::string>* Usage = nullptr)
{
    FString Out;
    FFingerprint A{Expected, {}}, B{Actual, {}};
    const auto& Left = Expected.At("outputs").object;
    const auto& Right = Actual.At("outputs").object;
    for (const auto& Pin : Left)
    {
        const auto Other = Right.find(Pin.first);
        if (Other == Right.end())
            Out += TEXT(" missing pin ") + FString(UTF8_TO_TCHAR(Pin.first.c_str()));
        else if (A.Link(Pin.second) != B.Link(Other->second))
            Out += TEXT(" pin ") + FString(UTF8_TO_TCHAR(Pin.first.c_str()));
    }
    for (const auto& Pin : Right)
        if (!Left.count(Pin.first))
            Out += TEXT(" extra pin ") + FString(UTF8_TO_TCHAR(Pin.first.c_str()));
    M::Json SettingsA = Expected, SettingsB = Actual;
    for (const char* Key : {"nodes", "outputs"})
    {
        SettingsA.object.erase(Key);
        SettingsB.object.erase(Key);
    }
    if (Usage)
    {
        std::set<std::string> Held;
        const auto Render = SettingsB.object.find("render_settings");
        if (Render != SettingsB.object.end() && Render->second.kind == M::Json::Object)
        {
            for (auto Field = Render->second.object.begin(); Field != Render->second.object.end();)
                if (Field->first.compare(0, 9, "bUsedWith") == 0 &&
                    !(SettingsA.Has("render_settings") && SettingsA.At("render_settings").Has(Field->first)))
                {
                    if (Field->second.kind == M::Json::Boolean && Field->second.boolean)
                        Held.insert(Field->first);
                    else
                        Out += TEXT(" usage-not-true");
                    Field = Render->second.object.erase(Field);
                }
                else
                    ++Field;
            if (Render->second.object.empty() && !SettingsA.Has("render_settings"))
                SettingsB.object.erase(Render);
        }
        if (Held != *Usage)
            Out += TEXT(" usage");
    }
    if (SettingsA.Dump() != SettingsB.Dump())
        Out += TEXT(" settings");
    return Out;
}

// Takes what the test made out of the project again, so that nothing of it stays in the session.
void Remove(const FString& Path)
{
    UObject* Object = FindObject<UObject>(nullptr, *Path);
    if (!Object)
        return;
    FAssetRegistryModule::AssetDeleted(Object);
    Object->ClearFlags(RF_Public | RF_Standalone);
    Object->Rename(
        nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
    Object->MarkPendingKill();
    // Texture imports also inspect the package's direct children. Moving only the texture leaves its
    // publication/import data behind, occupying the original slot in the next test in this editor.
    UPackage* Package = FindPackage(nullptr, *FPackageName::ObjectPathToPackageName(Path));
    if (Package)
    {
        TArray<UObject*> Left;
        GetObjectsWithOuter(Package, Left, false);
        for (UObject* Child : Left)
            if (Child && !Child->IsA<UMetaData>())
            {
                Child->ClearFlags(RF_Public | RF_Standalone);
                Child->Rename(nullptr, GetTransientPackage(),
                    REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
                Child->MarkPendingKill();
            }
    }
}

// The game folder's packages, as the import scan reads them: package name, class and files.
void Scan(const FString& Folder, TArray<TSharedPtr<FP4KadanSourceAsset>>& Assets, TArray<FString>& Problems)
{
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *Folder, TEXT("*.uasset"), true, false);
    Files.Sort();
    for (const FString& File : Files)
    {
        TArray<uint8> Header;
        const FString Body = FPaths::ChangeExtension(File, TEXT("uexp"));
        if (!FFileHelper::LoadFileToArray(Header, *File, FILEREAD_Silent))
        {
            Problems.Add(File + TEXT(": unreadable"));
            continue;
        }
        const auto Read = P4MapKit::ReadPackageHeader(
            std::vector<uint8_t>(Header.GetData(), Header.GetData() + Header.Num()),
            std::size_t(FMath::Max<int64>(0, IFileManager::Get().FileSize(*Body))));
        if (!Read.Error.Code.empty())
        {
            Problems.Add(File + TEXT(": ") + UTF8_TO_TCHAR(Read.Error.Code.c_str()));
            continue;
        }
        auto Asset = MakeShared<FP4KadanSourceAsset>();
        Asset->Package = UTF8_TO_TCHAR(Read.Package.c_str());
        Asset->Header = File;
        Asset->Body = Body;
        const FString Leaf = FPackageName::GetLongPackageAssetName(Asset->Package);
        for (const auto& Export : Read.Exports)
        {
            std::string Class;
            if (Export.Outer != 0 || !Read.Reference(Export.Class, Class))
                continue;
            FString Kind = UTF8_TO_TCHAR(Class.c_str());
            int32 Dot = INDEX_NONE;
            if (Kind.FindLastChar(TEXT('.'), Dot))
                Kind = Kind.Mid(Dot + 1);
            if (Asset->Kind.IsEmpty() || UTF8_TO_TCHAR(Export.Name.c_str()) == Leaf)
                Asset->Kind = Kind;
        }
        Assets.Add(Asset);
    }
}

// A master's cooked true usage flags, read from its own package (as the material recovery reads them).
bool CookedUsage(const TArray<TSharedPtr<FP4KadanSourceAsset>>& Assets, const FString& Package,
    std::set<std::string>& Out, FString& Why)
{
    const TSharedPtr<FP4KadanSourceAsset>* Found = Assets.FindByPredicate(
        [&](const TSharedPtr<FP4KadanSourceAsset>& One) { return One->Package == Package; });
    TArray<uint8> Header, Body;
    if (!Found || !FFileHelper::LoadFileToArray(Header, *(*Found)->Header) ||
        !FFileHelper::LoadFileToArray(Body, *(*Found)->Body))
    {
        Why = TEXT("unreadable");
        return false;
    }
    const std::vector<uint8_t> Bytes(Body.GetData(), Body.GetData() + Body.Num());
    const auto Inventory = P4MapKit::ReadPackageHeader(
        std::vector<uint8_t>(Header.GetData(), Header.GetData() + Header.Num()), Bytes.size());
    P4MapKit::FMaterialCookedRecord Record;
    P4MapKit::FReadError Error;
    P4MapKit::FMaterialCookedSettings Settings;
    if (!P4MapKit::ReadMaterialCookedRecord(Inventory, Bytes, Record, Error) ||
        !P4MapKit::ReadMaterialCookedSettings({Record}, Settings, Error))
    {
        Why = UTF8_TO_TCHAR(Error.Code.c_str());
        return false;
    }
    Out = std::set<std::string>(Settings.Usage.begin(), Settings.Usage.end());
    return true;
}
bool Compiled(UMaterialInterface* Material, FString& Why)
{
    FMaterialResource* Resource = Material ? Material->GetMaterialResource(GMaxRHIFeatureLevel) : nullptr;
    if (!Resource)
    {
        Why = TEXT("no material resource");
        return false;
    }
    if (Resource->GetCompileErrors().Num())
    {
        Why = Resource->GetCompileErrors()[0];
        return false;
    }
    if (!Resource->HasValidGameThreadShaderMap())
    {
        Why = TEXT("no shader map");
        return false;
    }
    return true;
}
// What the materials refer to, made in the project first: textures and parameter collections.
void ImportReferenced(
    FAutomationTestBase& Test, const TArray<TSharedPtr<FP4KadanSourceAsset>>& Assets, TArray<FString>& Made)
{
    for (const auto& Asset : Assets)
    {
        if (Asset->Kind.StartsWith(TEXT("Texture")))
        {
            const auto Imported = P4ImportOriginalTextureNative(Asset->Header, Asset->Body, false);
            if (Imported.IsValid())
                Made.Add(Imported.AssetPath);
            else
                Test.AddError(Asset->Package + TEXT(" texture: ") + Imported.ErrorDetail);
        }
        else if (Asset->Kind == TEXT("MaterialParameterCollection"))
        {
            const auto Imported = P4ImportOriginalAssetNative(Asset->Header, Asset->Body, false);
            if (Imported.IsValid())
                Made.Add(Imported.AssetPath);
            else
                Test.AddError(Asset->Package + TEXT(" collection: ") + Imported.ErrorDetail);
        }
    }
}

FString BuiltAsset(const FP4MaterialRecoveryResult& Result)
{
    TSharedPtr<FJsonObject> Report;
    FString Asset;
    if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Result.Report), Report) && Report.IsValid())
        Report->TryGetStringField(TEXT("asset"), Asset);
    return Asset;
}
} //namespace P4MaterialRecoveryTest

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialRecoveryCave000Test, "P4MapKit.Kadan.MaterialRecoveryCave000",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4MaterialRecoveryCave000Test::RunTest(const FString&)
{
    using namespace P4MaterialRecoveryTest;
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (Root.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "MaterialRecoveryCave000 was not exercised. Set P4_NATIVE_ROOT to private/mapkit-native-workflow."));
        return true;
    }
    const FString Game = Root / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    const FString Records = FPaths::GetPath(Root) / TEXT("material-re/decompile");
    M::Json Accepted, Masters, Instances;
    try
    {
        if (!ReadJson(Records / TEXT("cave000-masters.json"), Accepted) ||
            !ReadJson(Records / TEXT("cave000-now-masters.json"), Masters) ||
            !ReadJson(Records / TEXT("cave000-now-instances.json"), Instances))
        {
            AddError(TEXT("The Python decompiler's Cave000 records are missing under ") + Records);
            return false;
        }
    }
    catch (const std::exception& Error)
    {
        AddError(FString(TEXT("Unreadable record: ")) + UTF8_TO_TCHAR(Error.what()));
        return false;
    }

    // The figures of the acceptance: 20 masters with a graph, 53 materials drawn by them, 33 instances.
    int32 AcceptedGraphs = 0, AcceptedUsers = 0;
    for (const auto& Item : Accepted.object)
        if (Item.second.Has("graph"))
        {
            ++AcceptedGraphs;
            AcceptedUsers += int32(Item.second.At("users").array.size());
        }
    TestEqual(TEXT("masters with a graph in the acceptance record"), AcceptedGraphs, 20);
    TestEqual(TEXT("materials the acceptance's masters draw"), AcceptedUsers, 53);
    TestEqual(TEXT("instances in the record"), int32(Instances.object.size()), 33);
    TestEqual(TEXT("masters in the record of the decompiler now"), int32(Masters.object.size()), AcceptedGraphs);

    TArray<TSharedPtr<FP4KadanSourceAsset>> Assets;
    TArray<FString> Problems;
    Scan(Game, Assets, Problems);
    TMap<FString, int32> Kinds;
    for (const auto& Asset : Assets)
        ++Kinds.FindOrAdd(Asset->Kind);
    FString KindText;
    for (const auto& Pair : Kinds)
        KindText += FString::Printf(TEXT(" %s %d"), *Pair.Key, Pair.Value);
    AddInfo(FString::Printf(TEXT("Scanned %d packages (%d problems):"), Assets.Num(), Problems.Num()) + KindText);
    for (int32 I = 0; I < Problems.Num() && I < 5; ++I)
        AddWarning(TEXT("Scan: ") + Problems[I]);
    // The full map closure holds 118 instances. The decompiler record below covers its 33 map instances.
    TestEqual(TEXT("material instances scanned"), Kinds.FindRef(TEXT("MaterialInstanceConstant")), 118);
    FP4MaterialRecovery Recovery(Game, Assets);

    int32 SameGraphs = 0, SameInstances = 0, GameReadMasters = 0;
    for (const auto& Item : Masters.object)
    {
        const FString Package = UTF8_TO_TCHAR(Item.first.c_str());
        const FP4MaterialRecoveryResult Made = Recovery.Recover(Package);
        if (!Made.bOk)
        {
            AddError(Package + TEXT(" refused: ") + Made.Reason);
            continue;
        }
        if (!Item.second.Has("graph"))
        {
            AddError(Package + TEXT(" has a graph that the decompiler's record does not"));
            continue;
        }
        // Kadan game nodes stand for the scene reads the game's shader makes: compared as those reads.
        const M::Json Built = M::Json::Parse(Utf8(Made.Source), 64u * 1024u * 1024u, 2000000);
        bool GameReads = false;
        for (const auto& Node : Built.At("nodes").object)
            GameReads |= !M::KadanGameReadMeaning(Node.second.At("type").Text()).empty();
        GameReadMasters += GameReads ? 1 : 0;
        std::set<std::string> Usage;
        FString UsageWhy;
        if (!CookedUsage(Assets, Package, Usage, UsageWhy))
        {
            AddError(Package + TEXT(" cooked usage unreadable: ") + UsageWhy);
            continue;
        }
        const FString Differs = Differences(Item.second.At("graph"), M::StockGameReads(Built), &Usage);
        if (Differs.IsEmpty())
            ++SameGraphs;
        else
            AddError(Package + TEXT(" graph differs from the record:") + Differs);
    }
    for (const auto& Item : Instances.object)
    {
        const FString Package = UTF8_TO_TCHAR(Item.first.c_str());
        const FP4MaterialRecoveryResult Made = Recovery.Recover(Package);
        if (Item.second.Has("refused"))
        {
            if (Made.bOk)
                AddError(Package + TEXT(" is made but the record refuses it"));
            else
                ++SameInstances;
            continue;
        }
        if (!Made.bOk)
        {
            AddError(Package + TEXT(" refused: ") + Made.Reason);
            continue;
        }
        if (M::Json::Parse(Utf8(Made.Source)).Dump() == Item.second.Dump())
            ++SameInstances;
        else
            AddError(Package + TEXT(" differs from the record: ") + Made.Source);
    }
    TestEqual(TEXT("masters recovered with a graph"), Recovery.MastersWithGraph(), 20);
    // The four Soft masters are opaque and read the scene's depth and color.
    TestEqual(TEXT("masters with Kadan game reads"), GameReadMasters, 4);
    TestEqual(TEXT("master graphs the same as the decompiler's"), SameGraphs, 20);
    TestEqual(TEXT("instances the same as the decompiler's"), SameInstances, 33);

    // Build them in the project: what they refer to first, then the masters, then the instances.
    TArray<FString> Made;
    for (const auto& Asset : Assets)
    {
        if (Asset->Kind.StartsWith(TEXT("Texture")))
        {
            const auto Imported = P4ImportOriginalTextureNative(Asset->Header, Asset->Body, false);
            if (Imported.IsValid())
                Made.Add(Imported.AssetPath);
            else
                AddError(Asset->Package + TEXT(" texture: ") + Imported.ErrorDetail);
        }
        else if (Asset->Kind == TEXT("MaterialParameterCollection"))
        {
            const auto Imported = P4ImportOriginalAssetNative(Asset->Header, Asset->Body, false);
            if (Imported.IsValid())
                Made.Add(Imported.AssetPath);
            else
                AddError(Asset->Package + TEXT(" collection: ") + Imported.ErrorDetail);
        }
    }
    const bool Renderer = !GUsingNullRHI;
    int32 Built = 0, CompiledCount = 0;
    TArray<FString> Order;
    for (const auto& Item : Masters.object)
        Order.Add(UTF8_TO_TCHAR(Item.first.c_str()));
    const int32 MasterCount = Order.Num();
    for (const auto& Item : Instances.object)
        Order.Add(UTF8_TO_TCHAR(Item.first.c_str()));
    // An instance's parent can be another instance: parents first.
    TArray<FString> Pending(Order.GetData() + MasterCount, Order.Num() - MasterCount);
    Order.SetNum(MasterCount);
    while (Pending.Num())
    {
        bool Moved = false;
        for (int32 I = 0; I < Pending.Num(); ++I)
        {
            const M::Json& Entry = Instances.At(Utf8(Pending[I]));
            const FString Parent = Entry.Has("parent") ? FString(UTF8_TO_TCHAR(Entry.At("parent").Text().c_str()))
                                                       : FString();
            const FString ParentPackage = FPackageName::ObjectPathToPackageName(Parent);
            if (!Pending.ContainsByPredicate([&](const FString& Other) { return Other == ParentPackage; }))
            {
                Order.Add(Pending[I]);
                Pending.RemoveAt(I);
                Moved = true;
                break;
            }
        }
        if (!Moved)
        {
            Order.Append(Pending);
            break;
        }
    }
    for (const FString& Package : Order)
    {
        const FP4MaterialRecoveryResult Result = Recovery.Import(Package, false);
        TSharedPtr<FJsonObject> Report;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Result.Report), Report);
        FString Asset;
        if (Report.IsValid())
            Report->TryGetStringField(TEXT("asset"), Asset);
        if (!Asset.IsEmpty())
            Made.Add(Asset);
        if (!Result.bOk)
        {
            const TArray<TSharedPtr<FJsonValue>>* Errors = nullptr;
            FString First;
            if (Report.IsValid() && Report->TryGetArrayField(TEXT("compile_errors"), Errors) && Errors->Num())
                First = TEXT(" (") + (*Errors)[0]->AsString() + TEXT(")");
            AddError(Package + TEXT(" was not built: ") + Result.Reason + First);
            continue;
        }
        ++Built;
        if (!Renderer)
            continue;
        GShaderCompilingManager->FinishAllCompilation();
        FString Why;
        if (Compiled(FindObject<UMaterialInterface>(nullptr, *Asset), Why))
            ++CompiledCount;
        else
            AddError(Package + TEXT(" does not compile: ") + Why);
    }
    TestEqual(TEXT("materials and instances built"), Built, 53);
    if (Renderer)
        TestEqual(TEXT("materials and instances compiled"), CompiledCount, 53);
    else
        AddInfo(TEXT(
            "The renderer is NullRHI: the materials were built but not compiled. Run with -d3d11 in a session with a screen."));
    AddInfo(FString::Printf(
        TEXT("Cave000: %d master graphs and %d instances as the decompiler's, %d built, %d compiled with %s."),
        SameGraphs, SameInstances, Built, CompiledCount, Renderer ? GDynamicRHI->GetName() : TEXT("no renderer")));
    for (int32 I = Made.Num() - 1; I >= 0; --I)
        Remove(Made[I]);
    return true;
}
// The six masters the recovery holds through game-only operations: four UI gauges that test the exact
// +Inf word, and two postprocess materials that read a normal texture's encoded channels.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialRecoveryHeldTest, "P4MapKit.Kadan.MaterialRecoveryHeld",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4MaterialRecoveryHeldTest::RunTest(const FString&)
{
    using namespace P4MaterialRecoveryTest;
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (Root.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("MaterialRecoveryHeld was not exercised. Set P4_NATIVE_ROOT to private/mapkit-native-workflow."));
        return true;
    }
    const FString Game = FPaths::GetPath(Root) / TEXT("kadan-materials-held/game");
    if (!IFileManager::Get().DirectoryExists(*Game))
    {
        AddError(TEXT("The held masters' game files are missing at ") + Game);
        return false;
    }
    TArray<TSharedPtr<FP4KadanSourceAsset>> Assets;
    TArray<FString> Problems, Made;
    Scan(Game, Assets, Problems);
    ImportReferenced(*this, Assets, Made);
    FP4MaterialRecovery Recovery(Game, Assets);
    const FString Infinity = TEXT("Original positive infinity equality"),
                  Normal = TEXT("Original encoded normal texture channels");
    TArray<TPair<FString, FString>> Held;
    for (const TCHAR* Name :
        {TEXT("UI/M_ui_CircleGauge_Translucent_00_Master"), TEXT("UI/M_ui_CircleGauge_Translucent_01_Master"),
            TEXT("UI/M_ui_Countdown_Translucent_00_Master"), TEXT("UI/M_ui_LifeGauge_Translucent_00_Master")})
        Held.Emplace(FString(TEXT("/Game/Carrot4/MasterMaterial/")) + Name, Infinity);
    for (const TCHAR* Name : {TEXT("UI/PhotoMode/Frame/M_ui_Filter_Heat"), TEXT("PostProcess/PP_UnderWater_Master")})
        Held.Emplace(FString(TEXT("/Game/Carrot4/MasterMaterial/")) + Name, Normal);
    int32 Built = 0;
    for (const auto& Item : Held)
    {
        const FP4MaterialRecoveryResult Recovered = Recovery.Recover(Item.Key);
        if (!Recovered.bOk)
        {
            AddError(Item.Key + TEXT(" refused: ") + Recovered.Reason);
            continue;
        }
        int32 Operations = 0;
        M::Json Graph;
        try
        {
            Graph = M::Json::Parse(Utf8(Recovered.Source), 64u * 1024u * 1024u, 2000000);
        }
        catch (const std::exception& Error)
        {
            AddError(Item.Key + TEXT(" graph unreadable: ") + UTF8_TO_TCHAR(Error.what()));
            continue;
        }
        for (const auto& Node : Graph.At("nodes").object)
            if (Node.second.At("type").Text() == "Custom" && Node.second.At("props").Has("Description") &&
                Node.second.At("props").At("Description").Text() == Utf8(Item.Value))
                ++Operations;
        TestEqual(Item.Key + TEXT(" game operations"), Operations, Item.Value == Infinity ? 2 : 1);
        const FP4MaterialRecoveryResult Result = Recovery.Import(Item.Key, false);
        const FString Asset = BuiltAsset(Result);
        if (!Asset.IsEmpty())
            Made.Add(Asset);
        if (!Result.bOk)
        {
            AddError(Item.Key + TEXT(" was not built: ") + Result.Reason);
            continue;
        }
        ++Built;
    }
    TestEqual(TEXT("held masters built"), Built, 6);
    if (GUsingNullRHI)
        AddInfo(TEXT("The renderer is NullRHI: the held masters were built but not compiled."));
    else
        AddInfo(FString::Printf(TEXT("%d held masters built and compiled with %s."), Built, GDynamicRHI->GetName()));
    for (int32 I = Made.Num() - 1; I >= 0; --I)
        Remove(Made[I]);
    return true;
}

// A recovered master that reads the scene, and an instance of it, say that the editor draws them a
// little differently from the game: the read-only notice and the Content Browser's description.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialPreviewNoticeTest, "P4MapKit.Kadan.MaterialPreviewNotice",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4MaterialPreviewNoticeTest::RunTest(const FString&)
{
    using namespace P4MaterialRecoveryTest;
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (Root.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("MaterialPreviewNotice was not exercised. Set P4_NATIVE_ROOT to private/mapkit-native-workflow."));
        return true;
    }
    const FString Game = Root / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    TArray<TSharedPtr<FP4KadanSourceAsset>> Assets;
    TArray<FString> Problems, Made;
    Scan(Game, Assets, Problems);
    ImportReferenced(*this, Assets, Made);
    FP4MaterialRecovery Recovery(Game, Assets);
    const FString Soft = TEXT("/Game/Carrot4/MasterMaterial/stg/M_stg_Cave_Soft_Opaque_00_Master");
    const FString Instance = TEXT(
        "/Game/Carrot4/Terrain/Cave/Type/Soil/Meshes/CMA_T02_DC_DirtPile01/MI_stg_Soil_Cmn_DirtPile01_Soft");
    const FString Plain = TEXT("/Game/Carrot4/MasterMaterial/stg/M_stg_Cave_Opaque_00_Master");
    TMap<FString, UObject*> Objects;
    for (const FString& Package : {Soft, Instance, Plain})
    {
        const FP4MaterialRecoveryResult Result = Recovery.Import(Package, false);
        const FString Asset = BuiltAsset(Result);
        if (!Asset.IsEmpty())
            Made.Add(Asset);
        if (!Result.bOk)
        {
            AddError(Package + TEXT(" was not built: ") + Result.Reason);
            continue;
        }
        TestEqual(Package + TEXT(" preview flag"), Result.bPreviewDiffers, Package != Plain);
        Objects.Add(Package, FindObject<UObject>(nullptr, *Asset));
    }
    const FString Notice = TEXT(
        "The editor draws this material a little differently from the game. It looks right in the game.");
    for (const FString& Package : {Soft, Instance})
    {
        UObject* Object = Objects.FindRef(Package);
        if (!TestNotNull(*Package, Object))
            continue;
        TestEqual(Package + TEXT(" read-only notice"), P4Kadan::GetAssetPolicy(Object).PreviewNotice, Notice);
        TestTrue(Package + TEXT(" browser description"),
            P4Kadan::PreviewDescription(Object, FText()).ToString() == TEXT("Preview differs from the game."));
    }
    if (UObject* Object = Objects.FindRef(Plain))
    {
        TestTrue(TEXT("a master that draws as the game has no notice"),
            P4Kadan::GetAssetPolicy(Object).PreviewNotice.IsEmpty());
        TestTrue(TEXT("a master that draws as the game keeps its description"),
            P4Kadan::PreviewDescription(Object, FText()).IsEmpty());
    }
    TestEqual(TEXT("import report sentence"), Recovery.PreviewNote(),
        FString(TEXT(
            "The editor draws 2 materials a little differently from the game, and they look right in the game. They are M_stg_Cave_Soft_Opaque_00_Master and MI_stg_Soil_Cmn_DirtPile01_Soft.")));
    for (int32 I = Made.Num() - 1; I >= 0; --I)
        Remove(Made[I]);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialRecoveryHeld57Test, "P4MapKit.Kadan.MaterialRecoveryHeld57",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialRecoveryHeld57Test::RunTest(const FString&)
{
    const FString CatalogFile = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_HELD57_CATALOG"));
    const FString Game = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_HELD57_GAMEFILES"));
    if (CatalogFile.IsEmpty() || Game.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "MaterialRecoveryHeld57 was not exercised. Configure the exact catalogue and original game source folder."));
        return true;
    }
    if (GUsingNullRHI)
    {
        AddError(TEXT("MaterialRecoveryHeld57 requires an actual D3D11 renderer."));
        return false;
    }
    const bool Resume = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_HELD57_RESUME")) == TEXT("1");
    TSharedPtr<FJsonObject> PreviousImport;
    if (Resume)
    {
        FString OwnerText, PreviousText, Owner;
        TSharedPtr<FJsonObject> Marker;
        if (!FFileHelper::LoadFileToString(OwnerText, *(FPaths::ProjectDir() / TEXT("owner.json"))) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(OwnerText), Marker) || !Marker.IsValid() ||
            !Marker->TryGetStringField(TEXT("owner"), Owner) || Owner != TEXT("kadan-materials-held57-native") ||
            !FFileHelper::LoadFileToString(
                PreviousText, *(FPaths::ProjectSavedDir() / TEXT("Kadan/import-result.json"))) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PreviousText), PreviousImport) ||
            !PreviousImport.IsValid())
        {
            AddError(TEXT("Held57 continuation requires its owned project and actual ordinary import checkpoint."));
            return false;
        }
    }
    FString Text;
    TSharedPtr<FJsonObject> Catalog;
    const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
    if (!FFileHelper::LoadFileToString(Text, *CatalogFile) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Catalog) || !Catalog.IsValid() ||
        !Catalog->TryGetArrayField(TEXT("masters"), Names) || Names->Num() != 57)
    {
        AddError(TEXT("The exact held57 catalogue is missing or malformed."));
        return false;
    }
    TSet<FString> Roots;
    TSet<FName> Identities;
    for (const auto& Value : *Names)
    {
        FString Name;
        if (!Value->TryGetString(Name) || !Name.StartsWith(TEXT("/Game/")) || Identities.Contains(FName(*Name)))
        {
            AddError(TEXT("The held57 catalogue has an invalid or duplicate original master."));
            return false;
        }
        Roots.Add(Name);
        Identities.Add(FName(*Name));
        if (!Resume && (FindPackage(nullptr, *Name) || FPackageName::DoesPackageExist(Name)))
        {
            AddError(TEXT("The held57 sweep requires a fresh project without existing original masters."));
            return false;
        }
    }
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FString> Problems;
    if (!P4KadanScanGame(Game, Sources, Problems) || Problems.Num())
    {
        for (const auto& Problem : Problems)
            AddError(Problem);
        return false;
    }
    TMap<FName, TSharedPtr<FP4KadanSourceAsset>> SourceByIdentity;
    for (const auto& Source : Sources)
        SourceByIdentity.Add(FName(*Source->Package), Source);
    for (const FString& Name : Roots)
    {
        const auto* Source = SourceByIdentity.Find(FName(*Name));
        if (!Source || (*Source)->Kind != TEXT("Material"))
        {
            AddError(Name + TEXT(" is absent from the actual source as a master material."));
            return false;
        }
        if (Resume && FPackageName::DoesPackageExist((*Source)->Package))
        {
            const FString Asset = (*Source)->Package + TEXT(".") +
                FPackageName::GetLongPackageAssetName((*Source)->Package);
            auto* Material = LoadObject<UMaterial>(nullptr, *Asset);
            FP4OriginalRecord Record;
            FString Error;
            if (!Material || !P4ReadOriginalRecord(Material, Record, Error) || FName(*Record.Package) != FName(*Name))
            {
                AddError(Name + TEXT(" cannot be reused as the exact loaded original material. ") + Error);
                return false;
            }
        }
    }
    // Retry only explicitly named failed original masters in this owned fixture.
    // Successful recovered graphs and their shader cache remain in place.
    TArray<FString> RetryNames;
    FPlatformMisc::GetEnvironmentVariable(TEXT("P4_HELD57_REIMPORT")).ParseIntoArray(RetryNames, TEXT(";"), true);
    TArray<UPackage*> RetryPackages;
    TArray<FString> RetryFiles;
    const TSharedPtr<FJsonObject>* PreviousAssets = nullptr;
    if (RetryNames.Num() && (!Resume || !PreviousImport->TryGetObjectField(TEXT("assets"), PreviousAssets)))
    {
        AddError(
            TEXT("Selective Held57 reimport requires the owned continuation and its actual per-asset checkpoint."));
        return false;
    }
    for (const FString& Name : RetryNames)
    {
        const FName Identity(*Name);
        const auto* Source = SourceByIdentity.Find(Identity);
        if (!Identities.Contains(Identity) || !Source || (*Source)->Kind != TEXT("Material"))
        {
            AddError(Name + TEXT(" is outside this exact held57 master set."));
            return false;
        }
        const FString Asset = (*Source)->Package + TEXT(".") +
            FPackageName::GetLongPackageAssetName((*Source)->Package);
        UMaterial* Material = LoadObject<UMaterial>(nullptr, *Asset);
        FP4OriginalRecord Record;
        FString Error;
        const auto Policy = P4Kadan::GetAssetPolicy(Material);
        if (!Material || Material->GetOutermost()->IsDirty() || !P4ReadOriginalRecord(Material, Record, Error) ||
            FName(*Record.Package) != Identity || !Record.ReferenceOnly || !Policy.Original || Policy.AllowEdit ||
            !Policy.ReferenceOnly)
        {
            AddError(Name + TEXT(" is not a clean read-only reference-only original eligible for fixture reimport. ") +
                Error);
            return false;
        }
        const TSharedPtr<FJsonObject>* Prior = nullptr;
        bool PriorReferenceOnly = false;
        FString PriorHeader;
        if (!(*PreviousAssets)->TryGetObjectField((*Source)->Package, Prior) ||
            !(*Prior)->TryGetBoolField(TEXT("reference_only"), PriorReferenceOnly) || !PriorReferenceOnly ||
            !(*Prior)->TryGetStringField(TEXT("header"), PriorHeader) ||
            !FPaths::IsSamePath(PriorHeader, (*Source)->Header))
        {
            AddError(Name + TEXT(" has no actual failed import from this exact source in the owned checkpoint."));
            return false;
        }
        const FString Header = FPaths::ConvertRelativePathToFull(
            FPackageName::LongPackageNameToFilename((*Source)->Package, FPackageName::GetAssetPackageExtension()));
        const FString Content = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
        if (!FPaths::IsUnderDirectory(Header, Content))
        {
            AddError(Name + TEXT(" does not resolve inside the owned project Content."));
            return false;
        }
        for (const TCHAR* Extension : {TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl")})
        {
            const FString File = FPaths::ChangeExtension(Header, Extension);
#if PLATFORM_WINDOWS
            for (FString At = File; !At.IsEmpty();)
            {
                const DWORD Attributes = GetFileAttributesW(*At);
                if (Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                {
                    AddError(TEXT("The owned fixture reimport path contains a filesystem link."));
                    return false;
                }
                const FString Parent = FPaths::GetPath(At);
                if (Parent == At)
                    break;
                At = Parent;
            }
#else
            AddError(TEXT("Scoped fixture reimport requires the supported Windows path boundary."));
            return false;
#endif
            RetryFiles.AddUnique(File);
        }
        RetryPackages.AddUnique(Material->GetOutermost());
    }
    for (UPackage* Package : RetryPackages)
        if (!P4FixtureRetirement::Package(Package))
        {
            AddError(TEXT("The owned failed original could not be retired safely."));
            return false;
        }
    if (RetryPackages.Num())
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    for (const FString& File : RetryFiles)
        if (IFileManager::Get().FileExists(*File) && !IFileManager::Get().Delete(*File, false, false))
        {
            AddError(TEXT("The exact owned failed original file could not be removed: ") + File);
            return false;
        }
    FP4KadanImportSettings Settings;
    Settings.GameFolder = Game;
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (Settings.Repak.IsEmpty() && !Settings.Retoc.IsEmpty())
        Settings.Repak = FPaths::GetPath(FPaths::GetPath(Settings.Retoc)) / TEXT("repak/repak.exe");
    const FP4KadanImportPlan Plan = P4KadanPlanImport(Sources, Roots, Resume);
    const FP4KadanImportResult Imported = P4KadanRunImport(Settings, Plan, Resume);
    TSharedPtr<FJsonObject> Report;
    const TSharedPtr<FJsonObject>* Replies = nullptr;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Imported.Report), Report);
    if (!Report.IsValid() || !Report->TryGetObjectField(TEXT("assets"), Replies))
    {
        AddError(TEXT("The ordinary importer did not produce its per-asset report."));
        return false;
    }
    auto Sweep = MakeShared<FJsonObject>();
    auto Rows = MakeShared<FJsonObject>();
    Sweep->SetObjectField(TEXT("import"), Report);
    Sweep->SetBoolField(TEXT("import_ok"), Imported.Ok);
    Sweep->SetBoolField(TEXT("resumed"), Resume);
    TArray<TSharedPtr<FJsonValue>> Retried;
    for (const FString& Name : RetryNames)
        Retried.Add(MakeShared<FJsonValueString>(Name));
    Sweep->SetArrayField(TEXT("reimported"), Retried);
    if (PreviousImport.IsValid())
        Sweep->SetObjectField(TEXT("previous_import"), PreviousImport);
    for (const FString& Problem : Imported.Problems)
        AddError(TEXT("Ordinary importer: ") + Problem);
    int32 CompiledCount = 0, ReadOnlyCount = 0;
    GShaderCompilingManager->FinishAllCompilation();
    for (const FString& Name : Roots)
    {
        auto Row = MakeShared<FJsonObject>();
        const TSharedPtr<FJsonObject>* Reply = nullptr;
        const auto Source = SourceByIdentity.FindChecked(FName(*Name));
        if ((*Replies)->TryGetObjectField(Source->Package, Reply))
            Row->SetObjectField(TEXT("import"), *Reply);
        Row->SetStringField(TEXT("actual_source"), Source->Package);
        const FString Asset = Source->Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Source->Package);
        UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Asset);
        FString Why;
        const bool RendererCompiled = P4MaterialRecoveryTest::Compiled(Material, Why);
        const auto Policy = P4Kadan::GetAssetPolicy(Material);
        FP4OriginalRecord Record;
        FString IdentityError;
        const bool OriginalIdentity = Material && P4ReadOriginalRecord(Material, Record, IdentityError) &&
            FName(*Record.Package) == FName(*Source->Package);
        const bool ReadOnly = OriginalIdentity && Policy.Original && !Policy.AllowEdit && !Policy.ReferenceOnly;
        // A copied reference-only material can acquire an editor shader map. It
        // does not establish recovery of the original graph and never counts.
        const bool Compiled = ReadOnly && RendererCompiled;
        Row->SetBoolField(TEXT("renderer_compiled"), RendererCompiled);
        Row->SetBoolField(TEXT("original_identity"), OriginalIdentity);
        Row->SetBoolField(TEXT("reference_only"), Policy.ReferenceOnly);
        Row->SetStringField(TEXT("identity_problem"), IdentityError);
        Row->SetBoolField(TEXT("compiled"), Compiled);
        Row->SetBoolField(TEXT("read_only"), ReadOnly);
        Row->SetStringField(TEXT("compiler_problem"), Why);
        Rows->SetObjectField(Name, Row);
        CompiledCount += Compiled;
        ReadOnlyCount += ReadOnly;
        if (!Compiled)
            AddError(Name + TEXT(" does not compile. ") + Why);
        if (!ReadOnly)
            AddError(Name + TEXT(" was not recovered as a read-only original graph."));
    }
    Sweep->SetObjectField(TEXT("masters"), Rows);
    Sweep->SetNumberField(TEXT("compiled"), CompiledCount);
    Sweep->SetNumberField(TEXT("read_only"), ReadOnlyCount);
    Sweep->SetBoolField(TEXT("renderer_active"), true);
    FString Output;
    FJsonSerializer::Serialize(Sweep, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output));
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/Held57");
    if (!IFileManager::Get().MakeDirectory(*Folder, true) ||
        !FFileHelper::SaveStringToFile(
            Output, *(Folder / TEXT("report.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        AddError(TEXT("The held57 native evidence could not be saved."));
    TestEqual(TEXT("Held57 masters compile in the actual renderer"), CompiledCount, 57);
    TestEqual(TEXT("Held57 masters remain read-only originals"), ReadOnlyCount, 57);
    return !HasAnyErrors();
}
#endif
