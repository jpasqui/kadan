#include "P4InheritedTemplateAliases.h"
#include "P4Common.h"
#include "P4CreatorSource.h"
#include "P4OriginalRecord.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
void AliasTrace(const TCHAR* Stage, const FString& Detail = FString())
{
#if P4_KADAN_TEST_SEAMS
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_INHERITED_TEMPLATE_ALIAS")) == TEXT("1"))
        UE_LOG(LogTemp, Display, TEXT("Inherited source alias: %s %s"), Stage, *Detail);
#endif
}

FString CookPath(FString Path)
{
    Path.ReplaceInline(TEXT(":"), TEXT("."));
    return Path;
}

bool SameScalar(const TSharedPtr<FJsonObject>& Value, UObject* Parent)
{
    FString Name, Text, Type;
    double Index = -1;
    if (!Value.IsValid() || !Value->TryGetStringField(TEXT("name"), Name) ||
        !Value->TryGetStringField(TEXT("text"), Text) || !Value->TryGetStringField(TEXT("type"), Type) ||
        !Value->TryGetNumberField(TEXT("index"), Index) || Index != 0)
    {
        AliasTrace(TEXT("scalar-schema"), Name + TEXT(" ") + Type);
        return false;
    }
    FProperty* Property = FindFProperty<FProperty>(Parent->GetClass(), *Name);
    if (!Property || Property->ArrayDim != 1 || Property->GetClass()->GetName() != Type ||
        Property->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly | CPF_SkipSerialization) ||
        !(CastField<FBoolProperty>(Property) || CastField<FNumericProperty>(Property) ||
            CastField<FEnumProperty>(Property) || CastField<FNameProperty>(Property)))
    {
        AliasTrace(TEXT("scalar-field"), Name + TEXT(" ") + Type);
        return false;
    }
    void* Scratch = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
    Property->InitializeValue(Scratch);
    const TCHAR* End = Property->ImportText(*Text, Scratch, PPF_None, Parent);
    const bool Equal = End && !*End &&
        Property->Identical(Scratch, Property->ContainerPtrToValuePtr<void>(Parent), PPF_None);
    Property->DestroyValue(Scratch);
    FMemory::Free(Scratch);
    AliasTrace(Equal ? TEXT("scalar-equal") : TEXT("scalar-different"), Name + TEXT(" ") + Text);
    return Equal;
}

bool RedundantSourceOverride(UActorComponent* Part, const FString& Before, const FString& Now)
{
    AActor* Actor = Part ? Part->GetOwner() : nullptr;
    UActorComponent* Parent = Part ? Cast<UActorComponent>(Part->GetArchetype()) : nullptr;
    if (!Actor || !Parent || Parent->GetClass() != Part->GetClass() ||
        !CookPath(Parent->GetPathName()).Equals(Now, ESearchCase::IgnoreCase))
    {
        AliasTrace(TEXT("parent-unavailable"), Now);
        return false;
    }
    for (UClass* Class = Actor->GetClass(); Class; Class = Class->GetSuperClass())
    {
        auto* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy);
        FP4OriginalRecord Provenance;
        FString Error;
        // Blueprint recovery records intentionally have no source_digest.
        // Their typed source templates accompany the recovered marker; a
        // stand-in or creator class cannot qualify this equivalence.
        if (!Blueprint || P4HasCreatorSource(Blueprint) || !P4ReadOriginalRecord(Blueprint, Provenance, Error) ||
            Provenance.Kind != TEXT("blueprint") || Provenance.Fidelity != TEXT("recovered") ||
            Provenance.SourceDigestKind != TEXT("blueprint-import"))
        {
            AliasTrace(TEXT("class-source-unavailable"), GetPathNameSafe(Blueprint) + TEXT(" ") + Error);
            continue;
        }
        const FString Prefix = Class->GetPathName() + TEXT(".");
        if (!Before.StartsWith(Prefix, ESearchCase::IgnoreCase))
        {
            AliasTrace(TEXT("different-owner-prefix"), Prefix);
            continue;
        }
        TSharedPtr<FJsonObject> Source;
        const TArray<TSharedPtr<FJsonValue>>* Overrides = nullptr;
        FString Identity;
        const FString Text = Blueprint->GetOutermost()->GetMetaData()->GetValue(
            Blueprint, TEXT("P4OriginalInheritedTemplates"));
        if (!P4ParseJson(Text, Source) || !Source->TryGetStringField(TEXT("blueprint"), Identity) ||
            Identity != Blueprint->GetPathName() || !Source->TryGetArrayField(TEXT("overrides"), Overrides) ||
            Overrides->Num() > 256)
        {
            AliasTrace(TEXT("metadata-schema"), GetPathNameSafe(Blueprint));
            return false;
        }
        for (const auto& Item : *Overrides)
        {
            const auto Record = Item.IsValid() ? Item->AsObject() : nullptr;
            FString Variable, Owner, Kind;
            const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
            const TArray<TSharedPtr<FJsonValue>>* Subobjects = nullptr;
            if (!Record.IsValid() || !Record->TryGetStringField(TEXT("variable"), Variable) ||
                !Record->TryGetStringField(TEXT("owner"), Owner) || !Record->TryGetStringField(TEXT("cls"), Kind) ||
                !Record->TryGetArrayField(TEXT("values"), Values) || Values->Num() == 0 || Values->Num() > 256 ||
                !Record->TryGetArrayField(TEXT("subobjects"), Subobjects) || Subobjects->Num() != 0 ||
                Kind != Parent->GetClass()->GetPathName())
                continue;
            const FString Suffix = Variable + TEXT("_GEN_VARIABLE");
            if ((!Before.Equals(Prefix + Suffix, ESearchCase::IgnoreCase) &&
                    !Before.Equals(Prefix + TEXT("ICH-") + Suffix, ESearchCase::IgnoreCase)) ||
                !Now.Equals(Owner + TEXT(".") + Suffix, ESearchCase::IgnoreCase))
            {
                AliasTrace(TEXT("different-slot"), Variable + TEXT(" ") + Owner);
                continue;
            }
            // Reconstruction duplicates the inherited template and applies these
            // exact source fields. Stock compilation/load prunes it when every
            // applied field already equals the inherited value. No object or
            // instanced-subobject override is admitted by this scalar proof.
            for (const auto& Value : *Values)
                if (!SameScalar(Value.IsValid() ? Value->AsObject() : nullptr, Parent))
                    return false;
            AliasTrace(TEXT("qualified"), Before);
            return true;
        }
    }
    return false;
}
} // namespace

void P4WithInheritedTemplateAliases(UWorld* World, const TArray<FString>& Imported, TArray<FString>& Content)
{
    if (!IsValid(World) || !World->PersistentLevel)
        return;
    AliasTrace(TEXT("begin"), FString::Printf(TEXT("%d %d"), Imported.Num(), Content.Num()));
    const FString Mark = TEXT(" | made from ");
    TMap<FString, FString> Expected;
    for (const FString& Line : Imported)
    {
        FString Path, Archetype;
        if (Line.Split(Mark, &Path, &Archetype, ESearchCase::CaseSensitive))
            Expected.Add(Path, Archetype);
    }
    bool Changed = false;
    for (FString& Line : Content)
    {
        FString Path, Archetype;
        if (!Line.Split(Mark, &Path, &Archetype, ESearchCase::CaseSensitive))
            continue;
        const FString* Previous = Expected.Find(Path);
        if (!Previous || Previous->Equals(Archetype, ESearchCase::IgnoreCase))
            continue;
        auto* Part = Cast<UActorComponent>(StaticFindObject(UObject::StaticClass(), World->GetOutermost(), *Path));
        AliasTrace(TEXT("candidate"), Path + TEXT(" part=") + GetPathNameSafe(Part));
        if (Part && Part->GetOwner() && Part->GetOwner()->GetLevel() == World->PersistentLevel &&
            RedundantSourceOverride(Part, *Previous, Archetype))
        {
            Line = Path + Mark + *Previous;
            Changed = true;
        }
    }
    if (Changed)
        Content.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
}
