#pragma once

// The sole release gate for editing imported game assets. Change a family's row only after
// a whole edited asset has been observed in the game. Levels/tables retain their delta routes.
// No engine dependency: the release gate is also checked by a small host program.
namespace P4Kadan
{
enum class EGameAssetFamily
{
    Level,
    DataTable,
    ActorBlueprint,
    AnimationBlueprint,
    WidgetBlueprint,
    BlueprintInterface,
    OtherBlueprint,
    Texture,
    StaticMesh,
    SkeletalMesh,
    Material,
    MaterialInstance,
    MaterialParameters,
    Animation,
    Skeleton,
    PhysicsAsset,
    Particle,
    Niagara,
    Sound,
    AudioEvent,
    Sequence,
    Curve,
    Font,
    DataAsset,
    UserStruct,
    UserEnum,
    Other,
    Count
};
enum class EGameAssetShipping
{
    Unsupported,
    LevelChanges,
    TableFields,
    WholePackage
};
struct FGameAssetFamilyPolicy
{
    EGameAssetFamily Family;
    EGameAssetShipping Shipping;
};
constexpr FGameAssetFamilyPolicy GameAssetFamilies[] = {
    {EGameAssetFamily::Level, EGameAssetShipping::LevelChanges},
    {EGameAssetFamily::DataTable, EGameAssetShipping::TableFields},
    // GCrackPotS whole-package value/component/BeginPlay edits observed in Cave000, 2026-10-25.
    {EGameAssetFamily::ActorBlueprint, EGameAssetShipping::WholePackage},
    // Whole original parameter, texture, base, static-switch and C4 edits observed in game, 2026-10-25.
    {EGameAssetFamily::MaterialInstance, EGameAssetShipping::WholePackage},
    {EGameAssetFamily::Skeleton, EGameAssetShipping::WholePackage},
};
constexpr EGameAssetShipping GameAssetShipping(EGameAssetFamily Family)
{
    for (const auto& Row : GameAssetFamilies)
        if (Row.Family == Family)
            return Row.Shipping;
    return EGameAssetShipping::Unsupported;
}
} //namespace P4Kadan
