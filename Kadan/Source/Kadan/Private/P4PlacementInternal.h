// What the placement export files share: the helpers more than one of them uses.
#pragma once
#include "P4PlacementExport.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "P4AddedContent.h"
#include "P4LevelChanges.h"
#include "P4MapKitCore/placement_params.h"
#include "P4MapKitCore/property_decoder.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Level.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/EnumProperty.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UnrealType.h"

namespace P4PlacementDetail
{
namespace P = P4MapKit::Placement;
using P4MapKit::FDecodedValue;
UClass* GameClass(const TCHAR* Name);
std::string Utf8(const FString& Text);
FString Wide(const std::string& Text);
bool Derives(const UClass* Class, const TCHAR* Native);
UObject* OwnerOf(AActor* Actor, const P::SlotName& Slot);
UActorComponent* ChainComponentOf(AActor* Actor);
bool NotAProperty(const P::Field& Field);
std::vector<P::Field> LayoutFields(P::Slot Slot, const P::Context& Context, int Variant = 0);
const P::Field* FieldNamed(const std::vector<P::Field>& Fields, const std::string& Name);
bool Resolve(UObject* Object, const FString& Path, FProperty*& Leaf, void*& Data, FString& Error);
bool ResolvePlacementField(
    UObject* Owner, AActor* Actor, const FString& Path, FProperty*& Leaf, void*& Data, FString& Error);
const FNumericProperty* Numeric(const FProperty* Field);
bool ToValue(const FProperty* Field, const void* Data, const P::Type& Type, P::Value& Out, FString& Error);
int32 CountOf(UObject* Owner, const TCHAR* Path);
USceneComponent* SceneComponentNamed(AActor* Actor, const FName Name);
bool ComponentValue(AActor* Actor, const P::Field& Field, P::Value& Out, FString& Error);
bool TaggedRun(AActor* Actor, const std::string& Tag, uint64 Count, std::vector<P::Value>& Out);
bool FromValue(const FProperty* Field, void* Data, const P::Type& Type, const P::Value& In, FString& Error);
std::vector<std::string> Canonical(const P::Value& Value, const std::string& Path);
P::Context ContextOf(AActor* Actor);
FString SlotKey(const P::SlotName& Slot);
const P::SlotName* SlotNamed(const FString& Key);
bool ReadRowBlob(
    const FP4GamePlacementRow& Row, const FString& Key, const P::Context& Context, P::Blob& Out, FString& Why);
bool NotAnOption(const FString& Key);
uint64 PlacementNumber(const AActor* Actor);
FString LevelLeaf(const AActor* Actor);
FString ActorKey(const AActor* Actor);
} // namespace P4PlacementDetail
