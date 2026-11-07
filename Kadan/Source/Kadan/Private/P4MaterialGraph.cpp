#include "P4KadanReimportAsset.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4MaterialGraph.h"
#include "P4PackageSave.h"
#include "P4OriginalRecord.h"
#include "P4NativeHash.h"
#include "P4KadanMaterialExpressions.h"
#include "P4OriginalRawNormalOperation.h"
#include "P4OriginalPositiveInfinityOperation.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionClearCoatNormalCustomOutput.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialParameterCollection.h"
#include "MaterialExpressionIO.h"
#include "MaterialShared.h"
#include "ShaderCompiler.h"
#include "RHI.h"
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/StructuredArchive.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/MetaData.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/App.h"
#include "P4MaterialGameSettings.h"
#include "UObject/SoftObjectPath.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "P4MaterialGraphInternal.h"
// Internal source-origin boundary shared with P4MarkOriginalAsset. Establish
// intrinsic game provenance before capturing persistent reflected properties.
bool P4RecordKnownGameSource(UObject* Asset, const FString& OriginalPackage, bool Proxy);

namespace P4MaterialGraph
{

// An expression's input is an ExpressionInput. A material's own pins are MaterialInput structs, which in
// the engine's code derive from it and in its reflection data stand on their own.
bool IsInput(const UScriptStruct* Struct)
{
    static UScriptStruct* Plain = FindObject<UScriptStruct>(ANY_PACKAGE, TEXT("ExpressionInput"));
    static UScriptStruct* Pin = FindObject<UScriptStruct>(ANY_PACKAGE, TEXT("MaterialInput"));
    return Struct && ((Plain && Struct->IsChildOf(Plain)) || (Pin && Struct->IsChildOf(Pin)));
}
// An expression's or a material's input of that name, which is a property of it. Null when it has none.
FExpressionInput* InputOf(UObject* Owner, const FString& Name)
{
    if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Owner))
        for (FCustomInput& Input : Custom->Inputs)
            if (Input.InputName.ToString() == Name)
                return &Input.Input;
    if (UMaterialExpressionKadanGameCustom* Game = Cast<UMaterialExpressionKadanGameCustom>(Owner))
        for (FKadanGameCustomInput& Input : Game->Inputs)
            if (Input.InputName.ToString() == Name)
                return &Input.Input;
    // A material's customized UV pins, CustomizedUV0..7: one array property of the material.
    if (UMaterial* Material = Cast<UMaterial>(Owner))
        if (Name.Len() == 13 && Name.StartsWith(TEXT("CustomizedUV"), ESearchCase::CaseSensitive) &&
            FChar::IsDigit(Name[12]) && Name[12] < TCHAR('8'))
            return &Material->CustomizedUVs[Name[12] - TCHAR('0')];
    const FStructProperty* Property = FindFProperty<FStructProperty>(Owner->GetClass(), *Name);
    if (!Property || !IsInput(Property->Struct))
        return nullptr;
    return Property->ContainerPtrToValuePtr<FExpressionInput>(Owner);
}
bool Refuse(FString& Error, const FString& Why)
{
    if (Error.IsEmpty())
        Error = Why;
    return false;
}
bool IsStandIn(UObject* Asset)
{
    return IsValid(Asset) &&
        Asset->GetOutermost()->GetMetaData()->GetValue(Asset, TEXT("P4BlueprintStandIn")) == TEXT("1");
}
bool HasNonfiniteJsonNumber(const FString& Source)
{
    bool Quoted = false, Escaped = false;
    for (int32 At = 0; At < Source.Len(); ++At)
    {
        const TCHAR Character = Source[At];
        if (Quoted)
        {
            if (Escaped)
                Escaped = false;
            else if (Character == TCHAR('\\'))
                Escaped = true;
            else if (Character == TCHAR('"'))
                Quoted = false;
            continue;
        }
        if (Character == TCHAR('"'))
        {
            Quoted = true;
            continue;
        }
        if (Character != TCHAR('I') && Character != TCHAR('N'))
            continue;
        const FString Token = Character == TCHAR('I') ? TEXT("Infinity") : TEXT("NaN");
        const int32 End = At + Token.Len();
        if (Source.Mid(At, Token.Len()) != Token)
            continue;
        const bool Before = At == 0 || FChar::IsWhitespace(Source[At - 1]) ||
            FString(TEXT(":,[-")).Contains(FString(1, &Source[At - 1]));
        const bool After = End == Source.Len() ||
            (End < Source.Len() &&
                (FChar::IsWhitespace(Source[End]) || FString(TEXT(",]}")).Contains(FString(1, &Source[End]))));
        if (Before && After)
            return true;
    }
    return false;
}

// ----- a property's value from and to JSON -----
bool EnumValue(const UEnum* Enum, const TSharedPtr<FJsonValue>& Value, int64& Out)
{
    FString Text;
    if (!Enum || !Value->TryGetString(Text))
        return false;
    Out = Enum->GetValueByNameString(Text);
    return Out != INDEX_NONE;
}
bool EnumRefusal(const TSharedPtr<FJsonValue>& Value, const FString& Where, FString& Error)
{
    FString Named;
    const bool GameOnly = Value->TryGetString(Named) && Named.Contains(TEXT("C4"));
    return Refuse(Error,
        FString(GameOnly ? TEXT("material-graph-game-engine-enum-unavailable: ")
                         : TEXT("material-graph-property-needs-one-of-its-names: ")) +
            Where + (Named.IsEmpty() ? TEXT("") : TEXT("=") + Named));
}
bool Set(UObject* Object, FProperty* Property, const TSharedPtr<FJsonValue>& Value, FString& Error);
// False when the property is of a kind the format has no value for.
bool Get(const FProperty* Property, const void* At, TSharedPtr<FJsonValue>& Out);
// The highest texture coordinate index a graph's nodes read, -1 when none.
// The highest texture coordinate the material reads from the mesh's vertices. A pixel read of a coordinate below the
// number of customized UVs reads the material's own customized UV instead (UE's NUM_MATERIAL_TEXCOORDS_VERTEX).
int32 HighestTexCoord(const TSharedPtr<FJsonObject>& Nodes, const TSharedPtr<FJsonObject>& Outputs)
{
    int32 Highest = -1, Customized = 0;
    if (!Nodes.IsValid())
        return Highest;
    TSet<FString> Vertex;
    TFunction<void(const TSharedPtr<FJsonValue>&, int32)> Reach = [&](const TSharedPtr<FJsonValue>& Link, int32 Depth)
    {
        const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
        FString Id;
        const TSharedPtr<FJsonObject>* Node = nullptr;
        const TSharedPtr<FJsonObject>* Inputs = nullptr;
        if (Depth > 512 || !Link.IsValid() || !Link->TryGetArray(Items) || !Items->Num() ||
            !(*Items)[0]->TryGetString(Id) || Vertex.Contains(Id))
            return;
        Vertex.Add(Id);
        if (Nodes->TryGetObjectField(Id, Node) && (*Node)->TryGetObjectField(TEXT("inputs"), Inputs))
            for (const auto& Input : (*Inputs)->Values)
                Reach(Input.Value, Depth + 1);
    };
    if (Outputs.IsValid())
        for (const auto& Pin : Outputs->Values)
        {
            if (Pin.Key.StartsWith(TEXT("CustomizedUV"), ESearchCase::CaseSensitive) && Pin.Key.Len() == 13 &&
                FChar::IsDigit(Pin.Key[12]))
                Customized = FMath::Max(Customized, int32(Pin.Key[12] - TCHAR('0')) + 1);
            if (Pin.Key.StartsWith(TEXT("CustomizedUV"), ESearchCase::CaseSensitive) ||
                Pin.Key == TEXT("WorldPositionOffset"))
                Reach(Pin.Value, 0);
        }
    for (const auto& Pair : Nodes->Values)
    {
        const TSharedPtr<FJsonObject>* Node = nullptr;
        const TSharedPtr<FJsonObject>* Props = nullptr;
        FString Type;
        double Index = 0;
        if (Pair.Value->TryGetObject(Node) && (*Node)->TryGetStringField(TEXT("type"), Type) &&
            Type == TEXT("TextureCoordinate"))
        {
            const int32 At = (*Node)->TryGetObjectField(TEXT("props"), Props) &&
                    (*Props)->TryGetNumberField(TEXT("CoordinateIndex"), Index)
                ? int32(Index)
                : 0;
            if (At >= Customized || Vertex.Contains(Pair.Key))
                Highest = FMath::Max(Highest, At);
        }
    }
    return Highest;
}
// A game material setting stock Unreal 4.26 cannot compile, and the stock value the editor's material holds in
// its place so that the editor draws a preview. Null when the game's value is used as it is.
// - TranslucencyLightingMode: the game's Pikmin water lights its surface with two modes of its own that extend
//   stock per-pixel surface lighting (TLM_SurfacePerPixelLighting) with the game's screen-space reflection
//   pass. Stock Unreal has no such value; the editor uses the stock mode they extend.
// - DecalBlendMode: the game's water wetness decal has a mode of its own (DBM_DBuffer_C4_WaterBox) that writes the
//   game's fourth decal buffer target only. The editor uses DBuffer color, which reads the same input (BaseColor).
// - bUsedWithSkeletalMesh / bUsedWithMorphTargets: the game's cooked usage flag on a graph that reads texture
//   coordinate 4 or higher. Stock Unreal's GPU skin vertex factory takes at most 4 coordinate sets
//   (GpuSkinVertexFactory.ush: "Too many texture coordinate sets defined on GPUSkin vertex input. Max: 4."), so
//   with the flag the editor compiles no shader for the material at all. The editor leaves the flag off.
// The game's value is kept with the material, read back as the material's and named in its original record.
// A game master is reference material: the game draws its own master, never this one.
TSharedPtr<FJsonValue> GameRenderStandIn(
    const FString& Key, const TSharedPtr<FJsonValue>& GameValue, int32 HighestCoordinate)
{
    FString Named;
    if (Key == TEXT("TranslucencyLightingMode") && GameValue.IsValid() && GameValue->TryGetString(Named) &&
        (Named == TEXT("TLM_SurfacePerPixelLightingC4Custom0") ||
            Named == TEXT("TLM_SurfacePerPixelLightingC4Custom1")))
        return MakeShared<FJsonValueString>(TEXT("TLM_SurfacePerPixelLighting"));
    if (Key == TEXT("DecalBlendMode") && GameValue.IsValid() && GameValue->TryGetString(Named) &&
        Named == TEXT("DBM_DBuffer_C4_WaterBox"))
        return MakeShared<FJsonValueString>(TEXT("DBM_DBuffer_Color"));
    if ((Key == TEXT("bUsedWithSkeletalMesh") || Key == TEXT("bUsedWithMorphTargets")) && GameValue.IsValid() &&
        GameValue->Type == EJson::Boolean && GameValue->AsBool() && HighestCoordinate >= 4)
        return MakeShared<FJsonValueBoolean>(false);
    return nullptr;
}
const TCHAR* const GameRenderValuesKey = TEXT("P4MaterialGameRenderValuesV1");
// A render_settings key's property and the object that holds it: one of the material's own, or one of the settings
// only the game's engine has (C4...), which the material keeps in its C4 settings (P4MaterialGameSettings.h). With
// bAdd a material without C4 settings gets them; without, it reads the defaults.
FProperty* RenderSettingProperty(UMaterial* Material, const FString& Key, bool bAdd, UObject*& Owner)
{
    if (FProperty* Property = FindFProperty<FProperty>(UMaterial::StaticClass(), *Key))
    {
        Owner = Material;
        return Property;
    }
    FProperty* Property = FindFProperty<FProperty>(UP4MaterialGameSettings::StaticClass(), *Key);
    if (!Property)
    {
        Owner = nullptr;
        return nullptr;
    }
    UP4MaterialGameSettings* Settings = bAdd ? UP4MaterialGameSettings::FindOrAdd(Material)
                                             : UP4MaterialGameSettings::Find(Material);
    Owner = Settings ? static_cast<UObject*>(Settings) : GetMutableDefault<UP4MaterialGameSettings>();
    return Property;
}
// Sets one render_settings value on a material. GameValues gets the game's value of a setting whose editor
// value is its stock stand-in.
bool SetRenderSetting(UMaterial* Material, const FString& Key, const TSharedPtr<FJsonValue>& Value,
    int32 HighestCoordinate, TMap<FString, TSharedPtr<FJsonValue>>& GameValues, FString& Error)
{
    UObject* Owner = nullptr;
    FProperty* Property = RenderSettingProperty(Material, Key, true, Owner);
    if (!Property)
        return Refuse(Error, TEXT("material-graph-game-render-policy-unavailable: ") + Key);
    if (Owner != Material)
    {
        if (!Set(Owner, Property, Value, Error))
            return false;
        CastChecked<UP4MaterialGameSettings>(Owner)->Publish();
        return true;
    }
    const TSharedPtr<FJsonValue> StandIn = GameRenderStandIn(Key, Value, HighestCoordinate);
    if (!StandIn.IsValid())
        return Set(Material, Property, Value, Error);
    if (!Set(Material, Property, StandIn, Error))
        return false;
    GameValues.Add(Key, Value);
    // The game's two lighting modes of its own: the material's C4 setting says which (Kadan's format lights by it).
    FString Named;
    if (Key == TEXT("TranslucencyLightingMode") && Value->TryGetString(Named))
    {
        UP4MaterialGameSettings* Settings = UP4MaterialGameSettings::FindOrAdd(Material);
        Settings->C4TranslucencyLightingMode = Named == TEXT("TLM_SurfacePerPixelLightingC4Custom1")
            ? C4TLM_SurfacePerPixelLightingC4Custom1
            : C4TLM_SurfacePerPixelLightingC4Custom0;
        Settings->Publish();
    }
    if (Key == TEXT("DecalBlendMode") && Value->TryGetString(Named) && Named == TEXT("DBM_DBuffer_C4_WaterBox"))
    {
        UP4MaterialGameSettings* Settings = UP4MaterialGameSettings::FindOrAdd(Material);
        Settings->C4DecalBlendMode = C4DBM_DBuffer_C4_WaterBox;
        Settings->Publish();
    }
    return true;
}
// The game's values of the settings a material holds as stand-ins, by setting: {"game": ..., "editor": ...}.
TSharedPtr<FJsonObject> GameRenderValues(const UMaterial* Material)
{
    const FString Stored = Material->GetOutermost()->GetMetaData()->GetValue(Material, GameRenderValuesKey);
    TSharedPtr<FJsonObject> Out;
    if (Stored.IsEmpty() || !P4ParseJson(Stored, Out))
        return nullptr;
    return Out;
}
bool Set(UObject* Object, FProperty* Property, const TSharedPtr<FJsonValue>& Value, FString& Error)
{
    void* At = Property->ContainerPtrToValuePtr<void>(Object);
    double Number = 0;
    FString Text;
    int64 Named = 0;
    const FString Where = Object->GetClass()->GetName() + TEXT(".") + Property->GetName();
    if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
    {
        if (!Value->TryGetNumber(Number))
            return Refuse(Error, TEXT("material-graph-property-needs-a-number: ") + Where);
        Float->SetPropertyValue(At, float(Number));
    }
    else if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
    {
        // A JSON true or false: the reader would take any number.
        if (Value->Type != EJson::Boolean)
            return Refuse(Error, TEXT("material-graph-property-needs-true-or-false: ") + Where);
        Bool->SetPropertyValue(At, Value->AsBool());
    }
    else if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
    {
        if (!EnumValue(Enum->GetEnum(), Value, Named))
            return EnumRefusal(Value, Where, Error);
        Enum->GetUnderlyingProperty()->SetIntPropertyValue(At, Named);
    }
    else if (const FByteProperty* Byte = CastField<FByteProperty>(Property))
    {
        if (Byte->Enum ? !EnumValue(Byte->Enum, Value, Named) : !Value->TryGetNumber(Number))
            return EnumRefusal(Value, Where, Error);
        Byte->SetPropertyValue(At, uint8(Byte->Enum ? Named : int64(Number)));
    }
    else if (const FNumericProperty* Whole = CastField<FNumericProperty>(Property))
    {
        if (!Whole->IsInteger() || !Value->TryGetNumber(Number) || Number != FMath::FloorToDouble(Number))
            return Refuse(Error, TEXT("material-graph-property-needs-a-whole-number: ") + Where);
        Whole->SetIntPropertyValue(At, int64(Number));
    }
    else if (const FNameProperty* Name = CastField<FNameProperty>(Property))
    {
        if (!Value->TryGetString(Text))
            return Refuse(Error, TEXT("material-graph-property-needs-text: ") + Where);
        Name->SetPropertyValue(At, FName(*Text));
    }
    else if (const FStrProperty* String = CastField<FStrProperty>(Property))
    {
        if (!Value->TryGetString(Text))
            return Refuse(Error, TEXT("material-graph-property-needs-text: ") + Where);
        String->SetPropertyValue(At, Text);
    }
    else if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
        const TSharedPtr<FJsonObject>* Color = nullptr;
        if (Struct->Struct == TBaseStructure<FLinearColor>::Get() && Value->TryGetObject(Color))
        {
            double Parts[4] = {0, 0, 0, 1};
            const TCHAR* Names[] = {TEXT("R"), TEXT("G"), TEXT("B"), TEXT("A")};
            for (int32 Index = 0; Index < 4; ++Index)
                if (!(*Color)->TryGetNumberField(Names[Index], Parts[Index]))
                    return Refuse(Error, TEXT("material-graph-color-malformed: ") + Where);
            *static_cast<FLinearColor*>(At) = FLinearColor(
                float(Parts[0]), float(Parts[1]), float(Parts[2]), float(Parts[3]));
            return true;
        }
        if (Struct->Struct != TBaseStructure<FLinearColor>::Get() || !Value->TryGetArray(Items) || Items->Num() < 3 ||
            Items->Num() > 4)
            return Refuse(Error, TEXT("material-graph-property-needs-three-or-four-numbers: ") + Where);
        float Parts[4] = {0, 0, 0, 0};
        for (int32 Index = 0; Index < Items->Num(); ++Index)
        {
            if (!(*Items)[Index]->TryGetNumber(Number))
                return Refuse(Error, TEXT("material-graph-property-needs-three-or-four-numbers: ") + Where);
            Parts[Index] = float(Number);
        }
        *static_cast<FLinearColor*>(At) = FLinearColor(Parts[0], Parts[1], Parts[2], Parts[3]);
    }
    else if (const FObjectProperty* Reference = CastField<FObjectProperty>(Property))
    {
        if (!Value->TryGetString(Text))
            return Refuse(Error, TEXT("material-graph-property-needs-an-asset-path: ") + Where);
        UObject* Asset = StaticLoadObject(Reference->PropertyClass, nullptr, *Text, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!Asset)
            return Refuse(Error, TEXT("material-graph-asset-not-in-the-project: ") + Text);
        if (IsStandIn(Asset))
            return Refuse(Error, TEXT("material-graph-dependency-is-unrecovered-stand-in: ") + Text);
        Reference->SetObjectPropertyValue(At, Asset);
    }
    else
        return Refuse(Error, TEXT("material-graph-property-of-a-kind-it-cannot-set: ") + Where);
    return true;
}
// False when the property is of a kind the format has no value for.
bool Get(const FProperty* Property, const void* At, TSharedPtr<FJsonValue>& Out)
{
    if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
        Out = MakeShared<FJsonValueNumber>(double(Float->GetPropertyValue(At)));
    else if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
        Out = MakeShared<FJsonValueBoolean>(Bool->GetPropertyValue(At));
    else if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
        Out = MakeShared<FJsonValueString>(
            Enum->GetEnum()->GetNameStringByValue(Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(At)));
    else if (const FByteProperty* Byte = CastField<FByteProperty>(Property))
    {
        if (Byte->Enum)
            Out = MakeShared<FJsonValueString>(Byte->Enum->GetNameStringByValue(Byte->GetPropertyValue(At)));
        else
            Out = MakeShared<FJsonValueNumber>(double(Byte->GetPropertyValue(At)));
    }
    else if (const FNumericProperty* Whole = CastField<FNumericProperty>(Property))
    {
        if (!Whole->IsInteger())
            return false;
        Out = MakeShared<FJsonValueNumber>(double(Whole->GetSignedIntPropertyValue(At)));
    }
    else if (const FNameProperty* Name = CastField<FNameProperty>(Property))
        Out = MakeShared<FJsonValueString>(Name->GetPropertyValue(At).ToString());
    else if (const FStrProperty* String = CastField<FStrProperty>(Property))
        Out = MakeShared<FJsonValueString>(String->GetPropertyValue(At));
    else if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        if (Struct->Struct != TBaseStructure<FLinearColor>::Get())
            return false;
        const FLinearColor& Color = *static_cast<const FLinearColor*>(At);
        Out = MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(double(Color.R)),
            MakeShared<FJsonValueNumber>(double(Color.G)), MakeShared<FJsonValueNumber>(double(Color.B)),
            MakeShared<FJsonValueNumber>(double(Color.A))});
    }
    else if (const FObjectProperty* Reference = CastField<FObjectProperty>(Property))
    {
        const UObject* Asset = Reference->GetObjectPropertyValue(At);
        Out = MakeShared<FJsonValueString>(Asset ? Asset->GetPathName() : FString());
    }
    else
        return false;
    return true;
}

// ----- links -----
struct FLink
{
    FString Node, Channels;
    int32 Index = 0;
};
bool ReadLink(const TSharedPtr<FJsonValue>& Value, FLink& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    double Index = 0;
    if (!Value.IsValid() || !Value->TryGetArray(Items) || Items->Num() < 2 || Items->Num() > 3)
        return false;
    if (!(*Items)[0]->TryGetString(Out.Node) || !(*Items)[1]->TryGetString(Out.Channels))
        return false;
    if (Items->Num() == 3 &&
        (!(*Items)[2]->TryGetNumber(Index) || Index < 0 || Index > 64 || Index != double(int32(Index))))
        return false;
    Out.Index = int32(Index);
    for (const TCHAR C : Out.Channels)
        if (!FString(TEXT("RGBA")).Contains(FString(1, &C), ESearchCase::CaseSensitive))
            return false;
    return true;
}
bool Picks(const FExpressionOutput& Pin, bool R, bool G, bool B, bool A)
{
    return Pin.Mask && (Pin.MaskR != 0) == R && (Pin.MaskG != 0) == G && (Pin.MaskB != 0) == B && (Pin.MaskA != 0) == A;
}
void Plug(FExpressionInput& In, UMaterialExpression* Source, int32 Index)
{
    const FExpressionOutput& Pin = Source->GetOutputs()[Index];
    In.Expression = Source;
    In.OutputIndex = Index;
    In.Mask = Pin.Mask;
    In.MaskR = Pin.MaskR;
    In.MaskG = Pin.MaskG;
    In.MaskB = Pin.MaskB;
    In.MaskA = Pin.MaskA;
}
UMaterialExpression* Add(UMaterial* Material, UClass* Class)
{
    UMaterialExpression* Made = NewObject<UMaterialExpression>(Material, Class, NAME_None, RF_Transactional);
    Material->Expressions.Add(Made);
    Made->Material = Material;
    Made->UpdateMaterialExpressionGuid(true, false);
    return Made;
}

struct FBuilder
{
    UMaterial* Material = nullptr;
    TMap<FString, UMaterialExpression*> Made;
    TMap<FString, UMaterialExpression*> Helpers; // the masks and appends made for links, by what they take

    // The source's whole value: its pin of that index, or all four channels when its first pin is a part.
    bool Whole(FExpressionInput& In, UMaterialExpression* Source, int32 Index, FString& Error)
    {
        TArray<FExpressionOutput>& Outputs = Source->GetOutputs();
        if (!Outputs.IsValidIndex(Index))
            return Refuse(Error, TEXT("material-graph-node-has-no-such-output: ") + Source->GetClass()->GetName());
        const FExpressionOutput& First = Outputs[Index];
        if (Index != 0 || !First.Mask || Picks(First, true, true, true, true))
        {
            Plug(In, Source, Index);
            return true;
        }
        const int32 All = Outputs.IndexOfByPredicate(
            [](const FExpressionOutput& Pin) { return Picks(Pin, true, true, true, true); });
        if (All != INDEX_NONE)
        {
            Plug(In, Source, All);
            return true;
        }
        const int32 Color = Outputs.IndexOfByPredicate(
            [](const FExpressionOutput& Pin) { return Picks(Pin, true, true, true, false); });
        const int32 Alpha = Outputs.IndexOfByPredicate(
            [](const FExpressionOutput& Pin) { return Picks(Pin, false, false, false, true); });
        // No color and alpha pins to join: the pin is all the node gives (a scene depth is one number).
        if (Color == INDEX_NONE || Alpha == INDEX_NONE)
        {
            Plug(In, Source, Index);
            return true;
        }
        const FString Key = Source->GetName() + TEXT("|all");
        UMaterialExpression*& Joined = Helpers.FindOrAdd(Key);
        if (!Joined)
        {
            UMaterialExpressionAppendVector* Append = CastChecked<UMaterialExpressionAppendVector>(
                Add(Material, UMaterialExpressionAppendVector::StaticClass()));
            Plug(Append->A, Source, Color);
            Plug(Append->B, Source, Alpha);
            Joined = Append;
        }
        Plug(In, Joined, 0);
        return true;
    }
    bool Connect(FExpressionInput& In, const FLink& Link, FString& Error)
    {
        UMaterialExpression** Found = Made.Find(Link.Node);
        if (!Found)
            return Refuse(Error, TEXT("material-graph-link-to-a-node-that-is-not-there: ") + Link.Node);
        UMaterialExpression* Source = *Found;
        if (Link.Channels.IsEmpty())
            return Whole(In, Source, Link.Index, Error);
        const bool R = Link.Channels.Contains(TEXT("R")), G = Link.Channels.Contains(TEXT("G"));
        const bool B = Link.Channels.Contains(TEXT("B")), A = Link.Channels.Contains(TEXT("A"));
        if (Link.Channels !=
            (FString(R ? TEXT("R") : TEXT("")) + (G ? TEXT("G") : TEXT("")) + (B ? TEXT("B") : TEXT("")) +
                (A ? TEXT("A") : TEXT(""))))
        {
            if (Link.Channels.Len() > 4)
                return Refuse(Error, TEXT("material-graph-swizzle-too-wide: ") + Link.Channels);
            // ExpressionInput masks select in RGBA order. Scalar masks joined in source order
            // preserve a reordered or repeated swizzle without changing its shader value.
            const FString SwizzleKey = FString::Printf(
                TEXT("%s|%d|swizzle|%s"), *Source->GetName(), Link.Index, *Link.Channels);
            UMaterialExpression* Swizzled = Helpers.FindRef(SwizzleKey);
            if (!Swizzled)
            {
                FExpressionInput Accumulated;
                for (int32 ChannelIndex = 0; ChannelIndex < Link.Channels.Len(); ++ChannelIndex)
                {
                    FLink Scalar = Link;
                    Scalar.Channels = Link.Channels.Mid(ChannelIndex, 1);
                    FExpressionInput Component;
                    if (!Connect(Component, Scalar, Error))
                        return false;
                    if (ChannelIndex == 0)
                        Accumulated = Component;
                    else
                    {
                        auto* Append = CastChecked<UMaterialExpressionAppendVector>(
                            Add(Material, UMaterialExpressionAppendVector::StaticClass()));
                        Append->A = Accumulated;
                        Append->B = Component;
                        Plug(Accumulated, Append, 0);
                    }
                }
                Swizzled = Accumulated.Expression;
                Helpers.Add(SwizzleKey, Swizzled);
            }
            Plug(In, Swizzled, 0);
            return true;
        }
        TArray<FExpressionOutput>& Outputs = Source->GetOutputs();
        const int32 Pin = Link.Index == 0
            ? Outputs.IndexOfByPredicate([&](const FExpressionOutput& Out) { return Picks(Out, R, G, B, A); })
            : Outputs.IsValidIndex(Link.Index) && Picks(Outputs[Link.Index], R, G, B, A) ? Link.Index
                                                                                         : INDEX_NONE;
        if (Pin != INDEX_NONE)
        {
            Plug(In, Source, Pin);
            return true;
        }
        const FString Key = FString::Printf(TEXT("%s|%d|%s"), *Source->GetName(), Link.Index, *Link.Channels);
        UMaterialExpression* Masked = Helpers.FindRef(Key);
        if (!Masked)
        {
            UMaterialExpressionComponentMask* Mask = CastChecked<UMaterialExpressionComponentMask>(
                Add(Material, UMaterialExpressionComponentMask::StaticClass()));
            Mask->R = R;
            Mask->G = G;
            Mask->B = B;
            Mask->A = A;
            if (!Whole(Mask->Input, Source, Link.Index, Error))
                return false;
            Masked = Mask;
            Helpers.Add(Key, Masked);
        }
        Plug(In, Masked, 0);
        return true;
    }
};

// Node ids in an order that does not depend on how they were written: by name, with a number at the
// end counted as a number (Add_2 before Add_10).
void SortIds(TArray<FString>& Ids)
{
    const auto Split = [](const FString& Id, FString& Name, int64& Number)
    {
        int32 At = Id.Len();
        while (At > 0 && FChar::IsDigit(Id[At - 1]))
            --At;
        Name = Id.Left(At);
        Number = At < Id.Len() && Id.Len() - At < 18 ? FCString::Atoi64(*Id + At) : -1;
    };
    Ids.Sort(
        [&](const FString& A, const FString& B)
        {
            FString NameA, NameB;
            int64 NumberA = 0, NumberB = 0;
            Split(A, NameA, NumberA);
            Split(B, NameB, NumberB);
            const int32 Order = NameA.Compare(NameB, ESearchCase::CaseSensitive);
            return Order != 0        ? Order < 0
                : NumberA != NumberB ? NumberA < NumberB
                                     : A.Compare(B, ESearchCase::CaseSensitive) < 0;
        });
}

// Columns from the material's pins outward, so the graph opens readable.
void Layout(UMaterial* Material)
{
    TMap<UMaterialExpression*, int32> Depth;
    TArray<UMaterialExpression*> Order;
    TFunction<void(UMaterialExpression*, int32)> Visit = [&](UMaterialExpression* Node, int32 At)
    {
        if (!Node || At > 256)
            return;
        int32& Known = Depth.FindOrAdd(Node, -1);
        if (Known >= At)
            return;
        if (Known < 0)
            Order.Add(Node);
        Known = At;
        for (FExpressionInput* Input : Node->GetInputs())
            if (Input)
                Visit(Input->Expression, At + 1);
    };
    for (const TCHAR* Pin : Pins)
        if (FExpressionInput* Input = InputOf(Material, Pin))
            Visit(Input->Expression, 0);
    TMap<int32, int32> Rows;
    for (UMaterialExpression* Node : Order)
    {
        const int32 Column = Depth[Node];
        Node->MaterialExpressionEditorX = -320 * (Column + 1);
        Node->MaterialExpressionEditorY = 160 * Rows.FindOrAdd(Column)++;
    }
}
} //namespace P4MaterialGraph

namespace P4MaterialGraph
{
UMaterial* Build(const FString& GraphJson, const FString& Package, FString& Error)
{
    Error.Reset();
    if (!IsInGameThread())
    {
        Error = TEXT("material-graph-requires-editor-thread");
        return nullptr;
    }
    if (HasNonfiniteJsonNumber(GraphJson))
    {
        Error = TEXT("material-graph-nonfinite-json-number-unavailable");
        return nullptr;
    }
    TSharedPtr<FJsonObject> Graph;
    const TSharedPtr<FJsonObject>* Nodes = nullptr;
    const TSharedPtr<FJsonObject>* Outputs = nullptr;
    if (!P4ParseJson(GraphJson, Graph) || !Graph->TryGetObjectField(TEXT("nodes"), Nodes) ||
        !Graph->TryGetObjectField(TEXT("outputs"), Outputs))
    {
        Error = TEXT("material-graph-not-a-graph");
        return nullptr;
    }
    double Format = 1;
    if (Graph->HasField(TEXT("format")) &&
        (!Graph->TryGetNumberField(TEXT("format"), Format) || (Format != 1 && Format != 2)))
    {
        Error = TEXT("material-graph-format-unsupported");
        return nullptr;
    }
    if (Format == 2 && !Graph->HasTypedField<EJson::String>(TEXT("domain")))
    {
        Error = TEXT("material-graph-format2-needs-domain");
        return nullptr;
    }
    for (const TCHAR* Field : {TEXT("domain"), TEXT("blend_mode"), TEXT("shading_model")})
        if (Graph->HasField(Field) && !Graph->HasTypedField<EJson::String>(Field))
        {
            Error = FString(TEXT("material-graph-setting-needs-a-name: ")) + Field;
            return nullptr;
        }
    for (const TCHAR* Field : {TEXT("two_sided"), TEXT("tangent_space_normal")})
        if (Graph->HasField(Field) && !Graph->HasTypedField<EJson::Boolean>(Field))
        {
            Error = FString(TEXT("material-graph-setting-needs-true-or-false: ")) + Field;
            return nullptr;
        }
    if (Graph->HasField(TEXT("clip")) && !Graph->HasTypedField<EJson::Number>(TEXT("clip")))
    {
        Error = TEXT("material-graph-setting-needs-a-number: clip");
        return nullptr;
    }
    if (!P4IsValidContentPackage(Package))
    {
        Error = TEXT("material-graph-destination-not-in-content");
        return nullptr;
    }
    const FString Name = FPackageName::GetLongPackageAssetName(Package);
    if ((!P4KadanOwnsReimportSlot(Package) && FPackageName::DoesPackageExist(Package)) ||
        IsValid(FindObject<UObject>(nullptr, *(Package + TEXT(".") + Name))))
    {
        UObject* Existing = StaticLoadObject(
            UObject::StaticClass(), nullptr, *(Package + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
        Error = IsStandIn(Existing) ? (P4IsVerifiedStandIn(Existing) ? TEXT("material-graph-stand-in-fill-unavailable")
                                                                     : TEXT("material-graph-edited-stand-in-refused"))
                                    : TEXT("material-graph-destination-holds-another-asset");
        return nullptr;
    }

    FBuilder Builder;
    UMaterial* Material = NewObject<UMaterial>(
        CreatePackage(*Package), FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
    Builder.Material = Material;
    TStrongObjectPtr<UMaterial> Guard(Material);
    const auto Fail = [&]() -> UMaterial*
    {
        // Out of its place, so that a later attempt finds the path free.
        Material->ClearFlags(RF_Public | RF_Standalone);
        Material->Rename(
            nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
        Material->MarkPendingKill();
        return nullptr;
    };
    TArray<FString> Ids;
    (*Nodes)->Values.GetKeys(Ids);
    SortIds(Ids);
    for (const FString& Id : Ids)
    {
        const TSharedPtr<FJsonObject>* Node = nullptr;
        FString Type;
        if (!(*Nodes)->Values[Id]->TryGetObject(Node) || !(*Node)->TryGetStringField(TEXT("type"), Type))
        {
            Error = TEXT("material-graph-node-without-a-type: ") + Id;
            return Fail();
        }
        // ShadowReplace carries the exact Default/Shadow expression inputs through
        // the same typed reflected-input path as the other engine expressions.
        UClass* Class = FindObject<UClass>(ANY_PACKAGE, *(Prefix + Type));
        if (!Class || !Class->IsChildOf(UMaterialExpression::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract))
        {
            Error = TEXT("material-graph-node-type-the-engine-does-not-have: ") + Type;
            return Fail();
        }
        UMaterialExpression* Made = Add(Material, Class);
        if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Made))
            Custom->Inputs.Reset();
        if (UMaterialExpressionKadanGameCustom* Game = Cast<UMaterialExpressionKadanGameCustom>(Made))
            Game->Inputs.Reset();
        Builder.Made.Add(Id, Made);
        const TSharedPtr<FJsonObject>* Props = nullptr;
        if ((*Node)->TryGetObjectField(TEXT("props"), Props))
        {
            TArray<FString> Keys;
            (*Props)->Values.GetKeys(Keys);
            Keys.Sort();
            for (const FString& Key : Keys)
            {
                UMaterialExpressionKadanGameCustom* Game = Cast<UMaterialExpressionKadanGameCustom>(Made);
                if (Key == TEXT("Inputs") && (Cast<UMaterialExpressionCustom>(Made) || Game))
                {
                    UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Made);
                    const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
                    TSet<FName> Names;
                    if (!(*Props)->Values[Key]->TryGetArray(Inputs))
                    {
                        Error = TEXT("material-graph-custom-inputs-malformed: ") + Id;
                        return Fail();
                    }
                    if (Custom)
                        Custom->Inputs.Reset();
                    else
                        Game->Inputs.Reset();
                    for (const TSharedPtr<FJsonValue>& Item : *Inputs)
                    {
                        const TSharedPtr<FJsonObject>* Input = nullptr;
                        FString InputName;
                        if (!Item->TryGetObject(Input) || (*Input)->Values.Num() != 1 ||
                            !(*Input)->TryGetStringField(TEXT("InputName"), InputName) || InputName.IsEmpty() ||
                            Names.Contains(FName(*InputName)))
                        {
                            Error = TEXT("material-graph-custom-inputs-malformed: ") + Id;
                            return Fail();
                        }
                        Names.Add(FName(*InputName));
                        if (Custom)
                        {
                            FCustomInput Added;
                            Added.InputName = FName(*InputName);
                            Custom->Inputs.Add(Added);
                        }
                        else
                        {
                            FKadanGameCustomInput Added;
                            Added.InputName = FName(*InputName);
                            Game->Inputs.Add(Added);
                        }
                    }
                    continue;
                }
                FProperty* Property = FindFProperty<FProperty>(Class, *Key);
                if (!Property || Property->GetOwnerClass() == UMaterialExpression::StaticClass() || InputOf(Made, Key))
                {
                    Error = FString::Printf(TEXT("material-graph-node-has-no-such-property: %s.%s"), *Type, *Key);
                    return Fail();
                }
                if (!Set(Made, Property, (*Props)->Values[Key], Error))
                    return Fail();
            }
        }
        if (UMaterialExpressionTextureSample* Sample = Cast<UMaterialExpressionTextureSample>(Made))
        {
            // Texture objects inherit this base but do not sample. Their consuming sample owns the sampler source.
            const bool TextureObject = Made->IsA<UMaterialExpressionTextureObject>() ||
                Made->IsA<UMaterialExpressionTextureObjectParameter>();
            if (Format == 2 &&
                (!Props || !(*Props)->HasField(TEXT("SamplerType")) ||
                    (!TextureObject && !(*Props)->HasField(TEXT("SamplerSource")))))
            {
                Error = TEXT("material-graph-format2-texture-needs-sampler-type-and-source: ") + Id;
                return Fail();
            }
            if (Format == 1 && (!Props || !(*Props)->HasField(TEXT("SamplerType"))))
                Sample->AutoSetSampleType();
        }
        if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Made))
        {
            // The Carrot4 clipping globals are unavailable in the stock engine.
            // ReflectionStruct.ReflectionCubemap and its sampler are actual
            // UE4.26 bindings (ReflectionEnvironment.h / ReflectionEnvironmentComposite.ush).
            // Keep those formulas intact and let the real compiler diagnose them.
            if (Custom->Code.Contains(TEXT("C4GlobalClippingPlane")) ||
                Custom->Code.Contains(TEXT("C4XluGlobalClippingPlaneSign")))
            {
                Error = TEXT("material-graph-game-engine-custom-binding-unavailable: ") + Id;
                return Fail();
            }
        }
        if (UMaterialExpressionCollectionParameter* Collected = Cast<UMaterialExpressionCollectionParameter>(Made))
        {
            Collected->ParameterId = Collected->Collection
                ? Collected->Collection->GetParameterId(Collected->ParameterName)
                : FGuid();
            if (!Collected->ParameterId.IsValid())
            {
                Error = TEXT("material-graph-collection-has-no-such-parameter: ") + Collected->ParameterName.ToString();
                return Fail();
            }
        }
        if (Made->bIsParameterExpression)
            Made->UpdateParameterGuid(true, false);
    }
    for (const FString& Id : Ids)
    {
        const TSharedPtr<FJsonObject>& Node = (*Nodes)->Values[Id]->AsObject();
        const TSharedPtr<FJsonObject>* Inputs = nullptr;
        if (!Node->TryGetObjectField(TEXT("inputs"), Inputs))
            continue;
        TArray<FString> Keys;
        (*Inputs)->Values.GetKeys(Keys);
        Keys.Sort();
        for (const FString& Key : Keys)
        {
            FLink Link;
            FExpressionInput* Input = InputOf(Builder.Made[Id], Key);
            if (!Input)
            {
                Error = FString::Printf(
                    TEXT("material-graph-node-has-no-such-input: %s.%s"), *Node->GetStringField(TEXT("type")), *Key);
                return Fail();
            }
            if (!ReadLink((*Inputs)->Values[Key], Link))
            {
                Error = TEXT("material-graph-link-malformed: ") + Id + TEXT(".") + Key;
                return Fail();
            }
            if (!Builder.Connect(*Input, Link, Error))
                return Fail();
        }
    }
    for (const FString& Id : Ids)
        if (const auto* Sample = Cast<UMaterialExpressionCustom>(Builder.Made[Id]))
        {
            if (Sample->Description == P4OriginalRawNormalOperation::Description() &&
                !P4OriginalRawNormalOperation::Matches(Sample, Error))
                return Fail();
            if (Sample->Description == P4OriginalPositiveInfinityOperation::Description() &&
                !P4OriginalPositiveInfinityOperation::Matches(Sample, Error))
                return Fail();
        }
    TArray<FString> PinNames;
    (*Outputs)->Values.GetKeys(PinNames);
    PinNames.Sort();
    for (const FString& Pin : PinNames)
    {
        FLink Link;
        FExpressionInput* Input = nullptr;
        if (Pin == TEXT("ClearCoatBottomNormal"))
        {
            auto* Normal = CastChecked<UMaterialExpressionClearCoatNormalCustomOutput>(
                Add(Material, UMaterialExpressionClearCoatNormalCustomOutput::StaticClass()));
            Input = &Normal->Input;
        }
        for (const TCHAR* Known : Pins)
            if (Pin.Equals(Known, ESearchCase::CaseSensitive))
                Input = InputOf(Material, Pin);
        if (!Input)
        {
            Error = TEXT("material-graph-material-has-no-such-pin: ") + Pin;
            return Fail();
        }
        if (!ReadLink((*Outputs)->Values[Pin], Link))
        {
            Error = TEXT("material-graph-link-malformed: ") + Pin;
            return Fail();
        }
        if (!Builder.Connect(*Input, Link, Error))
            return Fail();
        // The material computes as many customized UVs as its highest connected one.
        if (Pin.StartsWith(TEXT("CustomizedUV"), ESearchCase::CaseSensitive))
            Material->NumCustomizedUVs = FMath::Max(Material->NumCustomizedUVs, int32(Pin[12] - TCHAR('0')) + 1);
    }

    FString Word;
    bool Flag = false;
    double Number = 0;
    if (Graph->TryGetStringField(TEXT("domain"), Word))
    {
        const int64 Value = StaticEnum<EMaterialDomain>()->GetValueByNameString(Word);
        if (Value == INDEX_NONE || Value >= int64(MD_MAX))
        {
            Error = TEXT("material-graph-domain-unknown: ") + Word;
            return Fail();
        }
        Material->MaterialDomain = EMaterialDomain(Value);
    }
    if (Graph->TryGetStringField(TEXT("blend_mode"), Word))
    {
        const int64 Value = StaticEnum<EBlendMode>()->GetValueByNameString(Word);
        if (Value == INDEX_NONE)
        {
            Error = TEXT("material-graph-blend-mode-unknown: ") + Word;
            return Fail();
        }
        Material->BlendMode = EBlendMode(Value);
    }
    if (Graph->TryGetStringField(TEXT("shading_model"), Word))
    {
        const int64 Value = StaticEnum<EMaterialShadingModel>()->GetValueByNameString(Word);
        if (Value == INDEX_NONE || Value >= int64(MSM_NUM))
        {
            Error = TEXT("material-graph-shading-model-unknown: ") + Word;
            return Fail();
        }
        Material->SetShadingModel(EMaterialShadingModel(Value));
    }
    if (Graph->TryGetBoolField(TEXT("two_sided"), Flag))
        Material->TwoSided = Flag;
    if (Graph->TryGetBoolField(TEXT("tangent_space_normal"), Flag))
        Material->bTangentSpaceNormal = Flag;
    if (Graph->TryGetNumberField(TEXT("clip"), Number))
        Material->OpacityMaskClipValue = float(Number);
    const TSharedPtr<FJsonObject>* Render = nullptr;
    TMap<FString, TSharedPtr<FJsonValue>> GameRenderValueOf;
    const int32 HighestCoordinate = HighestTexCoord(*Nodes, *Outputs);
    if (Graph->HasField(TEXT("render_settings")))
    {
        if (!Graph->TryGetObjectField(TEXT("render_settings"), Render))
        {
            Error = TEXT("material-graph-render-settings-malformed");
            return Fail();
        }
        TArray<FString> Keys;
        (*Render)->Values.GetKeys(Keys);
        Keys.Sort();
        for (const FString& Key : Keys)
            if (!SetRenderSetting(Material, Key, (*Render)->Values[Key], HighestCoordinate, GameRenderValueOf, Error))
                return Fail();
    }
    if (GameRenderValueOf.Num())
    {
        TSharedRef<FJsonObject> Kept = MakeShared<FJsonObject>();
        for (const auto& Pair : GameRenderValueOf)
        {
            TSharedRef<FJsonObject> One = MakeShared<FJsonObject>();
            One->SetField(TEXT("game"), Pair.Value);
            One->SetField(TEXT("editor"), GameRenderStandIn(Pair.Key, Pair.Value, HighestCoordinate));
            One->SetNumberField(TEXT("highest_texcoord"), HighestCoordinate);
            Kept->SetObjectField(Pair.Key, One);
        }
        FString Text = P4CondensedJson(Kept);
        Material->GetOutermost()->GetMetaData()->SetValue(Material, GameRenderValuesKey, *Text);
        UE_LOG(LogTemp, Display,
            TEXT("Kadan: %s keeps the game's %s; the editor previews it with stock Unreal's nearest setting."),
            *Package, *Text);
    }
    else
        Material->GetOutermost()->GetMetaData()->RemoveValue(Material, GameRenderValuesKey);
    // Preserve the source dictionary's applicability and explicit false/zero fields. Readback
    // reads their live values; it does not reintroduce renderer settings removed by an owner.
    FString RenderSource = TEXT("absent");
    if (Render)
        FJsonSerializer::Serialize((*Render).ToSharedRef(),
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&RenderSource));
    Material->GetOutermost()->GetMetaData()->SetValue(Material, TEXT("P4MaterialRenderSettingsV2"), *RenderSource);
    // These recovered postprocess calls use the stock scene lookup, but a Custom
    // expression alone does not tell the translator to emit its scene helpers.
    // The unused input declares that dependency without changing the lookup or UV.
    if (Material->MaterialDomain == MD_PostProcess)
    {
        for (const FString& Id : Ids)
        {
            UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Builder.Made[Id]);
            if (!Custom ||
                Custom->Code != TEXT("return SceneTextureLookup(ViewportUVToSceneTextureUV(UV, 14), 14, false);") ||
                Custom->Inputs.Num() != 1 || Custom->Inputs[0].InputName != FName(TEXT("UV")) ||
                !Custom->Inputs[0].Input.Expression)
                continue;
            UClass* SceneClass = FindObject<UClass>(ANY_PACKAGE, TEXT("MaterialExpressionSceneTexture"));
            if (!SceneClass || !SceneClass->IsChildOf(UMaterialExpression::StaticClass()))
            {
                Error = TEXT("material-graph-node-type-the-engine-does-not-have: SceneTexture");
                return Fail();
            }
            auto* Scene = static_cast<UMaterialExpressionSceneTexture*>(Add(Material, SceneClass));
            Scene->SceneTextureId = PPI_PostProcessInput0;
            Scene->bFiltered = false;
            FCustomInput Usage;
            Usage.InputName = FName(TEXT("P4PostProcessInput0Usage"));
            Usage.Input.Expression = Scene;
            Custom->Inputs.Add(Usage);
        }
    }
    Layout(Material);
    Material->PreEditChange(nullptr);
    Material->PostEditChange();
    if (Package.StartsWith(TEXT(P4_GAME_ASSET_ROOT "/"), ESearchCase::CaseSensitive))
    {
        // A recovered game master is reference material. The first release exports instances and
        // materials authored by the user, never an edited recovered master graph.
        FP4OriginalRecord Record;
        Record.Package = Package;
        Record.Class = Material->GetClass()->GetPathName();
        Record.Kind = TEXT("material");
        Record.ReferenceOnly = true;
        Record.ReadOnly = true;
        Record.SourceDigest = FMD5::HashAnsiString(*GraphJson);
        Record.SourceDigestKind = TEXT("material-graph-v2-md5");
        Record.ComparisonSchema = TEXT("material-native-state-v1");
        Record.Fidelity = TEXT("recovered-pixel-contract");
        for (const auto& Pair : GameRenderValueOf)
        {
            FP4OriginalValueNote Note;
            Note.Kind = TEXT("game-render-setting");
            Note.Object = Package;
            Note.Property = Pair.Key;
            Note.GameValue = P4CondensedJson(Pair.Value);
            Note.Action = TEXT("game-value-kept-editor-previews-with-") +
                P4CondensedJson(GameRenderStandIn(Pair.Key, Pair.Value, HighestCoordinate));
            Record.Values.Add(MoveTemp(Note));
        }
        if (!P4RecordKnownGameSource(Material, Package, false))
        {
            Error = TEXT("material-native-comparison-source-origin-not-ready");
            return Fail();
        }
        // Source recipe identity and the native graph are different contracts.
        // Capture the latter once, at construction, so unchanged game parents
        // can be omitted from a creator export without re-baselining edits.
        Record.ComparisonDigest = P4MaterialComparisonState(Material, Error);
        if (Record.ComparisonDigest.IsEmpty())
            return Fail();
        if (!P4MarkOriginalAsset(Material, Record, Error))
            return Fail();
    }
    FAssetRegistryModule::AssetCreated(Material);
    Material->MarkPackageDirty();
    return Material;
}

} //namespace P4MaterialGraph

UMaterial* P4BuildMaterialFromGraph(const FString& GraphJson, const FString& Package, FString& Error)
{
    return P4MaterialGraph::Build(GraphJson, Package, Error);
}
