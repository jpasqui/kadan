#include "P4EngineDeclarations.h"
#include "P4RecoveredFoliageType.h"
#include "P4RecoveredParticleSystem.h"
#include "P4ManualEngineClassRegistration.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"

namespace
{
UObject* RegisterC4GPUSubUVForLoader()
{
    P4RegisterEngineDeclarations();
    return FindObject<UClass>(nullptr, TEXT("/Script/Engine.ParticleModuleC4GPUSubUV"));
}
} //namespace

void P4RegisterEngineDeclarations()
{
    // Register before any saved original foliage package is loaded, including
    // starts where the unrelated particle declaration already exists.
    P4RegisterRecoveredFoliageType();
    P4RecoveredParticleSystemClass();
    const TCHAR* Path = TEXT("/Script/Engine.ParticleModuleC4GPUSubUV");
    if (FindObject<UClass>(nullptr, Path))
        return;
    UClass* Parent = FindObject<UClass>(nullptr, TEXT("/Script/Engine.ParticleModuleSubUVBase"));
    if (!Parent || !Parent->ClassConstructor)
        return;
    // Captured schema: native parent ParticleModuleSubUVBase, no own reflected
    // fields, and the same native size as the parent (48 bytes on the game).
    // Bind the stock parent's constructor at the editor's own size. Keeping the
    // original script identity lets particle assets retain their typed links.
    // Their unsupported native data remains reference-only under ImportObject.
    UClass* Class = NewObject<UClass>(
        Parent->GetOutermost(), TEXT("ParticleModuleC4GPUSubUV"), RF_Public | RF_Standalone);
    Class->SetSuperStruct(Parent);
    Class->ClassWithin = Parent->ClassWithin;
    Class->ClassConfigName = Parent->ClassConfigName;
    Class->ClassFlags = EClassFlags((Parent->ClassFlags & CLASS_Inherit) & ~CLASS_Abstract);
    Class->ClassCastFlags = Parent->ClassCastFlags;
    Class->ClassConstructor = Parent->ClassConstructor;
    Class->ClassVTableHelperCtorCaller = Parent->ClassVTableHelperCtorCaller;
    Class->ClassAddReferencedObjects = Parent->ClassAddReferencedObjects;
    FP4ManualEngineClassRegistration Registration(Class, &RegisterC4GPUSubUVForLoader);
    if (!Registration.IsValid())
    {
        Class->ClearFlags(RF_Public | RF_Standalone);
        Class->MarkPendingKill();
        return;
    }
    Class->Bind();
    Class->StaticLink(true);
    if (!Registration.FinishClass())
    {
        Class->ClearFlags(RF_Public | RF_Standalone);
        Class->MarkPendingKill();
        return;
    }
    Class->AddToRoot();
    Class->GetOutermost()->GetMetaData()->SetValue(Class, TEXT("P4DeclarationOnly"), TEXT("1"));
}
