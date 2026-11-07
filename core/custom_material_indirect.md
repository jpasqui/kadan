# Original forward indirect-lighting stage

`custom_material_indirect.h` emits first-party C++14 code for the eight observed
NoLightMap, precomputed volumetric lightmap, HQ lightmap and distance-field HQ
policy identities, each with its observed skylight permutation. It consumes
explicit original shader binding metadata and typed native uniform identities.
The helper does not admit a complete material for export.

```cpp
using namespace P4MapKit::CustomMaterial::Lighting;
PolicyEmitter policy(emitter, originalHeader);
IndirectEmitter indirect(policy);
IndirectInputs inputs{worldPosition, normal, lightmapUV};
auto result = EmitIndirectPolicy(indirect, originalHeader, uniformHashes,
    OriginalIndirectPolicy(originalTypeHash), inputs);
```

`worldPosition` and `normal` have three components, and `lightmapUV` has two.
These inputs come from the caller's qualified material and vertex-factory stages.
The helper preserves original per-policy choices; it does not substitute a stock
engine lighting producer, normalize the input normal, or invent resource bindings.

`IndirectResponse::OriginalTwoSidedSurface` explicitly selects the observed
foliage response for each policy family and retains both front and back
irradiance when the source produces them. It preserves the separate volumetric
back color scale, opposite-normal SH/HQ evaluation, original texture-read order
and different bent-normal visibility formula. The narrower
`OriginalTwoSidedVolumetric` response remains available only for volumetric
policies. Both responses require a qualified original material/shader
association. Neither a masked blend flag nor the lighting-policy identity
selects a response. The observed foliage source uses TwoSidedFoliage; its math
proof does not admit foliage as a creator DefaultLit material.

The returned `IndirectResult` contains:

* `irradiance`: the original indirect RGB before material diffuse/AO composition.
* `brightness`: the original RGB luminance used by reflection mixing, before AO.
* `diffuseDirection` and `skyVisibility`: the original bent-normal sky inputs.
* `directionalShadow`: the volumetric stationary-light shadow sample, or the
  original default of one when that branch is disabled or absent.
* `coordinates`: volumetric brick atlas coordinates for VLM or the enabled
  NoLightMap stationary-shadow branch. The diagnostic zero for an absent branch
  is not a source coordinate producer.
* `backIrradiance` and `hasBack`: the complete opposite-side response when an
  explicit original two-sided response has an irradiance producer.
* `hasIrradianceProducer`, `hasBrightnessProducer` and
  `hasDirectionalShadowProducer`: explicit source-presence contracts.

The directional-shadow scalar is not a baked shadow mask. The composed lighting
policy separately chooses the exact non-HQ, HQ or distance-field HQ mask. A
NoLightMap policy without sky has no original indirect producer, and NoLightMap
reflection does not use an indirect-brightness numerator. Diagnostic zero values
for those absent stages do not establish a numerical producer comparison.
The qualified ordinary consumers multiply irradiance by original diffuse
reflectance: base color times one minus metallic, including the original
conditional fully-rough specular absorption. That reflectance vector does not
include material AO. The caller retains the source-qualified later occlusion
factor once, then adds the original ambient term. This helper does not apply
those factors or add that ambient term.

Volumetric sampling retains the integer indirection fetch, brick coordinates,
ambient and six decoded SH coefficient textures, convolution constants, clamp,
volumetric color scale and original reciprocal-pi factor. Its optional shadow
branch requires the original Primitive identity at slot one and reads the
native float `UseVolumetricLightmapShadowFromStationaryLights` at byte 328.
The sampler used by indirection is the explicit original header default sampler.
NoLightMap has the same optional coordinate/shadow producer entirely inside that
predicate. HQ has no such branch.

HQ sampling uses the existing typed lightmap-cluster and precomputed-lighting
bindings. `ApplyHQIndirectShaping` applies the original View indirect color scale
after the qualified raw HQ decode. Its optional sky occlusion texture is exactly
cluster texture ordinal one, with the cluster's lightmap sampler. Volumetric and
HQ bent-normal visibility retain their different original formulas. The original
two-sided HQ response shares its two lightmap samples while evaluating both
normal directions; the texture-read order depends on the response and skylight
permutation.

Sky diffuse reads the exact View `SkyIrradiance` SRV role, seven aligned records
at bytes 0 through 96, and the original sky color. `IndirectEmitter::Program()`
adds the corresponding read-only `STORAGE` declarations. A composed caller must
use this program method so the emitted storage declarations are retained.
The native backend must separately qualify that exact read-only SRV binding;
generic storage or image admission is not implied by this API.

Unknown policy identities, missing or ambiguous uniform identities, unsupported
View/BasePass layouts, absent required roles and invalid sampler slots refuse
emission. The standalone stage tool additionally requires a proven unused
diagnostic constant-buffer slot. Original source and resource parity, native
compiler closure, full-policy composition and live behavior are distinct checks.
