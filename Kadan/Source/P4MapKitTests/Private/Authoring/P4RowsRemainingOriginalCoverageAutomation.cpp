// Private native regression draft, bound to the full 138/27 measured slice.
// Not compiled/executed by this worker. No feature or qualification claim.
#include "P4AuthoringRows.h"
#include "P4NativeValueStaging.h"
#include "P4NativeHash.h"
#include "P4AuthoringRowsCoverageShared.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
// PRIVATE append inside P4AuthoringRowsInspectionCoverageAutomation.cpp guard.
// Requires its authentic FN138Original/FN138Suite/FN138State helpers unchanged.
// No accepted invented source/schema; all controls bind real native captures.
#include "UObject/UObjectHash.h"
namespace P4OriginalRowsPublicInputMatrices250
{
using namespace P4RowsCoverage138;
bool Serialized(FN138Suite& F, UObject* Object, TArray<uint8>& Out)
{
    Out.Reset();
    FObjectWriter Writer(Object, Out, false, false, false, PPF_DuplicateVerbatim);
    return F.Check(
        TEXT("Required actual native serialization succeeds and is nonempty"), !Writer.IsError() && Out.Num() > 0);
}
} //namespace P4OriginalRowsPublicInputMatrices250

#endif

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS && PLATFORM_WINDOWS
// PRIVATE append inside P4AuthoringRowsInspectionCoverageAutomation.cpp guard.
// Reuses genuine source/state and reviewed P4OriginalRowsPublicInputMatrices250
// helpers. No original schema, native source verdict, or proof is invented.
#include "Camera/CameraComponent.h"
namespace P4RowsRemainingOriginalAndFixed272
{
using namespace P4RowsCoverage138;
bool ErrorContains(FN138Suite& F, const FString& Verdict, const TCHAR* Error)
{
    const auto Result = N138Parse(Verdict);
    FString Message;
    return F.Check(TEXT("Literal public refusal reason is retained"),
        Result.IsValid() && Result->TryGetStringField(TEXT("error"), Message) &&
            Message.Contains(Error, ESearchCase::CaseSensitive));
}
bool HashInput(const FString& Text, FString& Out)
{
    const uint8* Bytes = reinterpret_cast<const uint8*>(Text.GetCharArray().GetData());
    const int64 Size = int64(Text.GetCharArray().Num()) * sizeof(TCHAR);
    return P4HashByteStream(
        [&](const TFunction<bool(const uint8*, int32)>& Update)
        {
            for (int64 At = 0; At < Size;)
            {
                const int32 Count = int32(FMath::Min<int64>(64 * 1024, Size - At));
                if (!Update(Bytes + At, Count))
                    return false;
                At += Count;
            }
            return true;
        },
        Out);
}
struct FCameraGuard
{
    FN138Suite& Suite;
    UCameraComponent* Camera;
    FN138State Native;
    UObject* Default;
    TArray<uint8> DefaultBytes;
    EObjectFlags DefaultFlags;
    TArray<FProperty*> Fields;
    TArray<EPropertyFlags> Flags;
    TArray<int32> Dimensions, Sizes;
    UObject* Outer;
    UPackage* Package;
    EObjectFlags PackageObjectFlags;
    uint32 PackageFlags;
    FName FileName;
    FGuid Guid, PersistentGuid;
    uint64 FileSize;
    bool Ready = false;
    FCameraGuard(FN138Suite& F, UCameraComponent* In)
        : Suite(F), Camera(In), Native(In, true), Default(In->GetClass()->GetDefaultObject()),
          DefaultFlags(Default->GetFlags()), Outer(In->GetOuter()), Package(In->GetOutermost()),
          PackageObjectFlags(Package->GetFlags()), PackageFlags(Package->GetPackageFlags()),
          FileName(Package->FileName), Guid(Package->GetGuid()), PersistentGuid(Package->GetPersistentGuid()),
          FileSize(Package->FileSize)
    {
        auto Settings = FindFProperty<FStructProperty>(Camera->GetClass(), TEXT("PostProcessSettings"));
        auto Tints = Settings ? FindFProperty<FStructProperty>(Settings->Struct, TEXT("LensFlareTints")) : nullptr;
        if (!F.Check(TEXT("Actual camera supplies the real eight-element editable fixed color array"),
                Settings && Tints && Tints->ArrayDim == 8 && Tints->Struct == TBaseStructure<FLinearColor>::Get() &&
                    Settings->HasAnyPropertyFlags(CPF_Edit) && Tints->HasAnyPropertyFlags(CPF_Edit)))
            return;
        for (UStruct* Schema : {static_cast<UStruct*>(Camera->GetClass()), static_cast<UStruct*>(Settings->Struct),
                 static_cast<UStruct*>(Tints->Struct)})
            for (TFieldIterator<FProperty> It(Schema); It; ++It)
            {
                Fields.Add(*It);
                Flags.Add(It->GetPropertyFlags());
                Dimensions.Add(It->ArrayDim);
                Sizes.Add(It->ElementSize);
            }
        TArray<uint8> Buffer;
        Ready = P4OriginalRowsPublicInputMatrices250::Serialized(F, Camera, Buffer) && Buffer == Native.Bytes &&
            P4OriginalRowsPublicInputMatrices250::Serialized(F, Default, DefaultBytes);
        Native.Same(F);
        Ready = Ready && F.Good;
    }
    void Same()
    {
        Native.Same(Suite);
        TArray<uint8> Buffer;
        Suite.Check(TEXT("Full current camera serialization succeeds and matches every baseline byte"),
            P4OriginalRowsPublicInputMatrices250::Serialized(Suite, Camera, Buffer) && Buffer == Native.Bytes);
        Suite.Check(TEXT("Native camera CDO remains fully serialized and unchanged"),
            Camera->GetClass()->GetDefaultObject() == Default && Default->GetFlags() == DefaultFlags &&
                P4OriginalRowsPublicInputMatrices250::Serialized(Suite, Default, Buffer) && Buffer == DefaultBytes);
        Suite.Check(TEXT("Disposable camera native owner/package/file binding remains exact"),
            Camera->GetOuter() == Outer && Camera->GetOutermost() == Package &&
                Package->GetFlags() == PackageObjectFlags && Package->GetPackageFlags() == PackageFlags &&
                Package->FileName == FileName && Package->GetGuid() == Guid &&
                Package->GetPersistentGuid() == PersistentGuid && Package->FileSize == FileSize);
        for (int32 I = 0; I < Fields.Num(); ++I)
            Suite.Check(TEXT("Existing class/postprocess/color schema flags/dimensions/sizes remain exact"),
                Fields[I]->GetPropertyFlags() == Flags[I] && Fields[I]->ArrayDim == Dimensions[I] &&
                    Fields[I]->ElementSize == Sizes[I]);
        Suite.Check(TEXT("Owned camera remains unregistered and transient"),
            !Camera->IsRegistered() && Camera->HasAnyFlags(RF_Transient));
        Suite.NoDisk();
    }
};
} //namespace P4RowsRemainingOriginalAndFixed272

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RowsRemainingFixedSlotMatrix272, "P4MapKit.Quality.RowsRemainingFixedSlotMatrix272",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RowsRemainingFixedSlotMatrix272::RunTest(const FString&)
{
    using namespace P4RowsRemainingOriginalAndFixed272;
    FN138Suite F(*this, TEXT("RemainingFixedSlotMatrix272"));
    TStrongObjectPtr<UCameraComponent> Camera(
        NewObject<UCameraComponent>(GetTransientPackage(), NAME_None, RF_Transient));
    if (!F.Check(TEXT("Disposable native camera is initialized, unregistered and privately transient"),
            Camera.IsValid() && !Camera->IsRegistered()))
        return false;
    for (int32 I = 0; I < 8; ++I)
        Camera->PostProcessSettings.LensFlareTints[I] = FLinearColor(float(I), .25f, .5f, 1.f);
    FCameraGuard Guard(F, Camera.Get());
    if (!Guard.Ready)
        return false;
    const auto Color = [](int32 I) { return FString::Printf(TEXT("{\"R\":%d.0,\"G\":0.25,\"B\":0.5,\"A\":1.0}"), I); };
    TArray<FString> Colors;
    for (int32 I = 0; I < 8; ++I)
        Colors.Add(Color(I));
    const FString Whole = TEXT("[") + FString::Join(Colors, TEXT(",")) + TEXT("]");
    const FString Control = TEXT("{\"PostProcessSettings\":{\"LensFlareTints\":") + Whole + TEXT("}}");
    FString Canonical, Error;
    if (!F.Check(TEXT("Real complete fixed-color scratch import/control has independent exact native readback"),
            P4PrepareExactPrivateProperties(Camera.Get(), Control, Canonical, Error) && !Canonical.IsEmpty() &&
                Error.IsEmpty()))
        return false;
    Guard.Same();
    TArray<FString> Slots;
    for (int32 I = 0; I < 8; ++I)
        Slots.Add(N138Quote(FString::Printf(TEXT("LensFlareTints[%d]"), I)) + TEXT(":") +
            (I == 7 ? TEXT("\"not-native-struct-fields\"") : Color(I)));
    struct FCase
    {
        FString Input;
        const TCHAR* Error;
    };
    const TArray<FCase> Cases = {{TEXT("{\"PostProcessSettings\":{\"LensFlareTints]\":") + Whole + TEXT("}}"),
                                     TEXT("unknown or nonserialized native field")},
        {TEXT("{\"PostProcessSettings\":{\"LensFlareTints[]\":") + Color(0) + TEXT("}}"),
            TEXT("unknown or nonserialized native field")},
        {TEXT("{\"PostProcessSettings\":{\"LensFlareTints[0]\":") + Color(0) + TEXT(",\"LensFlareTints\":") + Whole +
                TEXT("}}"),
            TEXT("duplicate native field identity or fixed slot")},
        {TEXT("{\"PostProcessSettings\":{") + FString::Join(Slots, TEXT(",")) + TEXT("}}"),
            TEXT("native struct fields required")}};
    for (const FCase& Case : Cases)
    {
        const FString ExactInput = Case.Input;
        Canonical = TEXT("must-clear");
        Error.Empty();
        F.Check(TEXT("Malformed/mixed/invalid native fixed slots refuse whole disposable preparation"),
            !P4PrepareExactPrivateProperties(Camera.Get(), Case.Input, Canonical, Error) && Canonical.IsEmpty() &&
                Error.Contains(Case.Error, ESearchCase::CaseSensitive));
        F.Check(TEXT("Every caller-supplied fixed-slot JSON code unit retained"),
            Case.Input.Equals(ExactInput, ESearchCase::CaseSensitive));
        Guard.Same();
    }
    return F.Good && !HasAnyErrors();
}

// PRIVATE append inside final guard of P4AuthoringRowsInspectionCoverageAutomation.cpp.
// Genuine captured baseline and complete independent custody/native oracles.

// One genuine source-copy workflow, not qualification of new actor defaults.
// Append inside the existing automation guard in InspectionCoverageAutomation.
#include "Editor.h"
#include "AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Serialization/StructuredArchive.h"

namespace P4OriginalPlacement336
{
using namespace P4RowsCoverage138;
const TCHAR* SourcePackage = TEXT("/Game/Carrot4/Maps/Main/Area/Area001/ActorPlacementInfo/AP_Area001_P_Objects");
const TCHAR* ClassPath = TEXT("/Script/Carrot4.ActorPlacementInfoAsset");
const TCHAR* GoldenSha = TEXT("3a2cd236ace68b230dbfd023f5d8c8aee86fc1baf24c006d49ce427ad074d18d");
bool Exact(const FString& A, const FString& B)
{
    return A.Len() == B.Len() &&
        (A.IsEmpty() ||
            FMemory::Memcmp(A.GetCharArray().GetData(), B.GetCharArray().GetData(), SIZE_T(A.Len()) * sizeof(TCHAR)) ==
                0);
}
bool Verdict(const FString& Text)
{
    const auto J = N138Parse(Text);
    bool B = false;
    return J.IsValid() && J->TryGetBoolField(TEXT("valid"), B) && B;
}
// Independent comparison of two supplied graphs, not a copy of native property
// projection. All object keys/strings are exact; integers retain all 64 bits.
bool Graph(const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
{
    if (!A.IsValid() || !B.IsValid() || A->Type != B->Type)
        return false;
    if (A->Type == EJson::Null)
        return true;
    if (A->Type == EJson::String)
        return Exact(A->AsString(), B->AsString());
    if (A->Type == EJson::Boolean)
        return A->AsBool() == B->AsBool();
    if (A->Type == EJson::Number)
    {
        FString X, Y;
        if (!A->TryGetString(X) || !B->TryGetString(Y))
            return false;
        const bool Floating = X.Contains(TEXT(".")) || X.Contains(TEXT("e")) || X.Contains(TEXT("E")) ||
            Y.Contains(TEXT(".")) || Y.Contains(TEXT("e")) || Y.Contains(TEXT("E"));
        if (!Floating)
            return Exact(X, Y);
        double P = 0, Q = 0;
        return A->TryGetNumber(P) && B->TryGetNumber(Q) && FMemory::Memcmp(&P, &Q, sizeof(P)) == 0;
    }
    if (A->Type == EJson::Array)
    {
        const auto& X = A->AsArray();
        const auto& Y = B->AsArray();
        if (X.Num() != Y.Num())
            return false;
        for (int32 I = 0; I < X.Num(); ++I)
            if (!Graph(X[I], Y[I]))
                return false;
        return true;
    }
    if (A->Type == EJson::Object)
    {
        const auto X = A->AsObject(), Y = B->AsObject();
        if (!X.IsValid() || !Y.IsValid() || X->Values.Num() != Y->Values.Num())
            return false;
        for (const auto& P : X->Values)
        {
            const TSharedPtr<FJsonValue>* Match = nullptr;
            for (const auto& Q : Y->Values)
                if (Exact(P.Key, Q.Key))
                {
                    if (Match)
                        return false;
                    Match = &Q.Value;
                }
            if (!Match || !Graph(P.Value, *Match))
                return false;
        }
        return true;
    }
    return false;
}
// The game writes a struct held directly by the asset as its difference from the class default,
// so a member the source file leaves out holds the class default. Array elements are written
// whole and are compared as they are.
TSharedPtr<FJsonObject> WithClassDefaults(
    const TSharedPtr<FJsonObject>& Source, const TSharedPtr<FJsonObject>& Defaults)
{
    auto Out = MakeShared<FJsonObject>();
    Out->Values = Source->Values;
    for (const auto& P : Defaults->Values)
    {
        const TSharedPtr<FJsonValue> Have = Out->TryGetField(P.Key);
        if (!Have.IsValid())
            Out->SetField(P.Key, P.Value);
        else if (Have->Type == EJson::Object && P.Value->Type == EJson::Object)
            Out->SetField(
                P.Key, MakeShared<FJsonValueObject>(WithClassDefaults(Have->AsObject(), P.Value->AsObject())));
    }
    return Out;
}
bool NativeEquals(UObject* Object, const TSharedPtr<FJsonObject>& Expected)
{
    const auto Actual = N138Parse(UP4AuthoringTools::NativeAssetValuesJson(Object));
    const auto Defaults = Object
        ? N138Parse(UP4AuthoringTools::NativeAssetValuesJson(Object->GetClass()->GetDefaultObject()))
        : nullptr;
    return Actual.IsValid() && Expected.IsValid() && Defaults.IsValid() &&
        Graph(
            MakeShared<FJsonValueObject>(Actual), MakeShared<FJsonValueObject>(WithClassDefaults(Expected, Defaults)));
}
bool Whole(UObject* Object, TArray<uint8>& Bytes)
{
    Bytes.Reset();
    FObjectWriter Writer(Bytes);
    Writer.ArNoDelta = true;
    Writer.ArNoIntraPropertyDelta = true;
    Writer.SetForceUnicode(true);
    Writer.SetPortFlags(PPF_DuplicateVerbatim);
    Object->Serialize(Writer);
    return !Writer.IsError() && Bytes.Num() != 0;
}
struct FField
{
    FProperty* Property;
    FDefaultConstructedPropertyElement Copy;
    TArray<uint8> Bytes;
    FField(FProperty* P, UObject* O) : Property(P), Copy(P)
    {
        P->CopyCompleteValue(Copy.GetObjAddress(), P->ContainerPtrToValuePtr<void>(O));
        Capture(Copy.GetObjAddress(), Bytes);
    }
    bool Capture(const void* Value, TArray<uint8>& Out) const
    {
        Out.Reset();
        FObjectWriter Writer(Out);
        Writer.ArNoDelta = true;
        Writer.ArNoIntraPropertyDelta = true;
        Writer.SetForceUnicode(true);
        Writer.SetPortFlags(PPF_DuplicateVerbatim);
        FStructuredArchiveFromArchive Structured(Writer);
        Property->SerializeItem(Structured.GetSlot(), const_cast<void*>(Value), nullptr);
        return !Writer.IsError() && Out.Num() != 0;
    }
    bool Same(UObject* O) const
    {
        TArray<uint8> Actual;
        const void* Value = Property->ContainerPtrToValuePtr<void>(O);
        return Bytes.Num() != 0 && Property->Identical(Value, Copy.GetObjAddress(), PPF_DeepComparison) &&
            Capture(Value, Actual) && Actual == Bytes;
    }
};
struct FStorage
{
    TArray<TUniquePtr<FField>> Fields;
    TArray<uint8> Object, Metadata;
    FString Schema, Identity;
    EObjectFlags Flags, MetadataFlags;
    UMetaData* MetadataObject;
    bool Ready = false;
    UClass* Class = nullptr;
    explicit FStorage(UObject* O)
        : Schema(UP4AuthoringTools::NativeAssetSchema(O)), Identity(O->GetPathName()), Flags(O->GetFlags()),
          MetadataFlags(O->GetOutermost()->GetMetaData()->GetFlags()), MetadataObject(O->GetOutermost()->GetMetaData()),
          Class(O->GetClass())
    {
        Ready = Whole(O, Object) && Whole(O->GetOutermost()->GetMetaData(), Metadata) && !Schema.IsEmpty();
        for (TFieldIterator<FProperty> P(Class); P; ++P)
            if (!P->HasAnyPropertyFlags(CPF_Transient))
            {
                Fields.Add(MakeUnique<FField>(*P, O));
                Ready = Ready && Fields.Last()->Bytes.Num() != 0;
            }
        Ready = Ready && Fields.Num() == 2 && Same(O, true);
    }
    // Saved: Unreal's SavePackage marks the package and every object it wrote as loaded
    // (RF_WasLoaded and RF_LoadCompleted), since they now match the file.
    bool Same(UObject* O, bool WholeObject = false, bool Saved = false) const
    {
        return Difference(O, WholeObject, Saved).IsEmpty();
    }
    // What differs from the state taken at construction, as text. Empty when nothing does.
    FString Difference(UObject* O, bool WholeObject = false, bool Saved = false) const
    {
        const EObjectFlags Loaded = Saved ? (RF_WasLoaded | RF_LoadCompleted) : RF_NoFlags;
        if (!O || O->GetClass() != Class)
            return TEXT(" class;");
        FString Out;
        if (!Exact(Schema, UP4AuthoringTools::NativeAssetSchema(O)))
            Out += TEXT(" schema;");
        for (const auto& P : Fields)
            if (!P->Same(O))
                Out += TEXT(" field ") + P->Property->GetName() + TEXT(";");
        if (!WholeObject)
            return Out;
        TArray<uint8> B, M;
        if (!Exact(O->GetPathName(), Identity))
            Out += TEXT(" path ") + O->GetPathName() + TEXT(";");
        if (O->GetFlags() != (Flags | Loaded))
            Out += FString::Printf(
                TEXT(" object flags %08x, expected %08x;"), uint32(O->GetFlags()), uint32(Flags | Loaded));
        if (!Whole(O, B) || B != Object)
            Out += FString::Printf(TEXT(" object bytes %d, were %d;"), B.Num(), Object.Num());
        if (O->GetOutermost()->GetMetaData() != MetadataObject)
            Out += TEXT(" another metadata object;");
        else
        {
            if (MetadataObject->GetFlags() != (MetadataFlags | Loaded))
                Out += FString::Printf(TEXT(" metadata flags %08x, expected %08x;"), uint32(MetadataObject->GetFlags()),
                    uint32(MetadataFlags | Loaded));
            // Unreal gives a package its localization namespace when it is first saved
            // (TextNamespaceUtil::EnsurePackageNamespace in SavePackage). Everything else is compared.
            const FName Key(TEXT("PackageLocalizationNamespace"));
            FString Namespace;
            const bool Given = Saved && MetadataObject->RootMetaDataMap.RemoveAndCopyValue(Key, Namespace);
            const bool Equal = Whole(MetadataObject, M) && M == Metadata;
            if (Given)
                MetadataObject->RootMetaDataMap.Add(Key, Namespace);
            if (!Equal)
                Out += FString::Printf(TEXT(" metadata bytes %d, were %d;"), M.Num(), Metadata.Num());
            if (Saved && (!Given || Namespace.IsEmpty()))
                Out += TEXT(" no localization namespace after the save;");
        }
        return Out;
    }
};
struct FPackageState
{
    UPackage* Package;
    EObjectFlags ObjectFlags;
    uint32 Flags;
    FGuid Guid, PersistentGuid;
    FName Filename;
    uint64 Size;
    bool Dirty;
    explicit FPackageState(UObject* O)
        : Package(O->GetOutermost()), ObjectFlags(Package->GetFlags()), Flags(Package->GetPackageFlags()),
          Guid(Package->GetGuid()), PersistentGuid(Package->GetPersistentGuid()), Filename(Package->FileName),
          Size(Package->FileSize), Dirty(Package->IsDirty())
    {}
    // What differs from the state taken at construction, as text. Empty when nothing does.
    FString Difference(bool Saved = false) const
    {
        FString Out;
        const EObjectFlags WantObject = Saved ? (ObjectFlags | RF_WasLoaded | RF_LoadCompleted) : ObjectFlags;
        if (Package->GetFlags() != WantObject)
            Out += FString::Printf(
                TEXT(" object flags %08x, expected %08x;"), uint32(Package->GetFlags()), uint32(WantObject));
        if (Package->GetGuid() != Guid)
            Out += TEXT(" guid changed;");
        if (Package->GetPersistentGuid() != PersistentGuid)
            Out += TEXT(" persistent guid changed;");
        const uint32 WantFlags = Saved ? (Flags & ~uint32(PKG_NewlyCreated)) : Flags;
        if (Package->GetPackageFlags() != WantFlags)
            Out += FString::Printf(
                TEXT(" package flags %08x, expected %08x;"), uint32(Package->GetPackageFlags()), WantFlags);
        const FName WantFile = Saved ? FName(*Package->GetName()) : Filename;
        if (Package->FileName != WantFile)
            Out += TEXT(" file name ") + Package->FileName.ToString() + TEXT(", expected ") + WantFile.ToString() +
                TEXT(";");
        if (!(Saved ? Package->FileSize > 0 : Package->FileSize == Size))
            Out += FString::Printf(TEXT(" file size %llu, was %llu;"), uint64(Package->FileSize), Size);
        if (Package->IsDirty() != (Saved ? false : Dirty))
            Out += TEXT(" dirty state;");
        return Out;
    }
    bool Same(bool Saved = false) const { return Difference(Saved).IsEmpty(); }
};
bool FileHash(const FString& Path, const TCHAR* Expected)
{
    int64 N = 0;
    FString H;
    return P4HashFile(Path, 4 * 1024 * 1024, N, H) && N > 0 && Exact(H, Expected);
}
TSharedPtr<FJsonObject> Golden(const FString& Root)
{
    const FString Path = Root / TEXT("rows-property-after336/placement-independent-values.json");
    FString Text;
    return FileHash(Path, GoldenSha) && FFileHelper::LoadFileToString(Text, *Path) ? N138Parse(Text) : nullptr;
}
void PrescribeEdit(const TSharedPtr<FJsonObject>& J)
{
    J->GetArrayField(TEXT("ActorGeneratorList"))[0]->AsObject()->SetNumberField(TEXT("OriginalPhysicsRadiusZ"), 32.5);
}
bool Disk(const FString& Package, TMap<FString, TArray<uint8>>& Out)
{
    const FString Base = FPackageName::LongPackageNameToFilename(Package);
    Out.Reset();
    for (const TCHAR* S : {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl"), TEXT(".m.ubulk")})
    {
        const FString Path = Base + S;
        if (IFileManager::Get().FileExists(*Path))
            if (!FFileHelper::LoadFileToArray(Out.Add(Path), *Path) || Out.FindChecked(Path).Num() == 0)
                return false;
    }
    return Out.Num() != 0 && Out.Contains(Base + TEXT(".uasset"));
}
bool NoDisk(const FString& Package)
{
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(FPackageName::LongPackageNameToFilename(Package) + TEXT(".*")), true, false);
    return Files.Num() == 0;
}
bool SourceSame(
    const FString& Header, const TArray<uint8>& H, const TArray<uint8>& B, const TMap<FString, TArray<uint8>>& Custody)
{
    TArray<uint8> Now;
    if (!FFileHelper::LoadFileToArray(Now, *Header) || Now != H ||
        !FFileHelper::LoadFileToArray(Now, *FPaths::ChangeExtension(Header, TEXT("uexp"))) || Now != B)
        return false;
    for (const auto& P : Custody)
        if (!FFileHelper::LoadFileToArray(Now, *P.Key) || Now != P.Value)
            return false;
    return true;
}
} //namespace P4OriginalPlacement336

#endif
