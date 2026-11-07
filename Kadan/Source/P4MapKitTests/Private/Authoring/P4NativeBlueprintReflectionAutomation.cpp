#include "P4NativeBlueprintReflection.h"
#include "P4NativeBlueprintReflectionPlan.h"
#include "Async/Async.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NativeBlueprintReflectionTest, "P4MapKit.Native.BlueprintReflection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4NativeBlueprintReflectionTest::RunTest(const FString&)
{
    FString Error;
    FP4NativeBlueprintReflection Snapshot;
    TestFalse(TEXT("Malformed logical modules are refused"),
        P4CaptureNativeBlueprintReflection({TEXT("../Engine")}, {}, Snapshot, Error));
    TestTrue(TEXT("Malformed module refusal is named"), Error.StartsWith(TEXT("native-reflection-invalid-module:")));
    const FString OffThread = Async(EAsyncExecution::ThreadPool,
        []()
        {
            FP4NativeBlueprintReflection Result;
            FString Why;
            P4CaptureNativeBlueprintReflection({TEXT("Engine")}, {}, Result, Why);
            return Why;
        }).Get();
    TestEqual(TEXT("Worker capture refuses before reading UObjects"), OffThread,
        FString(TEXT("native-reflection-requires-editor-thread")));

    const FString FunctionPath = TEXT("/Script/Engine.Actor.K2_DestroyActor");
    const TArray<FString> FixturePaths = {FunctionPath, TEXT("/Script/Engine.GameplayStatics.GetSurfaceType"),
        TEXT("/Script/Engine.KismetMathLibrary.BreakRotator"),
        TEXT("/Script/Engine.GameplayStatics.BeginDeferredActorSpawnFromClass")};
    if (!P4CaptureNativeBlueprintReflection({TEXT("Engine"), TEXT("CoreUObject")}, FixturePaths, Snapshot, Error))
    {
        AddError(Error);
        return false;
    }
    const FP4NativeReflectionFunction* Actual = Snapshot.Functions.FindByPredicate(
        [&](const FP4NativeReflectionFunction& Row) { return Row.Path == FunctionPath; });
    UFunction* Declaration = AActor::StaticClass()->FindFunctionByName(TEXT("K2_DestroyActor"));
    if (!Actual || !Declaration)
    {
        AddError(TEXT("Loaded Actor declaration was not captured"));
        return false;
    }
    TestEqual(TEXT("Actual declaration flags are preserved"), Actual->Flags, uint32(Declaration->FunctionFlags));
    int32 Params = 0;
    for (TFieldIterator<FProperty> Property(Declaration, EFieldIteratorFlags::ExcludeSuper); Property; ++Property)
        if (Property->HasAnyPropertyFlags(CPF_Parm) && !Property->HasAnyPropertyFlags(CPF_ReturnParm))
            ++Params;
    TestEqual(TEXT("Actual parameter count is preserved"), Actual->Parameters.Num(), Params);
    using namespace P4MapKit::Blueprint;
    GraphSignatures Signatures(P4BlueprintReflectionPlanValue(Snapshot));
    const J Signature = Signatures.Get(TCHAR_TO_UTF8(*FunctionPath));
    TestTrue(TEXT("Direct typed projection resolves the exact declaration"), Signature.Kind == J::K::Array);
    if (Signature.Kind == J::K::Array)
        TestEqual(TEXT("Planner receives the loaded flags"), Signature[1].U(), uint64(Declaration->FunctionFlags));
    bool SawStrongEnum = false, SawEnumReturn = false, SawOut = false, SawReference = false;
    for (const FString& Path : FixturePaths)
    {
        FString OwnerPath, FunctionName;
        Path.Split(TEXT("."), &OwnerPath, &FunctionName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        UClass* Owner = FindObject<UClass>(nullptr, *OwnerPath);
        UFunction* Function = Owner ? Owner->FindFunctionByName(FName(*FunctionName)) : nullptr;
        const FP4NativeReflectionFunction* Row = Snapshot.Functions.FindByPredicate(
            [&](const FP4NativeReflectionFunction& Candidate) { return Candidate.Path == Path; });
        if (!Function || !Row)
        {
            AddError(TEXT("Reflection fixture missing: ") + Path);
            continue;
        }
        int32 Index = 0;
        for (TFieldIterator<FProperty> Property(Function, EFieldIteratorFlags::ExcludeSuper); Property; ++Property)
        {
            if (!Property->HasAnyPropertyFlags(CPF_Parm))
                continue;
            const FP4NativeReflectionProperty* Captured = Property->HasAnyPropertyFlags(CPF_ReturnParm)
                ? (Row->Return.IsSet() ? &Row->Return.GetValue() : nullptr)
                : (Row->Parameters.IsValidIndex(Index) ? &Row->Parameters[Index++] : nullptr);
            if (!Captured)
            {
                AddError(TEXT("Parameter missing: ") + Path + TEXT(".") + Property->GetName());
                continue;
            }
            TestEqual(TEXT("Parameter order/name: ") + Path, Captured->Name, Property->GetName());
            TestEqual(TEXT("Out direction: ") + Path, Captured->IsOut, Property->HasAnyPropertyFlags(CPF_OutParm));
            TestEqual(TEXT("Reference shape: ") + Path, Captured->IsReference,
                Property->HasAnyPropertyFlags(CPF_ReferenceParm));
            UEnum* Enum = nullptr;
            if (FEnumProperty* Strong = CastField<FEnumProperty>(*Property))
            {
                Enum = Strong->GetEnum();
                SawStrongEnum = true;
            }
            else if (FNumericProperty* Numeric = CastField<FNumericProperty>(*Property))
                Enum = Numeric->GetIntPropertyEnum();
            TestEqual(TEXT("Exact enum path: ") + Path, Captured->EnumPath, Enum ? Enum->GetPathName() : FString());
            SawEnumReturn |= Enum && Captured->IsReturn;
            SawOut |= Captured->IsOut;
            SawReference |= Captured->IsReference;
        }
        TestEqual(TEXT("All ordered parameters retained: ") + Path, Index, Row->Parameters.Num());
    }
    TestTrue(TEXT("Fixtures exercise strong enum properties"), SawStrongEnum);
    TestTrue(TEXT("Fixtures exercise enum-valued returns"), SawEnumReturn);
    TestTrue(TEXT("Fixtures exercise out parameters"), SawOut);
    TestTrue(TEXT("Fixtures exercise reference parameters"), SawReference);
    // Two stock forms exercise a namespaced and an enum-class declaration. The
    // original Engine additions use these same observed dimensions in a separate
    // qualified oracle, rather than deriving their form from entry spellings.
    for (const FString& Path :
        {FString(TEXT("/Script/CoreUObject.EAxis")), FString(TEXT("/Script/Engine.EAttachmentRule"))})
    {
        const UEnum* Enum = FindObject<UEnum>(nullptr, *Path);
        const FP4NativeReflectionEnum* Row = Snapshot.Enums.FindByPredicate(
            [&](const FP4NativeReflectionEnum& Candidate) { return Candidate.Path == Path; });
        if (!Enum || !Row)
        {
            AddError(TEXT("Native enum-dimension fixture missing: ") + Path);
            continue;
        }
        TestEqual(TEXT("Actual enum CPP form: ") + Path, Row->CppForm, uint8(Enum->GetCppForm()));
        TestEqual(TEXT("Actual enum CPP type: ") + Path, Row->CppType, Enum->CppType);
        TestEqual(TEXT("Complete enum numerical entries: ") + Path, Row->NumericValues.Num(), Enum->NumEnums());
        if (Row->NumericValues.Num() == Enum->NumEnums())
            for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
                TestEqual(TEXT("Exact signed native enum value: ") + Path, Row->NumericValues[Index],
                    Enum->GetValueByIndex(Index));
        TestEqual(TEXT("Captured flags identify actual flag state: ") + Path,
            Enum->HasAnyEnumFlags(EEnumFlags(Row->EnumFlags)), Row->EnumFlags != 0);
        const J Projected = P4BlueprintReflectionPlanValue(Snapshot);
        const J* ProjectedRow = nullptr;
        for (const J& Candidate : Projected["enums"].Items)
            if (Candidate["path"].S() == std::string(TCHAR_TO_UTF8(*Path)))
            {
                ProjectedRow = &Candidate;
                break;
            }
        if (!ProjectedRow)
        {
            AddError(TEXT("Enum dimensions missing from typed projection: ") + Path);
            continue;
        }
        TestEqual(TEXT("Typed projection preserves enum form: ") + Path, (*ProjectedRow)["cpp_form"].I(),
            int64(Row->CppForm));
        TestEqual(TEXT("Typed projection preserves enum flags: ") + Path, (*ProjectedRow)["enum_flags"].U(),
            uint64(Row->EnumFlags));
        TestTrue(TEXT("Typed projection preserves enum type: ") + Path,
            (*ProjectedRow)["cpp_type"].S() == std::string(TCHAR_TO_UTF8(*Row->CppType)));
    }
    const std::string Missing = "/Script/Engine.Actor.__P4MissingNativeDeclaration";
    TestTrue(TEXT("Absent functions are never invented"), Signatures.Get(Missing).Kind == J::K::Null);
    FP4NativeBlueprintReflection MissingSnapshot;
    TestFalse(TEXT("Required missing declarations refuse"),
        P4CaptureNativeBlueprintReflection({TEXT("Engine")}, {UTF8_TO_TCHAR(Missing.c_str())}, MissingSnapshot, Error));
    TestTrue(TEXT("Missing declaration is identified explicitly"),
        MissingSnapshot.Refused.Contains(
            TEXT("native-reflection-declaration-missing: ") + FString(UTF8_TO_TCHAR(Missing.c_str()))));

    // Qualification dumps are an explicit test output. Neither the adapter nor
    // the editor planner reads a private captured reflection file.
    FString ModuleText, Output;
    if (FParse::Value(FCommandLine::Get(), TEXT("P4ReflectionOutput="), Output))
    {
        // FParse's default stops at commas, which would capture only the first
        // explicitly requested module while making the native test look valid.
        FString ParserFixture;
        TestTrue(TEXT("Qualification module CSV is parsed"),
            FParse::Value(TEXT("-P4ReflectionModules=AIModule,Engine,CoreUObject -Next=Value"),
                TEXT("P4ReflectionModules="), ParserFixture, false));
        TestEqual(TEXT("Qualification module CSV retains all exact modules"), ParserFixture,
            FString(TEXT("AIModule,Engine,CoreUObject")));
        if (!FParse::Value(FCommandLine::Get(), TEXT("P4ReflectionModules="), ModuleText, false))
        {
            AddError(TEXT("Qualification output requires explicit P4ReflectionModules"));
            return false;
        }
        TArray<FString> Modules;
        ModuleText.ParseIntoArray(Modules, TEXT(","), true);
        const bool Captured = P4CaptureNativeBlueprintReflection(Modules, {}, Snapshot, Error);
        // Keep a partial diagnostic snapshot even when a requested module is
        // absent. Qualification must see its refusal, never infer a fallback.
        if (!Captured)
            AddError(Error);
        FString Destination = FPaths::ConvertRelativePathToFull(Output);
        FString Allowed = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("KadanQualification"));
        FPaths::NormalizeFilename(Destination);
        FPaths::NormalizeFilename(Allowed);
        if (!Destination.StartsWith(Allowed + TEXT("/"), ESearchCase::IgnoreCase))
        {
            AddError(TEXT("Reflection qualification output must stay under Saved/KadanQualification"));
            return false;
        }
        J Value = P4BlueprintReflectionPlanValue(Snapshot), Refused = J::Array();
        J RequestedModules = J::Array();
        for (const FString& Module : Modules)
            RequestedModules.Items.emplace_back(TCHAR_TO_UTF8(*Module));
        Value["requested_modules"] = MoveTemp(RequestedModules);
        // Preserve diagnostic flags separately from the planner projection.
        // Classification of extra editor fields needs actual loaded CPF evidence,
        // not guesses from names or unconditional filtering of mismatches.
        J FieldProfiles = J::Object();
        for (const FP4NativeReflectionStructure& Structure : Snapshot.Structures)
        {
            J Fields = J::Array();
            for (const FP4NativeReflectionProperty& Field : Structure.Fields)
                Fields.Items.push_back(J::Object(
                    {{"name", std::string(TCHAR_TO_UTF8(*Field.Name))}, {"flags", J(std::uint64_t(Field.Flags))},
                        {"array_dim", Field.ArrayDim}, {"editor_only", bool(Field.Flags & CPF_EditorOnly)},
                        {"deprecated", bool(Field.Flags & CPF_Deprecated)}}));
            FieldProfiles[std::string(TCHAR_TO_UTF8(*Structure.Path))] = MoveTemp(Fields);
        }
        Value["field_profiles"] = MoveTemp(FieldProfiles);
        for (const FString& Why : Snapshot.Refused)
            Refused.Items.emplace_back(TCHAR_TO_UTF8(*Why));
        Value["refused"] = Refused;
        if (!Captured)
            Value["capture_error"] = J(std::string(TCHAR_TO_UTF8(*Error)));
        const std::string Text = Value.Dump();
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true);
        if (Text.size() > 64u * 1024u * 1024u ||
            !FFileHelper::SaveStringToFile(
                UTF8_TO_TCHAR(Text.c_str()), *Destination, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            AddError(TEXT("Reflection qualification output failed"));
            return false;
        }
    }
    return !HasAnyErrors();
}
#endif
