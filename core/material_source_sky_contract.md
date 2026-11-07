# Original atmosphere disk source recovery

`PrepareOriginalSkyDiskContract(owner, archive)` reads the canonical primary
reference again from the owner's bounded map and prepares its actual archive
payload. It accepts the observed opaque Unlit surface specialization with a
local vertex factory. The returned immutable contract supplies a separate
`MaterialSurfaceRewrites` callback; existing recovery dispatch is unchanged.

The callback matches the complete ordered light-zero formula: the reflection
capture guard, strict disk-angle test, ground-sphere intersection, both positive
root tests, the full height/zenith transmittance coordinates and the three disk
luminance channels. It does not recognize a formula merely because it reads
the expected fields. Changed comparisons, constants, coordinates or components
leave the formula unmatched and recovery refuses it.

The original View identity must have name hash `f10cd51552e37c76`, layout
`166705f7`, and an active resource bit. Ordered constructor resource 52 is
`TransmittanceLutTexture` (byte 4576, Texture2D); resource 53 is its sampler
(byte 4584). Their actual shader slots come independently from the source SRT.
The immutable descriptor-cache producer must join the same pair into exactly
one TXL 2D consumer. A separate original symbolic trace checks level zero before
the material binder's scene-texture form discards LOD information.

The formula reads the original float members at bytes 3072 (direction), 3168
(disk luminance), 3200 (cosine of half apex angle), 3248 (sky camera origin),
3264 (planet center), 3368/3372 (bottom/top radius), and 3436 (capture mask).
Byte 3200 becomes part of the complete atmosphere node, never a free material
scalar or a guessed scene constant.

`ResourceFacts()` and optional rewrite facts report source resource identity,
consumer and replacement counts. `RequiresIndependentSkyProof` stays true:
the full original caller and physical formula need independent numerical
qualification. Resource identity and structural recognition do not establish
renderer access, a complete shader family or compiler parity. Those admission
flags remain false. This helper is deliberately unreferenced by the existing
production dispatch and does not alter the separate Sky actor-field contracts.

The standalone source qualification CLI uses explicit installed-user asset and
archive inputs. Its test compares full graphs and actual source programs,
including capture, ground and strict angular-boundary samples. Private payloads,
source paths and proof receipts are never export inputs.
