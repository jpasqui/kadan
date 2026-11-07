#include "P4FoliageContent.h"
#include "P4TextureBuild.h"
#include "P4RecoveredFoliageType.h"
#include "P4ManualEngineClassRegistration.h"
#include "P4MapKitCore/export_package.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/CoreRedirects.h"
#include "UObject/ObjectRedirector.h"

namespace
{
UObject* RegisterRecoveredFoliageForLoader()
{
    return P4RecoveredFoliageClass();
}

void CopyBaseDefaults(UObject* Target)
{
    const UObject* Base = UFoliageType_InstancedStaticMesh::StaticClass()->GetDefaultObject();
    for (TFieldIterator<FProperty> It(UFoliageType_InstancedStaticMesh::StaticClass()); It; ++It)
        if (!It->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
            It->CopyCompleteValue_InContainer(Target, Base);
}

bool ValidStorage(UClass* Class, UClass* Parent)
{
    if (!Class || Class->GetSuperClass() != Parent || Class->ClassConstructor != Parent->ClassConstructor ||
        Class->ClassVTableHelperCtorCaller != Parent->ClassVTableHelperCtorCaller ||
        Class->ClassAddReferencedObjects != Parent->ClassAddReferencedObjects)
        return false;
    int32 Own = 0;
    for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
    {
        ++Own;
        if (It->GetOffset_ForInternal() < Parent->GetPropertiesSize() ||
            It->GetOffset_ForInternal() + It->GetSize() > Class->GetPropertiesSize())
            return false;
    }
    const auto* Name = FindFProperty<FNameProperty>(Class, TEXT("FoliageTypeName"));
    const auto* Toggle = FindFProperty<FBoolProperty>(Class, TEXT("bUseCustomRadius"));
    const auto* Radius = FindFProperty<FFloatProperty>(Class, TEXT("CustomRadius"));
    return Own == 3 && Name && Toggle && Toggle->IsNativeBool() && Radius && Name->GetOwnerStruct() == Class &&
        Toggle->GetOwnerStruct() == Class && Radius->GetOwnerStruct() == Class;
}
bool RegisterLegacyFoliageLookups(UClass* Class)
{
    UPackage* Legacy = FindPackage(nullptr, TEXT("/Script/Kadan"));
    if (!Legacy || !Class)
        return false;
    const auto Alias = [&](const TCHAR* Name, UObject* Destination)
    {
        // CoreRedirects qualify serialized import paths. StaticLoadObject of an
        // already loaded script package instead looks for UObjectRedirector.
        // Preserve that API too, without changing package exclusion policy or
        // renaming/replacing any existing object in the legacy namespace.
        if (UObject* Existing = FindObjectFast<UObject>(Legacy, FName(Name)))
        {
            const auto* Redirector = Cast<UObjectRedirector>(Existing);
            return Redirector && Redirector->DestinationObject == Destination;
        }
        auto* Redirector = NewObject<UObjectRedirector>(Legacy, FName(Name), RF_Public | RF_Standalone);
        if (!Redirector)
            return false;
        Redirector->DestinationObject = Destination;
        Redirector->AddToRoot();
        return true;
    };
    return Alias(TEXT("P4RecoveredFoliageType"), Class) &&
        Alias(TEXT("Default__P4RecoveredFoliageType"), Class->GetDefaultObject());
}
} //namespace

UClass* P4RecoveredFoliageClass()
{
    if (!IsInGameThread())
        return nullptr;
    UClass* Parent = UFoliageType_InstancedStaticMesh::StaticClass();
    if (!Parent || !Parent->ClassConstructor)
        return nullptr;
    // The class and CDO of a saved legacy editor asset still resolve. This is
    // narrowly scoped to our compatibility type, never the Kadan package itself.
    static const bool Redirected = []
    {
        const TArray<FCoreRedirect> Redirects{
            FCoreRedirect(ECoreRedirectFlags::Type_Class, TEXT("/Script/Kadan.P4RecoveredFoliageType"),
                TEXT("/Script/Foliage.P4RecoveredFoliageType")),
            FCoreRedirect(ECoreRedirectFlags::Type_Object, TEXT("/Script/Kadan.Default__P4RecoveredFoliageType"),
                TEXT("/Script/Foliage.Default__P4RecoveredFoliageType"))};
        FCoreRedirects::AddRedirectList(Redirects, TEXT("KadanFoliageStorage"));
        // AddRedirectList returns false for an identical preexisting redirect.
        // Validate the resulting identities, including that repeat-load case.
        return FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class,
                   FCoreRedirectObjectName(FString(TEXT("/Script/Kadan.P4RecoveredFoliageType"))))
                   .ToString() == TEXT("/Script/Foliage.P4RecoveredFoliageType") &&
            FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Object,
                FCoreRedirectObjectName(FString(TEXT("/Script/Kadan.Default__P4RecoveredFoliageType"))))
                .ToString() == TEXT("/Script/Foliage.Default__P4RecoveredFoliageType");
    }();
    if (!Redirected)
        return nullptr;
    if (UClass* Existing = FindObject<UClass>(nullptr, TEXT("/Script/Foliage.P4RecoveredFoliageType")))
        return ValidStorage(Existing, Parent) && RegisterLegacyFoliageLookups(Existing) ? Existing : nullptr;

    // This is the existing P4EngineDeclarations native-construction mechanism,
    // with fields linked on a NEW child class rather than on the live native base.
    // A class in the editor-only Kadan script package is itself editor-only:
    // the native cook propagates that exclusion from class to every instance.
    // Store this compatibility schema alongside its real runtime parent. The
    // diagnostic/game cook still translates its identity to the original class.
    UClass* Class = NewObject<UClass>(
        Parent->GetOutermost(), TEXT("P4RecoveredFoliageType"), RF_Public | RF_Standalone);
    Class->SetSuperStruct(Parent);
    Class->ClassWithin = Parent->ClassWithin;
    Class->ClassConfigName = Parent->ClassConfigName;
    Class->ClassFlags = EClassFlags((Parent->ClassFlags & CLASS_Inherit) & ~CLASS_Abstract);
    Class->ClassCastFlags = Parent->ClassCastFlags;
    Class->ClassConstructor = Parent->ClassConstructor;
    Class->ClassVTableHelperCtorCaller = Parent->ClassVTableHelperCtorCaller;
    Class->ClassAddReferencedObjects = Parent->ClassAddReferencedObjects;
    // AddCppProperty prepends fields. Link in the same name/bool/float order as
    // the recovered type, letting UE allocate offsets after its own parent size.
    auto* Radius = new FFloatProperty(Class, TEXT("CustomRadius"), RF_Public);
    auto* Toggle = new FBoolProperty(Class, TEXT("bUseCustomRadius"), RF_Public);
    Toggle->SetBoolSize(sizeof(bool), true);
    auto* Name = new FNameProperty(Class, TEXT("FoliageTypeName"), RF_Public);
    for (FProperty* Field :
        {static_cast<FProperty*>(Radius), static_cast<FProperty*>(Toggle), static_cast<FProperty*>(Name)})
    {
        Field->SetPropertyFlags(CPF_Edit | CPF_NativeAccessSpecifierPublic);
        Field->SetMetaData(TEXT("Category"), TEXT("Foliage"));
        Class->AddCppProperty(Field);
    }
    FP4ManualEngineClassRegistration Registration(Class, &RegisterRecoveredFoliageForLoader);
    if (!Registration.IsValid())
    {
        Class->ClearFlags(RF_Public | RF_Standalone);
        Class->MarkPendingKill();
        return nullptr;
    }
    Class->Bind();
    Class->StaticLink(true);
    if (!ValidStorage(Class, Parent))
    {
        Class->ClearFlags(RF_Public | RF_Standalone);
        Class->MarkPendingKill();
        return nullptr;
    }
    if (!Registration.FinishClass())
    {
        Class->ClearFlags(RF_Public | RF_Standalone);
        Class->MarkPendingKill();
        return nullptr;
    }
    Class->AddToRoot();
    UObject* Defaults = Class->GetDefaultObject();
    Name->SetPropertyValue_InContainer(Defaults, FName(TEXT("Default")));
    Toggle->SetPropertyValue_InContainer(Defaults, false);
    Radius->SetPropertyValue_InContainer(Defaults, 0.f);
    CopyBaseDefaults(Defaults);
    return RegisterLegacyFoliageLookups(Class) ? Class : nullptr;
}

void P4RegisterRecoveredFoliageType()
{
    P4RecoveredFoliageClass();
}

void P4RefreshRecoveredFoliageDefaults()
{
    // Native CDOs can be created before the project's established engine-default corrections.
    // Keep the twin's actual serialization baseline synchronized with the corrected original CDO.
    if (UClass* Class = P4RecoveredFoliageClass())
        CopyBaseDefaults(Class->GetDefaultObject());
}

UFoliageType_InstancedStaticMesh* P4NewRecoveredFoliage(UObject* Outer, FName Name, EObjectFlags Flags)
{
    P4RefreshRecoveredFoliageDefaults();
    UClass* Class = P4RecoveredFoliageClass();
    auto* Made = Class ? Cast<UFoliageType_InstancedStaticMesh>(
                             NewObject<UObject>(Outer ? Outer : GetTransientPackage(), Class, Name, Flags))
                       : nullptr;
    // The native constructor and UE's PostConstructLink fast path initialize
    // inherited non-config fields from constructor literals, not a later
    // corrected CDO. Give this new instance the same serialization defaults as
    // the refreshed twin CDO before applying any original source deltas.
    if (Made)
        CopyBaseDefaults(Made);
    return Made;
}

bool P4TranslateRecoveredFoliageCook(std::vector<uint8>& Header, std::vector<uint8>& Body, FString& Error)
{
    const auto Before = P4MapKit::ReadPackage(Header, Body);
    if (!Before.Error.Code.empty())
    {
        Error = UTF8_TO_TCHAR(Before.Error.Code.c_str());
        return false;
    }
    bool Twin = false;
    for (const auto& Import : Before.Imports)
        Twin = Twin || Import.Name == "P4RecoveredFoliageType" ||
            (Import.ClassPackage == "/Script/Kadan" && Import.Class == "P4RecoveredFoliageType");
    if (!Twin)
        return true;
    // Rewriting a shared script package would also relabel an unrelated Kadan runtime import.
    for (const auto& Import : Before.Imports)
    {
        if (Import.ClassPackage == "/Script/Kadan" && Import.Class != "P4RecoveredFoliageType")
        {
            Error = TEXT("foliage-cook-holds-another-kadan-class");
            return false;
        }
        if (Import.Outer < 0)
        {
            const auto Slot = std::size_t(-std::int64_t(Import.Outer) - 1);
            if (Slot >= Before.Imports.size())
            {
                Error = TEXT("foliage-cook-import-outer-unreadable");
                return false;
            }
            if (Before.Imports[Slot].Name == "/Script/Kadan" && Import.Name != "P4RecoveredFoliageType" &&
                Import.Name != "Default__P4RecoveredFoliageType")
            {
                Error = TEXT("foliage-cook-holds-another-kadan-class");
                return false;
            }
        }
    }
    const std::map<std::string, std::string> Identities = {
        {"P4RecoveredFoliageType", "FoliageType_InstancedStaticMesh"},
        {"Default__P4RecoveredFoliageType", "Default__FoliageType_InstancedStaticMesh"},
        {"/Script/Kadan", "/Script/Foliage"}};
    std::map<std::string, std::string> Rename;
    std::map<uint32, uint32> ImportNames;
    for (const auto& Identity : Identities)
    {
        auto Old = std::find(Before.Names.begin(), Before.Names.end(), Identity.first);
        if (Old == Before.Names.end())
            continue;
        auto New = std::find(Before.Names.begin(), Before.Names.end(), Identity.second);
        if (New == Before.Names.end())
            Rename.insert(Identity);
        else
        {
            if (std::count(Before.Names.begin(), Before.Names.end(), Identity.first) != 1 ||
                std::count(Before.Names.begin(), Before.Names.end(), Identity.second) != 1)
            {
                Error = TEXT("foliage-cook-name-identity-collision");
                return false;
            }
            // A level already imports the Foliage module for its foliage actor. Reference that
            // existing namespace in the import slots; do not merge or rename any FName identity.
            ImportNames.emplace(uint32(Old - Before.Names.begin()), uint32(New - Before.Names.begin()));
        }
    }
    auto Candidate = Header;
    for (const auto& Span : Before.HeaderSpans)
        if (Span.Kind == "imports")
            for (std::size_t I = 0; I < Before.Imports.size(); ++I)
                for (const std::size_t Field : {std::size_t(0), std::size_t(8), std::size_t(20)})
                {
                    const auto At = Span.Offset + I * 28 + Field;
                    uint64 Index = 0, Number = 0;
                    if (!P4MapKit::Export::PackageRewriteDetail::Scalar(Candidate, At, Index) ||
                        !P4MapKit::Export::PackageRewriteDetail::Scalar(Candidate, At + 4, Number))
                    {
                        Error = TEXT("foliage-cook-import-slot-unreadable");
                        return false;
                    }
                    const auto Found = ImportNames.find(uint32(Index));
                    if (Found == ImportNames.end())
                        continue;
                    if (Number)
                    {
                        Error = TEXT("foliage-cook-numbered-class-identity");
                        return false;
                    }
                    P4MapKit::Export::PackageRewriteDetail::Put(Candidate, At, Found->second);
                }
    P4MapKit::Export::RewrittenPackage Rewritten;
    std::string Why;
    if (!P4MapKit::Export::RewriteCookedNames(Candidate, Body, Rename, Rewritten, Why))
    {
        Error = UTF8_TO_TCHAR(Why.c_str());
        return false;
    }
    if (Rewritten.Body != Body)
    {
        Error = TEXT("foliage-cook-translation-changed-export-bytes");
        return false;
    }
    const auto After = P4MapKit::ReadPackage(Rewritten.Header, Rewritten.Body);
    if (!After.Error.Code.empty())
    {
        Error = UTF8_TO_TCHAR(After.Error.Code.c_str());
        return false;
    }
    for (const auto& Import : After.Imports)
        if (Import.Name == "P4RecoveredFoliageType" || Import.ClassPackage == "/Script/Kadan")
        {
            Error = TEXT("foliage-cook-class-identity-not-restored");
            return false;
        }
    // Renaming the recovered namespace can append a larger name pool. Inline
    // texture offsets are absolute file positions even when export bytes did
    // not move within the body. Rebase only completely decoded payloads.
    if (Header.size() != Rewritten.Header.size() &&
        !P4RelocatePackageTextureImages(Header, Body, Rewritten.Header, Rewritten.Body, Error))
        return false;
    Header = std::move(Rewritten.Header);
    Body = std::move(Rewritten.Body);
    Error.Reset();
    return true;
}
