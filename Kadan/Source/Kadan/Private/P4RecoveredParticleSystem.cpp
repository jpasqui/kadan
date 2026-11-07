#include "P4RecoveredParticleSystem.h"
#include "P4ManualEngineClassRegistration.h"
#include "P4GameParticleModules.h"
#include "P4MapKitCore/export_package.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace P4RecoveredParticleStorage
{
UObject* RegisterForLoader()
{
    return P4RecoveredParticleSystemClass();
}
const TCHAR* const Names[] = {TEXT("bLinkedToActorMeshCulling"), TEXT("bCheckInfiniteLifetime"),
    TEXT("bInfiniteLifetime"), TEXT("bTickGroupOverwrite")};
void CopyOriginalParticleBoolDefaults(UObject* Target)
{
    // Original native CDO observation: linked mesh culling true; all three lifetime/tick
    // switches false. A field omitted by tagged source serialization inherits these values.
    for (int32 Index = 0; Index < 4; ++Index)
        if (const auto* Field = FindFProperty<FBoolProperty>(Target->GetClass(), Names[Index]))
            Field->SetPropertyValue_InContainer(Target, Index == 0);
}
void CopyParticleDefaults(UObject* Target)
{
    const UObject* Base = UParticleSystem::StaticClass()->GetDefaultObject();
    for (TFieldIterator<FProperty> It(UParticleSystem::StaticClass()); It; ++It)
        if (!It->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
            It->CopyCompleteValue_InContainer(Target, Base);
}
bool Valid(UClass* Class)
{
    UClass* Parent = UParticleSystem::StaticClass();
    if (!Class || Class->GetSuperClass() != Parent || Class->ClassConstructor != Parent->ClassConstructor ||
        Class->ClassVTableHelperCtorCaller != Parent->ClassVTableHelperCtorCaller ||
        Class->ClassAddReferencedObjects != Parent->ClassAddReferencedObjects)
        return false;
    int32 Count = 0;
    for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        ++Count;
    for (const TCHAR* Name : Names)
    {
        const auto* Field = FindFProperty<FBoolProperty>(Class, Name);
        if (!Field || !Field->IsNativeBool() || Field->GetOwnerStruct() != Class ||
            Field->GetOffset_ForInternal() < Parent->GetPropertiesSize() ||
            Field->GetOffset_ForInternal() + Field->GetSize() > Class->GetPropertiesSize())
            return false;
    }
    return Count == 4;
}
} //namespace P4RecoveredParticleStorage

UClass* P4RecoveredParticleSystemClass()
{
    if (!IsInGameThread())
        return nullptr;
    // Original particle assets hold the game's own module classes too. Declare them with
    // the system class, so they exist before any original particle package is read.
    P4RegisterGameParticleModuleClasses();
    UClass* Parent = UParticleSystem::StaticClass();
    if (!Parent || !Parent->ClassConstructor)
        return nullptr;
    if (UClass* Existing = FindObject<UClass>(nullptr, TEXT("/Script/Engine.P4RecoveredParticleSystem")))
        return P4RecoveredParticleStorage::Valid(Existing) ? Existing : nullptr;
    UClass* Class = NewObject<UClass>(
        Parent->GetOutermost(), TEXT("P4RecoveredParticleSystem"), RF_Public | RF_Standalone);
    Class->SetSuperStruct(Parent);
    Class->ClassWithin = Parent->ClassWithin;
    Class->ClassConfigName = Parent->ClassConfigName;
    Class->ClassFlags = EClassFlags((Parent->ClassFlags & CLASS_Inherit) & ~CLASS_Abstract);
    Class->ClassCastFlags = Parent->ClassCastFlags;
    Class->ClassConstructor = Parent->ClassConstructor;
    Class->ClassVTableHelperCtorCaller = Parent->ClassVTableHelperCtorCaller;
    Class->ClassAddReferencedObjects = Parent->ClassAddReferencedObjects;
    // Exact reflected native-bool types and source property flags. Host storage is allocated
    // after the real parent, not claimed to reproduce the Nintendo native ABI offsets217,224..226.
    for (int32 Index = 3; Index >= 0; --Index)
    {
        auto* Field = new FBoolProperty(Class, P4RecoveredParticleStorage::Names[Index], RF_Public);
        Field->SetBoolSize(sizeof(bool), true);
        Field->SetPropertyFlags(EPropertyFlags(Index == 0 || Index == 3 ? 0x18011040000201ULL : 0x18011040020201ULL));
        Field->SetMetaData(TEXT("Category"), TEXT("Particle"));
        Class->AddCppProperty(Field);
    }
    FP4ManualEngineClassRegistration Registration(Class, &P4RecoveredParticleStorage::RegisterForLoader);
    if (!Registration.IsValid())
    {
        Class->ClearFlags(RF_Public | RF_Standalone);
        Class->MarkPendingKill();
        return nullptr;
    }
    Class->Bind();
    Class->StaticLink(true);
    if (!P4RecoveredParticleStorage::Valid(Class))
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
    P4RecoveredParticleStorage::CopyParticleDefaults(Class->GetDefaultObject());
    P4RecoveredParticleStorage::CopyOriginalParticleBoolDefaults(Class->GetDefaultObject());
    return Class;
}

void P4RefreshRecoveredParticleSystemDefaults()
{
    if (UClass* Class = P4RecoveredParticleSystemClass())
    {
        P4RecoveredParticleStorage::CopyParticleDefaults(Class->GetDefaultObject());
        P4RecoveredParticleStorage::CopyOriginalParticleBoolDefaults(Class->GetDefaultObject());
    }
}

UParticleSystem* P4NewRecoveredParticleSystem(UObject* Outer, FName Name, EObjectFlags Flags)
{
    P4RefreshRecoveredParticleSystemDefaults();
    UClass* Class = P4RecoveredParticleSystemClass();
    auto* Made = Class
        ? Cast<UParticleSystem>(NewObject<UObject>(Outer ? Outer : GetTransientPackage(), Class, Name, Flags))
        : nullptr;
    if (Made)
    {
        P4RecoveredParticleStorage::CopyParticleDefaults(Made);
        P4RecoveredParticleStorage::CopyOriginalParticleBoolDefaults(Made);
    }
    return Made;
}

bool P4TranslateRecoveredParticleCook(std::vector<uint8>& Header, std::vector<uint8>& Body, FString& Error)
{
    const auto Before = P4MapKit::ReadPackage(Header, Body);
    if (!Before.Error.Code.empty())
    {
        Error = UTF8_TO_TCHAR(Before.Error.Code.c_str());
        return false;
    }
    bool Found = false;
    for (const auto& Import : Before.Imports)
    {
        if (Import.Class == "P4RecoveredParticleSystem" && Import.ClassPackage != "/Script/Engine")
        {
            Error = TEXT("particle-cook-recovered-class-package-differs");
            return false;
        }
        if (Import.Name == "P4RecoveredParticleSystem" || Import.Name == "Default__P4RecoveredParticleSystem")
        {
            std::string Outer;
            if (Import.Outer >= 0 || !Before.Reference(Import.Outer, Outer) || Outer != "/Script/Engine")
            {
                Error = TEXT("particle-cook-recovered-class-namespace-differs");
                return false;
            }
            Found = true;
        }
        Found = Found || Import.Class == "P4RecoveredParticleSystem";
    }
    if (!Found)
    {
        Error.Reset();
        return true;
    }
    const std::map<std::string, std::string> Identities = {{"P4RecoveredParticleSystem", "ParticleSystem"},
        {"Default__P4RecoveredParticleSystem", "Default__ParticleSystem"}};
    std::map<std::string, std::string> Rename;
    std::map<uint32, uint32> ImportNames;
    for (const auto& Identity : Identities)
    {
        auto Old = std::find(Before.Names.begin(), Before.Names.end(), Identity.first);
        if (Old == Before.Names.end())
            continue;
        auto New = std::find(Before.Names.begin(), Before.Names.end(), Identity.second);
        if (std::count(Before.Names.begin(), Before.Names.end(), Identity.first) != 1 ||
            (New != Before.Names.end() && std::count(Before.Names.begin(), Before.Names.end(), Identity.second) != 1))
        {
            Error = TEXT("particle-cook-name-identity-collision");
            return false;
        }
        if (New == Before.Names.end())
            Rename.insert(Identity);
        else
            ImportNames.emplace(uint32(Old - Before.Names.begin()), uint32(New - Before.Names.begin()));
    }
    auto Candidate = Header;
    for (const auto& Span : Before.HeaderSpans)
        if (Span.Kind == "imports")
            for (std::size_t Index = 0; Index < Before.Imports.size(); ++Index)
                for (const std::size_t Field : {std::size_t(0), std::size_t(8), std::size_t(20)})
                {
                    const auto At = Span.Offset + Index * 28 + Field;
                    uint64 Name = 0, Number = 0;
                    if (!P4MapKit::Export::PackageRewriteDetail::Scalar(Candidate, At, Name) ||
                        !P4MapKit::Export::PackageRewriteDetail::Scalar(Candidate, At + 4, Number))
                    {
                        Error = TEXT("particle-cook-import-slot-unreadable");
                        return false;
                    }
                    const auto Destination = ImportNames.find(uint32(Name));
                    if (Destination == ImportNames.end())
                        continue;
                    if (Number)
                    {
                        Error = TEXT("particle-cook-numbered-class-identity");
                        return false;
                    }
                    P4MapKit::Export::PackageRewriteDetail::Put(Candidate, At, Destination->second);
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
        Error = TEXT("particle-cook-translation-changed-export-bytes");
        return false;
    }
    const auto After = P4MapKit::ReadPackage(Rewritten.Header, Rewritten.Body);
    if (!After.Error.Code.empty())
    {
        Error = UTF8_TO_TCHAR(After.Error.Code.c_str());
        return false;
    }
    for (const auto& Import : After.Imports)
        if (Import.Name == "P4RecoveredParticleSystem" || Import.Class == "P4RecoveredParticleSystem")
        {
            Error = TEXT("particle-cook-class-identity-not-restored");
            return false;
        }
    Header = std::move(Rewritten.Header);
    Body = std::move(Rewritten.Body);
    Error.Reset();
    return true;
}
