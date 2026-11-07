#include "P4MaterialInstanceCopyContent.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4MaterialInstancePermutation.h"
#include "P4CreatorSource.h"
#include "P4GameReplacement.h"
#include "P4KadanAssetPolicy.h"
#include "P4LevelContent.h"
#include "P4MaterialInstanceComparison.h"
#include "P4NativeHash.h"
#include "P4OriginalRecord.h"
#include "Dom/JsonObject.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "Internationalization/Text.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StaticParameterSet.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace P4MICCopy
{
// These are storage schemas, not a list of fields that may be ignored. Unknown
// fields/types/layouts refuse even when their current values look like defaults.
struct FFieldSpec
{
    const TCHAR* Name;
    const TCHAR* Type;
    int32 Dim = 1;
    bool Transient = false;
};
using FSpecs = TMap<FString, TArray<FFieldSpec>>;

const FSpecs& Schemas()
{
    static const FSpecs Result = []
    {
        FSpecs S;
        S.Add(TEXT("/Script/CoreUObject.Object"), {});
        S.Add(TEXT("/Script/Engine.AssetUserData"), {});
        S.Add(TEXT("/Script/Kadan.P4CreatorSourceData"),
            {{TEXT("Format"), TEXT("int")}, {TEXT("Kind"), TEXT("name")}, {TEXT("SourceMode"), TEXT("name")},
                {TEXT("AssetClass"), TEXT("name")}, {TEXT("OriginPackage"), TEXT("name")}});
        S.Add(TEXT("/Script/Engine.MaterialInterface"),
            {{TEXT("SubsurfaceProfile"), TEXT("object:/Script/Engine.SubsurfaceProfile")},
                {TEXT("LightmassSettings"), TEXT("struct:/Script/Engine.LightmassMaterialInterfaceSettings")},
                {TEXT("bTextureStreamingDataSorted"), TEXT("bool"), 1, true},
                {TEXT("TextureStreamingDataVersion"), TEXT("int")},
                {TEXT("TextureStreamingData"), TEXT("array:struct:/Script/Engine.MaterialTextureInfo")},
                {TEXT("AssetUserData"), TEXT("array:object:/Script/Engine.AssetUserData")},
                {TEXT("TextureStreamingDataMissingEntries"), TEXT("array:struct:/Script/Engine.MaterialTextureInfo"), 1,
                    true},
                {TEXT("PreviewMesh"), TEXT("struct:/Script/CoreUObject.SoftObjectPath")},
                {TEXT("ThumbnailInfo"), TEXT("object:/Script/Engine.ThumbnailInfo")},
                {TEXT("LayerParameterExpansion"), TEXT("map:string,bool")},
                {TEXT("ParameterOverviewExpansion"), TEXT("map:string,bool")},
                {TEXT("AssetImportData"), TEXT("object:/Script/Engine.AssetImportData")},
                {TEXT("LightingGuid"), TEXT("struct:/Script/CoreUObject.Guid")}});
        S.Add(TEXT("/Script/Engine.MaterialInstance"),
            {{TEXT("PhysMaterial"), TEXT("object:/Script/PhysicsCore.PhysicalMaterial")},
                {TEXT("PhysicalMaterialMap"), TEXT("object:/Script/PhysicsCore.PhysicalMaterial"),
                    EPhysicalMaterialMaskColor::MAX},
                {TEXT("Parent"), TEXT("object:/Script/Engine.MaterialInterface")},
                {TEXT("bHasStaticPermutationResource"), TEXT("bool")},
                {TEXT("bOverrideSubsurfaceProfile"), TEXT("bool")},
                {TEXT("ScalarParameterValues"), TEXT("array:struct:/Script/Engine.ScalarParameterValue")},
                {TEXT("VectorParameterValues"), TEXT("array:struct:/Script/Engine.VectorParameterValue")},
                {TEXT("TextureParameterValues"), TEXT("array:struct:/Script/Engine.TextureParameterValue")},
                {TEXT("RuntimeVirtualTextureParameterValues"),
                    TEXT("array:struct:/Script/Engine.RuntimeVirtualTextureParameterValue")},
                {TEXT("FontParameterValues"), TEXT("array:struct:/Script/Engine.FontParameterValue")},
                {TEXT("bOverrideBaseProperties_DEPRECATED"), TEXT("bool")},
                {TEXT("BasePropertyOverrides"), TEXT("struct:/Script/Engine.MaterialInstanceBasePropertyOverrides")},
                {TEXT("ReferencedTextureGuids"), TEXT("array:struct:/Script/CoreUObject.Guid")},
                {TEXT("StaticParameters"), TEXT("struct:/Script/Engine.StaticParameterSet")},
                {TEXT("CachedLayerParameters"), TEXT("struct:/Script/Engine.MaterialCachedParameters")},
                {TEXT("CachedReferencedTextures"), TEXT("array:object:/Script/CoreUObject.Object")}});
        S.Add(TEXT("/Script/Engine.MaterialInstanceConstant"),
            {{TEXT("ParameterStateId"), TEXT("struct:/Script/CoreUObject.Guid")},
                {TEXT("PhysMaterialMask"), TEXT("object:/Script/Engine.PhysicalMaterialMask")}});
        // FGuid's reflected members are int32 (NoExportTypes.h), whatever the C++ struct says.
        S.Add(TEXT("/Script/CoreUObject.Guid"),
            {{TEXT("A"), TEXT("int")}, {TEXT("B"), TEXT("int")}, {TEXT("C"), TEXT("int")}, {TEXT("D"), TEXT("int")}});
        S.Add(TEXT("/Script/CoreUObject.LinearColor"),
            {{TEXT("R"), TEXT("float")}, {TEXT("G"), TEXT("float")}, {TEXT("B"), TEXT("float")},
                {TEXT("A"), TEXT("float")}});
        S.Add(TEXT("/Script/CoreUObject.Vector2D"), {{TEXT("X"), TEXT("float")}, {TEXT("Y"), TEXT("float")}});
        S.Add(TEXT("/Script/CoreUObject.SoftObjectPath"),
            {{TEXT("AssetPathName"), TEXT("name")}, {TEXT("SubPathString"), TEXT("string")}});
        S.Add(TEXT("/Script/Engine.LightmassMaterialInterfaceSettings"),
            {{TEXT("EmissiveBoost"), TEXT("float")}, {TEXT("DiffuseBoost"), TEXT("float")},
                {TEXT("ExportResolutionScale"), TEXT("float")}, {TEXT("bCastShadowAsMasked"), TEXT("bool")},
                {TEXT("bOverrideCastShadowAsMasked"), TEXT("bool")}, {TEXT("bOverrideEmissiveBoost"), TEXT("bool")},
                {TEXT("bOverrideDiffuseBoost"), TEXT("bool")}, {TEXT("bOverrideExportResolutionScale"), TEXT("bool")}});
        S.Add(TEXT("/Script/Engine.MaterialTextureInfo"),
            {{TEXT("SamplingScale"), TEXT("float")}, {TEXT("UVChannelIndex"), TEXT("int")},
                {TEXT("TextureName"), TEXT("name")},
                {TEXT("TextureReference"), TEXT("struct:/Script/CoreUObject.SoftObjectPath")},
                {TEXT("TextureIndex"), TEXT("int"), 1, true}});
        S.Add(TEXT("/Script/Engine.MaterialParameterInfo"),
            {{TEXT("Name"), TEXT("name")},
                {TEXT("Association"), TEXT("byte:/Script/Engine.EMaterialParameterAssociation")},
                {TEXT("Index"), TEXT("int")},
                {TEXT("ParameterLocation"), TEXT("struct:/Script/CoreUObject.SoftObjectPath"), 1, true}});
        S.Add(TEXT("/Script/Engine.ScalarParameterAtlasInstanceData"),
            {{TEXT("bIsUsedAsAtlasPosition"), TEXT("bool")},
                {TEXT("Curve"), TEXT("softobject:/Script/Engine.CurveLinearColor")},
                {TEXT("Atlas"), TEXT("softobject:/Script/Engine.CurveLinearColorAtlas")}});
        const auto Parameter = [&](const TCHAR* Type, const TCHAR* ValueType, bool Atlas, bool Deprecated)
        {
            TArray<FFieldSpec> Fields = {{TEXT("ParameterInfo"), TEXT("struct:/Script/Engine.MaterialParameterInfo")},
                {TEXT("ParameterValue"), ValueType}, {TEXT("ExpressionGUID"), TEXT("struct:/Script/CoreUObject.Guid")}};
            if (Deprecated)
                Fields.Add({TEXT("ParameterName_DEPRECATED"), TEXT("name")});
            if (Atlas)
                Fields.Add({TEXT("AtlasData"), TEXT("struct:/Script/Engine.ScalarParameterAtlasInstanceData")});
            S.Add(Type, MoveTemp(Fields));
        };
        Parameter(TEXT("/Script/Engine.ScalarParameterValue"), TEXT("float"), true, true);
        Parameter(
            TEXT("/Script/Engine.VectorParameterValue"), TEXT("struct:/Script/CoreUObject.LinearColor"), false, true);
        Parameter(TEXT("/Script/Engine.TextureParameterValue"), TEXT("object:/Script/Engine.Texture"), false, true);
        Parameter(TEXT("/Script/Engine.RuntimeVirtualTextureParameterValue"),
            TEXT("object:/Script/Engine.RuntimeVirtualTexture"), false, false);
        S.Add(TEXT("/Script/Engine.FontParameterValue"),
            {{TEXT("ParameterName_DEPRECATED"), TEXT("name")},
                {TEXT("ParameterInfo"), TEXT("struct:/Script/Engine.MaterialParameterInfo")},
                {TEXT("FontValue"), TEXT("object:/Script/Engine.Font")}, {TEXT("FontPage"), TEXT("int")},
                {TEXT("ExpressionGUID"), TEXT("struct:/Script/CoreUObject.Guid")}});
        S.Add(TEXT("/Script/Engine.MaterialInstanceBasePropertyOverrides"),
            {{TEXT("bOverride_OpacityMaskClipValue"), TEXT("bool")}, {TEXT("bOverride_BlendMode"), TEXT("bool")},
                {TEXT("bOverride_ShadingModel"), TEXT("bool")}, {TEXT("bOverride_DitheredLODTransition"), TEXT("bool")},
                {TEXT("bOverride_CastDynamicShadowAsMasked"), TEXT("bool")}, {TEXT("bOverride_TwoSided"), TEXT("bool")},
                {TEXT("TwoSided"), TEXT("bool")}, {TEXT("DitheredLODTransition"), TEXT("bool")},
                {TEXT("bCastDynamicShadowAsMasked"), TEXT("bool")},
                {TEXT("BlendMode"), TEXT("byte:/Script/Engine.EBlendMode")},
                {TEXT("ShadingModel"), TEXT("byte:/Script/Engine.EMaterialShadingModel")},
                {TEXT("OpacityMaskClipValue"), TEXT("float")}});
        S.Add(TEXT("/Script/Engine.StaticParameterSet"),
            {{TEXT("StaticSwitchParameters"), TEXT("array:struct:/Script/Engine.StaticSwitchParameter")},
                {TEXT("StaticComponentMaskParameters"),
                    TEXT("array:struct:/Script/Engine.StaticComponentMaskParameter")},
                {TEXT("TerrainLayerWeightParameters"),
                    TEXT("array:struct:/Script/Engine.StaticTerrainLayerWeightParameter")},
                {TEXT("MaterialLayersParameters"), TEXT("array:struct:/Script/Engine.StaticMaterialLayersParameter")}});
        S.Add(TEXT("/Script/Engine.StaticParameterBase"),
            {{TEXT("ParameterInfo"), TEXT("struct:/Script/Engine.MaterialParameterInfo")},
                {TEXT("bOverride"), TEXT("bool")}, {TEXT("ExpressionGUID"), TEXT("struct:/Script/CoreUObject.Guid")}});
        S.Add(TEXT("/Script/Engine.StaticSwitchParameter"), {{TEXT("Value"), TEXT("bool")}});
        S.Add(TEXT("/Script/Engine.StaticComponentMaskParameter"),
            {{TEXT("R"), TEXT("bool")}, {TEXT("G"), TEXT("bool")}, {TEXT("B"), TEXT("bool")},
                {TEXT("A"), TEXT("bool")}});
        S.Add(TEXT("/Script/Engine.StaticTerrainLayerWeightParameter"),
            {{TEXT("WeightmapIndex"), TEXT("int")}, {TEXT("bWeightBasedBlend"), TEXT("bool")}});
        S.Add(TEXT("/Script/Engine.StaticMaterialLayersParameter"),
            {{TEXT("Value"), TEXT("struct:/Script/Engine.MaterialLayersFunctions")}});
        S.Add(TEXT("/Script/Engine.MaterialLayersFunctions"),
            {{TEXT("Layers"), TEXT("array:object:/Script/Engine.MaterialFunctionInterface")},
                {TEXT("Blends"), TEXT("array:object:/Script/Engine.MaterialFunctionInterface")},
                {TEXT("LayerStates"), TEXT("array:bool")}, {TEXT("LayerNames"), TEXT("array:text")},
                {TEXT("RestrictToLayerRelatives"), TEXT("array:bool")},
                {TEXT("RestrictToBlendRelatives"), TEXT("array:bool")},
                {TEXT("LayerGuids"), TEXT("array:struct:/Script/CoreUObject.Guid")},
                {TEXT("LayerLinkStates"), TEXT("array:enum:/Script/Engine.EMaterialLayerLinkState")},
                {TEXT("DeletedParentLayerGuids"), TEXT("array:struct:/Script/CoreUObject.Guid")},
                {TEXT("KeyString_DEPRECATED"), TEXT("string")}});
        S.Add(TEXT("/Script/Engine.MaterialCachedParameterEntry"),
            {{TEXT("NameHashes"), TEXT("array:uint64")},
                {TEXT("ParameterInfos"), TEXT("array:struct:/Script/Engine.MaterialParameterInfo")},
                {TEXT("ExpressionGuids"), TEXT("array:struct:/Script/CoreUObject.Guid")},
                {TEXT("Overrides"), TEXT("array:bool")}});
        S.Add(TEXT("/Script/Engine.StaticComponentMaskValue"),
            {{TEXT("R"), TEXT("bool")}, {TEXT("G"), TEXT("bool")}, {TEXT("B"), TEXT("bool")},
                {TEXT("A"), TEXT("bool")}});
        S.Add(TEXT("/Script/Engine.ParameterChannelNames"),
            {{TEXT("R"), TEXT("text")}, {TEXT("G"), TEXT("text")}, {TEXT("B"), TEXT("text")},
                {TEXT("A"), TEXT("text")}});
        S.Add(TEXT("/Script/Engine.MaterialCachedParameters"),
            {{TEXT("RuntimeEntries"), TEXT("struct:/Script/Engine.MaterialCachedParameterEntry"),
                 NumMaterialRuntimeParameterTypes},
                {TEXT("ScalarValues"), TEXT("array:float")},
                {TEXT("VectorValues"), TEXT("array:struct:/Script/CoreUObject.LinearColor")},
                {TEXT("TextureValues"), TEXT("array:object:/Script/Engine.Texture")},
                {TEXT("FontValues"), TEXT("array:object:/Script/Engine.Font")},
                {TEXT("FontPageValues"), TEXT("array:int")},
                {TEXT("RuntimeVirtualTextureValues"), TEXT("array:object:/Script/Engine.RuntimeVirtualTexture")},
                {TEXT("EditorOnlyEntries"), TEXT("struct:/Script/Engine.MaterialCachedParameterEntry"),
                    NumMaterialEditorOnlyParameterTypes},
                {TEXT("StaticSwitchValues"), TEXT("array:bool")},
                {TEXT("StaticComponentMaskValues"), TEXT("array:struct:/Script/Engine.StaticComponentMaskValue")},
                {TEXT("ScalarMinMaxValues"), TEXT("array:struct:/Script/CoreUObject.Vector2D")},
                {TEXT("ScalarCurveValues"), TEXT("array:object:/Script/Engine.CurveLinearColor")},
                {TEXT("ScalarCurveAtlasValues"), TEXT("array:object:/Script/Engine.CurveLinearColorAtlas")},
                {TEXT("VectorChannelNameValues"), TEXT("array:struct:/Script/Engine.ParameterChannelNames")},
                {TEXT("VectorUsedAsChannelMaskValues"), TEXT("array:bool")},
                {TEXT("TextureChannelNameValues"), TEXT("array:struct:/Script/Engine.ParameterChannelNames")}});
        return S;
    }();
    return Result;
}

FString TypeOf(const FProperty* P)
{
    if (const auto* A = CastField<FArrayProperty>(P))
        return TEXT("array:") + TypeOf(A->Inner);
    if (const auto* M = CastField<FMapProperty>(P))
        return TEXT("map:") + TypeOf(M->KeyProp) + TEXT(",") + TypeOf(M->ValueProp);
    if (const auto* S = CastField<FStructProperty>(P))
        return TEXT("struct:") + S->Struct->GetPathName();
    if (const auto* S = CastField<FSoftObjectProperty>(P))
        return TEXT("softobject:") + S->PropertyClass->GetPathName();
    if (P->GetClass() == FObjectProperty::StaticClass())
        return TEXT("object:") + CastFieldChecked<FObjectProperty>(P)->PropertyClass->GetPathName();
    if (const auto* E = CastField<FEnumProperty>(P))
        return TEXT("enum:") + E->GetEnum()->GetPathName();
    if (const auto* B = CastField<FByteProperty>(P))
        return B->Enum ? TEXT("byte:") + B->Enum->GetPathName() : FString(TEXT("byte"));
    if (P->GetClass() == FBoolProperty::StaticClass())
        return TEXT("bool");
    if (P->GetClass() == FIntProperty::StaticClass())
        return TEXT("int");
    if (P->GetClass() == FUInt32Property::StaticClass())
        return TEXT("uint32");
    if (P->GetClass() == FUInt64Property::StaticClass())
        return TEXT("uint64");
    if (P->GetClass() == FFloatProperty::StaticClass())
        return TEXT("float");
    if (P->GetClass() == FNameProperty::StaticClass())
        return TEXT("name");
    if (P->GetClass() == FStrProperty::StaticClass())
        return TEXT("string");
    if (P->GetClass() == FTextProperty::StaticClass())
        return TEXT("text");
    return TEXT("unsupported:") + P->GetClass()->GetName();
}

bool Schema(UStruct* Type, TSet<const UStruct*>& Seen, FString& Error);
bool Layout(const FProperty* P, int32 Depth = 0)
{
    if (!P || Depth > 32 || P->ArrayDim < 1 || P->ElementSize < 1 || P->HasAnyPropertyFlags(CPF_SkipSerialization))
        return false;
    if (const auto* A = CastField<FArrayProperty>(P))
        return P->ElementSize == sizeof(FScriptArray) && A->Inner->ArrayDim == 1 && Layout(A->Inner, Depth + 1);
    if (const auto* M = CastField<FMapProperty>(P))
        return P->ElementSize == sizeof(FScriptMap) && M->KeyProp->ArrayDim == 1 && M->ValueProp->ArrayDim == 1 &&
            Layout(M->KeyProp, Depth + 1) && Layout(M->ValueProp, Depth + 1);
    if (const auto* S = CastField<FStructProperty>(P))
        return S->Struct && P->ElementSize == S->Struct->GetStructureSize();
    if (CastField<FSoftObjectProperty>(P))
        return P->ElementSize == sizeof(FSoftObjectPtr);
    if (P->GetClass() == FObjectProperty::StaticClass())
        return P->ElementSize == sizeof(UObject*);
    if (const auto* E = CastField<FEnumProperty>(P))
        return P->ElementSize == sizeof(uint8) && E->GetUnderlyingProperty() &&
            E->GetUnderlyingProperty()->GetClass() == FByteProperty::StaticClass();
    if (P->GetClass() == FByteProperty::StaticClass())
        return P->ElementSize == sizeof(uint8);
    if (P->GetClass() == FBoolProperty::StaticClass())
        return P->ElementSize == sizeof(uint8);
    if (P->GetClass() == FIntProperty::StaticClass())
        return P->ElementSize == sizeof(int32);
    if (P->GetClass() == FUInt32Property::StaticClass())
        return P->ElementSize == sizeof(uint32);
    if (P->GetClass() == FUInt64Property::StaticClass())
        return P->ElementSize == sizeof(uint64);
    if (P->GetClass() == FFloatProperty::StaticClass())
        return P->ElementSize == sizeof(float);
    if (P->GetClass() == FNameProperty::StaticClass())
        return P->ElementSize == sizeof(FName);
    if (P->GetClass() == FStrProperty::StaticClass())
        return P->ElementSize == sizeof(FString);
    if (P->GetClass() == FTextProperty::StaticClass())
        return P->ElementSize == sizeof(FText);
    return false;
}
bool NestedSchema(const FProperty* P, TSet<const UStruct*>& Seen, FString& Error)
{
    if (const auto* S = CastField<FStructProperty>(P))
        return Schema(S->Struct, Seen, Error);
    if (const auto* A = CastField<FArrayProperty>(P))
        return NestedSchema(A->Inner, Seen, Error);
    if (const auto* M = CastField<FMapProperty>(P))
        return NestedSchema(M->KeyProp, Seen, Error) && NestedSchema(M->ValueProp, Seen, Error);
    return true;
}
bool Schema(UStruct* Type, TSet<const UStruct*>& Seen, FString& Error)
{
    if (!Type || Seen.Num() >= 64)
    {
        Error = TEXT("copy-MIC-schema-depth-or-count");
        return false;
    }
    if (Seen.Contains(Type))
        return true;
    const TArray<FFieldSpec>* Expected = Schemas().Find(Type->GetPathName());
    if (!Expected)
    {
        Error = TEXT("copy-MIC-unknown-schema: ") + Type->GetPathName();
        return false;
    }
    Seen.Add(Type);
    int32 Count = 0;
    for (TFieldIterator<FProperty> It(Type, EFieldIteratorFlags::ExcludeSuper, EFieldIteratorFlags::IncludeDeprecated);
        It; ++It)
    {
        const FProperty* P = *It;
        // Installed FProperty::GetNameCPP appends _DEPRECATED from its actual
        // flag. The saved/reflected name of those members has no such suffix.
        const FFieldSpec* Spec = Expected->FindByPredicate(
            [&](const FFieldSpec& F) { return P4ExactNativeString(P->GetNameCPP(), F.Name); });
        if (!Spec || P->GetOwnerStruct() != Type || P->ArrayDim != Spec->Dim ||
            !P4ExactNativeString(TypeOf(P), Spec->Type) || P->HasAnyPropertyFlags(CPF_Transient) != Spec->Transient ||
            P->HasAnyPropertyFlags(CPF_Deprecated) !=
                FString(Spec->Name).EndsWith(TEXT("_DEPRECATED"), ESearchCase::CaseSensitive) ||
            !Layout(P) || P->GetOffset_ForInternal() < 0 ||
            int64(P->GetOffset_ForInternal()) + int64(P->ElementSize) * P->ArrayDim > Type->GetPropertiesSize())
        {
            Error = TEXT("copy-MIC-field-schema-not-supported: ") + Type->GetPathName() + TEXT(".") + P->GetName();
            return false;
        }
        ++Count;
        if (!NestedSchema(P, Seen, Error))
            return false;
    }
    if (Count != Expected->Num())
    {
        Error = TEXT("copy-MIC-field-schema-incomplete: ") + Type->GetPathName();
        return false;
    }
    return !Type->GetSuperStruct() || Schema(Type->GetSuperStruct(), Seen, Error);
}
bool Atom(FString& Into, const FString& Text)
{
    if (Text.Len() > 1024 * 1024 || Into.Len() > 4 * 1024 * 1024 - Text.Len() - 32)
        return false;
    Into += FString::FromInt(Text.Len()) + TEXT(":") + Text;
    return true;
}
bool Name(FString& Into, FName Value)
{
    // Printable names can collide (a literal suffix versus a numbered FName).
    return Atom(Into, Value.GetPlainNameString()) && Atom(Into, FString::FromInt(Value.GetNumber()));
}
bool Reference(FString& Into, const UObject* Object)
{
    if (!Object)
        return Atom(Into, TEXT("null"));
    if (!IsValid(Object) || Object->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad | RF_NeedInitialization))
        return false;
    TArray<const UObject*> Chain;
    for (const UObject* At = Object; At; At = At->GetOuter())
    {
        if (Chain.Num() >= 32 || Chain.Contains(At))
            return false;
        Chain.Add(At);
    }
    if (!Atom(Into, Object->GetClass()->GetPathName()) || !Atom(Into, FString::FromInt(Chain.Num())))
        return false;
    for (int32 At = Chain.Num() - 1; At >= 0; --At)
        if (!Name(Into, Chain[At]->GetFName()))
            return false;
    return true;
}
bool Value(const FProperty* P, const void* Data, FString& Out, int32 Depth = 0);
bool Members(
    const UStruct* Type, const void* Data, FString& Out, int32 Depth, bool Parameter = false, bool AllowValue = false)
{
    if (!Type || !Data || Depth > 32)
        return false;
    for (TFieldIterator<FProperty> It(Type, EFieldIteratorFlags::IncludeSuper, EFieldIteratorFlags::IncludeDeprecated);
        It; ++It)
    {
        const FProperty* P = *It;
        if (P->HasAnyPropertyFlags(CPF_Transient))
            continue;
        if (P->HasAnyPropertyFlags(CPF_SkipSerialization))
            return false;
        const FString Field = P->GetName();
        if (!Atom(Out, P->GetOwnerStruct()->GetPathName()) || !Atom(Out, Field) || !Atom(Out, TypeOf(P)) ||
            !Atom(Out, FString::FromInt(P->ArrayDim)))
            return false;
        // This path is called with Parameter only for the three exact native
        // parameter structs, after their full schemas have been checked.
        if (Parameter && P4ExactNativeString(Field, TEXT("ExpressionGUID")))
            continue;
        if (Parameter && AllowValue && P4ExactNativeString(Field, TEXT("ParameterValue")))
            continue;
        for (int32 At = 0; At < P->ArrayDim; ++At)
            if (!Value(P, P->ContainerPtrToValuePtr<void>(Data, At), Out, Depth + 1))
                return false;
    }
    return true;
}
bool Value(const FProperty* P, const void* Data, FString& Out, int32 Depth)
{
    if (!P || !Data || Depth > 32)
        return false;
    if (const auto* B = CastField<FBoolProperty>(P))
        return Atom(Out, B->GetPropertyValue(Data) ? TEXT("1") : TEXT("0"));
    if (const auto* F = CastField<FFloatProperty>(P))
    {
        const float Number = F->GetPropertyValue(Data);
        uint32 Word = 0;
        if (!FMath::IsFinite(Number))
            return false;
        FMemory::Memcpy(&Word, &Number, sizeof(Word));
        return Atom(Out, FString::Printf(TEXT("%08x"), Word));
    }
    if (const auto* I = CastField<FIntProperty>(P))
        return Atom(Out, FString::FromInt(I->GetPropertyValue(Data)));
    if (const auto* I = CastField<FUInt32Property>(P))
        return Atom(Out, FString::Printf(TEXT("%u"), I->GetPropertyValue(Data)));
    if (const auto* I = CastField<FUInt64Property>(P))
        return Atom(Out, FString::Printf(TEXT("%llu"), I->GetPropertyValue(Data)));
    if (const auto* I = CastField<FByteProperty>(P))
        return Atom(Out, FString::Printf(TEXT("%u"), uint32(I->GetPropertyValue(Data))));
    if (const auto* E = CastField<FEnumProperty>(P))
        return E->GetUnderlyingProperty()->GetClass() == FByteProperty::StaticClass() &&
            Atom(Out, FString::Printf(TEXT("%llu"), E->GetUnderlyingProperty()->GetUnsignedIntPropertyValue(Data)));
    if (const auto* N = CastField<FNameProperty>(P))
        return Name(Out, N->GetPropertyValue(Data));
    if (const auto* S = CastField<FStrProperty>(P))
        return Atom(Out, S->GetPropertyValue(Data));
    if (const auto* T = CastField<FTextProperty>(P))
    {
        FString Text;
        FTextStringHelper::WriteToBuffer(Text, T->GetPropertyValue(Data));
        return Atom(Out, Text); // Exact history; no display-label normalization.
    }
    if (const auto* S = CastField<FSoftObjectProperty>(P))
    {
        const FSoftObjectPath Path = S->GetPropertyValue(Data).ToSoftObjectPath();
        return Name(Out, Path.GetAssetPathName()) && Atom(Out, Path.GetSubPathString());
    }
    if (const auto* O = CastField<FObjectProperty>(P))
        return Reference(Out, O->GetObjectPropertyValue(Data));
    if (const auto* S = CastField<FStructProperty>(P))
        return Members(S->Struct, Data, Out, Depth + 1);
    if (const auto* A = CastField<FArrayProperty>(P))
    {
        FScriptArrayHelper Array(A, Data);
        if (Array.Num() > 10000 || !Atom(Out, FString::FromInt(Array.Num())))
            return false;
        for (int32 At = 0; At < Array.Num(); ++At)
            if (!Value(A->Inner, Array.GetRawPtr(At), Out, Depth + 1))
                return false;
        return true;
    }
    if (const auto* M = CastField<FMapProperty>(P))
    {
        FScriptMapHelper Map(M, Data);
        TArray<FString> Pairs;
        if (Map.Num() > 10000)
            return false;
        for (int32 At = 0; At < Map.GetMaxIndex(); ++At)
            if (Map.IsValidIndex(At))
            {
                FString Pair;
                if (!Value(M->KeyProp, Map.GetKeyPtr(At), Pair, Depth + 1) ||
                    !Value(M->ValueProp, Map.GetValuePtr(At), Pair, Depth + 1))
                    return false;
                Pairs.Add(MoveTemp(Pair));
            }
        Pairs.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
        if (!Atom(Out, FString::FromInt(Pairs.Num())))
            return false;
        for (const FString& Pair : Pairs)
            if (!Atom(Out, Pair))
                return false;
        return true;
    }
    return false;
}

bool Plain(const FMaterialParameterInfo& Info)
{
    return !Info.Name.IsNone() && Info.Association == EMaterialParameterAssociation::GlobalParameter &&
        Info.Index == INDEX_NONE;
}
bool InfoKey(const FMaterialParameterInfo& Info, FString& Out)
{
    return Plain(Info) && Name(Out, Info.Name) && Atom(Out, FString::FromInt(int32(Info.Association))) &&
        Atom(Out, FString::FromInt(Info.Index));
}
bool ContainsExact(const TArray<FString>& Names, const FString& Name)
{
    return Names.ContainsByPredicate([&](const FString& Current) { return P4ExactNativeString(Current, Name); });
}
bool ParentNames(UMaterialInterface* Parent, bool Scalars, TArray<FString>& Out)
{
    Out.Reset();
    TArray<FMaterialParameterInfo> Infos;
    TArray<FGuid> Ids;
    if (Scalars)
        Parent->GetAllScalarParameterInfo(Infos, Ids);
    else
        Parent->GetAllVectorParameterInfo(Infos, Ids);
    if (Infos.Num() != Ids.Num() || Infos.Num() > 10000)
        return false;
    for (const auto& Info : Infos)
    {
        // Layer entries cannot authorize a global override; they are not aliases.
        if (!Plain(Info))
            continue;
        FString Key;
        if (!InfoKey(Info, Key) || ContainsExact(Out, Key))
            return false;
        Out.Add(MoveTemp(Key));
    }
    return true;
}
bool ParameterPair(const FArrayProperty* P, UMaterialInstanceConstant* Source, UMaterialInstanceConstant* Copy,
    const TArray<FString>& Known, bool ValuesAllowed, bool& Different, FString& Error)
{
    FScriptArrayHelper A(P, P->ContainerPtrToValuePtr<void>(Source)), B(P, P->ContainerPtrToValuePtr<void>(Copy));
    const auto* Struct = CastField<FStructProperty>(P->Inner);
    if (!Struct || A.Num() != B.Num() || A.Num() > 10000)
    {
        Error = TEXT("copy-MIC-parameter-array-shape");
        return false;
    }
    const auto* Info = FindFProperty<FStructProperty>(Struct->Struct, TEXT("ParameterInfo"));
    const FProperty* ParameterValue = Struct->Struct->FindPropertyByName(TEXT("ParameterValue"));
    const auto* Deprecated = FindFProperty<FNameProperty>(Struct->Struct, TEXT("ParameterName"));
    TArray<FString> Seen;
    if (!Info || !ParameterValue || !Deprecated)
    {
        Error = TEXT("copy-MIC-parameter-field-unavailable");
        return false;
    }
    for (int32 At = 0; At < A.Num(); ++At)
    {
        const auto& AI = *Info->ContainerPtrToValuePtr<FMaterialParameterInfo>(A.GetRawPtr(At));
        const auto& BI = *Info->ContainerPtrToValuePtr<FMaterialParameterInfo>(B.GetRawPtr(At));
        FString AK, BK;
        if (!InfoKey(AI, AK) || !InfoKey(BI, BK) || !P4ExactNativeString(AK, BK) || ContainsExact(Seen, AK) ||
            P4IsGameViewParameter(AI.Name) || !Deprecated->GetPropertyValue_InContainer(A.GetRawPtr(At)).IsNone() ||
            !Deprecated->GetPropertyValue_InContainer(B.GetRawPtr(At)).IsNone())
        {
            Error = TEXT("copy-MIC-parameter-identity-or-deprecated-content");
            return false;
        }
        Seen.Add(AK);
        const bool Allowed = ValuesAllowed && ContainsExact(Known, AK);
        FString AS, BS, AV, BV;
        if (!Members(Struct->Struct, A.GetRawPtr(At), AS, 0, true, Allowed) ||
            !Members(Struct->Struct, B.GetRawPtr(At), BS, 0, true, Allowed) || !P4ExactNativeString(AS, BS) ||
            !Value(ParameterValue, ParameterValue->ContainerPtrToValuePtr<void>(A.GetRawPtr(At)), AV) ||
            !Value(ParameterValue, ParameterValue->ContainerPtrToValuePtr<void>(B.GetRawPtr(At)), BV))
        {
            Error = TEXT("copy-MIC-parameter-content-outside-supported-value-edit");
            return false;
        }
        if (!P4ExactNativeString(AV, BV))
        {
            if (!Allowed)
            {
                Error = TEXT("copy-MIC-unbound-or-texture-parameter-edit");
                return false;
            }
            Different = true;
        }
    }
    return true;
}

const TCHAR* SourceKey = TEXT("P4MaterialInstanceNativeSourceV1");
const TCHAR* HistoryKey = TEXT("P4KadanCopiedGameAsset");
const TCHAR* TransportKey = TEXT("P4MaterialInstanceTransportV2");
bool Hex(const FString& Text, int32 Length)
{
    if (Text.Len() != Length)
        return false;
    for (TCHAR C : Text)
        if (!((C >= TEXT('0') && C <= TEXT('9')) || (C >= TEXT('a') && C <= TEXT('f'))))
            return false;
    return true;
}
bool Ready(UObject* Object)
{
    return IsValid(Object) &&
        !Object->HasAnyFlags(
            RF_Transient | RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects | RF_NeedInitialization) &&
        Object->IsAsset();
}
bool ReadyMetadata(const UObject* Object)
{
    const UMetaData* Meta = Object->GetOutermost()->MetaData;
    return !Meta ||
        (IsValid(Meta) &&
            !Meta->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects | RF_NeedInitialization));
}
const TMap<FName, FString>* ExistingMap(const UObject* Object)
{
    // UMetaData::GetMapForObject calls Package.GetMetaData, which can allocate or
    // load an object. Explicit Review only reads an already available map.
    const UMetaData* Meta = Object->GetOutermost()->MetaData;
    return Meta && ReadyMetadata(Object) ? Meta->ObjectMetaDataMap.Find(FWeakObjectPtr(Object)) : nullptr;
}
bool Metadata(UObject* Object, FString& Out, bool ClosedMIC, FString& Error)
{
    if (!ReadyMetadata(Object))
    {
        Error = TEXT("copied-MIC-metadata-not-ready");
        return false;
    }
    const auto* Map = ExistingMap(Object);
    TArray<FString> Rows;
    if (Map)
        for (const auto& Pair : *Map)
        {
            if (ClosedMIC && Pair.Key == FName(TransportKey))
            {
                Error = TEXT("copied-MIC-transport-present-not-supported");
                return false;
            }
            if (ClosedMIC && Pair.Key != TEXT("P4OriginalRecord") && Pair.Key != TEXT("P4NativeObjectBaseline") &&
                Pair.Key != TEXT("P4NativeObjectValuesNotHeld") && Pair.Key != FName(SourceKey) &&
                Pair.Key != FName(HistoryKey))
            {
                Error = TEXT("copied-MIC-metadata-not-supported: ") + Pair.Key.ToString();
                return false;
            }
            if (Pair.Key == TEXT("P4NativeObjectValuesNotHeld") && !Pair.Value.IsEmpty())
            {
                Error = TEXT("copied-MIC-original-values-not-held");
                return false;
            }
            FString Row;
            if (!Name(Row, Pair.Key) || !Atom(Row, Pair.Value))
                return false;
            Rows.Add(MoveTemp(Row));
        }
    Rows.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
    if (!Atom(Out, FString::FromInt(Rows.Num())))
        return false;
    for (const auto& Row : Rows)
        if (!Atom(Out, Row))
            return false;
    return true;
}
bool Json(const FString& Text, TSharedPtr<FJsonObject>& Out)
{
    return Text.Len() <= 1024 * 1024 && P4ParseJson(Text, Out);
}
bool ExactField(const FJsonObject& Json, const TCHAR* Field, const FString& Expected)
{
    FString Actual;
    return Json.TryGetStringField(Field, Actual) && P4ExactNativeString(Actual, Expected);
}
bool LiteralRecord(UObject* Object, FP4OriginalRecord& Record, FString& Text, FString& Error)
{
    const auto* Meta = ExistingMap(Object);
    const FString* Found = Meta ? Meta->Find(TEXT("P4OriginalRecord")) : nullptr;
    if (!Found || !P4ReadOriginalRecord(Object, Record, Error))
        return false;
    Text = *Found;
    TSharedPtr<FJsonObject> J;
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    double Version = 0;
    bool Ref = true, Locked = false;
    const bool RecoveredParent = Object->GetClass() == UMaterial::StaticClass() &&
        P4ExactNativeString(Record.Kind, TEXT("material")) &&
        P4ExactNativeString(Record.ComparisonSchema, TEXT("material-native-state-v1")) &&
        P4ExactNativeString(Record.SourceDigestKind, TEXT("material-graph-v2-md5")) && Hex(Record.SourceDigest, 32) &&
        P4ExactNativeString(Record.Fidelity, TEXT("recovered-pixel-contract")) && Record.ReferenceOnly;
    if (!Json(Text, J) || J->Values.Num() != 15 || !J->TryGetNumberField(TEXT("format"), Version) || Version != 2 ||
        !ExactField(*J, TEXT("owner"), TEXT("p4mapkit-original")) || !J->TryGetArrayField(TEXT("values"), Values) ||
        Values->Num() || !J->TryGetBoolField(TEXT("reference_only"), Ref) || Ref != Record.ReferenceOnly ||
        (Ref && !RecoveredParent) || !J->TryGetBoolField(TEXT("read_only"), Locked) || !Locked ||
        !ExactField(*J, TEXT("package"), Object->GetOutermost()->GetName()) ||
        !ExactField(*J, TEXT("class"), Object->GetClass()->GetPathName()) ||
        !ExactField(*J, TEXT("fidelity"), RecoveredParent ? TEXT("recovered-pixel-contract") : TEXT("exact")) ||
        !ExactField(*J, TEXT("kind"), Record.Kind) || !ExactField(*J, TEXT("source_digest"), Record.SourceDigest) ||
        !ExactField(*J, TEXT("source_digest_kind"), Record.SourceDigestKind) ||
        !ExactField(*J, TEXT("comparison_digest"), Record.ComparisonDigest) ||
        !ExactField(*J, TEXT("comparison_schema"), Record.ComparisonSchema) ||
        !ExactField(*J, TEXT("legacy_key"), Record.LegacyKey) ||
        !ExactField(*J, TEXT("legacy_evidence"), Record.LegacyEvidence) || Record.Version != 2 || !Record.ReadOnly ||
        Record.Values.Num() || !Hex(Record.ComparisonDigest, 64))
    {
        Error = TEXT("copied-MIC-source-record-not-complete-exact-original");
        return false;
    }
    return true;
}
bool Original(UObject* Object, bool MIC, FString& Error)
{
    FString Origin, Meta, RecordText;
    FP4OriginalRecord Record;
    FP4GameReplacement Replaced;
    if (!Ready(Object) || Object->GetOutermost()->IsDirty() || P4HasCreatorSource(Object) ||
        P4HasCopiedGameData(Object) || P4HasIntrinsicGameProxy(Object) || P4IsGameReplacement(Object, &Replaced) ||
        !P4HasIntrinsicGameSource(Object, &Origin) || !P4ExactNativeString(Origin, Object->GetOutermost()->GetName()) ||
        P4Kadan::GetAssetPolicy(Object).AllowEdit || !LiteralRecord(Object, Record, RecordText, Error) ||
        !Metadata(Object, Meta, MIC, Error))
    {
        if (Error.IsEmpty())
            Error = TEXT("copied-MIC-current-original-source-not-qualified");
        return false;
    }
    const bool OldMIC = MIC && P4ExactNativeString(Record.Kind, TEXT("object")) &&
        P4ExactNativeString(Record.ComparisonSchema, TEXT("object-lines-v1")) &&
        P4ExactNativeString(Record.SourceDigestKind, TEXT("header-body-sha256-v1")) && Hex(Record.SourceDigest, 64);
    const bool FreshMIC = MIC && P4ExactNativeString(Record.Kind, TEXT("material-instance")) &&
        P4ExactNativeString(Record.ComparisonSchema, TEXT("material-instance-native-state-v1")) &&
        P4ExactNativeString(Record.SourceDigestKind, TEXT("material-instance-v2-md5")) && Hex(Record.SourceDigest, 32);
    if (MIC && !(OldMIC || FreshMIC))
    {
        Error = TEXT("copied-MIC-source-schema-not-supported");
        return false;
    }
    // A base graph has its own existing native contract. This finite exception
    // does not admit arbitrary original kinds/schemas merely because some other
    // diagnostic happens to support them.
    if (!MIC &&
        (Object->GetClass() != UMaterial::StaticClass() || !P4ExactNativeString(Record.Kind, TEXT("material")) ||
            !P4ExactNativeString(Record.ComparisonSchema, TEXT("material-native-state-v1")) ||
            !P4ExactNativeString(Record.SourceDigestKind, TEXT("material-graph-v2-md5")) ||
            !Hex(Record.SourceDigest, 32) ||
            !(P4ExactNativeString(Record.Fidelity, TEXT("exact")) ||
                (Record.ReferenceOnly && P4ExactNativeString(Record.Fidelity, TEXT("recovered-pixel-contract"))))))
    {
        Error = TEXT("copied-MIC-original-material-parent-contract-not-supported");
        return false;
    }
    const FP4OriginalDiagnostic Diagnostic = P4DiagnoseOriginalAsset(Object);
    if (!Diagnostic.Error.IsEmpty() || Diagnostic.Comparison != EP4OriginalComparison::Equal ||
        !P4ExactNativeString(Diagnostic.CurrentDigest, Record.ComparisonDigest) || Object->GetOutermost()->IsDirty())
    {
        Error = TEXT("copied-MIC-source-current-original-comparison-not-equal: ") + Diagnostic.Error;
        return false;
    }
    return true;
}
const UP4CreatorSourceData* Role(UMaterialInstanceConstant* Object, const FString& Origin, FString& Error)
{
    const auto* UserData = FindFProperty<FArrayProperty>(Object->GetClass(), TEXT("AssetUserData"));
    const auto* Inner = UserData ? CastField<FObjectProperty>(UserData->Inner) : nullptr;
    if (!Inner)
    {
        Error = TEXT("copied-MIC-source-role-schema-missing");
        return nullptr;
    }
    FScriptArrayHelper Data(UserData, UserData->ContainerPtrToValuePtr<void>(Object));
    const UP4CreatorSourceData* Result = Data.Num() == 1
        ? Cast<UP4CreatorSourceData>(Inner->GetObjectPropertyValue(Data.GetRawPtr(0)))
        : nullptr;
    TSet<const UStruct*> Seen;
    if (!IsValid(Result) ||
        Result->HasAnyFlags(
            RF_Transient | RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects | RF_NeedInitialization) ||
        Result->GetClass() != UP4CreatorSourceData::StaticClass() || Result->GetOuter() != Object ||
        !Schema(Result->GetClass(), Seen, Error))
    {
        if (Error.IsEmpty())
            Error = TEXT("copied-MIC-source-role-not-sole-exact-owned-native-role");
        return nullptr;
    }
    const auto* Format = FindFProperty<FIntProperty>(Result->GetClass(), TEXT("Format"));
    const auto Is = [&](const TCHAR* Key, FName Expected)
    {
        const auto* P = FindFProperty<FNameProperty>(Result->GetClass(), Key);
        return P && P->GetPropertyValue_InContainer(Result) == Expected;
    };
    if (!Format || Format->GetPropertyValue_InContainer(Result) != 1 || !Is(TEXT("Kind"), TEXT("game-source")) ||
        !Is(TEXT("SourceMode"), TEXT("original-publication")) ||
        !Is(TEXT("AssetClass"), FName(*Object->GetClass()->GetPathName())) ||
        !Is(TEXT("OriginPackage"), FName(*Origin)))
    {
        Error = TEXT("copied-MIC-source-role-identity-not-qualified");
        return nullptr;
    }
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Object->GetOutermost(), Objects, true);
    if (Objects.Num() > 8)
    {
        Error = TEXT("copied-MIC-extra-owned-object");
        return nullptr;
    }
    for (UObject* Other : Objects)
        if (!IsValid(Other) ||
            (Other != Object && Other != Result &&
                (Other != Object->GetOutermost()->MetaData || Other->GetClass() != UMetaData::StaticClass() ||
                    Other->GetOuter() != Object->GetOutermost())))
        {
            Error = TEXT("copied-MIC-extra-owned-object");
            return nullptr;
        }
    if (!ReadyMetadata(Result))
    {
        Error = TEXT("copied-MIC-role-metadata-not-ready");
        return nullptr;
    }
    const auto* RoleMeta = ExistingMap(Result);
    if (RoleMeta && RoleMeta->Num())
    {
        Error = TEXT("copied-MIC-role-extra-metadata");
        return nullptr;
    }
    return Result;
}
bool History(UMaterialInstanceConstant* Copy, UMaterialInstanceConstant* Source, FString& Error)
{
    if (!ReadyMetadata(Copy) || !ReadyMetadata(Source))
    {
        Error = TEXT("copied-MIC-history-metadata-not-ready");
        return false;
    }
    const auto* CopyMeta = ExistingMap(Copy);
    const auto* SourceMeta = ExistingMap(Source);
    if (!SourceMeta)
    {
        Error = TEXT("copied-MIC-source-metadata-missing");
        return false;
    }
    if (CopyMeta)
        for (const auto& Entry : *CopyMeta)
            if (Entry.Key != FName(HistoryKey))
            {
                Error = TEXT("copied-MIC-live-original-custody-or-unknown-copy-metadata");
                return false;
            }
    const FString* Text = CopyMeta ? CopyMeta->Find(FName(HistoryKey)) : nullptr;
    if (!Text)
        return true; // Native DuplicateAsset carries the intrinsic role, not metadata.
    TSharedPtr<FJsonObject> J;
    const TSharedPtr<FJsonObject>* Records = nullptr;
    double Version = 0;
    if (!Json(*Text, J) || J->Values.Num() != 3 || !J->TryGetNumberField(TEXT("format"), Version) || Version != 1 ||
        !ExactField(*J, TEXT("kind"), TEXT("copied-game-asset")) ||
        !J->TryGetObjectField(TEXT("source_records"), Records) ||
        !(*Records)->HasTypedField<EJson::String>(TEXT("P4OriginalRecord")))
    {
        Error = TEXT("copied-MIC-archived-source-history-unreadable");
        return false;
    }
    for (const auto& Pair : (*Records)->Values)
    {
        FString Value;
        const FString* Current = SourceMeta->Find(FName(*Pair.Key));
        if (!Pair.Value.IsValid() || !Pair.Value->TryGetString(Value) || !Current ||
            !P4ExactNativeString(Value, *Current) ||
            !(P4ExactNativeString(Pair.Key, TEXT("P4OriginalRecord")) ||
                P4ExactNativeString(Pair.Key, TEXT("P4NativeObjectBaseline")) ||
                P4ExactNativeString(Pair.Key, TEXT("P4NativeObjectValuesNotHeld")) ||
                P4ExactNativeString(Pair.Key, SourceKey)))
        {
            Error = TEXT("copied-MIC-archived-source-history-not-current-exact-source");
            return false;
        }
    }
    // A metadata-preserving copy must not lose the new schema's literal source
    // custody. Its original-only keys were retired, never copied as authority.
    for (const auto& Pair : *SourceMeta)
        if ((Pair.Key == TEXT("P4OriginalRecord") || Pair.Key == FName(SourceKey)) &&
            !(*Records)->HasTypedField<EJson::String>(Pair.Key.ToString()))
        {
            Error = TEXT("copied-MIC-archived-required-source-history-missing");
            return false;
        }
    return true;
}
bool Subset(UMaterialInstanceConstant* Object, FString& Error)
{
    const auto NoBool = [&](const UStruct* Type, const void* Data, const TCHAR* Name)
    {
        const auto* P = FindFProperty<FBoolProperty>(Type, Name);
        return P && !P->GetPropertyValue_InContainer(Data);
    };
    const auto Empty = [&](const TCHAR* Name)
    {
        const auto* P = FindFProperty<FArrayProperty>(Object->GetClass(), Name);
        return P && FScriptArrayHelper(P, P->ContainerPtrToValuePtr<void>(Object)).Num() == 0;
    };
    const auto Null = [&](const TCHAR* Name)
    {
        const auto* P = FindFProperty<FObjectProperty>(Object->GetClass(), Name);
        return P && !P->GetObjectPropertyValue_InContainer(Object);
    };
    const auto* Static = FindFProperty<FStructProperty>(Object->GetClass(), TEXT("StaticParameters"));
    const auto* Base = FindFProperty<FStructProperty>(Object->GetClass(), TEXT("BasePropertyOverrides"));
    const auto* Cached = FindFProperty<FStructProperty>(Object->GetClass(), TEXT("CachedLayerParameters"));
    if (!Static || !Base || !Cached || !NoBool(Object->GetClass(), Object, TEXT("bHasStaticPermutationResource")) ||
        !NoBool(Object->GetClass(), Object, TEXT("bOverrideSubsurfaceProfile")) ||
        !NoBool(Object->GetClass(), Object, TEXT("bOverrideBaseProperties")) ||
        !Empty(TEXT("RuntimeVirtualTextureParameterValues")) || !Empty(TEXT("FontParameterValues")) ||
        !Null(TEXT("ThumbnailInfo")) || !Null(TEXT("AssetImportData")))
    {
        Error = TEXT("copied-MIC-native-content-subset-not-supported");
        return false;
    }
    for (TFieldIterator<FArrayProperty> It(Static->Struct); It; ++It)
        if (FScriptArrayHelper(*It, It->ContainerPtrToValuePtr<void>(Static->ContainerPtrToValuePtr<void>(Object)))
                .Num())
        {
            Error = TEXT("copied-MIC-static-parameter-entry-not-supported");
            return false;
        }
    for (const TCHAR* Name : {TEXT("bOverride_OpacityMaskClipValue"), TEXT("bOverride_BlendMode"),
             TEXT("bOverride_ShadingModel"), TEXT("bOverride_DitheredLODTransition"),
             TEXT("bOverride_CastDynamicShadowAsMasked"), TEXT("bOverride_TwoSided")})
        if (!NoBool(Base->Struct, Base->ContainerPtrToValuePtr<void>(Object), Name))
        {
            Error = TEXT("copied-MIC-base-override-not-supported");
            return false;
        }
    FString Actual, Default;
    if (!Value(Cached, Cached->ContainerPtrToValuePtr<void>(Object), Actual) ||
        !Value(Cached, Cached->ContainerPtrToValuePtr<void>(Object->GetClass()->GetDefaultObject()), Default) ||
        !P4ExactNativeString(Actual, Default))
    {
        Error = TEXT("copied-MIC-layer-cache-not-canonical-default");
        return false;
    }
    for (const auto& Scalar : Object->ScalarParameterValues)
        if (Scalar.AtlasData.bIsUsedAsAtlasPosition || !Scalar.AtlasData.Curve.IsNull() ||
            !Scalar.AtlasData.Atlas.IsNull())
        {
            Error = TEXT("copied-MIC-curve-atlas-content-not-supported");
            return false;
        }
    return true;
}
bool Pair(UMaterialInstanceConstant* Source, UMaterialInstanceConstant* Copy, const TArray<FString>& Scalars,
    const TArray<FString>& Vectors, bool& Different, FString& Error)
{
    Different = false;
    for (TFieldIterator<FProperty> It(
             Source->GetClass(), EFieldIteratorFlags::IncludeSuper, EFieldIteratorFlags::IncludeDeprecated);
        It; ++It)
    {
        const auto* P = *It;
        const FString Field = P->GetName();
        if (P->HasAnyPropertyFlags(CPF_Transient))
            continue;
        if (P4ExactNativeString(Field, TEXT("AssetUserData")) || P4ExactNativeString(Field, TEXT("LightingGuid")) ||
            P4ExactNativeString(Field, TEXT("ParameterStateId")))
            continue;
        const bool Scalar = P4ExactNativeString(Field, TEXT("ScalarParameterValues"));
        const bool Vector = P4ExactNativeString(Field, TEXT("VectorParameterValues"));
        const bool Texture = P4ExactNativeString(Field, TEXT("TextureParameterValues"));
        if (Scalar || Vector || Texture)
        {
            const auto* Array = CastField<FArrayProperty>(P);
            if (!Array ||
                !ParameterPair(Array, Source, Copy, Scalar ? Scalars : Vectors, Scalar || Vector, Different, Error))
                return false;
            continue;
        }
        for (int32 At = 0; At < P->ArrayDim; ++At)
        {
            FString A, B;
            if (!Value(P, P->ContainerPtrToValuePtr<void>(Source, At), A) ||
                !Value(P, P->ContainerPtrToValuePtr<void>(Copy, At), B) || !P4ExactNativeString(A, B))
            {
                Error = TEXT("copied-MIC-persistent-content-outside-supported-value-edit: ") + Field;
                return false;
            }
        }
    }
    return true;
}
bool Snapshot(UMaterialInstanceConstant* Object, FString& Out, FString& Error)
{
    // Unlike meaningful-content comparison this includes all GUIDs and role
    // references, so a cook cannot change even ignored binding fields unnoticed.
    return Atom(Out, Object->GetPathName()) && Atom(Out, FString::FromInt(int32(Object->GetFlags()))) &&
        Atom(Out, Object->GetOutermost()->IsDirty() ? TEXT("dirty") : TEXT("clean")) &&
        Metadata(Object, Out, true, Error) && Members(Object->GetClass(), Object, Out, 0);
}
bool ExactNames(const TArray<FString>& A, const TArray<FString>& B)
{
    if (A.Num() != B.Num())
        return false;
    for (int32 At = 0; At < A.Num(); ++At)
        if (!P4ExactNativeString(A[At], B[At]))
            return false;
    return true;
}
} // namespace P4MICCopy

FP4CopiedMICContent P4CompareCopiedMaterialInstanceContent(UObject* Asset)
{
    using namespace P4MICCopy;
    FP4CopiedMICContent Result;
    if (!IsInGameThread() || !IsValid(Asset) || !Asset->IsA<UMaterialInstanceConstant>() || P4HasCreatorSource(Asset) ||
        !P4HasCopiedGameData(Asset))
        return Result;
    Result.State = EP4CopiedMICContent::Unreadable;
    const auto Fail = [&](const TCHAR* Why)
    {
        if (Result.Error.IsEmpty())
            Result.Error = Why;
        return Result;
    };
    FString Origin;
    FP4GameReplacement Replaced;
    if (Asset->GetClass() != UMaterialInstanceConstant::StaticClass() || !Ready(Asset) ||
        P4HasIntrinsicGameProxy(Asset) || P4IsGameReplacement(Asset, &Replaced) ||
        !P4HasIntrinsicGameSource(Asset, &Origin) || P4ExactNativeString(Origin, Asset->GetOutermost()->GetName()) ||
        !P4IsValidContentPackage(Origin))
        return Fail(TEXT("copied-MIC-intrinsic-origin-not-qualified"));
    TStrongObjectPtr<UMaterialInstanceConstant> Copy(CastChecked<UMaterialInstanceConstant>(Asset));
    TSet<const UStruct*> Seen;
    if (!Schema(Copy->GetClass(), Seen, Result.Error))
        return Fail(TEXT("copied-MIC-schema-unreadable"));
    const FString SourcePath = P4ObjectPathOf(Origin);
    TStrongObjectPtr<UMaterialInstanceConstant> Source(LoadObject<UMaterialInstanceConstant>(nullptr, *SourcePath));
    if (!Source.IsValid() || Source->GetClass() != UMaterialInstanceConstant::StaticClass() ||
        Source.Get() == Copy.Get() || !P4ExactNativeString(Source->GetPathName(), SourcePath))
        return Fail(TEXT("copied-MIC-current-origin-asset-not-found"));
    Result.OriginalPackage = Origin;
    if (!Original(Source.Get(), true, Result.Error) || !History(Copy.Get(), Source.Get(), Result.Error) ||
        !Subset(Source.Get(), Result.Error) || !Subset(Copy.Get(), Result.Error))
        return Fail(TEXT("copied-MIC-source-or-copy-not-supported"));
    const UP4CreatorSourceData* SourceRole = Role(Source.Get(), Origin, Result.Error);
    const UP4CreatorSourceData* CopyRole = Role(Copy.Get(), Origin, Result.Error);
    if (!SourceRole || !CopyRole || Source->Parent != Copy->Parent || !Source->Parent)
        return Fail(TEXT("copied-MIC-parent-or-role-differs"));
    TArray<TStrongObjectPtr<UMaterialInterface>> Parents;
    TArray<FString> ParentMetadata;
    for (UMaterialInterface* At = Source->Parent; At;)
    {
        if (Parents.Num() >= 32 ||
            Parents.ContainsByPredicate([&](const TStrongObjectPtr<UMaterialInterface>& P) { return P.Get() == At; }) ||
            (At->GetClass() != UMaterial::StaticClass() &&
                At->GetClass() != UMaterialInstanceConstant::StaticClass()) ||
            !Original(At, At->GetClass() == UMaterialInstanceConstant::StaticClass(), Result.Error))
            return Fail(TEXT("copied-MIC-original-parent-chain-not-qualified"));
        FString Meta;
        if (!Metadata(At, Meta, false, Result.Error))
            return Fail(TEXT("copied-MIC-parent-metadata-unreadable"));
        ParentMetadata.Add(MoveTemp(Meta));
        Parents.Emplace(At);
        At = Cast<UMaterialInstanceConstant>(At) ? CastChecked<UMaterialInstanceConstant>(At)->Parent : nullptr;
    }
    TArray<FString> Scalars, Vectors;
    if (!ParentNames(Source->Parent, true, Scalars) || !ParentNames(Source->Parent, false, Vectors))
        return Fail(TEXT("copied-MIC-parent-parameter-info-unreadable"));
    FString SourceBefore, CopyBefore;
    if (!Snapshot(Source.Get(), SourceBefore, Result.Error) || !Snapshot(Copy.Get(), CopyBefore, Result.Error))
        return Fail(TEXT("copied-MIC-field-snapshot-unreadable"));
    bool Different = false;
    if (!Pair(Source.Get(), Copy.Get(), Scalars, Vectors, Different, Result.Error) ||
        !P4MaterialInstanceCopyCookBoundary(Source.Get(), SourceRole, Result.Error) ||
        !P4MaterialInstanceCopyCookBoundary(Copy.Get(), CopyRole, Result.Error))
        return Fail(TEXT("copied-MIC-native-boundary-not-supported"));
    if (!Original(Source.Get(), true, Result.Error) || !History(Copy.Get(), Source.Get(), Result.Error) ||
        SourceRole != Role(Source.Get(), Origin, Result.Error) || CopyRole != Role(Copy.Get(), Origin, Result.Error))
        return Fail(TEXT("copied-MIC-source-custody-changed-during-read"));
    for (int32 At = 0; At < Parents.Num(); ++At)
    {
        FString Meta;
        if (!Original(
                Parents[At].Get(), Parents[At]->GetClass() == UMaterialInstanceConstant::StaticClass(), Result.Error) ||
            !Metadata(Parents[At].Get(), Meta, false, Result.Error) || !P4ExactNativeString(Meta, ParentMetadata[At]))
            return Fail(TEXT("copied-MIC-parent-custody-changed-during-read"));
    }
    FString SourceAfter, CopyAfter;
    TArray<FString> ScalarsAfter, VectorsAfter;
    if (Source->Parent != Copy->Parent || Source->Parent != Parents[0].Get() ||
        !ParentNames(Source->Parent, true, ScalarsAfter) || !ParentNames(Source->Parent, false, VectorsAfter) ||
        !ExactNames(Scalars, ScalarsAfter) || !ExactNames(Vectors, VectorsAfter) ||
        !Snapshot(Source.Get(), SourceAfter, Result.Error) || !Snapshot(Copy.Get(), CopyAfter, Result.Error) ||
        !P4ExactNativeString(SourceBefore, SourceAfter) || !P4ExactNativeString(CopyBefore, CopyAfter))
        return Fail(TEXT("copied-MIC-live-content-changed-during-read"));
    Result.State = Different ? EP4CopiedMICContent::Different : EP4CopiedMICContent::Equal;
    Result.Error.Reset();
    return Result;
}
