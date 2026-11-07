# Opaque creator policy boundary

`custom_material_opaque_policy.h` emits a complete new opaque forward pixel
program. Its input graph is creator authored. It consumes the selected original
template's checked binding descriptions, uniform identities, vertex-factory
interpolants and policy identity; it does not copy the original pixel program or
its control block into the result.

This builder is an offline qualification API. `RenderAvailability` retains the
`game-lighting-unproved` production hold until the complete export, runtime
binding and rendered creator route is accepted.

## Required caller facts

The caller must check the actual creator material is Surface, Opaque, single
MSM_DefaultLit and single sided. Recovered game masters remain read only. The
actual selected shader-map reference supplies the uniform hashes, policy type
and vertex factory; the archive resource/index association must also match.
Header layouts alone cannot substitute for missing uniform identities.

Pass `OriginalDecalResponse::Color` or `ColorRoughness` from the checked template
settings. There is no inferred decal-response default. Normal-response decals
and other response modes need their own source and resource qualification.
Unknown enum values and missing or unknown protocol response strings refuse
before emitting a program. They do not fall back to a known response.

Pass the separately qualified `OpaqueDirectionalResponse`:

| Response | Original behavior |
| --- | --- |
| `SurfaceShadow` | Directional diffuse and specular use the surface shadow. |
| `SeparateSpecularShadow` | View word 2592 blends the specular shadow toward one and permits the original specular-only fallback. |

This distinction comes from the original compiled policy. The policy type,
decal response, material name and custom-property names do not establish it.
The trailing default preserves earlier qualification callers; production
callers must supply the checked response explicitly.

## Program construction

`OpaqueMaterialProgram` composes exact geometry, seeded graph inputs, decal
surface, reflectance, indirect lighting, baked and forward shadows, directional
and clustered local lighting, reflection, ambient, emissive, fog and exposure.
It preserves the following original associations:

- Directional accumulation checks primitive lighting channels and applies the
  original C4 directional multiplier before initializing the local loop.
- Capsule occlusion and final local accumulation occlusion are separate uses
  of the material AO input. The final accumulation keeps MUL then MAD order.
- Reflection radiance uses C4 reflection occlusion; its multibounce polynomial
  uses material AO. Reflection shadow uses the raw directional surface before
  the C4 strength blend, with the original disabled-directional default one.
- Ambient uses the post-decal material BaseColor. Indirect irradiance multiplies
  the post-capture diffuse reflectance before its C4 baseline MAD.
- Paired reflection terms preserve the final MAD into the direct sum. Connected
  creator emissive is clamped and added before fog; exposure follows fog.

The generated program retains the original structural scene-resource bindings.
The exact sky-irradiance storage proof must be retained by the backend. Its
current seal does not admit creator texture rebinding. This dependency must be
resolved explicitly before admitting textured creator exports.

`OpaqueMaterialStageProgram` exposes bounded diagnostic stage outputs. Neither
API introduces a diagnostic uniform buffer into a complete creator program.

## Initial cooked creator example

The first runtime candidate uses a fresh creator material with BaseColor
`(0.18, 0.48, 0.8)`, Metallic `0`, Specular `0.5`, Roughness `0.35` and
AmbientOcclusion `1`. Normal and EmissiveColor are unconnected. The material
settings must be read from the actual cooked material, and the mesh's actual
factory and lighting permutation must match the selected template. An unbaked
mesh cannot be assigned a baked-lightmap policy just to satisfy the builder.

Creator textures, functions and runtime parameter updates are outside this
initial candidate. ScalarParameter and VectorParameter currently emit captured
default constants. CollectionParameter requires a resolved captured value.
Graph node availability therefore does not establish runtime material-instance
or collection updates. A changed captured value requires recompilation.

## Evidence limits

Continuous CPU comparisons cover all 24 ordinary original policy and factory
contexts with measured material entry values and independent scene fixtures.
Those comparisons use a generic numeric evaluator plus explicitly recognized
IEEE32 bit-root, packed-half and storage-read boundaries. They are not a GPU
arithmetic emulator or a rendered game acceptance result.

The complete 24 programs also pass the native Maxwell compiler with unchanged
structural scene bindings and no diagnostic uniform input. An independent
first-party compiler produces identical program code. These compiler and CPU
checks remain separate from cooked-package custody and the rendered game gate.

The independent implementation may fetch immutable clustered descriptors in a
different order. It also evaluates the independent static baked-shadow sample
after indirect lighting, where the original evaluates it before. Qualification
checks its same resource, sampler and coordinates separately, while preserving
the remaining scene-texture order. These reorderings do not change their
read-only inputs or the qualified output arithmetic.
