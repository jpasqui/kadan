#include "P4CollisionSettings.h"
#include "Engine/CollisionProfile.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/UnrealType.h"

namespace P4CollisionSettings
{
// What the settings save, with names as text.
TArray<uint8> Said(FBodyInstance& Instance)
{
    TArray<uint8> Bytes;
    FMemoryWriter Memory(Bytes, true);
    FObjectAndNameAsStringProxyArchive Writer(Memory, false);
    FBodyInstance::StaticStruct()->SerializeItem(Writer, &Instance, nullptr);
    return Bytes;
}
} //namespace P4CollisionSettings

FString P4CollisionProjectIssue(FBodyInstance& Instance, UObject* Owner)
{
    const UCollisionProfile* Project = UCollisionProfile::Get();
    const FArrayProperty* Responses = FindFProperty<FArrayProperty>(
        FCollisionResponse::StaticStruct(), TEXT("ResponseArray"));
    if (!Project || !Owner || !Responses || !CastField<FStructProperty>(Responses->Inner) ||
        CastField<FStructProperty>(Responses->Inner)->Struct != FResponseChannel::StaticStruct())
        return TEXT("collision-settings-unreadable");
    const FName Profile = Instance.GetCollisionProfileName();
    FCollisionResponseTemplate Template;
    // Custom is the engine's word for settings that follow no profile.
    if (Profile != NAME_None && Profile != UCollisionProfile::CustomCollisionProfileName &&
        !Project->GetProfileTemplate(Profile, Template))
        return TEXT("collision-profile-not-in-the-project: ") + Profile.ToString();
    TSet<FName> Known;
    for (int32 Index = 0; Index < 32; ++Index)
        Known.Add(Project->ReturnChannelNameFromContainerIndex(Index));
    FScriptArrayHelper Rows(Responses, Responses->ContainerPtrToValuePtr<void>(&Instance.GetCollisionResponse()));
    for (int32 Row = 0; Row < Rows.Num(); ++Row)
    {
        const FName Channel = reinterpret_cast<const FResponseChannel*>(Rows.GetRawPtr(Row))->Channel;
        if (!Known.Contains(Channel))
            return TEXT("collision-channel-not-in-the-project: ") + Channel.ToString();
    }
    const TArray<uint8> Before = P4CollisionSettings::Said(Instance);
    Instance.FixupData(Owner);
    if (P4CollisionSettings::Said(Instance) != Before)
        return TEXT("collision-profile-differs-in-the-project: ") + Profile.ToString();
    return FString();
}
