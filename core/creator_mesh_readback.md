# Creator mesh construction and cooked facts

The first supported mesh storage profile combines latest UE4.26 stock split
packages (864/522) and the existing retoc 0/0 representation. The scoped adapter
retains actual version words and custom versions; the generic package reader is
unchanged. Stock inputs require the independently checked Rendering 44,
AnimPhys 17, ExternalPhysics 40, FortniteMain 43, Release 38, Framework 37,
Editor 40 and FortniteRelease 1 gates. A retoc profile describes serialization,
not an assertion about the original platform, creator or installed game.

`creator_mesh_geometry.h` reads a bounded triangulated OBJ with explicit vertex,
normal, UV and material-slot indices. It accepts Unreal centimetres or an
explicit Y-up metres conversion, including the reflection's winding reversal.
It refuses external material files, missing attributes, nonfinite values,
degenerate triangles and inconsistent normals. Metadata lines do not override
geometry; a producer retains the complete input bytes' digest. Decoding an OBJ
alone establishes no authorship or Unreal object custody.

`static_mesh_facts.h` supplies typed mesh/material/property facts and the full
render payload. `physics_body.h` consumes the retained BodySetup native tail.
Its first qualified collision stream is the PhysX 3.4 CPU triangle mesh, version
14, BVH33/RTree version 1. All vertices, indices, materials, face remaps,
adjacency, page metadata and node bounds, triangle extra bytes and UV data are
read and bounded. Tree validation checks reachable pages, cycles, leaf ranges
and complete triangle coverage. The complete stream remains in the result.
Convex/mirrored streams, BVH34, GRB and other versions are named refusals. This
profile does not imply support for every PhysX shape or Switch behavior.

`mesh_nav_collision.h` reads the complete v4 NavCollision_X geometry, shape
starts, area-class reference and payload. These are collision inputs; they do
not constitute a rebuilt navigation mesh or a walkability proof.

`mesh_cooked_readback.h` combines the scoped header, typed tags/values, complete
mesh render/material facts, BodySetup collision and optional owned navigation
collision. It requires an exact mesh/body/nav export census, complete export
partition and split-body trailer. Socket and unknown extra-export profiles
refuse. Its bounded canonical facts retain native float bits, GUIDs, complete
physics/navigation payloads and export identities. They omit storage offsets,
name/import indices and equivalent bulk-file placement. The only physics format
label normalization is PhysXPC versus PhysXGeneric; it does not rewrite native
collision bytes. Top-level property tag metadata is retained alongside typed
nested values; arbitrary nested user-struct GUID profiles are not independently
qualified by the creator constructor.

`creator_mesh_package.h::RelabelPhysX` performs the existing names-only format
relocation through the scoped stock adapter. It preserves actual version words,
the complete custom-version table, body and sidecar bytes. The caller compares
full readback facts before and after conversion. A label change is not a physics
platform conversion proof.

The following Unreal units are deliberately pending until one coherent native
build and actual tests qualify them:

- `P4CreatorMeshGeometry`: constructs an ordinary new editable UStaticMesh from
  independently decoded source geometry, verifies its native description and
  retains the actual source/native-state binding. Material objects are explicit
  construction dependencies whose export custody is still unqualified.
- `P4MeshCookedReadback`: acquires a bounded actual split bundle under native
  read-only file leases, hashes complete files, reads all supported facts and
  revalidates the bundle. A receipt proves acquired facts, not creator origin.
- `P4CreatorMeshCook`: consumes the constructor-issued binding, performs a fresh
  native Windows cook with explicit asynchronous package-metadata restoration,
  revalidates the source, makes a names-only candidate with hardlinked unchanged
  body/sidecar files and compares complete readbacks. It independently matches
  source OBJ triangles to actual native collision geometry/material indices,
  then checks the decoded PhysX triangles against the native source's installed
  winding policy. The constructor also retains its actual native render-buffer
  observation; subsequent derived geometry edits expire that binding.
  Existing output is refused;
  one scratch directory can be reused under its owner's lifecycle.

Material/MIC/shader custody, whole lighting source and bake custody, actual
container census and packed readback, physics platform behavior, rendered game
results and walkability remain separate required gates. Neither a caller flag,
a namespace, an unchanged source hash, nor a retained parser fixture admits a
creator mesh to final export.

Portable tests cover bounded/truncated/malformed source and archive inputs,
output preservation, exact version gates, names-only relocation, full native
tail comparison and byte/semantic tampering. The parity tool uses the existing
first-party tagged/nav readers and an independent geometry decoder against
retained own cooked bytes. A retained already-converted Generic fixture is
parser parity, not an actual new PC cook or packed/game acceptance.

Run the authored portable cases with:

```
python tools/navigation/test_native_creator_mesh.py --sanitize --out <owned-output-folder>
```

Add `--header <retained.uasset> --package <canonical-package> --object
<canonical-object>` to qualify the explicit retained stock profile as well.
The harness preserves input hashes, reuses its output folder and records named
cases/binary/source hashes; it never launches Unreal or admits an export.
