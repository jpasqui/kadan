# Materials with shader maps of their own

This is a historical technical summary of the material work recorded from October 18 through
October 25, 2026. The original research journal and raw receipts remain outside the publication
repository. No new editor, emulator, or physical console run was performed for this summary.

## Material pipeline

A creator material carries its own cooked material export, inline shader map, and shader library.
The map's resource hash names the library. Tsuchi opens the mod's library when mounting the mod,
before its packages load. The game creates shader resources through its own renderer.

Kadan writes the permutation list, shader payloads and bindings, shader-map image, cooked material,
and library. The permutation list accounts for vertex factories, lighting policies, blend modes,
and usage flags. Maps and programs are authored outputs; exports do not copy the game's shader
library. A mod must keep its material, map, payloads, and library identities consistent.

The native compile route uses Unreal Engine 4.26's material translator and shader source, DXC's
HLSL-to-SPIR-V output, Kadan's SPIR-V front end, and the Maxwell backend. The compiler receives the
renderer settings and interface patches required by the game. The shader format participates in
Unreal's compile workers and Derived Data Cache. The earlier hand-written material emitter remains
a legacy route with narrower coverage.

Features that require renderer pipelines absent from the game, including hair strands, single-layer
water, and runtime virtual textures, need runtime renderer work. Shader compilation alone cannot
provide those pipelines. Unsupported stages and tessellation remain refusals where no producer is
qualified. Compiler acceptance does not establish visual fidelity or support for every editor node.

## Historical stage boundaries

| Stage | Recorded result | Evidence level and boundary |
| --- | --- | --- |
| Mod-owned libraries | Two mods, each with its own library and material, drew together. | Eden; not a physical console result. |
| Typed shader-map reconstruction | Material maps were reconstructed from typed class fields, bindings, resources, and names. | Host comparisons covered 7,361 material maps; this is separate from the later 382-master research corpus. |
| Authored vertex and geometry programs | Programs for local and instanced vertex factories were emitted and compared with matching game inputs. | Host evaluation and bounded Eden views; not complete renderer equivalence. |
| Authored material exports | Opaque, lit, translucent, masked, depth, shadow, and instanced routes progressed through authored maps and libraries. | Individual Eden controls; the coverage table records remaining producer and import limits. |
| Unreal compile target | Unreal-generated HLSL passes through DXC, the SPIR-V front end, and the Maxwell backend. | Host comparisons, native editor compilation, and selected Eden controls remain distinct results. |

The historical compiler round trip covered 16,190 game programs. That comparison is a compiler
measurement, not permission to distribute those original programs or a claim that every authored
material has the same behavior.

## Native format22 compilation, October 22

The selected research corpus contained 382 masters: 324 Surface, 43 UI, 10 PostProcess,
three DeferredDecal, and two LightFunction graphs. All 382 masters completed in 12 batches with
28,369 validation-ledger entries. Requested maps compiled, froze, and passed the native runner's
payload, name, and resource checks.

An earlier attempt stopped at batch 008's 1,000-second editor limit. Eight complete batches and
31 valid maps in the interrupted batch were retained. Resume checked 2,483 retained hashes,
compiled only the pending master in that batch, and completed batches 009 through 011 without
changing source, extending the deadline, or replacing completed output. Resume exited successfully
after 2,308.6 seconds. Native compilation does not establish game visibility or numerical parity.

A separate decoder audit joined 1,146 index, resource, and name hashes across all 382 maps.
All 381 SSR-ineligible graphs had no raw or frozen Xlu shaders. Ordinary WaterSurface was the sole
eligible graph in this corpus: 24 raw Xlu identities became 20 frozen identities, with ten SSR and
ten Recombine entries. Four ordinary cached-point standalone variants were pruned. Four
self-shadowed cached-point variants remained, with their game-policy eligibility still unproved.
This was a membership audit, not native material-setting readback or program-equivalence proof.

## Host program comparisons, October 22

The unchanged comparator requested four scenes and used a 1e-4 predicate. Its contained execution
completed in 838.6 seconds. Successful scene counts were not recorded per pair, so four requested
scenes cannot be described as four successful scenes for every pair.

| Input mode | Reported pairs | Matching pairs | Evaluator failures | Comparison time |
| --- | ---: | ---: | ---: | ---: |
| Material held constant | 9,752 | 8,049 | 73 | 322.1 seconds |
| Randomized words | 9,752 | 7,390 | 73 | 326.2 seconds |

The 73 failures are included in the pair totals. They identify the evaluator's refusal of
`FOnePassPointShadowDepthGS` at `PRIMITIVE_IN`; they do not identify a demonstrated rendering defect
or which side of the comparison failed.

All 9,752 shared class entries matched the preceding format21 report's recorded fields, with no
numerical increases or improvements. The membership change removed 72 unintended Unreal-only SSR
identities without losing an earlier matching identity. This establishes sampled non-regression
under that comparator, rather than byte or runtime equivalence.

Compared with the earlier report103 baseline, held-material mode retained nineteen numerical
increases and word mode eighteen, across 29 already nonmatching identities: eight instanced
shadow-bias cases and 21 model-6 visibility-floor cases. These findings were not waived.

Remaining findings were 104 and 71 NaN-positive pairs, 88 and 181 pairs with missing outputs in
held-material and word modes respectively, and 2,449 pairs with reported header differences.
These groups overlap. The raw comparison also contained 6,140 Unreal-only and 96 game-only
identities, including permutations absent from the game. Those counts alone do not prove runtime
defects. The four ordinary-water self-shadowed cached-point entries still lacked proven policy
eligibility.

## Bounded emulator observations, October 22

These checks used Eden, with runtime-constructed controls. They do not establish cooked initial-map
admission, first-frame readiness, physical console behavior, or complete original-program parity.

| Feature | Observation | Remaining limit |
| --- | --- | --- |
| Opaque, translucent, and two-sided | Lit and unlit surfaces, bounded transparency, and a paired backface control were visible. | Exact opacity, every lighting model, and full program equality were not established. |
| Masked | The old control surface was visible but solid green. | It supplied no accepted cut; the corrected October 23 control is recorded below. |
| Skeletal and instanced | Exported skeletal geometry and a green instanced cube drew with own and stock references. | Cropping and overlap prevented full bounds, culling, animation, geometry, and per-instance claims. |
| Active morph | Corresponding original and creator deformation at values 0 and 1 was visible at matched affine poses. | Occlusion prevented complete silhouette, section, and LOD parity claims. |
| GPU sprites | Warm original sparks and green creator sparks changed arrangement across four images. | Setup used three one-LOD GPU emitters, but the pixels did not identify each emitter, complete shape, or native-frame age. |
| CPU sprites | Original fire and creator quads changed against a fixed reference across four images. | Only CPU slots 0, 3, 4, 5, and 6 received the own material; GPU slots 1 and 2 stayed original. The mixed reference did not qualify each emitter or fire-opacity fidelity. |
| Mesh particles and beams | Recognizable sheet, tube, ring, and segmented beam geometry drew. | The template mixed eleven sprite and five mesh first-LOD rows. Beam views were partly cropped; no complete subtype or bounds proof resulted. |
| Existing-image UI | A creator checker material drew in a HUD Image. | This did not qualify a saved creator WidgetBlueprint or new layout. |
| Flowmap material | A cyan surface changed pattern phase and contrast in comparable views. | Direction, speed, refraction, SSR, physical water behavior, and original-water parity were not isolated. |
| Postprocess | Green-tinted and normal scene states appeared with corresponding markers. | The route used 13-second game-time phases; priority, numerical color, and exact image/log timestamp joins were not established. |
| Color and emissive decals | A green projection drew on an own receiver, with emissive appearance on nearby bodies. | Full receiver policy, normal response, alpha cuts, and WaterBox semantics were not qualified. |
| Light functions | A checker projection differed from a smooth cyan game control. | This was a channel-3 movable shadowless-spot diagnostic; later default-channel work is separate below. |

Earlier GPU and CPU attempts retained their inconclusive or failed statuses. Setup counters,
1,200 motion updates, and aggregate counts were not treated as presented-age or every-emitter
proof. Constant opaque creator sprite materials explain rectangular billboards without proving a
shader defect.

At this date, cube, volume, array, render-target, and media textures and saved own material-function
or material-parameter-collection packages lacked qualified producers. Static instance permutations,
saved-consumer staging, and spline assignments had narrower route-specific gaps; later results do
not erase those historical boundaries. The C4 settings registry's first-owner behavior was also a
conditional, unobserved source risk when differently configured owners shared a leaf name. Unique
names in the selected corpus did not qualify the general identity case.

## Export memory and corrected controls, October 23

The cold export memory failure came from game-content identity checking, before shader compilation.
The old path loaded 6,365 static meshes and 7,916 textures without releasing them. Private memory
rose about 45 MB per second and exceeded 10.1 GiB at 200 seconds, with no shader compiled.

The correction released gathered packages every 128 assets and filtered texture reads by source
dimensions. The same export read 6,365 meshes in 137 seconds and fourteen 2-by-2 textures in two
seconds, with a 1.87 GiB peak. A 512-by-512 creator texture caused 2,016 texture reads in eleven
seconds at 2.1 GiB. Later exports reused the digest cache. Callers needed weak references across
garbage collection; the translucent fixture's cleanup was corrected accordingly.

Five corrected controls passed: Decal, LitTranslucent, Masked, Translucent, and WaterBoxDecal,
with 2.3 to 2.4 GiB peaks. The other fifteen creator-export controls were not run in that batch.
A normal creator fixture used a 512 checker texture, imported cube, material, and instance,
connected texture alpha to Opacity Mask with clip 0.3, and placed three cubes in Cave000.
Eden showed yellow cells retained and magenta alpha-zero cells cut away. The older plane control
had been viewed edge-on after its OBJ coordinate conversion and supplied no cutout evidence.

| Historical level-admission route | Cubes existed | First image showing them |
| --- | --- | --- |
| Late-admission implementation | 10.5 seconds after the cave world began | 11.8 seconds |
| Initial-streaming implementation | From the world's first step | 7.3 seconds |

Both routes drew the material complete in the first image showing the cubes. The old late-admission
path waited 300 world updates for added levels and up to 900 updates or sublevel discovery for edits.
The initial-streaming route admitted native descriptors with the map. Those dated timings do not
qualify current load latency or a distinct half-drawn shader phase.

Additional bounded observations on this date were:

- The reported Cave033 chunk rendered while its unsupported air collision stayed game-owned.
  Its original collision was not reconstructed or copied into the mod. This did not qualify all
  unsupported asset kinds or global lighting parity.
- A bent spline with a masked creator material drew after export restaged its material slots.
  The fixture required the full editor; its headless spawn path could not provide a scene view.
- A new owned CarrotBaseWidget with CanvasPanel and Image displayed a creator material through Lua.
  A saved creator WidgetBlueprint remained a separate unqualified producer.
- A creator light function drew on the default shadowless channel 1 when overlapping lights were
  hidden. Directional assignment readback did not establish a visible directional projection.
  Unbuilt stationary lights still lacked usable baked shadow channels.

## Imported graph form and instances

The October 20 host comparison of graph-form rewrites matched all 382 masters, including the
245 changed by rewriting. The independently read source facts also matched for all 382. Sorting
node keys changed generated node names in 33 graphs without changing their predicted uniform sets;
that prediction matched the game's set on 271 of 382 masters.

The complete import comparison had 338 exact graph matches, four differing only by node names,
thirty differences already present before canonicalization, and ten recovery refusals. Twenty-eight
of the thirty differences were pins; two were postprocess defaults omitted by the cooked material.
Two decal graphs differed in a particle-sprite usage flag. Those import graphs had not been compiled
through the native Switch path in that comparison, so the counts were not native compile proof.

Later native instance work recorded 9,260 reconstructed recipe matches from 9,264 indexed originals:
7,657 with fully native local overrides and 1,603 retaining C4 flags as metadata. One raw
AlphaComposite-parent record was excluded and three MediaTexture-parent instances refused import.
No final local recipe differed. The index contained no material layers, so its zero count did not
qualify native layer rendering. Consumer staging and arbitrary material-interface fields still
needed their own checks.

## Qualification

These are bounded historical results from host tools, native editor automation, and Eden. They do
not qualify a current native plugin archive, every original material, every imported graph, every
LOD or pass, or physical Switch hardware. The release process must bind its own source and archive
to the editor and game results it requires.
