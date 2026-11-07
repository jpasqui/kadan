# Original lighting source facts

`OriginalLighting::ReadPackageFacts(package, header, body, facts, error)` reads
an immutable installed-user package pair. `package` is a canonical `/Game/`
identity from the source inventory, never a filesystem path. The constructor
validates the supported summary through `lighting_package.h` and records its
UE3/UE4 versions and explicit retoc-latest form; the returned
capability binds the exact header, body, and concatenated SHA-256. `Matches`
revalidates bytes before consuming facts. Input bytes and source inventory
custody remain the caller's responsibility; digest equality authenticates
neither a game's origin nor a runtime state. A retoc summary digest describes
the actual extracted pair; it is not mislabeled as an original cooked-header digest.

`Objects()` retains each selected light, sky, reflection capture, WorldSettings,
environment controller, sky sphere, World/Level, and native Carrot4 EnvParam
export. Every stored property has its exact tag/payload envelope and decoded
typed value, including hard-reference archive indices, nested curves, GUIDs,
and names. Nothing merges inherited properties or evaluates a curve. Native
World and Level tails use the existing bounded full-span readers. Invalid,
duplicate, imported, or out-of-range live actor entries refuse the whole request.
Other tails are recorded by span and digest; only a completely consumed object
ID trailer is recognized. Unknown tails retain an explicit unresolved issue.

`MissingFields` means absent from this export's tagged list. It does not mean
zero, an engine default, an inherited value, or a game's native CDO value.
For Scene/Lighting/WorldSettings presets the required names follow the existing
observed native field schema (17/30/5); field names establish no ABI or runtime
producer. Other component requirements are an explicit diagnostic subset,
with all unknown native default requirements still unresolved.
`Dependencies` preserves reference spellings; a local export spelling requires
the containing package identity and its retained archive index. Template
inheritance, native defaults, attachments/sockets/world transforms, runtime
Day/Night selection, curve evaluation, and native Rendering production remain
unresolved. Stored `LightGuid` is a source fact, never a supplied GUID or proof
of a reconstructed stationary sun. No caller rotation, fitted recovered light,
empty BuiltData registry, or default recipe can complete this authority.

`OriginalLightingAuthority()` remains false. These facts are a separate
boundary from `CreatorLighting::Recipe`, editor factories, Lightmass builds,
cook/export comparison, renderer tests, and Pikmin behavior. The helper is
unreferenced by the released importer until those owners explicitly wire a
qualified policy. Refusal publishes no partial output.

The diagnostic CLI accepts `package header body`, or `--batch` with one
`package TAB header TAB body` request per line. It emits source facts without
machine filenames or private provenance. The Python test uses private original
files only as an independent oracle and never bundles them.
