// A material's whole inline shader map and shader library written by Kadan from a description (stages 4 to 6 of
// tools/navigation/OWN_SHADER_MAPS.md), without Python. A port of recomp/graphics/nvasm: own_uniforms.uniform_set
// (the uniform expression set from named facts), shader_objects.make_base_pass_object and own_material.shader_node
// (base pass shader objects), material_freeze.freeze (the FMemoryImage flatten order), material_map_writer.write_map
// and write_block (hash tables, ShaderContentHash, name table, name patches, pointer table, type dependencies),
// material_image Resource and InlineShaderMaps writing, own_material.build_versions (map hash and id hash) and
// own_material.library. Checked byte for byte against the Python by recomp/graphics/nvasm/own_map_check.py.
//
// Parameter and preshader names may be ASCII, kana, CJK or Hangul (NeedName); shader class and struct names ASCII.
#pragma once
#include "own_map_facts.h"
