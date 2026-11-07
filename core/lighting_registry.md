# Cooked lighting registry readback

`lighting_registry.h` reads the complete native tail of a latest UE4.26
`MapBuildDataRegistry` export after its tagged properties. It retains mesh GUIDs,
light/shadow texture identities, light GUIDs, coefficient vectors, coordinate
scale/bias, channel flags, penumbra values, irrelevant lights and per-instance UV
biases. The remaining light-volume, volumetric-lightmap, light-depth,
reflection-capture and sky-atmosphere maps are fully consumed and included in
the canonical content, including their complete sample/layer payload bytes.
Light records additionally retain typed shadow channels, depth-map matrices and
dimensions, and all serialized depth half-float bits.

GUID maps are canonicalized by their actual GUID keys. Texture package indices
are resolved to exact canonical package/object outer-chain and class identities.
Export-index, map-order and storage-offset relocation therefore does not discard
content or invalidate otherwise equal lighting. Float bits, including signed
zero, remain part of the canonical content. Counts, spans and canonical output
have bounds; unknown strip flags, legacy/virtual lightmap layouts, duplicate
GUIDs, imported lighting textures, invalid references, nonfinite scalar fields,
truncation and any unread native tail refuse. Output is preserved on failure.

`lighting_sun.h` identifies exactly one stationary depth map along the supplied
rotation, requires shadow channel zero, and rewrites only parser-recorded GUID
fields in that light record, mesh shadow maps and irrelevant-light lists. It
refuses existing target identities and overlap with static-light or other GUID
domains. The complete output is parsed again, and reversing the approved fields
must restore the original bytes and canonical content. Caller GUID/rotation
values confer no original-light or bake authority; the native source producer
must establish that binding independently. The transform preserves volume/VLM
data; export policy still has to refuse or remove map-wide lighting through a
qualified creator-cook path.

`lighting_package.h` explicitly dispatches versionless retoc legacy headers and
stock UE4.26 summary versions 864/522. Original version words and custom-version
records remain retained. A versioned header requires the exact qualified
Rendering 44, Mobile 3 and ReflectionCapture 1 custom gates. Relevant older or
future versions, missing gates and duplicate custom GUIDs refuse. The generic
package reader's accepted formats are unchanged.

These readers establish serialization/content facts. They do not establish
creator ownership, a Lightmass build, actor source invariance, material source
eligibility, game readiness, or export admission. The native pending readback
service also retains exact source bundle hashes and actual compressed texture
mip content. A producer must separately bind the live creator actor/registry/
texture census, native bake process and expected cook before comparing the
actual packed readback.

Portable tests include complete authored native tails, relocation equivalence,
content changes and every truncation boundary. The read-only
`tools/navigation/native_lighting_registry_parity.py` compares retained cooked
fixtures with the first-party `lightmap_scene.py` decoder; this is decoder parity
and does not replace fresh producer, container or game acceptance.
