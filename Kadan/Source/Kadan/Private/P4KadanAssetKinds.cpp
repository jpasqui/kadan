// The kinds of the game's packages as the import sorts them: the order they import in, what they
// follow, and which stay the game's.

#include "P4KadanImportInternal.h"
#include "P4KadanImportDependencies.h"

using namespace P4KadanImport;

namespace P4KadanImport
{
bool RetainedNativeKind(const FName Kind)
{
    // These families now have exact stored-field/native-tail reconstruction.
    // Refresh their retained dependencies without expanding sound/media support.
    return Kind == TEXT("DataTable") || Kind == TEXT("ParticleSystem") || Kind == TEXT("EftCustomRequest") ||
        Kind == TEXT("VectorFieldStatic") || Kind == TEXT("FoliageGridDataAsset") || Kind == TEXT("AnimSequence") ||
        Kind == TEXT("AnimMontage") || Kind == TEXT("SkeletalMesh") || Kind == TEXT("EnvParamWorldSettings");
}

int32 Rank(const FString& Kind)
{
    // A media texture names its player, and a player its playlist and sources, so they come first.
    if (Kind == TEXT("MediaPlayer") || Kind == TEXT("MediaPlaylist") ||
        Kind.EndsWith(TEXT("MediaSource"), ESearchCase::CaseSensitive))
        return -1;
    if (Kind.StartsWith(TEXT("Texture")) || Kind == TEXT("MediaTexture") || Kind == TEXT("VolumeTexture"))
        return 0;
    if (Kind == TEXT("MaterialParameterCollection") || Kind == TEXT("MaterialFunction"))
        return 1;
    if (Kind == TEXT("Material"))
        return 2;
    if (Kind == TEXT("MaterialInstanceConstant"))
        return 3;
    if (Kind == TEXT("StaticMesh") || Kind == TEXT("SkeletalMesh") || Kind == TEXT("Skeleton") ||
        Kind == TEXT("PhysicsAsset"))
        return 4;
    if (Kind == TEXT("Blueprint") || Kind == TEXT("AnimBlueprint") || Kind == TEXT("WidgetBlueprint") ||
        Kind == TEXT("LevelSequence"))
        return 6;
    if (Kind == TEXT("World"))
        return 7;
    return 5;
}

// Levels and placement lists follow soft references to their contents.
// Other assets follow hard references such as classes, meshes, materials,
// textures and animations. Their other soft references remain paths.
bool FollowsSoftReferences(const FString& Kind)
{
    return P4KadanImportFollowsSoftReferences(Kind);
}

bool SoftDependency(const FP4KadanSourceAsset& Asset, const FString& Package)
{
    const FName Identity(*Package);
    return Asset.SoftDependencies.ContainsByPredicate([&](const FString& Name) { return FName(*Name) == Identity; });
}

bool BlueprintKind(const FString& Kind)
{
    return Kind == TEXT("Blueprint") || Kind == TEXT("AnimBlueprint") || Kind == TEXT("WidgetBlueprint") ||
        Kind == TEXT("LevelSequence");
}

FString KindName(const FString& Original)
{
    if (Original == TEXT("BlueprintGeneratedClass"))
        return TEXT("Blueprint");
    if (Original == TEXT("AnimBlueprintGeneratedClass"))
        return TEXT("AnimBlueprint");
    if (Original == TEXT("WidgetBlueprintGeneratedClass"))
        return TEXT("WidgetBlueprint");
    return Original;
}

} // namespace P4KadanImport

// The kinds a map import does not bring in from the game's containers: what placed things need in
// the editor is meshes, skeletons, physics, materials, textures, curves, animations and Blueprints.
// Sound, interface, effects, fonts, media and tables a Blueprint names stay the game's own.
bool P4KadanKeptAsTheGames(const FString& Kind)
{
    static const TCHAR* Kinds[] = {TEXT("WidgetBlueprint"), TEXT("Font"), TEXT("FontFace"), TEXT("ParticleSystem"),
        TEXT("EftRequest"), TEXT("EftCustomRequest"), TEXT("PaperSprite"), TEXT("FileMediaSource"), TEXT("MediaPlayer"),
        TEXT("MediaTexture"), TEXT("TextureRenderTarget2D"), TEXT("DataTable"), TEXT("SoundWave"), TEXT("SoundCue")};
    for (const TCHAR* One : Kinds)
        if (Kind == One)
            return true;
    return Kind.StartsWith(TEXT("Ak"), ESearchCase::CaseSensitive) ||
        Kind.StartsWith(TEXT("UIParticle"), ESearchCase::CaseSensitive);
}
