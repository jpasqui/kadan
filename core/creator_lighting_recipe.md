# Creator lighting source boundary

`creator_lighting_recipe.h` is a closed typed creator input. It validates finite
sun/scene-captured-sky and Lightmass settings and encodes every field, including
exact float bits, under a versioned domain. Bounds are conservative authoring
limits, not recovered game limits. No JSON property bag or unknown field is
implicitly applied. The recipe cannot establish an original map's missing light
settings, game class defaults, original stationary-light GUID or sky assets.

The separate pending `P4CreatorLitWorldSource` native producer creates a new own
World and stock static-mesh actors from a constructor-issued mesh cook capability.
Every material slot must match a revalidated opaque material constructor/cook
capability. An explicit diagnostic policy allows only the exact engine default
material; its returned source remains marked diagnostic. Both routes report
`ProductionAdmitted() == false`.

Creation refuses loaded, saved or orphaned World/BuiltData destination bundles.
It uses no existing imported World, cloned lighting package, fallback cubemap,
shared sky material or caller-created source receipt. The first recipe captures
the constructed scene for the sky; it has no external sky asset. It records the
actual complete native serialization of a bounded census of known stock native
objects, including editor source settings and native object tails. Names and
object references use exact paths. Unknown classes, property families, maps,
sets, bound delegates, nonfinite reflected values and outside dependencies
refuse rather than being omitted. This is a session-native source seal; it is
not an independent decoder of a cooked World or an attestation of class behavior.

The factory saves only its new World and source-created mesh dependencies that
have no file. It records actual saved bundle presence/absence, byte hashes and
the current project file. File hashing holds a Windows no-write/no-delete read
lease. The source capability strongly owns its World and retains its mesh and
material producer capabilities. Revalidation checks actual live serialization,
independent source/cook/dependency capabilities, all saved file pins and absence
of preexisting BuiltData. No revalidation writes a source file. Creation failures
remove only files newly saved by that call whose exact acquired seals still
match; changed files are retained and rollback failure is reported.

These pending native files have not been compiled or exercised in Unreal. Their
automation is intended to construct and save real own diagnostic source, then
test undirtied actor/component mutations, saved byte mutation, destination
preservation and orphaned BuiltData refusal. The engine-material positive is
explicitly a source-lifecycle diagnostic, not a MIC, shader, material, mesh
platform, navigation, lighting bake or game positive.

A completed export additionally needs independently bound original lighting
when adopting an original map, a producer-bound actual native lighting process,
whole source/dependency invariance across reload, owned registry/texture census,
complete fresh/packed cooked World and registry/texture readbacks, platform and
shader admission, rebuilt navigation/collision evidence where required, and
actual game rendering and walking. Ordinary save success, caller recipe values,
process exit code or live census alone cannot replace those gates.
