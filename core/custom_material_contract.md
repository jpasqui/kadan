# Native custom material contract

The portable graph compiler and material binding helpers run in process. They
require no Python, driver compiler, editor process or external compiler program
at runtime. The shader backend calls the native assembly compiler with resolved
assembly and builds its own code, control metadata and payload.

`custom_material_support.h` exposes `NodeAvailability`, `RenderAvailability` and
`GraphAvailability`. The last accepts optional texture parameter mappings and
material function definitions. Node availability describes compiler capability;
render availability separately describes the requested material and pass route.
An editor may show recovered materials even when game export is unavailable.

The current admission contract is version 1 for both graph and render bindings.
Only newly authored Surface graphs use this compiler. Recovered game master
graphs stay read-only. Supported diagnostic routes use Opaque or Masked and
DefaultLit or emissive-only Unlit. Mesh routes are static and instanced static;
Masked instanced meshes are unavailable until their per-instance LOD dither is
ported. Other blend modes, domains, shading models and vertex factories return
named refusals.

Game lighting currently returns `game-lighting-unproved`: the complete indirect
and direct lighting producers, clustered loop and material/pass composition
still need joined shader and game proof. The existing sun
and fixed ambient recipe is admitted only with `lighting="diagnostic-sun"` and
`production=false`. It does not authorize a release export.

`CompileMaterialInputs` emits the material pins before lighting, including
normal, base color, metallic, specular, roughness, ambient occlusion, emissive
and the masked opacity test. `custom_material_lighting.h` emits independent
typed blocks for the original BRDF parameters, high-quality lightmap decode,
distance-field coverage, reflection mip, fog composition, C4 reflection
occlusion and opaque directional diffuse/specular response. These functions
receive explicit inputs. They do not infer a packed game buffer layout or copy
an original shader tail. A complete game policy still needs qualified direct
shadow filtering, clustered light iteration and shading-model-specific bindings.

## Compilation and binding stages

1. `CompileGraph` expands functions and compiles graph nodes into new pixel
   assembly. Its placeholders name texture parameters and vertex interpolants.
2. `ReadPixelBindings` reads structural metadata from the original pass payload
   and skips original code and control bytes. The serialized pixel marker is
   `0x5053`, the little-endian representation of the engine's `'PS'` tag.
3. `ResolveMaterialBindings` pairs Material texture and sampler resource words
   with the frozen uniform texture names. `Resolve` validates cache and varying
   ranges and replaces placeholders with exact pass bindings.
4. For a new texture list, `ReplaceTextureList` rewrites typed frozen texture and
   resource layout sections, recomputes their relative pointers and relocates
   raw FName identities. `RebindMaterialTextures` reserves other buffers, loose
   shader textures/samplers and the default sampler before assigning slots.
5. The native backend compiles assembly. `PixelPayload` writes a new structural
   header and `CleanControl` accepts only computed native compiler metadata.
   Original assembly and control sections are never emitted.
6. `WriteShaderSet3` binds each new material to the original cooked shader map
   **ID hash**, shader count and per-map shader positions. It writes payload
   SHA-1 hashes and optional content COPY/OWN operations with FName patches.

The archive ResourceHash locates its shader map in an original archive. It is
distinct from the cooked shader map ID hash used by the runtime format 3 row.
Unknown pass or vertex factory identities must be refused, never inferred from
another shader occupying the same archive position.

## Identity and export boundaries

All shader type hashes, vertex factory hashes and original binary offsets are
read using typed bounded binary readers. JSON metadata represents 64-bit hashes
and opaque digests as hex strings. Authoring CLI numeric identity fields are
bounded unsigned integers no wider than 32 bits, and reject fractions, negative
values and values outside their declared range before conversion. Graph math
constants remain finite floating point values.

`RecipeDigest` binds deterministic graph, settings, template identity and pass
facts. `ValidateRecipe` requires the graph and binding versions, exact SHA-256
digest, cooked shader map ID and material identities. The runtime shader set
does not itself serve as proof of a lighting or mesh route.

COPY operations refer to original frozen bytes supplied by the player's game.
OWN operations contain newly written structural metadata. New shader sets do
not contain original game shader code, original assembly, original control
sections or full original assets. Keep private evidence and local paths out of
shareable exports.

Offline qualification tools are `tools/navigation/test_native_custom_material.py`
and `tools/navigation/test_native_custom_material_bindings.py`. The additional
`tools/navigation/test_native_custom_material_lighting.py` compares individually
generated mathematical blocks against bounded original executable slices with
strict numeric equality and explicit policy census checks. It includes zero
roughness, zero occlusion, disabled specular cap and back-facing light cases.
The CPU evaluator uses Python arithmetic; GPU scheduling and live rendering
remain separate proofs. These tools compare
assembly, typed layouts, relocated frozen bytes, control writers and shader set
layout with the original independent implementation. Their passing results do
not replace editor build, runtime acceptance or live render validation.

`ReadForwardPolicyBindings` qualifies the exact View and opaque BasePass layouts
before exposing named texture, sampler and SRV roles. It retains missing roles
as unavailable. Fog sampling explicitly uses the View shared clamp sampler;
the header default sampler is an independent binding.

`ReflectionMix` and `ReflectionResponse` accept separate captured radiance,
extra skylight, capture coverage, roughness mixing, material and screen occlusion,
and shadow inputs. Mixing now matches all 40 original opaque/masked local and
instanced policies. The response stage preserves the original opaque C4
occlusion polynomial and the distinct masked screen-occlusion product.

`custom_material_reflection.h` emits the original single-capture lookup from
Primitive word 412, indexed capture properties, array-cube mip sampling, HDR
brightness and color scaling. Exact original ARM64 metadata proves the Primitive
field and the capture arrays' 341-element layouts. `custom_material_skylight.h`
emits the original apply, blend and static/dynamic branches for the 20 enabled
permutations; 20 other source permutations omit these branches.

`custom_material_reflection_policy.h` joins capture selection, samples, sky,
brightness ratio, roughness mixing, environment BRDF and reflection response.
Its eight shader type identities are explicit. A continuous original reflection
stage comparison passes all 40 policies, with no intermediate sample, radiance
or BRDF seeding. Direct and indirect lighting arrive as explicit stage inputs;
this proof does not qualify their producers or complete game export.

`custom_material_lightmap.h` checks the exact PrecomputedLightingBuffer and
LightmapResourceCluster name hashes and layouts. It emits the two HQ reads,
their UV split and original coefficient decode. All 20 original HQ stages match
the independently emitted result and immutable texture handles/coordinates.

`VolumetricFogCoordinates` consumes world position, clip matrix x/y/w components
and logarithmic depth parameters. `PolicyEmitter::ApplyVolumetricFog` emits its
own enabled/disabled branch, exact validated resource handles, level-zero volume
sample and vertex/volume composition. Diagnostic `FogStageProgram` exposes its
world-position and vertex-fog inputs in a reserved hardware buffer for offline qualification;
that buffer is not an original production binding. The full policy supplies
computed values directly to the stage API.

Diagnostic stages use `ReservedDiagnosticBuffer{13, true}` only after the caller
proves that original assembly neither declares nor references buffer 13 and the
header has no conflicting resource/layout. Their payload separately declares
the synthetic input. Program buffer indices are bounded to 0..14; buffer 14 is
reserved for immutable handles. Diagnostic input declarations do not establish
a production uniform binding.

Separate reflection, fog-coordinate and full fog-stage qualification tools are
`test_native_custom_material_reflection.py`, `test_native_custom_material_fog.py`
and `test_native_custom_material_fog_stage.py`. Experimental whole reflection
producer checks require `--experimental-nearest` and write a separate receipt;
their results do not change the qualified block census or production admission.
The joined policy now has its own `test_native_custom_material_reflection_policy.py`
receipt, separate from that older experimental block. Capture, sky, HQ and all
response stages also have scoped qualification tools. Native backend compiler
receipts independently check their computed program bytes, payload headers,
control and checksums. Complete material lighting and live rendering remain
separate gates.
