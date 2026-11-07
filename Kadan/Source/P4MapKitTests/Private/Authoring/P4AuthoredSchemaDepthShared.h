// The helpers that P4AuthoredSchemaDepthCoverageAutomation.cpp and its companion test files share.
#pragma once
#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DataAsset.h"
#include "Engine/UserDefinedStruct.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

namespace P4AuthoredSchemaDepthAfter242
{
inline FString Quote(const FString& Text)
{
    FString Out;
    auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
    Writer->WriteArrayStart();
    Writer->WriteValue(Text);
    Writer->WriteArrayEnd();
    Writer->Close();
    return Out.Mid(1, Out.Len() - 2);
}
inline TArray<uint8> Bytes(UObject* Object, bool& Good, FAutomationTestBase& Test)
{
    TArray<uint8> Out;
    FObjectWriter Writer(Object, Out, false, false, false, PPF_DuplicateVerbatim);
    Good = Test.TestTrue(TEXT("Actual native object serialization succeeds with nonempty complete output"),
        !Writer.IsError() && Out.Num() > 0);
    return Out;
}
struct FFixture
{
    FAutomationTestBase& Test;
    FString Root = TEXT("/Game/P4AuthoredDepthCoverage/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TArray<UPackage*> Packages;
    TArray<UObject*> Roots;
    TArray<UUserDefinedStruct*> Chain;
    TArray<FName> Members;
    explicit FFixture(FAutomationTestBase& In) : Test(In) {}
    UPackage* Package(const FString& Name)
    {
        UPackage* Result = CreatePackage(*(Root / Name));
        Result->GetMetaData();
        Packages.Add(Result);
        return Result;
    }
    template <typename T>
    T* Hold(T* Object)
    {
        if (Object)
        {
            Object->AddToRoot();
            Roots.Add(Object);
        }
        return Object;
    }
    ~FFixture()
    {
        // All preservation checks precede cleanup. Native compiler objects
        // remain alive through strong fixture roots; no global GC is invoked.
        for (UPackage* Owner : Packages)
        {
            TArray<UObject*> Objects;
            GetObjectsWithOuter(Owner, Objects, true);
            for (UObject* Object : Objects)
                if (Object->IsAsset())
                    FAssetRegistryModule::AssetDeleted(Object);
            for (UObject* Object : Objects)
            {
                Object->ClearFlags(RF_Public | RF_Standalone);
                Object->SetFlags(RF_Transient);
            }
        }
        for (UObject* Object : Roots)
            Object->RemoveFromRoot();
        TArray<FAssetData> Remaining;
        FAssetRegistryModule::GetRegistry().GetAssetsByPath(FName(*Root), Remaining, true);
        Test.TestEqual(TEXT("No GUID-owned authored asset remains published in the live registry after cleanup"),
            Remaining.Num(), 0);
        CheckNoPublication();
    }
    void CheckNoPublication()
    {
        TArray<FString> Files;
        IFileManager::Get().FindFilesRecursive(
            Files, *FPackageName::LongPackageNameToFilename(Root), TEXT("*"), true, false);
        Test.TestEqual(TEXT("No GUID-owned package or partial sidecar was saved"), Files.Num(), 0);
    }
    bool BuildChain(int32 Maximum)
    {
        for (int32 I = 0; I <= Maximum; ++I)
        {
            const FString Name = FString::Printf(TEXT("AuthoredStruct_%02d"), I);
            UUserDefinedStruct* Struct = Hold(FStructureEditorUtils::CreateUserDefinedStruct(
                Package(Name), FName(*Name), RF_Public | RF_Standalone | RF_Transactional));
            if (!Test.TestNotNull(TEXT("Supported editor API creates real authored UDS"), Struct))
                return false;
            const auto& Descriptions = FStructureEditorUtils::GetVarDesc(Struct);
            if (!Test.TestEqual(TEXT("Editor producer supplies one real initial member"), Descriptions.Num(), 1))
                return false;
            const FGuid Guid = Descriptions[0].VarGuid;
            FEdGraphPinType Pin;
            Pin.PinCategory = I ? UEdGraphSchema_K2::PC_Struct : UEdGraphSchema_K2::PC_Float;
            if (I)
                Pin.PinSubCategoryObject = Chain.Last();
            if (!Test.TestTrue(TEXT("Editor accepts acyclic authored member type"),
                    FStructureEditorUtils::ChangeVariableType(Struct, Guid, Pin)) ||
                !Test.TestTrue(TEXT("Editor assigns authored friendly label"),
                    FStructureEditorUtils::RenameVariable(Struct, Guid, I ? TEXT("Child") : TEXT("Amount"))))
                return false;
            FStructureEditorUtils::CompileStructure(Struct);
            const auto& Compiled = FStructureEditorUtils::GetVarDesc(Struct);
            if (!Test.TestTrue(TEXT("Real compiler admits exactly one completed authored member"),
                    Struct->Status == UDSS_UpToDate && Compiled.Num() == 1 && Struct->GetStructureSize() > 0))
                return false;
            FProperty* Member = Struct->FindPropertyByName(Compiled[0].VarName);
            if (!Test.TestTrue(TEXT("Field identity comes from actual compiled editor description"),
                    Member && !Member->HasAnyPropertyFlags(CPF_Transient) &&
                        (I ? (CastField<FStructProperty>(Member) &&
                                 CastField<FStructProperty>(Member)->Struct == Chain.Last())
                           : CastField<FFloatProperty>(Member) != nullptr)))
                return false;
            Members.Add(Member->GetFName());
            Chain.Add(Struct);
        }
        return true;
    }
    UBlueprint* Blueprint(const TCHAR* Name, int32 Depth, bool AddArray = false)
    {
        // Kismet caches generated classes by short name across compiler GC.
        // Fixture packages alone do not make that cache identity unique.
        const FString FixtureName = FString(Name) + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        UBlueprint* BP = Hold(FKismetEditorUtilities::CreateBlueprint(UPrimaryDataAsset::StaticClass(),
            Package(FixtureName), FName(*FixtureName), BPTYPE_Normal, UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass(), TEXT("P4AuthoredDepthCoverage")));
        if (!Test.TestNotNull(TEXT("Supported editor API creates actual authored PrimaryDataAsset Blueprint"), BP))
            return nullptr;
        for (const FName Variable :
            {FName(TEXT("Shallow")), FName(TEXT("Deep")), FName(TEXT("ShallowArray")), FName(TEXT("DeepArray"))})
        {
            const bool IsArray = Variable == FName(TEXT("DeepArray")) || Variable == FName(TEXT("ShallowArray"));
            const bool IsShallow = Variable == FName(TEXT("Shallow")) || Variable == FName(TEXT("ShallowArray"));
            if (IsArray && !AddArray)
                continue;
            FEdGraphPinType Pin;
            Pin.PinCategory = UEdGraphSchema_K2::PC_Struct;
            Pin.PinSubCategoryObject = Chain[IsShallow ? 0 : Depth];
            if (IsArray)
                Pin.ContainerType = EPinContainerType::Array;
            if (!Test.TestTrue(TEXT("Actual authored Blueprint member declaration"),
                    FBlueprintEditorUtils::AddMemberVariable(BP, Variable, Pin)))
                return nullptr;
            // Ordinary editor 'Instance Editable' authoring, before compile.
            // No live FProperty flags or inherited/native metadata is rewritten.
            FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(BP, Variable, false);
        }
        FCompilerResultsLog Log;
        FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Log);
        if (!Test.TestTrue(TEXT("Native Blueprint compiler produces concrete DataAsset class and initialized CDO"),
                Log.NumErrors == 0 && BP->IsUpToDate() && BP->GeneratedClass &&
                    BP->GeneratedClass->IsChildOf(UPrimaryDataAsset::StaticClass()) &&
                    !BP->GeneratedClass->HasAnyClassFlags(CLASS_Abstract) && BP->GeneratedClass->GetDefaultObject()))
            return nullptr;
        for (const FName NameToCheck : {FName(TEXT("Shallow")), FName(TEXT("Deep"))})
        {
            auto* Property = FindFProperty<FStructProperty>(BP->GeneratedClass, NameToCheck);
            if (!Test.TestTrue(TEXT("Compiler created genuine editable authored struct field"),
                    Property && Property->Struct == Chain[NameToCheck == FName(TEXT("Shallow")) ? 0 : Depth] &&
                        Property->HasAnyPropertyFlags(CPF_Edit) &&
                        !Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance | CPF_Transient)))
                return nullptr;
        }
        if (AddArray)
        {
            for (const FName Variable : {FName(TEXT("ShallowArray")), FName(TEXT("DeepArray"))})
            {
                auto* Array = FindFProperty<FArrayProperty>(BP->GeneratedClass, Variable);
                auto* Inner = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
                if (!Test.TestTrue(TEXT("Compiler supplies actual declared array element schema"),
                        Inner && Inner->Struct == Chain[Variable == FName(TEXT("ShallowArray")) ? 0 : Depth]))
                    return nullptr;
            }
        }
        return BP;
    }
    UObject* Instance(UBlueprint* BP, const TCHAR* Name)
    {
        if (!BP)
            return nullptr;
        UPackage* Owner = Package(Name);
        UObject* Object = Hold(UP4AuthoringTools::CreateNativeAsset(BP->GeneratedClass, Owner->GetName()));
        if (Object)
        {
            Test.TestTrue(TEXT("Public factory registers exact actual authored DataAsset"),
                FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(FName(*Object->GetPathName())).IsValid());
            Owner->SetDirtyFlag(false); // Ordinary owned clean editor fixture before its edit.
        }
        return Object;
    }
    FString Json(int32 Depth, const FString& Number) const
    {
        FString Out = TEXT("{") + Quote(Members[0].ToString()) + TEXT(":") + Number + TEXT("}");
        for (int32 I = 1; I <= Depth; ++I)
            Out = TEXT("{") + Quote(Members[I].ToString()) + TEXT(":") + Out + TEXT("}");
        return Out;
    }
    FString Text(int32 Depth, const FString& Number) const
    {
        FString Out = TEXT("(") + Members[0].ToString() + TEXT("=") + Number + TEXT(")");
        for (int32 I = 1; I <= Depth; ++I)
            Out = TEXT("(") + Members[I].ToString() + TEXT("=") + Out + TEXT(")");
        return Out;
    }
    bool ShallowValue(UObject* Object, float Expected) const
    {
        auto* RootProperty = FindFProperty<FStructProperty>(Object->GetClass(), TEXT("Shallow"));
        auto* Value = RootProperty ? FindFProperty<FFloatProperty>(RootProperty->Struct, Members[0]) : nullptr;
        return Value &&
            Value->GetPropertyValue_InContainer(RootProperty->ContainerPtrToValuePtr<void>(Object)) == Expected;
    }
    bool ShallowArrayValue(UObject* Object, float Expected) const
    {
        auto* Array = FindFProperty<FArrayProperty>(Object->GetClass(), TEXT("ShallowArray"));
        auto* Inner = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
        auto* Value = Inner ? FindFProperty<FFloatProperty>(Inner->Struct, Members[0]) : nullptr;
        if (!Value)
            return false;
        FScriptArrayHelper Elements(Array, Array->ContainerPtrToValuePtr<void>(Object));
        return Elements.Num() == 1 && Value->GetPropertyValue_InContainer(Elements.GetRawPtr(0)) == Expected;
    }
    int32 ScalarDepth(UObject* Object, FName RootField) const
    {
        // Count actual compiled schema edges, independently of the plugin's
        // recursive routines: class->root=1, array->element=1, struct->member=1.
        FProperty* Property = Object->GetClass()->FindPropertyByName(RootField);
        int32 Edges = 1;
        for (int32 I = 0; Property && I < 64; ++I)
        {
            if (auto* Array = CastField<FArrayProperty>(Property))
            {
                Property = Array->Inner;
                ++Edges;
                continue;
            }
            if (auto* Struct = CastField<FStructProperty>(Property))
            {
                TFieldIterator<FProperty> Field(Struct->Struct);
                if (!Field)
                    return INDEX_NONE;
                Property = *Field;
                ++Field;
                if (Field)
                    return INDEX_NONE;
                ++Edges;
                continue;
            }
            return CastField<FFloatProperty>(Property) ? Edges : INDEX_NONE;
        }
        return INDEX_NONE;
    }
    bool DeepValue(UObject* Object, float Expected) const
    {
        FProperty* Property = Object->GetClass()->FindPropertyByName(TEXT("Deep"));
        if (!Property)
            return false;
        void* Storage = Property->ContainerPtrToValuePtr<void>(Object);
        for (int32 I = 0; I < 64; ++I)
        {
            auto* Struct = CastField<FStructProperty>(Property);
            if (!Struct)
            {
                auto* Value = CastField<FFloatProperty>(Property);
                return Value && Value->GetPropertyValue(Storage) == Expected;
            }
            TFieldIterator<FProperty> Field(Struct->Struct);
            if (!Field)
                return false;
            Property = *Field;
            ++Field;
            if (Field)
                return false;
            Storage = Property->ContainerPtrToValuePtr<void>(Storage);
        }
        return false;
    }
};
struct FSnapshot
{
    struct FPropertyCopy
    {
        FProperty* Property;
        FDefaultConstructedPropertyElement Stored;
        FPropertyCopy(FProperty* In, UObject* Object) : Property(In), Stored(In)
        {
            Property->CopyCompleteValue(Stored.GetObjAddress(), Property->ContainerPtrToValuePtr<void>(Object));
        }
        bool Same(UObject* Object) const
        {
            for (int32 Slot = 0; Slot < Property->ArrayDim; ++Slot)
                if (!Property->Identical(Property->ContainerPtrToValuePtr<void>(Object, Slot),
                        static_cast<const uint8*>(Stored.GetObjAddress()) + Slot * Property->ElementSize,
                        PPF_DeepComparison))
                    return false;
            return true;
        }
    };
    struct FObject
    {
        UObject* Object;
        UObject* Outer;
        UClass* Class;
        FString Path;
        EObjectFlags Flags;
        bool SerializationGood = false;
        TArray<uint8> Serialized;
        TArray<TUniquePtr<FPropertyCopy>> Copies;
        FObject(UObject* In, FAutomationTestBase& Test)
            : Object(In), Outer(In->GetOuter()), Class(In->GetClass()), Path(In->GetPathName()), Flags(In->GetFlags()),
              Serialized(Bytes(In, SerializationGood, Test))
        {
            if (In->IsA<UDataAsset>())
                for (TFieldIterator<FProperty> It(In->GetClass()); It; ++It)
                    Copies.Add(MakeUnique<FPropertyCopy>(*It, In));
        }
        bool IdentitySame() const
        {
            return Object->GetOuter() == Outer && Object->GetClass() == Class &&
                Object->GetPathName().Equals(Path, ESearchCase::CaseSensitive) && Object->GetFlags() == Flags;
        }
        bool Same(FAutomationTestBase& Test) const
        {
            bool Good = false;
            const auto Current = Bytes(Object, Good, Test);
            return SerializationGood && Good && IdentitySame() && Current == Serialized;
        }
    };
    struct FSchema
    {
        UStruct* Struct;
        int32 Size;
        EClassFlags ClassFlags;
        TArray<FProperty*> Fields;
        TArray<EPropertyFlags> Flags;
        TArray<int32> Dimensions, ElementSizes;
        explicit FSchema(UStruct* In)
            : Struct(In), Size(In->GetStructureSize()),
              ClassFlags(Cast<UClass>(In) ? CastChecked<UClass>(In)->GetClassFlags() : CLASS_None)
        {
            for (TFieldIterator<FProperty> It(In); It; ++It)
            {
                Fields.Add(*It);
                Flags.Add(It->GetPropertyFlags());
                Dimensions.Add(It->ArrayDim);
                ElementSizes.Add(It->ElementSize);
            }
        }
        bool Same() const
        {
            if (Struct->GetStructureSize() != Size ||
                (Cast<UClass>(Struct) && CastChecked<UClass>(Struct)->GetClassFlags() != ClassFlags))
                return false;
            TArray<FProperty*> Now;
            for (TFieldIterator<FProperty> It(Struct); It; ++It)
                Now.Add(*It);
            if (Now != Fields)
                return false;
            for (int32 I = 0; I < Fields.Num(); ++I)
                if (Fields[I]->GetPropertyFlags() != Flags[I] || Fields[I]->ArrayDim != Dimensions[I] ||
                    Fields[I]->ElementSize != ElementSizes[I])
                    return false;
            return true;
        }
    };
    FFixture& Fixture;
    TArray<FObject> Objects;
    TArray<FSchema> Schemas;
    TArray<bool> Dirty;
    TArray<uint32> PackageFlags;
    TArray<TArray<UObject*>> Members;
    int32 Pre = 0, Post = 0;
    FDelegateHandle A, B;
    bool Owns(UObject* Object) const { return Object && Fixture.Packages.Contains(Object->GetOutermost()); }
    explicit FSnapshot(FFixture& In) : Fixture(In)
    {
        for (UPackage* Package : Fixture.Packages)
        {
            Dirty.Add(Package->IsDirty());
            PackageFlags.Add(Package->GetPackageFlags());
            TArray<UObject*> Children;
            GetObjectsWithOuter(Package, Children, true);
            for (UObject* Object : Children)
            {
                Objects.Emplace(Object, Fixture.Test);
                if (auto* Struct = Cast<UStruct>(Object))
                    Schemas.Emplace(Struct);
            }
            Members.Add(MoveTemp(Children));
        }
        A = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
            [this](UObject* Object, const FEditPropertyChain&)
            {
                if (Owns(Object))
                    ++Pre;
            });
        B = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
            [this](UObject* Object, FPropertyChangedEvent&)
            {
                if (Owns(Object))
                    ++Post;
            });
    }
    ~FSnapshot()
    {
        FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(A);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(B);
    }
    bool Ready() const
    {
        for (const FObject& Object : Objects)
            if (!Object.SerializationGood)
                return false;
        return true;
    }
    void Check(UObject* EditedControl = nullptr, int32 ExpectedPre = 0, int32 ExpectedPost = 0,
        FName OtherEditedField = NAME_None, FName FirstEditedField = TEXT("Shallow"))
    {
        for (const FObject& Object : Objects)
        {
            if (Object.Object != EditedControl)
                Fixture.Test.TestTrue(
                    TEXT("Complete authored asset/class/CDO/UDS/editor-data/metadata bytes and identities retained"),
                    Object.Same(Fixture.Test));
            else
            {
                bool Good = false;
                Bytes(EditedControl, Good, Fixture.Test);
                Fixture.Test.TestTrue(TEXT("Edited control retains successful nonempty native serialization"), Good);
                Fixture.Test.TestTrue(
                    TEXT("Edited control retains native owner/class/path/flags"), Object.IdentitySame());
                for (const auto& Copy : Object.Copies)
                    if (Copy->Property->GetFName() != FirstEditedField &&
                        Copy->Property->GetFName() != OtherEditedField)
                        Fixture.Test.TestTrue(TEXT("Every unselected initialized native authored property retained"),
                            Copy->Same(EditedControl));
            }
        }
        for (const FSchema& Schema : Schemas)
            Fixture.Test.TestTrue(
                TEXT("Actual compiled schema field pointers, flags, dimensions and class flags retained"),
                Schema.Same());
        for (int32 I = 0; I < Fixture.Packages.Num(); ++I)
        {
            UPackage* Package = Fixture.Packages[I];
            TArray<UObject*> Now;
            GetObjectsWithOuter(Package, Now, true);
            Fixture.Test.TestTrue(TEXT("Exact native package child census retained"), Now.Num() == Members[I].Num());
            for (UObject* Object : Members[I])
                Fixture.Test.TestTrue(TEXT("Exact native child pointer retained"), Now.Contains(Object));
            Fixture.Test.TestEqual(
                TEXT("Exact native package flags retained"), Package->GetPackageFlags(), PackageFlags[I]);
            Fixture.Test.TestEqual(
                TEXT("Existing schema/package dirty state or genuine control edit dirtiness retained"),
                Package->IsDirty(), EditedControl && Package == EditedControl->GetOutermost() ? true : Dirty[I]);
        }
        Fixture.Test.TestEqual(TEXT("Exact authored pre-edit notifications"), Pre, ExpectedPre);
        Fixture.Test.TestEqual(TEXT("Exact authored post-edit notifications"), Post, ExpectedPost);
        Fixture.CheckNoPublication();
    }
};
inline bool Refuses(FAutomationTestBase& Test, UObject* Object, const FString& Json)
{
    const FString Response = UP4AuthoringTools::NativeAssetReadback(Object, Json);
    TSharedPtr<FJsonObject> Result;
    bool Valid = true;
    FString Error;
    return Test.TestTrue(TEXT("Public readback returns exact depth refusal with diagnostic"),
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response), Result) && Result.IsValid() &&
            Result->TryGetBoolField(TEXT("valid"), Valid) && !Valid &&
            Result->TryGetStringField(TEXT("error"), Error) &&
            Error.Contains(TEXT("excessive property nesting"), ESearchCase::CaseSensitive));
}
} //namespace P4AuthoredSchemaDepthAfter242
#endif
