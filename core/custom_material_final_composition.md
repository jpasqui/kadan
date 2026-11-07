# Original opaque final composition

`custom_material_final_composition.h` preserves the final ordinary forward
lighting joins and gives their caller inputs explicit source roles. It reuses
the qualified stage math and typed policy bindings. It does not admit a complete
material or qualify live lighting inputs.

The original opaque caller roles differ from historical generic helper names:

| Boundary | Original caller input |
| --- | --- |
| C4 ambient tint and scalar polynomial | Post-decal material base color |
| C4 ambient metallic and AO | Original material surface producers |
| Reflection response's nominal `materialAO` | C4 reflection occlusion factor |
| Reflection response's nominal `screenAO` | Original material AO |
| Reflection response's shadow AO | Raw directional surface before C4 strength |

`EmitOpaqueAmbient` supplies the base-color association explicitly. The raw
directional reflection shadow value starts at one when the directional branch
does not execute; its producer remains separate from the shaped directional
surface and transmission outputs.

`OpaqueReflectionInputs` names the actual material AO and C4 factor separately.
`EmitOpaqueReflectionPolicyTerms` performs the original capture, sky, mixing and
response stages and returns `OpaqueReflectionTerms{radianceBeforeWeight,
shadowBlend}`. The final join retains the original MAD with the direct sum. An
already multiplied reflected RGB value cannot replace those terms while
preserving that rounding boundary. The C4 reflection factor is already applied
to `radianceBeforeWeight`.

`OpaqueFinalInputs` contains the accumulated direct diffuse/specular, indirect
irradiance, post-capture diffuse reflectance, C4 ambient, reflection terms, C4
reflection factor and emissive input. Its two explicit presence flags identify
whether indirect irradiance and emissive have producers. Original-source
qualification uses the captured source presence. A creator caller derives
authored emissive presence from its graph; the blend mode does not establish it.

`EmitOpaqueLinearComposition` retains this order:

1. Form the indirect baseline as irradiance times post-capture diffuse
   reflectance, then MAD the C4 factor and C4 ambient. Without an indirect
   producer, use C4 ambient directly.
2. Add accumulated direct diffuse and specular, then MAD the reflection terms
   with that direct sum.
3. Add the indirect/ambient baseline.
4. When emissive has a producer, clamp it against zero and add it.

`EmitOpaqueFinalColor` applies joined fog with MAD, multiplies the original View
`PreExposure` scalar at byte 2180 and appends the original opaque output alpha
of zero. The caller supplies the qualified joined fog producer, including its
original conditional volumetric sampling. No additional reflection factor or
material AO multiplication belongs at this final join.

Required value shapes refuse invalid inputs. Absent indirect and emissive
values are not consumed. Source role association, isolated stage numerical
parity, compiler parity, complete program qualification and runtime behavior
remain separate boundaries.
