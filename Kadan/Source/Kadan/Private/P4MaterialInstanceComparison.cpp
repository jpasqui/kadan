#include "P4MaterialInstanceComparison.h"
#include "P4Common.h"
#include "P4MaterialInstanceGameSettings.h"
#include "P4CreatorSource.h"
#include "P4KadanImportSources.h"
#include "P4MaterialGraph.h"
#include "P4MaterialRecovery.h"
#include "P4NativeHash.h"
#include "P4OriginalRecord.h"
#include "P4OriginalImportSave.h"
#include "P4KadanReimportAsset.h"
#include "P4LevelContent.h"
#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/material_instance_binding_comparison.h"
THIRD_PARTY_INCLUDES_END

namespace
{
using Value = P4MapKit::FDecodedValue;
Value Word(Value::EKind Kind, const FString& Text)
{
    return Value::Atom(Kind, TCHAR_TO_UTF8(*Text));
}
Value Guid(const FGuid& Id)
{
    Value Out;
    Out.Fields.emplace_back("A", Value::Atom(Value::EKind::Number, std::to_string(int32(Id.A))));
    Out.Fields.emplace_back("B", Value::Atom(Value::EKind::Number, std::to_string(int32(Id.B))));
    Out.Fields.emplace_back("C", Value::Atom(Value::EKind::Number, std::to_string(int32(Id.C))));
    Out.Fields.emplace_back("D", Value::Atom(Value::EKind::Number, std::to_string(int32(Id.D))));
    return Out;
}

bool Frame(const char* Group, const TArray<FMaterialParameterInfo>& Infos, const TArray<FGuid>& Ids,
    std::vector<P4MapKit::Material::MaterialInstanceParentBinding>& Out, FString& Error)
{
    if (Infos.Num() != Ids.Num())
    {
        Error = TEXT("MIC parent parameter/GUID frame count differs");
        return false;
    }
    for (int32 I = 0; I < Infos.Num(); ++I)
    {
        const auto& Info = Infos[I];
        if (Info.Association != EMaterialParameterAssociation::GlobalParameter || Info.Index != INDEX_NONE ||
            !Ids[I].IsValid())
        {
            Error = TEXT("MIC parent frame has unsupported association/layer");
            return false;
        }
        P4MapKit::Material::MaterialInstanceParentBinding Binding;
        Binding.Group = Group;
        Binding.NameBase = TCHAR_TO_UTF8(*Info.Name.GetPlainNameString());
        Binding.NameNumber = Info.Name.GetNumber();
        Binding.Info.Fields.emplace_back("Name", Word(Value::EKind::String, Info.Name.ToString()));
        Binding.Info.Fields.emplace_back("Association", Value::Atom(Value::EKind::String, "GlobalParameter"));
        Binding.Info.Fields.emplace_back("Index", Value::Atom(Value::EKind::Number, "-1"));
        Binding.Guid = Guid(Ids[I]);
        Out.push_back(MoveTemp(Binding));
    }
    return true;
}
} //namespace

namespace
{
const TCHAR* NativeMICSchema = TEXT("material-instance-native-state-v1");
const TCHAR* NativeMICSource = TEXT("P4MaterialInstanceNativeSourceV1");
const TCHAR* MICTransport = TEXT("P4MaterialInstanceTransportV2");
bool Hex32(const FString& Text)
{
    if (Text.Len() != 32)
        return false;
    for (TCHAR C : Text)
        if (!(C >= TEXT('0') && C <= TEXT('9')) && !(C >= TEXT('a') && C <= TEXT('f')))
            return false;
    return true;
}
} //namespace

FString P4MaterialInstanceComparisonState(UMaterialInstanceConstant* Instance, FString& Error,
    FP4MaterialInstanceNativeEvidence* Evidence, TArray<FString>* NativeLines)
{
    Error.Reset();
    if (Evidence)
        *Evidence = FP4MaterialInstanceNativeEvidence();
    if (NativeLines)
        NativeLines->Reset();
    const auto Fail = [&](const TCHAR* Why)
    {
        Error = Why;
        return FString();
    };
    FString Origin;
    if (!IsInGameThread() || !IsValid(Instance) || Instance->GetClass() != UMaterialInstanceConstant::StaticClass() ||
        Instance->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects | RF_NeedInitialization) ||
        P4HasCreatorSource(Instance) || P4HasCopiedGameData(Instance) || P4HasIntrinsicGameProxy(Instance) ||
        !P4HasIntrinsicGameSource(Instance, &Origin) ||
        !P4ExactNativeString(Origin, Instance->GetOutermost()->GetName()))
        return Fail(TEXT("material-instance-native-source-origin-not-qualified"));
    UMetaData* Meta = Instance->GetOutermost()->GetMetaData();
    const FString Custody = Meta->GetValue(Instance, NativeMICSource);
    TSharedPtr<FJsonObject> Source;
    FString Package, Class, Sha, Recipe, Owner;
    double Format = 0;
    if (!P4ParseJson(Custody, Source) || !Source->TryGetNumberField(TEXT("format"), Format) || Format != 1 ||
        !Source->TryGetStringField(TEXT("owner"), Owner) ||
        !P4ExactNativeString(Owner, TEXT("p4mapkit-recovered-material-instance")) ||
        !Source->TryGetStringField(TEXT("package"), Package) || !P4ExactNativeString(Package, Origin) ||
        !Source->TryGetStringField(TEXT("class"), Class) ||
        !P4ExactNativeString(Class, Instance->GetClass()->GetPathName()) ||
        !Source->TryGetStringField(TEXT("header_body_sha256"), Sha) || !P4IsSha256Hex(Sha) ||
        !Source->TryGetStringField(TEXT("recipe_md5"), Recipe) || !Hex32(Recipe))
        return Fail(TEXT("material-instance-native-source-custody-unreadable"));
    FP4MaterialInstanceNativeEvidence Current;
    Current.SourceSha256 = Sha;
    Current.RecipeMd5 = Recipe;
    // WindowsNoEditor cooking excludes UMetaData, including OriginalRecord.
    // This is the existing native object-lines contract, without new omissions.
    Current.NativeDigest = NativeLines ? P4OriginalObjectAssetStateAndLines(Instance, *NativeLines)
                                       : P4OriginalObjectAssetState(Instance);
    Current.HasTransport = Meta->HasValue(Instance, MICTransport);
    const FString Transport = P4MaterialInstanceComparisonTransport(Instance, Meta->GetValue(Instance, MICTransport));
    Current.TransportLength = Transport.Len();
    Current.HasCustody = Meta->HasValue(Instance, NativeMICSource);
    Current.CustodyLength = Custody.Len();
    if (!P4IsSha256Hex(Current.NativeDigest) || !P4HashUtf8(Transport, Current.TransportDigest) ||
        !P4HashUtf8(Custody, Current.CustodyDigest))
        return Fail(TEXT("material-instance-native-state-unreadable"));
    FString Out;
    const FString Frame = FString(NativeMICSchema) + TEXT("\nnative:") + Current.NativeDigest +
        TEXT("\ntransport-present:") + (Current.HasTransport ? TEXT("1") : TEXT("0")) + TEXT("\ntransport:") +
        Current.TransportDigest + TEXT("\nsource-custody:") + Current.CustodyDigest;
    if (!P4HashUtf8(Frame, Out))
        return Fail(TEXT("material-instance-native-state-hash-failed"));
    if (Evidence)
        *Evidence = MoveTemp(Current);
    return Out;
}

FP4RecoveredMaterialInstanceProof::FP4RecoveredMaterialInstanceProof(const FString& InPackage, const FString& InRecipe,
    FP4KadanSourceBinding&& InSource, const TFunction<bool(FString&)>& InCurrentReads)
    : Package(InPackage), Recipe(InRecipe), Source(MoveTemp(InSource)), CurrentReads(InCurrentReads)
{}

FString FP4RecoveredMaterialInstanceProof::SourceObjectName() const
{
    const auto Inventory = P4MapKit::ReadPackage(
        std::vector<uint8>(Source.Header.GetData(), Source.Header.GetData() + Source.Header.Num()),
        std::vector<uint8>(Source.Body.GetData(), Source.Body.GetData() + Source.Body.Num()));
    std::string Name;
    return Inventory.Error.Code.empty() && Inventory.Reference(1, Name) ? FString(UTF8_TO_TCHAR(Name.c_str()))
                                                                        : FString();
}

bool FP4RecoveredMaterialInstanceProof::Current(
    const FString& InstanceJson, const FString& Destination, FString& Error) const
{
    Error.Reset();
    const auto Fail = [&](const TCHAR* Why)
    {
        Error = Why;
        return false;
    };
    if (!IsInGameThread() || !P4ImportPublication::IsImporting() || !CurrentReads ||
        !P4ExactNativeString(Package, Destination) || !P4ExactNativeString(Recipe, InstanceJson) ||
        !P4ExactNativeString(Source.Package, Package) || !P4IsSha256Hex(Source.SourceDigest))
        return Fail(TEXT("material-instance-native-build-authority-not-qualified"));
    const auto Inventory = P4MapKit::ReadPackage(
        std::vector<uint8>(Source.Header.GetData(), Source.Header.GetData() + Source.Header.Num()),
        std::vector<uint8>(Source.Body.GetData(), Source.Body.GetData() + Source.Body.Num()));
    std::string Class, Name;
    if (!Inventory.Error.Code.empty() || Inventory.Package != TCHAR_TO_UTF8(*Package) || Inventory.Exports.empty() ||
        Inventory.Exports[0].Outer || !Inventory.Reference(Inventory.Exports[0].Class, Class) ||
        Class != "/Script/Engine.MaterialInstanceConstant" || !Inventory.Reference(1, Name) ||
        !FString(UTF8_TO_TCHAR(Name.c_str()))
            .Equals(FPackageName::GetLongPackageAssetName(Package), ESearchCase::IgnoreCase))
        return Fail(TEXT("material-instance-native-current-raw-class-or-owner-differs"));
    // Inventory parsing above uses this proof's private immutable snapshot and
    // makes no editor calls. Validate the current files/family once afterwards.
    return CurrentReads(Error) && P4KadanSourceBindingCurrent(Source, Error);
}

bool FP4RecoveredMaterialInstanceProof::CaptureFresh(
    UMaterialInstanceConstant* Instance, const FString& InstanceJson, FP4OriginalRecord& Record, FString& Error) const
{
    if (!Current(InstanceJson, Record.Package, Error))
        return false;
    const auto Fail = [&](const TCHAR* Why)
    {
        Error = Why;
        return false;
    };
    if (!IsValid(Instance) || Instance->GetClass() != UMaterialInstanceConstant::StaticClass() ||
        !P4ExactNativeString(Instance->GetOutermost()->GetName(), Package) ||
        !P4ExactNativeString(Instance->GetName(), SourceObjectName()) ||
        (FPackageName::DoesPackageExist(Package) && !P4KadanOwnsReimportSlot(Package)) ||
        Instance->GetOutermost()->GetMetaData()->HasValue(Instance, TEXT("P4OriginalRecord")) ||
        Instance->GetOutermost()->GetMetaData()->HasValue(Instance, NativeMICSource) || P4HasCreatorSource(Instance) ||
        !P4ExactNativeString(Record.SourceDigest, FMD5::HashAnsiString(*Recipe)))
        return Fail(TEXT("material-instance-native-capture-needs-fresh-owned-source-build"));
    if (!P4RecordKnownGameSource(Instance, Package, false))
        return Fail(TEXT("material-instance-native-source-origin-not-ready"));
    auto Custody = MakeShared<FJsonObject>();
    Custody->SetNumberField(TEXT("format"), 1);
    Custody->SetStringField(TEXT("owner"), TEXT("p4mapkit-recovered-material-instance"));
    Custody->SetStringField(TEXT("package"), Package);
    Custody->SetStringField(TEXT("class"), Instance->GetClass()->GetPathName());
    Custody->SetStringField(TEXT("header_body_sha256"), Source.SourceDigest);
    Custody->SetStringField(TEXT("recipe_md5"), Record.SourceDigest);
    FString Text;
    if (!FJsonSerializer::Serialize(
            Custody, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)))
        return Fail(TEXT("material-instance-native-source-custody-write-failed"));
    Instance->GetOutermost()->GetMetaData()->SetValue(Instance, NativeMICSource, *Text);
    Record.ComparisonSchema = NativeMICSchema;
    Record.ReadOnly = true;
    const bool Trace = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_REIMPORT_TRACE")) == TEXT("1") &&
        P4ExactNativeString(
            FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MI_ADMISSION_WITNESS_PACKAGE")), Package);
    FP4MaterialInstanceNativeEvidence Captured;
    Record.ComparisonDigest = P4MaterialInstanceComparisonState(Instance, Error, Trace ? &Captured : nullptr);
    if (Trace)
        UE_LOG(LogTemp, Display,
            TEXT(
                "MIC native components capture %s: schema=%s composite=%s native=%s transport-present=%d transport-length=%d transport=%s custody-present=%d custody-length=%d custody=%s source=%s recipe=%s error=%s"),
            *Package, NativeMICSchema, *Record.ComparisonDigest, *Captured.NativeDigest, Captured.HasTransport,
            Captured.TransportLength, *Captured.TransportDigest, Captured.HasCustody, Captured.CustodyLength,
            *Captured.CustodyDigest, *Captured.SourceSha256, *Captured.RecipeMd5, *Error);
    return !Record.ComparisonDigest.IsEmpty() && Error.IsEmpty() && Current(InstanceJson, Package, Error);
}

bool P4ProjectOriginalMaterialInstanceBindings(UObject* Asset, TArray<FString>& Lines, FString& Error)
{
    Error.Reset();
    if (!IsInGameThread() || !IsValid(Asset) || Asset->GetClass() != UMaterialInstanceConstant::StaticClass())
        return false;
    FP4OriginalRecord Record;
    if (!P4ReadOriginalRecord(Asset, Record, Error))
        return false;
    if (!P4KadanHasBoundSource(Record.Package))
    {
        Error.Reset();
        return false;
    }
    const auto Refuse = [&](const TCHAR* Why)
    {
        Error = Why;
        return false;
    };
    FString Origin;
    if (Record.Version != 2 || !P4ExactNativeString(Record.Package, Asset->GetOutermost()->GetName()) ||
        !P4ExactNativeString(Record.Class, TEXT("/Script/Engine.MaterialInstanceConstant")) ||
        !P4ExactNativeString(Record.Kind, TEXT("object")) ||
        !P4ExactNativeString(Record.SourceDigestKind, TEXT("header-body-sha256-v1")) ||
        !P4ExactNativeString(Record.ComparisonSchema, TEXT("object-lines-v1")) ||
        !P4ExactNativeString(Record.Fidelity, TEXT("exact")) || !Record.ReadOnly ||
        !P4IsSha256Hex(Record.SourceDigest) || !P4IsSha256Hex(Record.ComparisonDigest) || P4HasCreatorSource(Asset) ||
        !P4HasIntrinsicGameSource(Asset, &Origin) || !P4ExactNativeString(Origin, Record.Package) ||
        P4HasIntrinsicGameProxy(Asset))
        return Refuse(TEXT("MIC original source/comparison custody is incomplete"));
    auto* Instance = CastChecked<UMaterialInstanceConstant>(Asset);
    UMaterial* Parent = Cast<UMaterial>(Instance->Parent);
    if (!Parent)
        return Refuse(TEXT("MIC binding projection requires its direct recovered material parent"));
    if (!P4KadanHasBoundSource(Parent->GetOutermost()->GetName()))
        return false;
    FP4OriginalRecord ParentRecord;
    if (!P4ReadOriginalRecord(Parent, ParentRecord, Error))
        return false;
    if (ParentRecord.Version != 2 || !P4ExactNativeString(ParentRecord.Package, Parent->GetOutermost()->GetName()) ||
        !P4ExactNativeString(ParentRecord.Class, TEXT("/Script/Engine.Material")) ||
        !P4ExactNativeString(ParentRecord.Kind, TEXT("material")) ||
        !P4ExactNativeString(ParentRecord.SourceDigestKind, TEXT("material-graph-v2-md5")) ||
        !P4ExactNativeString(ParentRecord.ComparisonSchema, TEXT("material-native-state-v1")) ||
        !ParentRecord.ReadOnly || !ParentRecord.ReferenceOnly ||
        !P4ExactNativeString(ParentRecord.Fidelity, TEXT("recovered-pixel-contract")) ||
        !P4IsSha256Hex(ParentRecord.ComparisonDigest) || P4HasCreatorSource(Parent) ||
        !P4HasIntrinsicGameSource(Parent, &Origin) || !P4ExactNativeString(Origin, ParentRecord.Package) ||
        P4HasIntrinsicGameProxy(Parent) || Parent->GetOutermost()->IsDirty())
        return Refuse(TEXT("MIC parent recovered-material custody differs"));
    const FString ParentState = P4MaterialComparisonState(Parent, Error);
    if (ParentState.IsEmpty() || !P4ExactNativeString(ParentState, ParentRecord.ComparisonDigest))
        return Refuse(TEXT("MIC recovered parent native graph differs"));
    FP4KadanSourceBinding Source, ParentSource;
    if (!P4KadanReadBoundSource(Record.Package, Source, Error) ||
        !P4ExactNativeString(Source.SourceDigest, Record.SourceDigest) ||
        !P4KadanReadBoundSource(ParentRecord.Package, ParentSource, Error) ||
        !P4KadanSourceBindingCurrent(Source, Error) || !P4KadanSourceBindingCurrent(ParentSource, Error))
    {
        if (Error.IsEmpty())
            Error = TEXT("MIC bound original source digest differs");
        return false;
    }
    const auto ParentInventory = P4MapKit::ReadPackage(
        std::vector<uint8>(ParentSource.Header.GetData(), ParentSource.Header.GetData() + ParentSource.Header.Num()),
        std::vector<uint8>(ParentSource.Body.GetData(), ParentSource.Body.GetData() + ParentSource.Body.Num()));
    std::string ParentClass, ParentName;
    if (!ParentInventory.Error.Code.empty() || ParentInventory.Package != TCHAR_TO_UTF8(*ParentRecord.Package) ||
        ParentInventory.Exports.empty() || ParentInventory.Exports[0].Outer ||
        !ParentInventory.Reference(ParentInventory.Exports[0].Class, ParentClass) ||
        ParentClass != "/Script/Engine.Material" || !ParentInventory.Reference(1, ParentName) ||
        std::string(TCHAR_TO_UTF8(*Parent->GetPathName())) != ParentInventory.Package + "." + ParentName)
        return Refuse(TEXT("MIC bound parent source class/owner differs"));
    // An unchanged live graph does not authenticate today's same-package raw
    // file. Only a genuine already recovered merged-family read witness whose
    // recipe matches the existing parent's identity can bridge those facts.
    if (!P4CurrentRecoveredMaterialParentRecipe(
            ParentRecord.Package, ParentSource.SourceDigest, ParentRecord.SourceDigest, Error))
        return false;
    std::vector<P4MapKit::Material::MaterialInstanceParentBinding> Bindings;
    TArray<FMaterialParameterInfo> Infos;
    TArray<FGuid> Ids;
    Parent->GetAllScalarParameterInfo(Infos, Ids);
    if (!Frame("ScalarParameterValues", Infos, Ids, Bindings, Error))
        return false;
    Infos.Reset();
    Ids.Reset();
    Parent->GetAllVectorParameterInfo(Infos, Ids);
    if (!Frame("VectorParameterValues", Infos, Ids, Bindings, Error))
        return false;
    Infos.Reset();
    Ids.Reset();
    Parent->GetAllTextureParameterInfo(Infos, Ids);
    if (!Frame("TextureParameterValues", Infos, Ids, Bindings, Error))
        return false;
    std::vector<std::string> Current, Projected;
    for (const FString& Line : Lines)
        Current.emplace_back(TCHAR_TO_UTF8(*Line));
    std::string Problem;
    std::size_t Changed = 0;
    if (!P4MapKit::Material::ProjectOriginalMaterialInstanceBindingLines(TCHAR_TO_UTF8(*Record.Package),
            TCHAR_TO_UTF8(*Record.SourceDigest),
            std::vector<uint8>(Source.Header.GetData(), Source.Header.GetData() + Source.Header.Num()),
            std::vector<uint8>(Source.Body.GetData(), Source.Body.GetData() + Source.Body.Num()),
            TCHAR_TO_UTF8(*Parent->GetPathName()), Bindings, Current, Projected, Changed, Problem))
    {
        Error = UTF8_TO_TCHAR(Problem.c_str());
        return false;
    }
    if (!P4KadanSourceBindingCurrent(Source, Error) || !P4KadanSourceBindingCurrent(ParentSource, Error))
        return false;
    TArray<FString> Finished;
    for (const auto& Line : Projected)
        Finished.Add(UTF8_TO_TCHAR(Line.c_str()));
    Lines = MoveTemp(Finished);
    return true;
}
