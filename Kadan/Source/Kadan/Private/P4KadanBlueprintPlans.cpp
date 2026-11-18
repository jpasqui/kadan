#include "P4KadanBlueprintPlans.h"
#include "P4GamePaths.h"
#include "P4ImportIdentityMemory.h"
#include "P4KadanImport.h"
#include "P4KadanImportSources.h"
#include "P4KadanAcquisitionVeto.h"
#include "P4NativeHash.h"
#include "P4OriginalZenSource.h"
#include "P4RegistryInputLease.h"
#include "P4NativeBlueprintReflection.h"
#include "P4NativeBlueprintReflectionPlan.h"
// The portable planner is plain C++ with names that shadow each other, as Unreal's warnings count it.
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/blueprint_native_plan.h"
THIRD_PARTY_INCLUDES_END
#include "HAL/FileManager.h"
#include "Hash/CityHash.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include <memory>
#include <new>
#include <stdexcept>

namespace
{
using P4MapKit::Blueprint::J;
#if P4_KADAN_TEST_SEAMS
FP4KadanBlueprintWrappedSourceControl* BlueprintWrappedSourceControl = nullptr;
#endif

struct FBlueprintPlanSourceError : std::runtime_error
{
    FString Package;
    FBlueprintPlanSourceError(const FString& InPackage, const FString& Why)
        : std::runtime_error(TCHAR_TO_UTF8(*Why)), Package(InPackage)
    {}
};
struct FBlueprintPlanBoundSource
{
    FP4KadanSourceBinding Binding;
    TSharedPtr<FP4KadanSourceAsset> Descriptor;
    FString Kind;
    TUniquePtr<P4RegistryLease::FInput> HeaderLease, BodyLease;
    void RequireCurrent() const
    {
        // Binding was parsed/hashed from these exact files while the leases
        // were held. Rechecking their immutable identities needs no reread.
        if (!Descriptor || !P4ExactNativeString(Descriptor->Package, Binding.Package) ||
            !P4ExactNativeString(Descriptor->Kind, Kind) ||
            !FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(Descriptor->Header), Binding.HeaderPath) ||
            !FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(Descriptor->Body), Binding.BodyPath) ||
            !HeaderLease || !BodyLease || !HeaderLease->Current() || !BodyLease->Current())
            throw FBlueprintPlanSourceError(Binding.Package, TEXT("Blueprint source file custody changed"));
    }
};
struct FBlueprintPlanInputs
{
    TMap<FString, TSharedPtr<FP4KadanSourceAsset>> Descriptors;
    TMap<FString, TSharedPtr<FBlueprintPlanBoundSource>> Loaded;
    FString FailedPackage, FailedReason;
    explicit FBlueprintPlanInputs(const TMap<FString, TSharedPtr<FP4KadanSourceAsset>>& In) : Descriptors(In) {}
    void RecordFailure(const FBlueprintPlanSourceError& Why)
    {
        FailedPackage = Why.Package;
        FailedReason = UTF8_TO_TCHAR(Why.what());
    }
    TSharedPtr<FBlueprintPlanBoundSource> Read(const FString& Package)
    {
        try
        {
            return ReadOne(Package);
        }
        catch (const FBlueprintPlanSourceError& Why)
        {
            RecordFailure(Why);
            throw;
        }
    }
    TSharedPtr<FBlueprintPlanBoundSource> ReadOne(const FString& Package)
    {
        const auto* Descriptor = Descriptors.Find(Package);
        if (!Descriptor)
            return nullptr;
        if (const auto* Cached = Loaded.Find(Package))
        {
            (*Cached)->RequireCurrent();
            return *Cached;
        }
        auto Source = MakeShared<FBlueprintPlanBoundSource>();
        Source->Binding.Package = Package;
        Source->Descriptor = *Descriptor;
        Source->Kind = (*Descriptor)->Kind;
        Source->HeaderLease = MakeUnique<P4RegistryLease::FInput>();
        Source->BodyLease = MakeUnique<P4RegistryLease::FInput>();
#if PLATFORM_WINDOWS
        if (!Source->HeaderLease->Open(std::wstring(*FPaths::ConvertRelativePathToFull((*Descriptor)->Header))) ||
            !Source->BodyLease->Open(std::wstring(*FPaths::ConvertRelativePathToFull((*Descriptor)->Body))))
            throw FBlueprintPlanSourceError(Package, TEXT("Blueprint source cannot be held immutable"));
#else
        throw FBlueprintPlanSourceError(
            Package, TEXT("Blueprint immutable source custody is unavailable on this platform"));
#endif
        FString Error;
        if (!P4KadanReadBoundSource(Package, Source->Binding, Error))
            throw FBlueprintPlanSourceError(Package, Error);
        Source->RequireCurrent();
        Loaded.Add(Package, Source);
        return Source;
    }
    void RequireNoFailure() const
    {
        // A core decoder may catch a loader exception as unsupported syntax.
        // A real source-read failure must still prevent that admission.
        if (!FailedReason.IsEmpty())
            throw FBlueprintPlanSourceError(FailedPackage, FailedReason);
    }
    void RequireCurrent() const
    {
        RequireNoFailure();
        for (const auto& Source : Loaded)
            Source.Value->RequireCurrent();
    }
    void AcknowledgeFailure(const FString& Package)
    {
        if (P4ExactNativeString(FailedPackage, Package))
        {
            FailedPackage.Reset();
            FailedReason.Reset();
        }
    }
};
FString KadanText(const std::string& In)
{
    return UTF8_TO_TCHAR(In.c_str());
}
std::string KadanUtf8(const FString& In)
{
    return std::string(TCHAR_TO_UTF8(*In));
}
bool WriteText(const FString& File, const std::string& Content)
{
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    TArray<uint8> Bytes;
    Bytes.Append(reinterpret_cast<const uint8*>(Content.data()), int32(Content.size()));
    return FFileHelper::SaveArrayToFile(Bytes, *File);
}
std::uint64_t HashIdentityName(const std::string& Normalized)
{
    const FString Name(UTF8_TO_TCHAR(Normalized.c_str()));
    return std::uint64_t(CityHash64(reinterpret_cast<const char*>(*Name), Name.Len() * sizeof(TCHAR)));
}
// A package whose imports retoc could not name has them named from the original Zen package.
std::shared_ptr<const P4MapKit::Blueprint::FVerifiedImportIdentities> ReadIdentities(const FString& Package,
    const std::vector<std::uint8_t>& Header, const std::vector<std::uint8_t>& Body,
    const FP4KadanImportSettings& Settings, FP4CurrentGameRegistryPtr& SharedReader,
    FP4OriginalZenSourceBatch& OriginalBatch, FP4OriginalZenSourcePtr& Original,
    const TSharedPtr<FBlueprintPlanBoundSource>& Source, bool& ReadOriginal,
    const TMap<FString, TSharedPtr<FP4KadanSourceAsset>>& Converted)
{
    using namespace P4MapKit::Blueprint;
    ReadOriginal = false;
    const auto Inventory = P4MapKit::ReadPackage(Header, Body);
    if (!Inventory.Error.Code.empty())
        throw P4MapKit::Blueprint::Error(Inventory.Error.Code);
    bool Unresolved = false;
    for (std::size_t I = 0; I < Inventory.Imports.size(); ++I)
    {
        std::string Path;
        if (Inventory.Imports[I].Name == "UnknownExport" && Inventory.Reference(-std::int32_t(I) - 1, Path) &&
            Path == "/Engine/UnknownPackage.UnknownExport")
        {
            Unresolved = true;
            break;
        }
    }
    if (Unresolved && (!Settings.GamePaks.IsEmpty() || !Settings.Retoc.IsEmpty()))
    {
        if (Settings.GamePaks.IsEmpty() || Settings.Retoc.IsEmpty() || Settings.Repak.IsEmpty())
            throw FBlueprintPlanSourceError(Package,
                TEXT("import-identities-original-tools-missing (selected containers, retoc and repak required)"));
        const std::string SourcePackage = KadanUtf8(Package);
        ImportIdentityDetail::Need(
            ImportIdentityDetail::PackagePath(SourcePackage) && SourcePackage.compare(0, 6, "/Game/") == 0,
            "source-package");
        FP4ExportContainerTools Tools;
        Tools.Retoc = Settings.Retoc;
        Tools.Repak = Settings.Repak;
        FString Error;
#if P4_KADAN_TEST_SEAMS
        if (!SharedReader && !P4KadanSourceAcquisitionAllowed({Package}, Error))
            throw FBlueprintPlanSourceError(Package, Error);
#endif
        if ((SharedReader && !SharedReader->IsCurrent(Tools, Settings.GamePaks, Error)) ||
            (!SharedReader &&
                !P4AcquireCurrentGameRegistry(Tools, Settings.GamePaks,
                    FPaths::ProjectSavedDir() / TEXT("Kadan/BlueprintRegistry"), SharedReader, Error)))
            throw FBlueprintPlanSourceError(Package, Error);
        Source->RequireCurrent();
        if (!OriginalBatch.Acquire(SharedReader, Tools, Settings.GamePaks, Package,
                FPaths::ProjectSavedDir() / TEXT("Kadan/OriginalZenSources"), Original, Error))
            throw FBlueprintPlanSourceError(Package, Error);
        std::shared_ptr<const FVerifiedImportIdentities> Verified;
        if (!Original->Read(
                [&](const TArray<uint8>& Raw, const TArray<uint8>& Metadata, FString&) -> bool
                {
                    Source->RequireCurrent();
                    auto Value = RecoverImportIdentities(Header, Body,
                        std::vector<uint8>(Raw.GetData(), Raw.GetData() + Raw.Num()),
                        std::vector<uint8>(Metadata.GetData(), Metadata.GetData() + Metadata.Num()), SourcePackage,
                        HashIdentityName,
                        [&Converted](const std::string& Imported)
                        {
                            // The export names of a package this one imports, from its converted header.
                            std::vector<std::string> Out;
                            const TSharedPtr<FP4KadanSourceAsset>* Found =
                                Converted.Find(FString(UTF8_TO_TCHAR(Imported.c_str())));
                            TArray<uint8> Bytes;
                            if (!Found || !Found->IsValid() ||
                                !FFileHelper::LoadFileToArray(Bytes, *(*Found)->Header, FILEREAD_Silent))
                                return Out;
                            const int64 BodySize = IFileManager::Get().FileSize(*(*Found)->Body);
                            const auto Inventory = P4MapKit::ReadPackageHeader(
                                std::vector<std::uint8_t>(Bytes.GetData(), Bytes.GetData() + Bytes.Num()),
                                std::size_t(FMath::Max<int64>(BodySize, 0)));
                            if (!Inventory.Error.Code.empty())
                                return Out;
                            for (const auto& Export : Inventory.Exports)
                                Out.push_back(Export.Name);
                            return Out;
                        });
                    Verified = std::make_shared<const FVerifiedImportIdentities>(std::move(Value));
                    // The class-default proof reads these converted bytes again later in the run.
                    P4RememberImportIdentities(Package, Header, Body, Verified->Imports());
                    Source->RequireCurrent();
                    return true;
                },
                Error))
            throw FBlueprintPlanSourceError(Package, Error);
        ReadOriginal = true;
        return Verified;
    }
    return {};
}
} //namespace

#if P4_KADAN_TEST_SEAMS
FP4KadanBlueprintWrappedSourceControl::FP4KadanBlueprintWrappedSourceControl(
    const FString& InRoot, const FString& InDependency)
    : RootPackage(InRoot), DependencyPackage(InDependency)
{
    check(IsInGameThread());
    Previous = BlueprintWrappedSourceControl;
    BlueprintWrappedSourceControl = this;
}
FP4KadanBlueprintWrappedSourceControl::~FP4KadanBlueprintWrappedSourceControl()
{
    check(IsInGameThread() && BlueprintWrappedSourceControl == this);
    BlueprintWrappedSourceControl = Previous;
}
void* P4BeginWrappedBlueprintSource(const FString& Root, const FString& Dependency)
{
    return new FP4KadanBlueprintWrappedSourceControl(Root, Dependency);
}
void P4EndWrappedBlueprintSource(void* Scope)
{
    delete static_cast<FP4KadanBlueprintWrappedSourceControl*>(Scope);
}
int32 P4WrappedBlueprintSourceFailures(const void* Scope)
{
    return static_cast<const FP4KadanBlueprintWrappedSourceControl*>(Scope)->WrappedFailures();
}
#endif

TSet<FString> P4KadanBlueprintReferenceClosure(
    const TMap<FString, TArray<FString>>& Required, FP4KadanBlueprintPlans& InOut)
{
    TSet<FString> Excluded;
    for (bool Changed = true; Changed;)
    {
        Changed = false;
        for (const auto& Candidate : Required)
        {
            FString Missing;
            bool SourceFailure = false;
            for (const FString& Dependency : Candidate.Value)
                if (InOut.Refused.Contains(Dependency))
                {
                    Missing = Dependency;
                    if (!InOut.ReferenceOnlyReasons.Contains(Dependency))
                    {
                        SourceFailure = true;
                        break;
                    }
                }
            if (Missing.IsEmpty())
                continue;
            if (Excluded.Contains(Candidate.Key) && (!SourceFailure || InOut.SourceProblems.Contains(Candidate.Key)))
                continue;
            const FString Reason = TEXT("Its required Blueprint remains the game's original: ") + Missing;
            Excluded.Add(Candidate.Key);
            InOut.Refused.Add(Candidate.Key, Reason);
            if (SourceFailure)
            {
                InOut.ReferenceOnlyReasons.Remove(Candidate.Key);
                InOut.SourceProblems.Add(Candidate.Key, Reason);
            }
            else
                InOut.ReferenceOnlyReasons.Add(Candidate.Key, Reason);
            Changed = true;
        }
    }
    return Excluded;
}

bool P4KadanPlanBlueprints(const TArray<TSharedPtr<FP4KadanSourceAsset>>& Scanned, const TArray<FString>& Packages,
    const FString& Folder, FP4KadanBlueprintPlans& Out, FString& OutError)
{
    FP4CurrentGameRegistryPtr Reader;
    return P4KadanPlanBlueprints(Scanned, Packages, Folder, Out, OutError, FP4KadanImportSettings(), Reader);
}

bool P4KadanPlanBlueprints(const TArray<TSharedPtr<FP4KadanSourceAsset>>& Scanned, const TArray<FString>& Packages,
    const FString& Folder, FP4KadanBlueprintPlans& Out, FString& OutError, const FP4KadanImportSettings& Settings,
    FP4CurrentGameRegistryPtr& SharedReader, FP4OriginalZenSourceBatch* OriginalBatch)
{
    using namespace P4MapKit;
    using namespace P4MapKit::Blueprint;
    Out = FP4KadanBlueprintPlans();
    Out.Folder = Folder;
    FP4OriginalZenSourceBatch LocalOriginalBatch;
    FP4OriginalZenSourceBatch& Originals = OriginalBatch ? *OriginalBatch : LocalOriginalBatch;
    if (!IsInGameThread())
    {
        OutError = TEXT("Blueprint planning must run on the editor's main thread.");
        return false;
    }
    TMap<FString, TSharedPtr<FP4KadanSourceAsset>> ByPackage;
    for (const auto& Asset : Scanned)
        if (Asset)
            ByPackage.Add(Asset->Package, Asset);
    FP4KadanImportSourceScope BoundSources(Scanned);
    const auto Inputs = MakeShared<FBlueprintPlanInputs>(ByPackage);

    // The packages to plan: the ones asked for and every Blueprint they need that the game folder holds.
    // What a package names is read from its header first, for the native modules it uses.
    struct FFile
    {
        std::vector<std::uint8_t> Header, Body;
        std::shared_ptr<const FVerifiedImportIdentities> Identities;
        TSharedPtr<FBlueprintPlanBoundSource> Source;
        FP4OriginalZenSourcePtr Original;
        void RequireCurrent() const
        {
            Source->RequireCurrent();
            FString Error;
            if (Original && !Original->Current(Error))
                throw FBlueprintPlanSourceError(Source->Binding.Package, Error);
        }
    };
    TMap<FString, FFile> Files;
    TSet<FString> Modules;
    TArray<FString> Pending = Packages, Order;
    while (Pending.Num())
    {
        const FString Package = Pending.Pop(false);
        if (Files.Contains(Package) || Out.Refused.Contains(Package))
            continue;
        const TSharedPtr<FP4KadanSourceAsset>* Found = ByPackage.Find(Package);
        FFile File;
        if (!Found)
        {
            const FString Why = TEXT("The game folder does not hold this Blueprint's files.");
            Out.Refused.Add(Package, Why);
            Out.SourceProblems.Add(Package, Why);
            continue;
        }
        bool ReadOriginal = false;
        try
        {
            File.Source = Inputs->Read(Package);
            File.Header.assign(File.Source->Binding.Header.GetData(),
                File.Source->Binding.Header.GetData() + File.Source->Binding.Header.Num());
            File.Body.assign(File.Source->Binding.Body.GetData(),
                File.Source->Binding.Body.GetData() + File.Source->Binding.Body.Num());
            File.Identities = ReadIdentities(Package, File.Header, File.Body, Settings, SharedReader, Originals,
                File.Original, File.Source, ReadOriginal, ByPackage);
            File.RequireCurrent();
        }
        catch (const FBlueprintPlanSourceError& Why)
        {
            const FString Detail = KadanText(Why.what());
            Out.Refused.Add(Package, Detail);
            Out.SourceProblems.Add(Package, Detail);
            Inputs->AcknowledgeFailure(Why.Package);
            continue;
        }
        catch (const std::bad_alloc&)
        {
            OutError = TEXT("Blueprint source planning ran out of memory.");
            return false;
        }
        catch (const std::exception& Why)
        {
            UE_LOG(LogTemp, Display, TEXT("Kadan Blueprint identities %s %s"), *Package, *KadanText(Why.what()));
            const FString Reason = TEXT("The names of what it uses could not be read from the game's containers (") + KadanText(Why.what()) + TEXT(").");
            Out.Refused.Add(Package, Reason);
            Out.SourceProblems.Add(Package, Reason);
            continue;
        }
        const FPackageInventory Inventory = ReadPackage(File.Header, File.Body);
        if (!Inventory.Error.Code.empty())
        {
            const FString Why = TEXT("Its package cannot be read: ") + KadanText(Inventory.Error.Code);
            Out.Refused.Add(Package, Why);
            Out.SourceProblems.Add(Package, Why);
            continue;
        }
        if (!P4IsValidContentPackage(Package) || KadanText(Inventory.Package) != Package ||
            (File.Identities && File.Identities->Package() != KadanUtf8(Package)))
        {
            const FString Why = TEXT("Its verified source package identity differs from the requested Blueprint.");
            Out.Refused.Add(Package, Why);
            Out.SourceProblems.Add(Package, Why);
            continue;
        }
        for (const FPackageImport& Import : Inventory.Imports)
            if (Import.Outer == 0 && Import.Class == "Package" && Import.Name.compare(0, 8, "/Script/") == 0)
            {
                const FString Module = KadanText(Import.Name.substr(8));
                Modules.Add(Module);
                Out.ModuleUsers.FindOrAdd(Module).AddUnique(Package);
            }
        for (const FPackageImport& Import : Inventory.Imports)
        {
            const FString Name = KadanText(Import.Name);
            const TSharedPtr<FP4KadanSourceAsset>* Needed = Import.Outer == 0 ? ByPackage.Find(Name) : nullptr;
            if (Needed &&
                ((*Needed)->Kind == TEXT("Blueprint") || (*Needed)->Kind == TEXT("AnimBlueprint") ||
                    (*Needed)->Kind == TEXT("WidgetBlueprint")))
                Pending.Add(Name);
        }
        if (ReadOriginal)
            ++Out.OriginalIdentityPackages;
        Files.Add(Package, MoveTemp(File));
        Order.Add(Package);
    }

    // The declarations of the native modules the Blueprints name, as this editor has them loaded.
    TArray<FString> Loaded;
    for (const FString& Module : Modules)
        (FModuleManager::Get().IsModuleLoaded(*Module) ? Loaded : Out.ModulesNotLoaded).Add(Module);
    Loaded.Sort();
    Out.ModulesNotLoaded.Sort();
    Out.Modules = Loaded;
    FP4NativeBlueprintReflection Snapshot;
    if (!P4CaptureNativeBlueprintReflection(Loaded, {}, Snapshot, OutError))
        return false;

    try
    {
        // Every scanned package can be read by a plan: a parent's class, a function another Blueprint declares.
        GraphSignatures::Sources Sources;
        for (const auto& Asset : Scanned)
            if (Asset)
                Sources[KadanUtf8(Asset->Package)] = KadanUtf8(Asset->Package);
        const GraphSignatures::Loader Loader = [Inputs](const std::string& Name) -> J
        {
            try
            {
                const auto Source = Inputs->Read(KadanText(Name));
                if (!Source)
                    return J();
                const auto& Binding = Source->Binding;
                const std::vector<uint8> Header(
                    Binding.Header.GetData(), Binding.Header.GetData() + Binding.Header.Num()),
                    Body(Binding.Body.GetData(), Binding.Body.GetData() + Binding.Body.Num());
                FPackageInventory Package = ReadPackage(Header, Body);
                if (!Package.Error.Code.empty())
                    throw FBlueprintPlanSourceError(Binding.Package, KadanText(Package.Error.Code));
                const J Structure = ReadClassStructure(Package, Body);
                Source->RequireCurrent();
                return Structure;
            }
            catch (const FBlueprintPlanSourceError& Why)
            {
                Inputs->RecordFailure(Why);
#if P4_KADAN_TEST_SEAMS
                if (BlueprintWrappedSourceControl &&
                    P4ExactNativeString(BlueprintWrappedSourceControl->Dependency(), Why.Package))
                {
                    BlueprintWrappedSourceControl->RecordWrap();
                    throw std::runtime_error("controlled wrapped Blueprint loader source failure");
                }
#endif
                throw;
            }
        };
        const J Reflection = P4BlueprintReflectionPlanValue(Snapshot);
        auto Signatures = std::make_unique<GraphSignatures>(Reflection, Sources, Loader);
        const auto RefuseSource = [&](const FString& Root, const FBlueprintPlanSourceError& Why)
        {
            const FString Detail = TEXT("A required Blueprint source could not be read: ") + Why.Package + TEXT(": ") +
                KadanText(Why.what());
            Out.Refused.Add(Root, Detail);
            Out.SourceProblems.Add(Root, Detail);
            Out.ReferenceOnlyReasons.Remove(Root);
            if (!Why.Package.IsEmpty())
            {
                Out.Refused.Add(Why.Package, Detail);
                Out.SourceProblems.Add(Why.Package, Detail);
                Out.ReferenceOnlyReasons.Remove(Why.Package);
            }
            Inputs->AcknowledgeFailure(Why.Package);
            // Healthy roots share this cache. Discard it only after a source
            // failure so swallowed/cached misses cannot poison the next root.
            Signatures.reset();
        };
        // Admitted plans go into the index. A refused plan is written beside them, for reading, and not indexed.
        std::map<std::string, NativeBlueprintPlan> Plans, Written;
        for (const FString& Package : Order)
        {
            const FFile& File = Files[Package];
            try
            {
                // Each source read and this root retain their own identity checks.
                // All previously loaded bytes are immutable under held leases;
                // the complete set is checked before any plan is published.
                // Reopening that growing set around every decoder was quadratic.
                Inputs->RequireNoFailure();
                File.RequireCurrent();
                if (!Signatures)
                    Signatures = std::make_unique<GraphSignatures>(Reflection, Sources, Loader);
#if P4_KADAN_TEST_SEAMS
                if (BlueprintWrappedSourceControl &&
                    P4ExactNativeString(BlueprintWrappedSourceControl->Root(), Package))
                    Signatures->Doc(KadanUtf8(BlueprintWrappedSourceControl->Dependency()));
#endif
                NativeBlueprintPlan Plan = File.Identities
                    ? BuildNativeBlueprintPlanWithIdentities(
                          File.Header, File.Body, KadanUtf8(Package), *Signatures, *File.Identities, true)
                    : BuildNativeBlueprintPlan(File.Header, File.Body, KadanUtf8(Package), *Signatures, true);
                Inputs->RequireNoFailure();
                File.RequireCurrent();
                if (Plan.AdmissionIssues().Truth())
                {
                    std::string Issues = Plan.AdmissionIssues().Dump();
                    if (Issues.size() > 600)
                        Issues = Issues.substr(0, 600) + "...";
                    UE_LOG(LogTemp, Display, TEXT("Kadan Blueprint plan %s %s"), *Package, *KadanText(Issues));
                    Out.Refused.Add(Package, TEXT("It uses something the Blueprint import cannot make yet."));
                    Out.ReferenceOnlyReasons.Add(Package, KadanText(Issues));
                    Written.emplace(KadanUtf8(Package), std::move(Plan));
                    continue;
                }
                if (File.Identities)
                    for (const auto& Reference : File.Identities->Imports())
                        Out.ReferenceOnlyPaths.AddUnique(KadanText(Reference.second));
                Plans.emplace(KadanUtf8(Package), std::move(Plan));
            }
            catch (const std::bad_alloc&)
            {
                // Resource failure is a planner infrastructure error, not an
                // unsupported asset. Let the outer run report it as fatal.
                throw;
            }
            catch (const FBlueprintPlanSourceError& Why)
            {
                RefuseSource(Package, Why);
            }
            catch (const std::exception& Why)
            {
                // Also check when a graph layer wrapped the typed loader throw
                // into its own semantic exception before Build returned.
                try
                {
                    Inputs->RequireNoFailure();
                }
                catch (const FBlueprintPlanSourceError& SourceWhy)
                {
                    RefuseSource(Package, SourceWhy);
                    continue;
                }
                UE_LOG(LogTemp, Display, TEXT("Kadan Blueprint plan %s %s"), *Package, *KadanText(Why.what()));
                // Files entered this loop only after canonical identity,
                // package inventory and export byte ranges were verified.
                // Decoding/recovery beyond that boundary is importer support:
                // preserve the actual game identity when it cannot recover it.
                Out.Refused.Add(Package, TEXT("The Blueprint's editable reconstruction is not supported."));
                Out.ReferenceOnlyReasons.Add(Package, KadanText(Why.what()));
            }
        }
        // A recovered child must never compile against a refused parent or
        // another required Blueprint shell. Preserve both original identities
        // instead; only already-admitted plans remain in the editable index.
        TMap<FString, TArray<FString>> Required;
        for (const auto* Group : {&Plans, &Written})
            for (const auto& Pair : *Group)
            {
                auto& Names = Required.Add(KadanText(Pair.first));
                for (const auto& Need : Pair.second.IndexEntry()["needs"].Items)
                    Names.Add(KadanText(Need.S()));
            }
        const TSet<FString> Excluded = P4KadanBlueprintReferenceClosure(Required, Out);
        for (auto It = Plans.begin(); It != Plans.end();)
            if (Excluded.Contains(KadanText(It->first)))
            {
                Written.emplace(It->first, std::move(It->second));
                It = Plans.erase(It);
            }
            else
                ++It;
        const auto RequirePublicationCurrent = [&]()
        {
            Inputs->RequireCurrent();
            for (const auto& File : Files)
                File.Value.RequireCurrent();
            FP4ExportContainerTools Tools;
            Tools.Retoc = Settings.Retoc;
            Tools.Repak = Settings.Repak;
            FString Error;
            if (SharedReader && !SharedReader->IsCurrent(Tools, Settings.GamePaks, Error))
                throw FBlueprintPlanSourceError(Packages.Num() ? Packages[0] : FString(), Error);
        };
        RequirePublicationCurrent();
        IFileManager::Get().DeleteDirectory(*Folder, false, true);
        for (const auto* Group : {&Plans, &Written})
            for (const auto& Pair : *Group)
            {
                const FString Relative = KadanText(Pair.first).Mid(1) + TEXT(".json");
                if (!WriteText(Folder / TEXT("import") / Relative, Pair.second.ImportText()) ||
                    !WriteText(Folder / TEXT("evidence") / Relative, Pair.second.EvidenceText()))
                {
                    OutError = TEXT("Cannot write the Blueprint plans under ") + Folder;
                    return false;
                }
                if (Group == &Plans)
                    Out.Planned.Add(KadanText(Pair.first));
            }
        // These writes serialize private plan snapshots only, without editor
        // callbacks. Revalidate the held sources as a batch before publishing
        // the index; reopening every source for every file was quadratic.
        RequirePublicationCurrent();
        if (!WriteText(Folder / TEXT("index.json"), NativeBlueprintPlanIndex(Plans).Dump()))
        {
            OutError = TEXT("Cannot write the Blueprint plan index under ") + Folder;
            return false;
        }
        RequirePublicationCurrent();
        auto WidgetInputs = MakeShared<TMap<FString, FFile>>();
        for (auto& Pair : Files)
        {
            const auto Planned = Plans.find(KadanUtf8(Pair.Key));
            if (Planned != Plans.end() && Planned->second.IndexEntry()["kind"].S() == "WidgetBlueprint")
                WidgetInputs->Add(Pair.Key, MoveTemp(Pair.Value));
        }
        Out.ValidateWidgetSource = [WidgetInputs](
                                       const FString& Package, const TSharedPtr<FJsonObject>& Claim, FString& Error)
        {
            const FFile* File = WidgetInputs->Find(Package);
            if (!File && !Claim.IsValid())
                return true; // An ordinary non-Widget source has no witness.
            if (!File || !Claim.IsValid())
            {
                Error = TEXT("Replan this Widget Blueprint import; its original-source witness is missing.");
                return false;
            }
            try
            {
                File->RequireCurrent();
                const auto Source = File->Identities
                    ? FBlueprintClassSource::ReadWithIdentities(
                          File->Header, File->Body, KadanUtf8(Package), *File->Identities)
                    : FBlueprintClassSource::Read(File->Header, File->Body, KadanUtf8(Package));
                const auto Facts = FWidgetGeneratedMemberFacts::Read(Source, File->Body);
                FString Json;
                if (!FJsonSerializer::Serialize(Claim.ToSharedRef(), TJsonWriterFactory<TCHAR>::Create(&Json)) ||
                    !Facts.Proof().Truth() || !Facts.MatchesClaim(J::Parse(KadanUtf8(Json))))
                {
                    Error = TEXT(
                        "Replan this Widget Blueprint import; its generated members differ from the original source.");
                    return false;
                }
                File->RequireCurrent();
                return true;
            }
            catch (const std::exception& Why)
            {
                Error = TEXT("widget generated-member source: ") + KadanText(Why.what());
                return false;
            }
        };
    }
    catch (const FBlueprintPlanSourceError& Why)
    {
        const FString Detail = KadanText(Why.what());
        Out.Refused.Add(Why.Package, Detail);
        Out.SourceProblems.Add(Why.Package, Detail);
        Out.Planned.Reset();
        OutError = TEXT("Blueprint source custody failed: ") + Detail;
        return false;
    }
    catch (const std::exception& Why)
    {
        UE_LOG(LogTemp, Display, TEXT("Kadan Blueprint planner %s"), *KadanText(Why.what()));
        OutError = TEXT("The Blueprint planner stopped.");
        return false;
    }
    return true;
}

FString P4KadanUnloadedModuleNote(const FString& Module, const TArray<FString>& Users)
{
    TArray<FString> Names;
    for (const FString& User : Users)
        Names.Add(FPackageName::GetShortName(User));
    Names.Sort();
    const FString Who = Names.Num() ? FString::Join(Names, TEXT(", ")) : FString(TEXT("Some of the Blueprints"));
    const TCHAR* Uses = Names.Num() == 1 ? TEXT("uses") : TEXT("use");
    // A plugin this project has, switched off.
    for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetDiscoveredPlugins())
        for (const FModuleDescriptor& Each : Plugin->GetDescriptor().Modules)
            if (Each.Name.ToString().Equals(Module, ESearchCase::IgnoreCase))
                return FString::Printf(
                    TEXT(
                        "%s %s the %s module of the %s plugin, which %s, so what they hold of its classes was left out. %s, restart the editor and import them again."),
                    *Who, Uses, *Module, *Plugin->GetName(),
                    Plugin->IsEnabled() ? TEXT("did not load") : TEXT("is switched off"),
                    Plugin->IsEnabled() ? TEXT("Check the Output Log for why it did not load")
                                        : TEXT("Turn the plugin on in Edit > Plugins"));
    return FString::Printf(
        TEXT(
            "%s %s the %s module of a later Unreal Engine than this one, which this editor does not have, so what they hold of its classes was left out. The game uses its own copies of them, so they still work there while you leave them unchanged."),
        *Who, Uses, *Module);
}
