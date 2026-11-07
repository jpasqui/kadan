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
    Locked,
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
    {EGameAssetFamily::AnimationBlueprint, EGameAssetShipping::Locked},
    {EGameAssetFamily::WidgetBlueprint, EGameAssetShipping::Locked},
    {EGameAssetFamily::BlueprintInterface, EGameAssetShipping::Locked},
    {EGameAssetFamily::OtherBlueprint, EGameAssetShipping::Locked},
    {EGameAssetFamily::Texture, EGameAssetShipping::Locked},
    {EGameAssetFamily::StaticMesh, EGameAssetShipping::Locked},
    {EGameAssetFamily::SkeletalMesh, EGameAssetShipping::Locked},
    {EGameAssetFamily::Material, EGameAssetShipping::Locked},
    // Whole original parameter, texture, base, static-switch and C4 edits observed in game, 2026-10-25.
    {EGameAssetFamily::MaterialInstance, EGameAssetShipping::WholePackage},
    {EGameAssetFamily::MaterialParameters, EGameAssetShipping::Locked},
    {EGameAssetFamily::Animation, EGameAssetShipping::Locked},
    {EGameAssetFamily::Skeleton, EGameAssetShipping::Locked},
    {EGameAssetFamily::PhysicsAsset, EGameAssetShipping::Locked},
    {EGameAssetFamily::Particle, EGameAssetShipping::Locked},
    {EGameAssetFamily::Niagara, EGameAssetShipping::Locked},
    {EGameAssetFamily::Sound, EGameAssetShipping::Locked},
    {EGameAssetFamily::AudioEvent, EGameAssetShipping::Locked},
    {EGameAssetFamily::Sequence, EGameAssetShipping::Locked},
    {EGameAssetFamily::Curve, EGameAssetShipping::Locked},
    {EGameAssetFamily::Font, EGameAssetShipping::Locked},
    {EGameAssetFamily::DataAsset, EGameAssetShipping::Locked},
    {EGameAssetFamily::UserStruct, EGameAssetShipping::Locked},
    {EGameAssetFamily::UserEnum, EGameAssetShipping::Locked},
    {EGameAssetFamily::Other, EGameAssetShipping::Locked},
};
constexpr EGameAssetShipping GameAssetShipping(EGameAssetFamily Family)
{
    for (const auto& Row : GameAssetFamilies)
        if (Row.Family == Family)
            return Row.Shipping;
    return EGameAssetShipping::Locked;
}
} //namespace P4Kadan
