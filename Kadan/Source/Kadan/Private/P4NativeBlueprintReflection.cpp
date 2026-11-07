#include "P4NativeBlueprintReflection.h"
#include "EdGraphSchema_K2.h"
#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include <type_traits>

namespace
{
constexpr int32 MaxModules = 128, MaxDeclarations = 200000, MaxFields = 1000000;
bool ModuleName(const FString& Name)
{
    if (Name.IsEmpty() || Name.Len() > 128)
        return false;
    for (TCHAR C : Name)
        if (!(C == '_' || (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9')))
            return false;
    return true;
}
FP4NativeReflectionProperty ReadProperty(FProperty* Property)
{
    FP4NativeReflectionProperty Out;
    Out.Name = Property->GetName();
    Out.Flags = uint64(Property->GetPropertyFlags());
    Out.ArrayDim = Property->ArrayDim;
    Out.IsReturn = Property->HasAnyPropertyFlags(CPF_ReturnParm);
    Out.IsOut = Property->HasAnyPropertyFlags(CPF_OutParm);
    Out.IsReference = Property->HasAnyPropertyFlags(CPF_ReferenceParm);
    UEnum* Enum = nullptr;
    if (FEnumProperty* Strong = CastField<FEnumProperty>(Property))
        Enum = Strong->GetEnum();
    else if (FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
        Enum = Numeric->GetIntPropertyEnum();
    if (Enum)
        Out.EnumPath = Enum->GetPathName();
    return Out;
}
bool Included(const UObject* Object, const TSet<FName>& Packages)
{
    return Object && Packages.Contains(Object->GetOutermost()->GetFName());
}
} //namespace

bool P4CaptureNativeBlueprintReflection(const TArray<FString>& Modules, const TArray<FString>& RequiredPaths,
    FP4NativeBlueprintReflection& Out, FString& Error)
{
    Out = {};
    Error.Reset();
    if (!IsInGameThread())
    {
        Error = TEXT("native-reflection-requires-editor-thread");
        return false;
    }
    if (Modules.Num() == 0 || Modules.Num() > MaxModules || RequiredPaths.Num() > MaxDeclarations)
    {
        Error = TEXT("native-reflection-request-bound");
        return false;
    }
    TSet<FName> Packages;
    bool MissingModule = false;
    for (const FString& Module : Modules)
    {
        if (!ModuleName(Module))
        {
            Error = TEXT("native-reflection-invalid-module: ") + Module;
            return false;
        }
        const FString Package = TEXT("/Script/") + Module;
        UPackage* Loaded = FindPackage(nullptr, *Package);
        if (!Loaded)
        {
            MissingModule = true;
            Out.Refused.Add(TEXT("native-reflection-module-not-loaded: ") + Package);
        }
        else if (!Loaded->GetName().Equals(Package, ESearchCase::CaseSensitive))
        {
            MissingModule = true;
            Out.Refused.Add(TEXT("native-reflection-module-identity-differs: ") + Package);
        }
        else
            Packages.Add(FName(*Package));
    }
    int32 Fields = 0, Declarations = 0;
    auto Admit = [&]() { return ++Declarations <= MaxDeclarations; };
    TSet<FString> Paths;
    for (TObjectIterator<UStruct> It; It; ++It)
    {
        UStruct* Structure = *It;
        if (!Included(Structure, Packages) || Structure->IsA<UFunction>() ||
            (!Structure->IsA<UClass>() && !Structure->IsA<UScriptStruct>()))
            continue;
        if (!Admit())
        {
            Error = TEXT("native-reflection-declaration-bound");
            return false;
        }
        FP4NativeReflectionStructure Row;
        Row.Path = Structure->GetPathName();
        if (const UStruct* Super = Structure->GetSuperStruct())
            Row.SuperPath = Super->GetPathName();
        for (TFieldIterator<FProperty> Property(Structure, EFieldIteratorFlags::ExcludeSuper); Property; ++Property)
        {
            if (++Fields > MaxFields)
            {
                Error = TEXT("native-reflection-field-bound");
                return false;
            }
            Row.Fields.Add(ReadProperty(*Property));
        }
        Paths.Add(Row.Path);
        Out.Structures.Add(MoveTemp(Row));
    }
    for (TObjectIterator<UFunction> It; It; ++It)
    {
        UFunction* Function = *It;
        if (!Included(Function, Packages))
            continue;
        if (!Admit())
        {
            Error = TEXT("native-reflection-declaration-bound");
            return false;
        }
        FP4NativeReflectionFunction Row;
        // UObject uses ':' for nested fields; the planner consumes Owner.Function.
        Row.Path = Function->GetOuter()->GetPathName() + TEXT(".") + Function->GetName();
        Row.Flags = uint32(Function->FunctionFlags);
        FString Refusal;
        for (TFieldIterator<FProperty> Property(Function, EFieldIteratorFlags::ExcludeSuper); Property; ++Property)
        {
            if (!Property->HasAnyPropertyFlags(CPF_Parm))
                continue;
            if (++Fields > MaxFields)
            {
                Error = TEXT("native-reflection-field-bound");
                return false;
            }
            FEdGraphPinType Pin;
            if (Property->ArrayDim != 1 || !GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(*Property, Pin))
            {
                Refusal = TEXT("native-reflection-unsupported-parameter: ") + Row.Path + TEXT(".") +
                    Property->GetName();
                break;
            }
            FP4NativeReflectionProperty Parameter = ReadProperty(*Property);
            if (Parameter.IsReturn)
            {
                if (Row.Return.IsSet())
                {
                    Refusal = TEXT("native-reflection-multiple-returns: ") + Row.Path;
                    break;
                }
                Row.Return = MoveTemp(Parameter);
            }
            else
                Row.Parameters.Add(MoveTemp(Parameter));
        }
        if (!Refusal.IsEmpty())
            Out.Refused.Add(MoveTemp(Refusal));
        else
        {
            Paths.Add(Row.Path);
            Out.Functions.Add(MoveTemp(Row));
        }
    }
    for (TObjectIterator<UEnum> It; It; ++It)
    {
        UEnum* Enum = *It;
        if (!Included(Enum, Packages))
            continue;
        if (!Admit() || Enum->NumEnums() > MaxFields - Fields)
        {
            Error = TEXT("native-reflection-enum-bound");
            return false;
        }
        FP4NativeReflectionEnum Row;
        Row.Path = Enum->GetPathName();
        Row.CppForm = uint8(Enum->GetCppForm());
        Row.CppType = Enum->CppType;
        if (Row.CppType.Len() > 65536)
        {
            Error = TEXT("native-reflection-enum-cpp-type-bound");
            return false;
        }
        // UE4.26 exposes HasAnyEnumFlags rather than a flags getter. Observe
        // every raw bit, including unknown bits, without inferring from names.
        using FEnumBits = std::make_unsigned<std::underlying_type<EEnumFlags>::type>::type;
        static_assert(sizeof(FEnumBits) <= sizeof(uint32), "Native enum flags exceed the typed capture width");
        for (uint32 Bit = 0; Bit < sizeof(FEnumBits) * 8; ++Bit)
        {
            const FEnumBits Mask = FEnumBits(FEnumBits(1) << Bit);
            if (Enum->HasAnyEnumFlags(EEnumFlags(Mask)))
                Row.EnumFlags |= uint32(Mask);
        }
        for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
        {
            ++Fields;
            Row.Values.Add(Enum->GetNameByIndex(Index).ToString());
            Row.NumericValues.Add(Enum->GetValueByIndex(Index));
        }
        Paths.Add(Row.Path);
        Out.Enums.Add(MoveTemp(Row));
    }
    auto HasExactPath = [&](const FString& Path)
    {
        const FString* Actual = Paths.Find(Path);
        return Actual && Actual->Equals(Path, ESearchCase::CaseSensitive);
    };
    for (const FString& Path : RequiredPaths)
        if (!HasExactPath(Path))
            Out.Refused.Add(TEXT("native-reflection-declaration-missing: ") + Path);
    Out.Functions.Sort(
        [](const FP4NativeReflectionFunction& A, const FP4NativeReflectionFunction& B) { return A.Path < B.Path; });
    Out.Structures.Sort(
        [](const FP4NativeReflectionStructure& A, const FP4NativeReflectionStructure& B) { return A.Path < B.Path; });
    Out.Enums.Sort([](const FP4NativeReflectionEnum& A, const FP4NativeReflectionEnum& B) { return A.Path < B.Path; });
    Out.Refused.Sort();
    // Unsupported unrequested functions remain explicit diagnostic rows. They do
    // not invalidate otherwise usable declarations; required missing paths do.
    bool Missing = MissingModule;
    for (const FString& Path : RequiredPaths)
        Missing |= !HasExactPath(Path);
    if (Missing)
        Error = TEXT("native-reflection-required-declarations-unavailable");
    return !Missing;
}
