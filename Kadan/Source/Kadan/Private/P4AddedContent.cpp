#include "P4AddedContent.h"
#include "P4CreatorSource.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "P4KadanEditor.h"
#include "P4OriginalRecord.h"
#include "P4ModDescriptor.h"
#include "P4GamePreviewWorld.h"
#include "AssetTypeCategories.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
bool ShortName(const FString& Name)
{
    auto Alnum = [](TCHAR C) { return (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('0') && C <= TEXT('9')); };
    if (Name.IsEmpty() || Name.Len() > 47 || !Alnum(Name[0]) || !Alnum(Name[Name.Len() - 1]))
        return false;
    for (TCHAR C : Name)
        if (!Alnum(C) && C != TEXT('.') && C != TEXT('_') && C != TEXT('-'))
            return false;
    return true;
}
bool StockId(const FString& Name)
{
    // Tsuchi plain IDs have 1 to 95 ASCII letters, digits, dots, underscores or hyphens.
    if (Name.IsEmpty() || Name.Len() > 95)
        return false;
    for (TCHAR C : Name)
        if (!(C >= TEXT('a') && C <= TEXT('z')) && !(C >= TEXT('A') && C <= TEXT('Z')) &&
            !(C >= TEXT('0') && C <= TEXT('9')) && C != TEXT('.') && C != TEXT('_') && C != TEXT('-'))
            return false;
    return true;
}
bool Family(const UClass* Class, const TCHAR* Name)
{
    for (; Class; Class = Class->GetSuperClass())
        if (Class->GetPathName() == FString(TEXT(P4_GAME_SCRIPT_PACKAGE ".")) + Name)
            return true;
    return false;
}
bool Kind(const UClass* Class, const TCHAR* ComponentFamily)
{
    const AActor* Default = Class ? Cast<AActor>(Class->GetDefaultObject()) : nullptr;
    if (!Default)
        return false;
    if (const FObjectPropertyBase* AI = CastField<FObjectPropertyBase>(
            Class->FindPropertyByName(TEXT("CarrotAIComponent"))))
        if (const UObject* Component = AI->GetObjectPropertyValue_InContainer(Default))
            if (Family(Component->GetClass(), ComponentFamily))
                return true;
    TInlineComponentArray<UActorComponent*> Components(Default);
    for (const UActorComponent* Component : Components)
        if (Component && Family(Component->GetClass(), ComponentFamily))
            return true;
    // Blueprint construction components live in SCS templates until an actor is spawned. Resolve
    // inherited overrides against this creator class, rather than assuming the CDO owns them.
    UBlueprintGeneratedClass* Generated = Cast<UBlueprintGeneratedClass>(const_cast<UClass*>(Class));
    for (const UClass* Parent = Class; Generated && Parent; Parent = Parent->GetSuperClass())
    {
        const UBlueprint* Blueprint = Cast<UBlueprint>(Parent->ClassGeneratedBy);
        if (!Blueprint || !Blueprint->SimpleConstructionScript)
            continue;
        for (const USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
            if (const UActorComponent* Component = Node ? Node->GetActualComponentTemplate(Generated) : nullptr)
                if (Family(Component->GetClass(), ComponentFamily))
                    return true;
    }
    return false;
}
} //namespace

UP4AddedContentFactory::UP4AddedContentFactory()
{
    SupportedClass = UP4AddedContent::StaticClass();
    bCreateNew = true;
    bEditAfterNew = true;
}
FText UP4AddedContentFactory::GetDisplayName() const
{
    return FText::FromString(TEXT("Treasure or creature"));
}
uint32 UP4AddedContentFactory::GetMenuCategories() const
{
    return EAssetTypeCategories::Misc;
}
UObject* UP4AddedContentFactory::FactoryCreateNew(
    UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
    FString Root, Error;
    if (!IsInGameThread() || Class != SupportedClass || !Parent || !P4KadanActiveCreatorRoot(Root, Error) ||
        !P4IsModContentPath(Root, Parent->GetName()) || FindObjectFast<UObject>(Parent, Name) ||
        FPackageName::DoesPackageExist(Parent->GetName()))
        return nullptr;
    return NewObject<UP4AddedContent>(Parent, Class, Name, Flags);
}

FString P4CreatorBlueprintIssue(const UBlueprint* Blueprint, const FString& PackageRoot)
{
    if (!IsValid(Blueprint) || !Blueprint->GeneratedClass || Blueprint->Status == BS_Error ||
        Blueprint->BlueprintType != BPTYPE_Normal || !Blueprint->ParentClass)
        return TEXT("Compile the Blueprint and fix its errors before exporting it.");
    // A copy of one of the game's Blueprints is the creator's, like any other copy; the game's own is not.
    if (!P4IsModContentPath(PackageRoot, Blueprint->GetOutermost()->GetName()) ||
        P4QueryOriginalAsset(const_cast<UBlueprint*>(Blueprint)).IsOriginal())
        return TEXT("This is the game's own Blueprint. Make a child class or a copy of it in this mod.");
    // Installed engine templates, such as the default level's sky Blueprint,
    // are copied only inside the owning temporary preview cook scope.
    if (P4IsTrustedGamePreviewEngineCopy(Blueprint))
        return FString();
    // As the game's own classes are made: a gameplay class on one of the game's gameplay Blueprints, with its own
    // components and graphs; an animation Blueprint on an animation instance class; an animation notify; a component.
    // A class of this mod may stand between it and the game's or the engine's class.
    const UClass* Root = Blueprint->ParentClass;
    while (Root && Root->ClassGeneratedBy && P4IsCreatorAsset(Root->ClassGeneratedBy))
        Root = Root->GetSuperClass();
    const FString RootPackage = Root ? Root->GetOutermost()->GetName() : FString();
    const bool Native = RootPackage.StartsWith(TEXT("/Script/"));
    const bool GameContent = P4IsContentPackage(RootPackage) && !P4IsModFolderPackage(RootPackage);
    const bool Gameplay = GameContent && Family(Root, TEXT("GActor"));
    const bool Supporting = (Native || GameContent) && Root &&
        (Root->IsChildOf(UAnimInstance::StaticClass()) || Root->IsChildOf(UAnimNotify::StaticClass()) ||
            Root->IsChildOf(UAnimNotifyState::StaticClass()) || Root->IsChildOf(UActorComponent::StaticClass()));
    if (!Gameplay && !Supporting)
        return TEXT(
            "Choose a parent of the game's: a gameplay Blueprint, an animation instance class, an animation notify or a component.");
    return FString();
}

bool P4AddedContentDeclaration(
    const UP4AddedContent* Definition, const FString& PackageRoot, TSharedPtr<FJsonObject>& Out, FString& Error)
{
    auto No = [&](const TCHAR* Why)
    {
        Error = Why;
        return false;
    };
    if (!IsInGameThread() || !IsValid(Definition) ||
        !P4IsModContentPath(PackageRoot, Definition->GetOutermost()->GetName()))
        return No(TEXT(
            "The treasure or creature declaration is in another mod's folder. Give this mod its own outside that folder."));
    if (!ShortName(Definition->ShortName))
        return No(TEXT(
            "Give the new kind a short lowercase name made of letters and digits with dots, underscores or hyphens inside."));
    if (Definition->From.IsNone() || !StockId(Definition->From.ToString()))
        return No(TEXT("Name a stock donor row for the new kind."));
    if (Definition->DisplayName.IsEmpty())
        return No(TEXT("Give the new kind a display name."));
    UClass* Class = Definition->ActorClass.LoadSynchronous();
    const UBlueprint* Blueprint = Class ? Cast<UBlueprint>(Class->ClassGeneratedBy) : nullptr;
    Error = P4CreatorBlueprintIssue(Blueprint, PackageRoot);
    if (!Error.IsEmpty())
        return false;
    if (!Kind(Class,
            Definition->Kind == EP4AddedContentKind::Treasure ? TEXT("OtakaraAIComponent") : TEXT("TekiAIComponent")))
        return No(TEXT("Choose a child Blueprint of the same kind as this declaration."));
    if (Definition->MinimumCarriers != -1 || Definition->MaximumCarriers != -1)
        if (Definition->MinimumCarriers < 1 || Definition->MaximumCarriers < Definition->MinimumCarriers)
            return No(TEXT("Set both carrying counts and keep the maximum at least the minimum."));
    if (Definition->Sparklium < -1)
        return No(TEXT("Use a nonnegative Sparklium value or leave the donor value unchanged."));
    if (!FMath::IsFinite(Definition->Life) ||
        (Definition->Life != -1.f && (Definition->Life <= 0.f || double(Definition->Life) > double(MAX_int32))))
        return No(TEXT("Use positive life within the game's signed integer range or leave the donor value unchanged."));
    if (Definition->Kind == EP4AddedContentKind::Treasure && Definition->Life != -1.f)
        return No(TEXT("Life applies to creatures. Leave it unchanged for a treasure."));
    if (Definition->Kind == EP4AddedContentKind::Creature &&
        (Definition->Sparklium != -1 || Definition->MinimumCarriers != -1 || Definition->MaximumCarriers != -1))
        return No(
            TEXT("Sparklium and treasure carrying counts apply to treasures. Leave them unchanged for a creature."));
    if (!Definition->Icon.IsNull() && !Definition->Icon.ToSoftObjectPath().GetSubPathString().IsEmpty())
        return No(TEXT("Choose a texture asset for the icon without a subobject path."));
    auto Value = MakeShared<FJsonObject>();
    Value->SetStringField(TEXT("class"), Class->GetPathName());
    Value->SetStringField(TEXT("from"), Definition->From.ToString());
    auto Text = MakeShared<FJsonObject>();
    Text->SetStringField(TEXT("name"), Definition->DisplayName);
    if (!Definition->Description.IsEmpty())
        Text->SetStringField(TEXT("description"), Definition->Description);
    Value->SetObjectField(TEXT("text"), Text);
    if (!Definition->Icon.IsNull())
        Value->SetStringField(TEXT("icon"), Definition->Icon.ToSoftObjectPath().ToString());
    if (Definition->Kind == EP4AddedContentKind::Treasure)
    {
        if (Definition->Sparklium >= 0)
            Value->SetNumberField(TEXT("sparklium"), Definition->Sparklium);
        if (Definition->MinimumCarriers >= 1)
            Value->SetArrayField(TEXT("carriers"),
                {MakeShared<FJsonValueNumber>(Definition->MinimumCarriers),
                    MakeShared<FJsonValueNumber>(Definition->MaximumCarriers)});
    }
    else if (Definition->Life >= 0.f)
        Value->SetNumberField(TEXT("life"), Definition->Life);
    Out = Value;
    Error.Reset();
    return true;
}
