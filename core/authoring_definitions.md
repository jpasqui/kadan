# Native author definition boundary

`authoring_definitions.h` supplies typed requests and the first-party manifest writer in
`P4MapKit::Definitions`. It targets the final Tsuchi `content` and `new_maps` loaders,
not the legacy treasure APIs or Map Loader. It creates no UE assets and performs no
cook, source acquisition, native row mutation, or runtime registration.

`Build(Descriptor, Set, Context, Result, Refusal)` validates requests privately.
`FrameworkFiles(Descriptor, MapEdit*, vector<TableEdit::Table>, Set, Context,
Files, Result, Refusal)` composes map changes, original-table field deltas and
definitions without accepting caller JSON. Both leave their output unchanged on
failure. `Optional<T>::Present` distinguishes omitted fields from explicit zero,
empty text, empty catalog objects, and empty Pikmin lists. It is not a proof flag.

## Source and cook admission

The editor helper acquires an original donor through the native, strongly held,
cook-compared `P4PrepareOriginalTableSourceNative` result. Copy the successful
result's `SourceDigestKind` (`header-body-sha256-v1`), `SourceDigest`,
`ComparisonSchema` (`object-lines-v1`), and `ComparisonDigest` into `RowSource`.
The source producer supplies `CustodyKey`; an author does not invent it. Preserve
the exact `Table`, `Row`, and `RowStruct`. `FieldTypes` holds the portable scalar
serialization category for an exact reflected path. It does not replace the full
held native schema, FProperty class, width, referenced class, or enum identity.

The native export driver independently reopens/verifies the held source, matches
the source record, resolves row/class identities, admits each field using its
actual FProperty, checks creator source custody, and checks the actual cooked
class/package inventory. Valid digest strings and tokens alone do not admit
anything. The driver obtains `Context` from the verified target game/table
contract and occupied IDs/creator recipes. An authored string or slot list does
not establish the target build or census.

`Result` retains `Assets`, `Donors`, `Rows`, `Reservations`, and `Slots` for that
independent admission. These fields and their custody/digest information never
appear in the public manifest. Other-mod donor IDs require a separately resolved
native row and an actual running/declared dependency; the driver must prove that
relationship. A reference to another mod's asset additionally carries its owner
and uses that owner's derived identity root. A game reference needs the actual
stock inventory; changing its `Origin` is not source proof.

## Treasures and creatures

`Treasure` and `Creature` take a short local `Id`; reservation identities are
`treasures:<mod>:<id>` and `creatures:<mod>:<id>`. `Series` uses
`treasure_series:<mod>:<id>`. The manifest retains the short keys and the runtime
joins the owner's mod ID. Plain original IDs are case-insensitive; added IDs are
`mod:local`, with lowercase mod spelling. Numeric placeholders are deliberately
outside this first authoring boundary.

`Donor` contains the logical `Id`, resolved `NativeRow`, original `ClassPath`,
and exact held source rows. The required parameter table is
`DT_OtakaraParameter` for treasures or `DT_TekiParameter` for creatures, together
with matching native rows in `DT_ZukanBP` and `DT_ActorTexture`. Plain donor IDs
must match the native row. An optional catalog requires the matching native row
in `DT_ZukanOtakaraParameter` or `DT_ZukanTekiParameter`. Generic `values` binds
only to the parameter source schema; catalog values bind to the catalog schema.
Whole-row additions/removal and unsupported structured values are refused.

Creator `Class`/`NightClass` references are unique generated classes in their
mod's derived `/Game/Mods/<slug_hash>` root. Their explicit type label is
`BlueprintGeneratedClass`; the native driver verifies their actual inheritance
against the held donor class. An ordinary editor Blueprint preserves the donor's
inherited `CID` property, type and default. It never writes literal `mod:id` into
that property. The runtime assigns its own allocated native row name to the
class default object's readable `CID` after loading. The native helper must
refuse a class without the qualified inherited property.

The public fields are exactly:

- Treasure: `class`, `from`, `text`, optional `sparklium`, `carriers`,
  `battle_carriers`, `icon`, `values`, `catalog`.
- Treasure text: `name`, `catalog_name`, `result_name`, `description`,
  `olimar_notes`, `louie_notes`; catalog: local `series`, optional `values`.
- Creature: `class`, `from`, `text`, optional `night_class`, `life`, `sparklium`,
  `carriers`, `battle_carriers`, `pikmin`, `icon`, `values`, `night`, `catalog`.
- Creature text: `name`, `scientific_name`, `family`, `description`,
  `olimar_notes`, `louie_notes`. `night` accepts only the numeric/carrier/value
  fields. Creature catalog can be explicitly empty or contain `values`.

Missing numeric fields inherit the donor. No numeric default is fabricated.
Life is finite and greater than zero, through `INT32_MAX`; other explicit
numbers are nonnegative signed 32-bit integers. Carrier ranges are positive and
ordered. `Texture2D` icon references retain their explicit origin requirements.

## Maps

`NewMap` reserves `new_maps:<mod>:<id>`. `MapBuild` carries an explicit `Area`,
creator World, scene, root scene, and counts asset. The verified first target is
version `1.1.1`, code build `060F827BD2B38703E9EA11E3210D89BC`, free areas
`5, 7, 8, 9`. `SlotClaim.Id` is the exact joined `<mod>:<map>` ID. Duplicate
claims or builds refuse; an existing identical claim can be reused, while a
different claimant blocks that area. The runtime, rather than the authoring
helper, durably allocates a player's actual slot.

World package names are `AreaNNN_P`; scene package/object names are
`SD_AreaNNN`, with matching numbers. World uses type `World`; scene and root
scene use `/Script/Carrot4.GameSceneData`; counts uses
`/Script/Carrot4.ActorPlacedNumAsset`. All four must be creator assets.
Root/counts references are private requirements, not extra manifest fields.
The driver must independently prove that the cooked scene names the World,
points to the qualified root scene, and points to the qualified counts object.
The native runtime checks parent/world/counts at schema offsets `0x30`, `0x50`,
`0xA0`; these offsets do not establish reflected editor property names or values.
The UE helper reads those names/types from the actual original schema and refuses
when it cannot bind them. It does not guess count fields or initialize a map by
silently zeroing original data.

Public map fields are `name`, `builds` (only `world`, `scene`), optional `preview`,
`radar`, `pikmin`, `totals`. Radar accepts `texture`, `offset`, `size`, `angle`,
`scale`, `limits`; totals accepts only `treasures`, `castaways`, `flarlic`, from
zero to 10000. Map names are strict UTF-8, at most 160 UTF-16 units. No day-detail
field exists in this final manifest. Missing preview/radar/detail declarations
are not fabricated from another map.

## Examples and evidence limits

`tests/authoring_definitions_test.cpp` supplies typed examples for an Eclair-based
treasure, a KOCHAPPY-based creature with night/catalog declarations, and an
Area007 floor with preview/radar. Its source facts, hashes, fields and class paths
are explicitly synthetic. It checks exact public projection, composition,
refusal and unchanged output; it proves neither an editable source nor a cook.
Run `tools/navigation/test_authoring_definitions.py` to compile it as C++14 with
warnings treated as errors and check the parsed manifest.

The ordinary UE helper and export driver must produce real examples of each
kind from their native source producers, save/reopen them, cook them and verify
the independent readback. Runtime behavior/class compatibility and row
publication stay unsupported until the actual source/cook/runtime evidence
qualifies them. This contract does not enable arbitrary authored AI or create a
new native game class from a property schema.
