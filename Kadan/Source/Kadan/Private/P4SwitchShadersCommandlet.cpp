#include "P4SwitchShadersCommandlet.h"
#include "P4Common.h"
#include "P4MaterialGraph.h"
#include "P4SwitchShaderMapFreeze.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "UObject/StrongObjectPtr.h"

// A map's uniform expression set as JSON, laid out as material_shadermap.uniform_set reads the game's: the vector and
// scalar preshaders (offset and size in the preshader data), the parameters they read, the textures by type, the
// preshader data itself (hex) and the parameter collections. UE declares this class a friend of FUniformExpressionSet
// (for its own debugging) and defines it nowhere, so this is the way to its fields.
class FDebugUniformExpressionSet
{
public:
    static FString Json(const FUniformExpressionSet& Set)
    {
        auto Name = [](const FHashedMaterialParameterInfo& Info)
        {
            const FString Text = ScriptNameToName(Info.Name).ToString();
            FString Out = TEXT("\"");
            for (const TCHAR C : Text)
            {
                if (C == TEXT('"') || C == TEXT('\\'))
                    Out += TEXT('\\');
                Out += C;
            }
            return Out +
                FString::Printf(TEXT("\", \"index\": %d, \"association\": %d"), Info.Index, int32(Info.Association));
        };
        auto Headers = [](const TMemoryImageArray<FMaterialUniformPreshaderHeader>& List)
        {
            FString Out = TEXT("[");
            for (int32 i = 0; i < List.Num(); ++i)
                Out += FString::Printf(
                    TEXT("%s[%u, %u]"), i ? TEXT(", ") : TEXT(""), List[i].OpcodeOffset, List[i].OpcodeSize);
            return Out + TEXT("]");
        };
        FString Out = FString::Printf(
            TEXT("{\"vector_preshaders\": %s, \"scalar_preshaders\": %s, \"scalar_params\": ["),
            *Headers(Set.UniformVectorPreshaders), *Headers(Set.UniformScalarPreshaders));
        for (int32 i = 0; i < Set.UniformScalarParameters.Num(); ++i)
            Out += FString::Printf(TEXT("%s{\"name\": %s, \"default\": %.9g}"), i ? TEXT(", ") : TEXT(""),
                *Name(Set.UniformScalarParameters[i].ParameterInfo), Set.UniformScalarParameters[i].DefaultValue);
        Out += TEXT("], \"vector_params\": [");
        for (int32 i = 0; i < Set.UniformVectorParameters.Num(); ++i)
        {
            const FLinearColor& D = Set.UniformVectorParameters[i].DefaultValue;
            Out += FString::Printf(TEXT("%s{\"name\": %s, \"default\": [%.9g, %.9g, %.9g, %.9g]}"),
                i ? TEXT(", ") : TEXT(""), *Name(Set.UniformVectorParameters[i].ParameterInfo), D.R, D.G, D.B, D.A);
        }
        Out += TEXT("], \"textures\": [");
        for (int32 t = 0; t < NumMaterialTextureParameterTypes; ++t)
        {
            Out += t ? TEXT(", [") : TEXT("[");
            for (int32 i = 0; i < Set.UniformTextureParameters[t].Num(); ++i)
            {
                const FMaterialTextureParameterInfo& T = Set.UniformTextureParameters[t][i];
                Out += FString::Printf(TEXT("%s{\"name\": %s, \"texture_index\": %d, \"sampler_source\": %d}"),
                    i ? TEXT(", ") : TEXT(""), *Name(T.ParameterInfo), T.TextureIndex,
                    int32(T.SamplerSource.GetValue()));
            }
            Out += TEXT("]");
        }
        Out += FString::Printf(
            TEXT("], \"external_textures\": %d, \"preshader_names\": ["), Set.UniformExternalTextureParameters.Num());
        for (int32 i = 0; i < Set.UniformPreshaderData.Names.Num(); ++i)
            Out += FString::Printf(TEXT("%s\"%s\""), i ? TEXT(", ") : TEXT(""),
                *ScriptNameToName(Set.UniformPreshaderData.Names[i]).ToString());
        Out += TEXT("], \"preshader_data\": \"");
        for (int32 i = 0; i < Set.UniformPreshaderData.Data.Num(); ++i)
            Out += FString::Printf(TEXT("%02x"), Set.UniformPreshaderData.Data[i]);
        Out += TEXT("\", \"parameter_collections\": [");
        for (int32 i = 0; i < Set.ParameterCollections.Num(); ++i)
            Out += FString::Printf(
                TEXT("%s\"%s\""), i ? TEXT(", ") : TEXT(""), *Set.ParameterCollections[i].ToString());
        return Out +
            FString::Printf(
                TEXT("], \"constant_buffer_size\": %d}"), int32(Set.GetUniformBufferLayout().ConstantBufferSize));
    }
};

namespace
{
// -Freeze: each map also as the game's cooked material resource (P4SwitchShaderMapFreeze.h), q<quality>/resource.bin
// with its name table q<quality>/names.txt (one name a line), for comparing with the game's own map.
bool bFreeze = false;

// What UMaterial::CacheResourceShadersForCooking does for a shader platform (it is private and not exported): a
// resource for the platform's feature level, its shaders cached through UE's compile jobs and Derived Data Cache.
// The default quality level only, for now.
FMaterialResource* CacheForPlatform(UMaterial* Material, EShaderPlatform Platform)
{
    return P4CacheMaterialForPlatform(Material, Platform);
}

// A JSON string (UE's ReplaceCharWithEscapedChar also escapes ', which JSON does not allow).
FString Quote(const FString& S)
{
    FString Out = TEXT("\"");
    for (const TCHAR C : S)
    {
        if (C == TEXT('"') || C == TEXT('\\'))
        {
            Out += TEXT('\\');
            Out += C;
        }
        else if (C == TEXT('\n'))
            Out += TEXT("\\n");
        else if (C == TEXT('\r'))
            Out += TEXT("\\r");
        else if (C == TEXT('\t'))
            Out += TEXT("\\t");
        else if (C < 0x20)
            Out += FString::Printf(TEXT("\\u%04x"), int32(C));
        else
            Out += C;
    }
    return Out + TEXT("\"");
}

FString Infos(const TMemoryImageArray<FShaderParameterInfo>& List)
{
    FString Out = TEXT("[");
    for (int32 i = 0; i < List.Num(); ++i)
        Out += FString::Printf(
            TEXT("%s[%d, %d]"), i ? TEXT(", ") : TEXT(""), int32(List[i].BaseIndex), int32(List[i].Size));
    return Out + TEXT("]");
}

FString Loose(const TMemoryImageArray<FShaderLooseParameterBufferInfo>& List)
{
    FString Out = TEXT("[");
    for (int32 i = 0; i < List.Num(); ++i)
    {
        Out += FString::Printf(TEXT("%s{\"buffer\": %d, \"size\": %d, \"parameters\": %s}"), i ? TEXT(", ") : TEXT(""),
            int32(List[i].BaseIndex), int32(List[i].Size), *Infos(List[i].Parameters));
    }
    return Out + TEXT("]");
}

bool ShaderCode(const FShaderMapResourceCode::FShaderEntry& Entry, TArray<uint8>& Out)
{
    if (Entry.Code.Num() == Entry.UncompressedSize)
    {
        Out = Entry.Code;
        return true;
    }
    Out.SetNumUninitialized(Entry.UncompressedSize);
    return FCompression::UncompressMemory(
        NAME_LZ4, Out.GetData(), Entry.UncompressedSize, Entry.Code.GetData(), Entry.Code.Num());
}

// Compiles one material for SP_SWITCH and writes Folder/index.json and Folder/q<quality>/NNNN.payload. Notes go
// into the index as they are ("notes"). False when it did not compile.
bool CompileAndWrite(UMaterial* Material, const FString& Path, const FString& Folder, const FString& Notes)
{
    const double Start = FPlatformTime::Seconds();
    bool bOk = true;
    TArray<FMaterialResource*> Resources;
    FMaterialResource* Cached = CacheForPlatform(Material, SP_SWITCH);
    if (!Cached)
    {
        UE_LOG(LogTemp, Error, TEXT("P4SwitchShaders: %s has an unsupported shader resource allocator"), *Path);
        return false;
    }
    Resources.Add(Cached);
    if (GShaderCompilingManager)
        GShaderCompilingManager->FinishAllCompilation();
    FString Index = FString::Printf(TEXT("{\"material\": %s, \"seconds\": %.1f, \"notes\": %s, \"resources\": ["),
        *Quote(Path), 0.0, *Quote(Notes));
    for (int32 R = 0; R < Resources.Num(); ++R)
    {
        FMaterialResource* Resource = Resources[R];
        Resource->FinishCompilation();
        FMaterialShaderMap* Map = Resource->GetGameThreadShaderMap();
        FString Errors = TEXT("[");
        for (int32 E = 0; E < Resource->GetCompileErrors().Num(); ++E)
            Errors += (E ? TEXT(", ") : TEXT("")) + Quote(Resource->GetCompileErrors()[E]);
        Errors += TEXT("]");
        Index += FString::Printf(TEXT("%s{\"quality\": %d, \"errors\": %s, "), R ? TEXT(", ") : TEXT(""),
            int32(Resource->GetQualityLevel()), *Errors);
        // The material's translated HLSL beside the payloads, also when it did not compile.
        FString Source;
        if (Resource->GetMaterialExpressionSource(Source))
            FFileHelper::SaveStringToFile(Source,
                *(Folder / FString::Printf(TEXT("q%d/material.hlsl"), int32(Resource->GetQualityLevel()))),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        if (!Map || !Map->IsCompilationFinalized() || !Map->CompiledSuccessfully())
        {
            UE_LOG(LogTemp, Error, TEXT("P4SwitchShaders: %s quality %d did not compile"), *Path,
                int32(Resource->GetQualityLevel()));
            for (const FString& E : Resource->GetCompileErrors())
                UE_LOG(LogTemp, Error, TEXT("  %s"), *E);
            bOk = false;
            Index += TEXT("\"shaders\": []}");
            continue;
        }
        Index += TEXT("\"uniform_set\": ") + FDebugUniformExpressionSet::Json(Map->GetUniformExpressionSet()) +
            TEXT(", \"shaders\": [");
        FShaderMapResourceCode* Code = Map->GetResourceCode();
        TMap<FShaderId, TShaderRef<FShader>> Shaders;
        Map->GetShaderList(Shaders);
        int32 Written = 0;
        for (const auto& Pair : Shaders)
        {
            const FShaderId& Id = Pair.Key;
            const TShaderRef<FShader>& Shader = Pair.Value;
            const int32 ResourceIndex = Shader->GetResourceIndex();
            TArray<uint8> Payload;
            const bool bCode = Code && Code->ShaderEntries.IsValidIndex(ResourceIndex) &&
                ShaderCode(Code->ShaderEntries[ResourceIndex], Payload);
            const FString Name = FString::Printf(
                TEXT("q%d/%04d.payload"), int32(Resource->GetQualityLevel()), ResourceIndex);
            if (bCode)
                FFileHelper::SaveArrayToFile(Payload, *(Folder / Name));
            const FShaderParameterMapInfo& Info = Shader->ParameterMapInfo;
            Index += FString::Printf(
                TEXT(
                    "%s{\"type\": %s, \"vf\": %s, \"permutation\": %d, \"frequency\": %d, \"resource\": %d, \"payload\": %s, ")
                    TEXT("\"uniform_buffers\": %s, \"textures_samplers\": %s, \"srvs\": %s, \"loose\": %s}"),
                Written ? TEXT(", ") : TEXT(""), *Quote(Id.Type ? Id.Type->GetName() : TEXT("")),
                *Quote(Id.VFType ? Id.VFType->GetName() : TEXT("")), Id.PermutationId, int32(Shader->GetFrequency()),
                ResourceIndex, bCode ? *Quote(Name) : TEXT("null"), *Infos(Info.UniformBuffers),
                *Infos(Info.TextureSamplers), *Infos(Info.SRVs), *Loose(Info.LooseParameterBuffers));
            ++Written;
        }
        Index += TEXT("]}");
        if (bFreeze)
        {
            TArray<uint8> Block, Zero;
            Zero.SetNumZeroed(20);
            TArray<FName> Names;
            FString Error;
            const FString Quality = FString::Printf(TEXT("q%d/"), int32(Resource->GetQualityLevel()));
            if (P4FreezeSwitchShaderMap(*Resource, Zero, Zero, Names, Block, Error))
            {
                FFileHelper::SaveArrayToFile(Block, *(Folder / Quality + TEXT("resource.bin")));
                FString Lines;
                for (const FName& N : Names)
                    Lines += N.ToString() + TEXT("\n");
                FFileHelper::SaveStringToFile(
                    Lines, *(Folder / Quality + TEXT("names.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("P4SwitchShaders: %s not frozen: %s"), *Path, *Error);
                bOk = false;
            }
        }
        UE_LOG(LogTemp, Display, TEXT("P4SwitchShaders: %s quality %d: %d shaders"), *Path,
            int32(Resource->GetQualityLevel()), Written);
    }
    Index += TEXT("]}\n");
    Index = Index.Replace(
        TEXT("\"seconds\": 0.0"), *FString::Printf(TEXT("\"seconds\": %.1f"), FPlatformTime::Seconds() - Start));
    FFileHelper::SaveStringToFile(
        Index, *(Folder / TEXT("index.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    for (FMaterialResource* Resource : Resources)
    {
        P4ReleaseMaterialResource(Resource);
    }
    return bOk;
}

// Sets the material's usage flags by their UMaterial property names (bUsedWithSkeletalMesh, ...). Notes gets each
// name UMaterial has no flag for.
void SetUsage(UMaterial* Material, const TArray<TSharedPtr<FJsonValue>>& Names, FString& Notes)
{
    for (const TSharedPtr<FJsonValue>& Value : Names)
    {
        const FString Name = Value->AsString();
        FBoolProperty* Flag = FindFProperty<FBoolProperty>(UMaterial::StaticClass(), *Name);
        if (!Flag)
        {
            Notes += TEXT("no usage flag ") + Name + TEXT("; ");
            continue;
        }
        Flag->SetPropertyValue(Flag->ContainerPtrToValuePtr<void>(Material), true);
    }
}

// Lets a built material go: nothing keeps it once its package is no longer standalone.
void Release(UMaterial* Material)
{
    Material->ClearFlags(RF_Public | RF_Standalone);
    Material->GetOutermost()->ClearFlags(RF_Public | RF_Standalone);
}
} //namespace

UP4SwitchShadersCommandlet::UP4SwitchShadersCommandlet()
{
    // UE rebuilds a material's cached expression data (its referenced textures) only in a client.
    IsClient = true;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UP4SwitchShadersCommandlet::Main(const FString& Params)
{
    bFreeze = FParse::Param(*Params, TEXT("Freeze"));
    FString MaterialList, OutDir;
    FParse::Value(*Params, TEXT("Materials="), MaterialList, false);
    FParse::Value(*Params, TEXT("Out="), OutDir, false);
    TArray<FString> Paths;
    MaterialList.ParseIntoArray(Paths, TEXT("+"));
    // -Graphs=<file>: many graphs, {"<name>": {"graph": <graph>, "usage": ["bUsedWithSkeletalMesh", ...]}}. Each is
    // built in /Game/KadanSwitchGraphs/<name> (transient, not saved) with those usage flags and compiled into
    // Out/<name>; a name whose index.json is there already is skipped, so a run that stopped resumes.
    FString GraphsFile;
    if (FParse::Value(*Params, TEXT("Graphs="), GraphsFile, false))
    {
        FString Json;
        TSharedPtr<FJsonObject> All;
        if (OutDir.IsEmpty() || !FFileHelper::LoadFileToString(Json, *GraphsFile) || !P4ParseJson(Json, All))
        {
            UE_LOG(LogTemp, Error, TEXT("P4SwitchShaders: -Graphs=<json> -Out=<folder>: cannot read %s"), *GraphsFile);
            return 1;
        }
        TArray<FString> Names;
        All->Values.GetKeys(Names);
        Names.Sort();
        int32 Done = 0, Failed = 0, Since = 0;
        for (const FString& Name : Names)
        {
            const FString Folder = OutDir / Name;
            if (IFileManager::Get().FileExists(*(Folder / TEXT("index.json"))))
                continue;
            const TSharedPtr<FJsonObject> Entry = All->Values[Name]->AsObject();
            const TSharedPtr<FJsonObject>* Graph = nullptr;
            FString Notes, GraphText, Error;
            if (!Entry.IsValid() || !Entry->TryGetObjectField(TEXT("graph"), Graph))
            {
                Error = TEXT("no graph");
            }
            else
            {
                FJsonSerializer::Serialize(Graph->ToSharedRef(),
                    TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&GraphText));
                UMaterial* Material = P4BuildMaterialFromGraph(
                    GraphText, TEXT("/Game/KadanSwitchGraphs/") + Name, Error);
                if (Material)
                {
                    if (!Error.IsEmpty())
                        Notes += TEXT("graph: ") + Error + TEXT("; ");
                    Error.Reset();
                    const TArray<TSharedPtr<FJsonValue>>* Usage = nullptr;
                    if (Entry->TryGetArrayField(TEXT("usage"), Usage))
                        SetUsage(Material, *Usage, Notes);
                    if (!CompileAndWrite(Material, Name, Folder, Notes))
                        ++Failed;
                    Release(Material);
                    ++Done;
                }
            }
            if (!Error.IsEmpty())
            {
                // Not built: the index says why, so the run does not try it again.
                UE_LOG(LogTemp, Error, TEXT("P4SwitchShaders: %s did not build: %s"), *Name, *Error);
                FFileHelper::SaveStringToFile(
                    FString::Printf(TEXT("{\"material\": %s, \"build_error\": %s, \"resources\": []}\n"), *Quote(Name),
                        *Quote(Error)),
                    *(Folder / TEXT("index.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
                ++Failed;
            }
            if (++Since >= 16)
            {
                CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
                Since = 0;
            }
        }
        UE_LOG(LogTemp, Display, TEXT("P4SwitchShaders: %d graphs compiled, %d failed"), Done, Failed);
        return Failed ? 1 : 0;
    }
    // -Graph=<file>: a graph of Kadan's material decompiler ({"graph": ...} or the graph itself), built as a material
    // in -GraphPackage (transient, not saved) and compiled with the others.
    FString GraphFile, GraphPackage = TEXT("/Game/KadanSwitchTest/M_Graph");
    TStrongObjectPtr<UMaterial> Built;
    if (FParse::Value(*Params, TEXT("Graph="), GraphFile, false))
    {
        FParse::Value(*Params, TEXT("GraphPackage="), GraphPackage, false);
        FString Json, Error;
        if (!FFileHelper::LoadFileToString(Json, *GraphFile))
        {
            UE_LOG(LogTemp, Error, TEXT("P4SwitchShaders: cannot read %s"), *GraphFile);
            return 1;
        }
        Built.Reset(P4BuildMaterialFromGraph(Json, GraphPackage, Error));
        if (!Built.IsValid())
        {
            UE_LOG(LogTemp, Error, TEXT("P4SwitchShaders: the graph did not build: %s"), *Error);
            return 1;
        }
        if (!Error.IsEmpty())
            UE_LOG(LogTemp, Warning, TEXT("P4SwitchShaders: graph notes: %s"), *Error);
        // As the export's runtime copy of a creator's material: drawn with lightmaps in the game's levels.
        Built->bUsedWithStaticLighting = true;
        Paths.Add(Built->GetPathName());
    }
    if (!Paths.Num() || OutDir.IsEmpty())
    {
        UE_LOG(LogTemp, Error,
            TEXT("P4SwitchShaders: -Materials=/Game/A+/Game/B -Out=<folder>, or -Graphs=<json> -Out=<folder>"));
        return 1;
    }
    int32 Failures = 0;
    for (const FString& Path : Paths)
    {
        UMaterial* Material = LoadObject<UMaterial>(nullptr, *Path);
        if (!Material)
        {
            UE_LOG(LogTemp, Error, TEXT("P4SwitchShaders: no material %s"), *Path);
            ++Failures;
            continue;
        }
        if (!CompileAndWrite(Material, Path, OutDir / FPaths::GetBaseFilename(Path), FString()))
            ++Failures;
    }
    return Failures ? 1 : 0;
}
