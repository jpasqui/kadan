# Kadan's vertex and geometry shaders

Kadan carries these project-authored programs as NVN payloads. The plugin embeds the payloads
through `core/own_shader_payloads.h`, and its material exporter selects them by name. The GLSL
sources are in [source/](source/). The project license covers the authored sources and payloads;
it does not license Unreal Engine, a graphics driver, or other external tools.

| Payload | Source and use |
| --- | --- |
| `static_mesh_vs_1uv`, `static_mesh_vs_lightmap_1uv`, `static_mesh_vs_lightmap_df_1uv` | `static_mesh_base_pass.vert`, opaque and masked static-mesh base passes |
| `instanced_vs_1uv`, `instanced_vs_lightmap_1uv`, `instanced_vs_lightmap_df_1uv` | The same source with `INSTANCED`, instanced static-mesh base passes |
| `translucent_vs_1uv`, `translucent_vs_lightmap_1uv`, `translucent_vs_lightmap_df_1uv` | The same source with `TRANSLUCENT`, lit translucent passes |
| `translucent_fog_vs_1uv` | The same source with `TRANSLUCENT_VERTEX_FOG`, the vertex-fog pass |
| `depth_only_vs` | `depth_passes.vert`, masked depth pass |
| `shadow_vs`, `shadow_loose_vs`, `shadow_perspective_vs`, `shadow_perspective_loose_vs` | `depth_passes.vert`, masked shadow-depth passes |
| `shadow_for_gs_vs`, `shadow_for_gs_onepass_vs` | `depth_passes.vert`, input to the point-light geometry pass |
| `onepass_point_shadow_gs` | `onepass_point_shadow.geom`, point-light shadow geometry pass |

[PROVENANCE.json](PROVENANCE.json) records the source variants and SHA-256 hashes of the source,
payloads, header generator, and embedded header. Text hashes use UTF-8 with LF line endings so
the checks survive Windows and Unix checkouts. Payload hashes cover their bytes unchanged.

Run the source and payload checks from the repository root:

```sh
python own_shaders/verify_sources.py --check
```

This verifies the recorded files and calls the header generator in memory. It writes no files
and runs no graphics driver. To update the embedded header after an intentional payload change:

```sh
python own_shaders/make_payload_header.py
```

Review the changed payloads and update their provenance before committing. The generator keeps
the earlier repository-relative label in its output so existing payload headers remain stable.

## Build boundary

The original build pipeline compiled these GLSL variants through an external NVIDIA OpenGL
driver and GLSL-to-NV_gpu_program5 frontend. It adapted the assembly bindings to the renderer,
then used the project-authored Maxwell backend now in `compiler/` to encode it. A payload
serializer wrote the NVN binding and control sections from named interfaces, program fields,
assembly text, and the code hash.

The complete driver frontend, assembly-adaptation tools, and payload serializer are not bundled
here. Rebuilding shaders requires those external development prerequisites and a compatible
NVIDIA driver. The source and hash inventory establish which files ship. They do not establish
that a new GPU compilation of the GLSL produces the shipped machine code. That equivalence is
**unverified** in this publication. Host CI verifies the manifest and embedded header only.
