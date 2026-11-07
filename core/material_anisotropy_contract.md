# Original anisotropy recovery

`RecoverOriginalMaterialAnisotropy(owner, archive, primaryGraph)` adds the
checked `Anisotropy` and `Tangent` outputs to an already recovered original
primary graph. It returns a graph, source facts, and prepared original programs.
It preserves existing nodes, outputs and material settings. It refuses existing
anisotropy pins or auxiliary node-name collisions.

The route is limited to the two original terrain/treasure anisotropy master
families and their actual instance parent chains. It reads canonical native
shader references and the archive resource-map association. Both local and
default GPU-skin factories must provide one checked `FAnisotropyPS` and a
matching `FAnisotropyVS` interface. Eight local and four skin base-pass resource
groups must retain the normal-coupled tangent projection and named scalar
clamp. Shared references retain their actual shader-class hash and permutation.

The auxiliary route checks the original payload and instruction identities,
uniform layout, varying names/locations, material texture ordinals, native asset
samplers and automatic View mip bias. It traces only straight-line producers
with initialized live lanes. Saturated, conditional, changed or ambiguous
projection/clamp patterns refuse. Metadata-only storage declarations are
validated and omitted from executable source indices.

Tangent-map inputs retain normal decoding, asset sampling and original UV0.
Without a tangent map, the recovered `(1,0,0)` is a canonical direction for the
original normalized vertex tangent. Authored tangent magnitude and the original
editor connection cannot be uniquely recovered after normalization.

The caller supplies the primary graph from original recovery. This helper does
not certify an arbitrary edited primary graph, an entire shader family, newly
compiled shaders or rendering. `FamilyComplete`, `CompilerProven`,
`RendererProven` and `ProductionAdmitted` remain false.

The offline check is:

```text
py -3.11 tools/navigation/test_native_material_anisotropy.py
py -3.11 tools/navigation/test_native_material_anisotropy.py --sanitizer
```

It independently compares raw-owner native graph roots and source metadata,
checks primary-graph preservation, and tests malformed source/association
refusals. Selected CPU comparisons separately exercise the coupled normal,
tangent, anisotropy and auxiliary encoded RGBA against original source slices.
Those comparisons use explicit LOD-invariant texture fixtures; structural
guards retain the original mip-bias and sampler contract. They do not establish
GPU arithmetic or production renderer acceptance.
