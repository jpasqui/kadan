// First-party Surface/Translucent/Unlit pixel compiler with creator-owned uniforms.
#pragma once
#include "mod_folders.h"
#include "custom_material_uniforms.h"
#include "material_engine_hashes.h"
#include "material_cooked.h"
#include "material_resource_boundary.h"
#include "native_custom_backend_template.h"
#include <functional>
namespace P4MapKit {
namespace CustomMaterial {
namespace NativeBackend {
struct TranslucentSource {
  Bytes header, body;
};
using TranslucentSourceResolver =
    std::function<bool(const std::string &, TranslucentSource &)>;
struct TranslucentCompilation {
  TemplateCompilation compiled;
  OwnedUniforms uniforms;
};
inline std::vector<P4MapKit::FMaterialCookedRecord>
TranslucentParentChain(const std::string &requested,
                       const TranslucentSource &selected,
                       const TranslucentSourceResolver &resolve) {
  std::vector<P4MapKit::FMaterialCookedRecord> chain;
  std::set<std::string> seen;
  auto raw = selected;
  auto wanted = requested;
  for (unsigned depth = 0; depth < 128; ++depth) {
    auto package = P4MapKit::ReadPackageHeader(raw.header, raw.body.size());
    P4MapKit::FMaterialCookedRecord record;
    P4MapKit::FReadError error;
    if (!ReadMaterialCookedRecord(package, raw.body, record, error))
      throw std::runtime_error("The original material source could not be read. " + error.Code);
    if (P4MapKit::PropertyIdentity(record.Package) !=
            P4MapKit::PropertyIdentity(wanted) ||
        !AssetPath(wanted, false) ||
        !seen.insert(P4MapKit::PropertyIdentity(wanted)).second)
      throw std::runtime_error("translucent parent identity/cycle");
    chain.push_back(record);
    if (record.Class == "Material") {
      if (chain.size() < 2)
        throw std::runtime_error(
            "translucent runtime donor requires owning-resource instance");
      std::reverse(chain.begin(), chain.end());
      return chain;
    }
    const auto parent = record.Properties.Field("Parent");
    if (!parent ||
        (parent->Kind != P4MapKit::FDecodedValue::EKind::HardReference &&
         parent->Kind != P4MapKit::FDecodedValue::EKind::SoftPath))
      throw std::runtime_error("translucent parent reference absent");
    wanted = Package(parent->Text);
    if (!resolve || !resolve(wanted, raw))
      throw std::runtime_error("The original material parent " + wanted + " is unavailable.");
  }
  throw std::runtime_error("translucent parent depth limit");
}
inline unsigned UniqueResourceSlot(const ShaderBindings &h, bool sampler,
                                   unsigned buffer, unsigned ordinal) {
  const auto table = ReadResourceTable(sampler ? h.samplers : h.textures);
  const auto row = table.find(buffer);
  if (row == table.end())
    throw std::runtime_error("translucent scene binding absent");
  unsigned slot = ~0u;
  for (auto binding : row->second)
    if (binding.first == ordinal) {
      if (slot != ~0u)
        throw std::runtime_error("translucent scene binding ambiguous");
      slot = binding.second;
    }
  if (slot == ~0u)
    throw std::runtime_error("translucent scene resource role absent");
  return slot;
}
inline void RequireTranslucentBindings(const ShaderBindings &h,
                                       const FMaterialShaderReference &ref) {
  const auto view = ref.UniformBufferHashes.find(0);
  if (view == ref.UniformBufferHashes.end() ||
      view->second != EngineHash::ViewUniformBuffer || h.layout_hashes.size() != 3 ||
      h.layout_hashes[0] != EngineHash::ViewLayout || h.layout_hashes[1] != 0x05ec0deeu ||
      (h.resource_bits & 3) != 3 || !h.global_arrays.empty())
    throw std::runtime_error("translucent View/BasePass layout unavailable");
  for (const auto &b : h.uniform_buffers)
    if (!b.empty())
      throw std::runtime_error("translucent packed uniform layout unavailable");
  (void)UniqueResourceSlot(h, false, 1, 28);
  (void)UniqueResourceSlot(h, true, 0, 43);
}
// The retained vertex program must not consume the numeric material layout we
// replace. Its stage-local roles are
// BasePass/View/Primitive/LocalVertexFactory.
inline void RequireTranslucentVertex(const FMaterialShaderReference &ref,
                                     const Bytes &payload) {
  if (ref.Frequency != 0 || ref.VertexFactoryHash != EngineHash::LocalVertexFactory ||
      ref.TypeHash != 0xe99e2d4408e39e36ull)
    throw std::runtime_error(
        "translucent retained vertex permutation unavailable");
  for (const auto &binding : ref.NamedBuffers)
    if (binding.second == "Material")
      throw std::runtime_error(
          "translucent retained vertex consumes donor material uniforms");
  const std::map<std::uint16_t, std::uint64_t> expected = {
      {1, EngineHash::ViewUniformBuffer},
      {2, 0x60929d69929b2b59ull},
      {3, 0x426d1240410b8fa2ull}};
  if (ref.UniformBufferHashes != expected || payload.size() < 6 ||
      payload[4] != 0x53 || payload[5] != 0x56)
    throw std::runtime_error(
        "translucent retained vertex uniform identities unavailable");
  auto structural = payload;
  structural[5] = 0x50;
  const auto header = ReadPixelBindings(structural);
  if (header.layout_hashes !=
          std::vector<std::uint32_t>{0x05ec0dee, EngineHash::ViewLayout, 0, 0x064606e7} ||
      !header.global_arrays.empty())
    throw std::runtime_error(
        "translucent retained vertex structural layout unavailable");
  for (const auto &buffer : header.uniform_buffers)
    if (!buffer.empty())
      throw std::runtime_error(
          "translucent retained vertex packed material data unavailable");
}
inline std::string TranslucentUnlitProgram(
    const Json &graph, const OwnedUniforms &uniforms,
    const ShaderBindings &original, unsigned materialBuffer,
    const std::map<std::string, std::string> &textureParams) {
  Emitter initial(false, Flag(graph, "two_sided"));
  initial.owned_uniforms = true;
  initial.material_parameters = uniforms.bindings;
  initial.material_buffer = materialBuffer;
  auto inputs =
      CompileTranslucentInputs(graph, std::move(initial), textureParams);
  auto &e = inputs.emitter;
  auto load = [&](unsigned buffer, unsigned offset, int size) {
    auto reg = e.temp();
    e.emit("LDC." +
           std::string(size == 1   ? "F32"
                       : size == 2 ? "F32X2"
                                   : "F32X4") +
           " " + reg + "." + Comp(size) + ", buf" + std::to_string(buffer) +
           "[" + std::to_string(offset) + "]");
    return Value{reg, size};
  };
  // Vertex fog is supplied by the retained game vertex stage. Its alpha is
  // transmittance; the scene volume composes with it before pre-exposure.
  auto vertexFog = e.attrib("in_texcoord7", 4);
  auto fog = e.temp();
  e.emit("MOV.F32 " + fog + ", " + vertexFog.text);
  auto enabled = load(1, 1836, 1),
       condition = e.op("SGT", 1, {enabled, Constant({0})});
  auto test = e.temp();
  e.emit("TRUNC.U.CC " + test + ".x, " + condition.text + ".x");
  e.emit("IF NE.x");
  auto world = e.world_position();
  std::array<Value, 4> rows = {
      {load(0, 64, 4), load(0, 80, 4), load(0, 96, 4), load(0, 112, 4)}};
  auto view =
      e.op("MUL", 3, {e.swizzle(world, "r"), e.swizzle(rows[0], "rga")});
  view =
      e.op("MAD", 3, {e.swizzle(world, "g"), e.swizzle(rows[1], "rga"), view});
  view =
      e.op("MAD", 3, {e.swizzle(world, "b"), e.swizzle(rows[2], "rga"), view});
  view = e.op("ADD", 3, {view, e.swizzle(rows[3], "rga")});
  auto depth = load(0, 3840, 3), scale = load(0, 3824, 3);
  auto z = e.op(
      "MAD", 1,
      {e.swizzle(view, "b"), e.swizzle(depth, "r"), e.swizzle(depth, "g")});
  z = e.op("MUL", 1, {e.per_component("LG2", z), e.swizzle(depth, "b")});
  z = e.op("MUL", 1, {z, e.swizzle(scale, "b")});
  auto xy = e.op("DIV", 2, {e.swizzle(view, "rg"), e.swizzle(view, "b")});
  xy = e.op("MAD", 2, {xy, Constant({.5, -.5}), Constant({.5, .5})});
  auto uvw = e.append(e.append(xy, z), Constant({0}));
  auto volume = e.temp();
  const auto texture = UniqueResourceSlot(original, false, 1, 28),
             sampler = UniqueResourceSlot(original, true, 0, 43);
  e.uses_texture = true;
  e.emit("LDC.U64 D1.x, buf14[" + std::to_string(1352 + 8 * sampler) + "]");
  e.emit("LDC.U64 D0.x, buf14[" + std::to_string(328 + 8 * texture) + "]");
  e.emit("OR.U64 D0.x, D0, D1");
  e.emit("TXL.F " + volume + ", " + uvw.text + ", handle(D0.x), 3D");
  e.handle.clear();
  auto fogColor = e.op(
      "MAD", 3,
      {e.swizzle(vertexFog, "rgb"), e.swizzle({volume, 4}, "a"), {volume, 3}});
  auto fogAlpha =
      e.op("MUL", 1, {e.swizzle(vertexFog, "a"), e.swizzle({volume, 4}, "a")});
  auto composed = e.append(fogColor, fogAlpha);
  e.emit("MOV.F32 " + fog + ", " + composed.text);
  e.emit("ENDIF");
  auto color = e.op(
      "MAD", 3,
      {inputs.pins.at("EmissiveColor"), e.swizzle({fog, 4}, "a"), {fog, 3}});
  auto exposure = e.view("pre_exposure", 1);
  e.emit("MUL.F32 result_color0.xyz, " + color.text + ", " + exposure.text +
         ".x");
  e.emit("MOV.F32 result_color0.w, " + inputs.pins.at("Opacity").text + ".x");
  auto program = e.program();
  const std::string declaration = "CBUFFER buf1[] = { program.buffer[1] };\n";
  if (program.find(declaration) == std::string::npos) {
    const auto at = program.find("ATTRIB fragment_position");
    if (at == std::string::npos)
      throw std::runtime_error("translucent emitter declaration boundary");
    program.insert(at, declaration);
  }
  return program;
}
inline std::string TranslucentAuthoringIssue(
    const Json &graph,
    const std::map<std::string, std::string> &textureParams = {},
    const std::vector<TextureParameter> &textures = {}) {
  try {
    const auto version = Number(graph, "format", 1);
    if (version != 1 && version != 2)
      throw std::runtime_error("custom material graph version unavailable");
    for (const auto &setting :
         std::map<std::string, std::string>{{"domain", "MD_Surface"},
                                            {"blend_mode", "BLEND_Translucent"},
                                            {"shading_model", "MSM_Unlit"}})
      if (graph.Has(setting.first) &&
          Text(graph, setting.first) != setting.second)
        throw std::runtime_error("The translucent material has an unsupported " + setting.first + " setting.");
    if (Flag(graph, "two_sided"))
      throw std::runtime_error("Choose a one-sided translucent material.");
    for (const auto &output : Get(graph, "outputs").object)
      if (output.second.kind != Json::Null && output.first != "EmissiveColor" &&
          output.first != "Opacity")
        throw std::runtime_error("The unlit translucent compiler cannot export the connected " + output.first + " input yet.");
    if (Get(graph, "clip").kind != Json::Null)
      throw std::runtime_error("translucent opacity is not an opacity mask");
    const auto &render = Get(graph, "render_settings");
    for (const auto &setting : std::map<std::string, bool>{
             {"bEnableSeparateTranslucency", true},
             {"bUseTranslucencyVertexFog", true},
             {"bEnableMobileSeparateTranslucency", false},
             {"bApplyCloudFogging", false},
             {"bEnableResponsiveAA", false},
             {"bScreenSpaceReflections", false},
             {"bContactShadows", false},
             {"bDisableDepthTest", false},
             {"bOutputTranslucentVelocity", false},
             {"bAllowNegativeEmissiveColor", false}})
      if (render.Has(setting.first) &&
          Flag(render, setting.first) != setting.second)
        throw std::runtime_error("The translucent material has an unsupported " + setting.first + " setting.");
    for (const auto &node : Get(graph, "nodes").object) {
      const auto type = Text(node.second, "type");
      const auto &props = Get(node.second, "props");
      if (type == "CollectionParameter")
        throw std::runtime_error("translucent collection parameters require "
                                 "dynamic collection bindings");
      if (type == "TextureSample" || type == "TextureSampleParameter2D" ||
          type == "TextureObjectParameter") {
        if (Text(props, "SamplerSource", "SSM_FromTextureAsset") !=
            "SSM_FromTextureAsset")
          throw std::runtime_error(
              "Use the texture asset's sampler source for this translucent material.");
        if (Text(props, "MipValueMode", "TMVM_None") != "TMVM_None")
          throw std::runtime_error("translucent explicit mip mode unavailable");
      }
    }
    auto own = GraphUniforms(InlineFunctions(graph, Json()), textures);
    Emitter e(false, false);
    e.owned_uniforms = true;
    e.material_parameters = own.bindings;
    e.material_buffer = 2;
    auto program = CompileTranslucentInputs(graph, std::move(e), textureParams);
    if (program.emitter.uses_primitive)
      throw std::runtime_error(
          "the translucent donor has no primitive uniform route");
    for (const auto &entry : program.emitter.cache)
      if (entry.first.compare(0, 7, "attrib:") == 0 &&
          entry.first != "attrib:in_texcoord0")
        throw std::runtime_error("the translucent donor has no requested "
                                 "normal/tangent/instance varying");
    return {};
  } catch (const std::exception &e) {
    return e.what();
  }
}
inline TranslucentCompilation CompileTranslucentCandidate(
    const Json &graph, const std::string &material,
    const std::string &gameMaterial, const TranslucentSource &selected,
    const TranslucentSourceResolver &resolver, const ShaderArchive &archive,
    const std::map<std::string, std::string> &textureParams = {},
    const std::vector<TextureParameter> &textures = {}) {
  const auto authoringIssue =
      TranslucentAuthoringIssue(graph, textureParams, textures);
  if (!authoringIssue.empty())
    throw std::runtime_error(authoringIssue);
  if (!AssetPath(material, true) || !P4MapKit::Export::ModFolderPackage(material))
    throw std::runtime_error("translucent creator material path");
  for (const auto &output : Get(graph, "outputs").object)
    if (output.second.kind != Json::Null && output.first != "EmissiveColor" &&
        output.first != "Opacity")
      throw std::runtime_error("The unlit translucent compiler cannot export the connected " + output.first + " input yet.");
  if (Get(graph, "clip").kind != Json::Null)
    throw std::runtime_error("translucent opacity is not an opacity mask");
  for (const auto &node : Get(graph, "nodes").object)
    if (Text(node.second, "type") == "CollectionParameter")
      throw std::runtime_error("translucent collection parameters require "
                               "dynamic collection bindings");
  const auto chain = TranslucentParentChain(gameMaterial, selected, resolver);
  FMaterialCookedSettings settings;
  FReadError error;
  if (!ReadMaterialCookedSettings(chain, settings, error))
    throw std::runtime_error(error.Code);
  if (settings.Domain != "MD_Surface" ||
      settings.BlendMode != "BLEND_Translucent" ||
      settings.ShadingModel != "MSM_Unlit" ||
      settings.TwoSided != Flag(graph, "two_sided"))
    throw std::runtime_error("translucent donor render settings mismatch");
  auto package = ReadPackageHeader(selected.header, selected.body.size());
  std::size_t root = ~std::size_t(0);
  const auto leaf = gameMaterial.substr(gameMaterial.find_last_of('/') + 1);
  for (std::size_t i = 0; i < package.Exports.size(); ++i)
    if (package.Exports[i].Name == leaf) {
      if (root != ~std::size_t(0))
        throw std::runtime_error("translucent source root ambiguous");
      root = i;
    }
  FMaterialResourceBoundary boundary;
  if (!DiscoverMaterialResourceBoundary(package, selected.body, root, boundary,
                                        error) ||
      boundary.Class != "MaterialInstanceConstant")
    throw std::runtime_error("The original translucent material resource could not be read. " + error.Code);
  FMaterialShaderMapEnvelope map;
  if (!ReadMaterialShaderMapEnvelope(selected.body, boundary.ResourceStart,
                                     boundary.ExportEnd, map, error))
    throw std::runtime_error(error.Code);
  std::vector<FMaterialShaderReference> refs;
  if (!ReadMaterialShaderReferences(map, refs, error))
    throw std::runtime_error(error.Code);
  const auto indices =
      archive.Map(Bytes(map.ResourceHash.begin(), map.ResourceHash.end()));
  std::map<unsigned, FMaterialShaderReference> pixels;
  for (const auto &ref : refs) {
    if (ref.ResourceIndex < 0 ||
        std::size_t(ref.ResourceIndex) >= indices.size() ||
        archive.Frequency(indices[ref.ResourceIndex]) != ref.Frequency)
      throw std::runtime_error("translucent shader archive association");
    if (ref.Frequency != 3) {
      RequireTranslucentVertex(ref,
                               archive.Payload(indices[ref.ResourceIndex]));
      continue;
    }
    if (ref.VertexFactoryHash != EngineHash::LocalVertexFactory ||
        (ref.TypeHash != 0x4507f03a199c6526ull &&
         ref.TypeHash != 0xa8d53bcb8fca168dull))
      throw std::runtime_error("translucent pixel pass unavailable");
    auto old = pixels.find(unsigned(ref.ResourceIndex));
    if (old != pixels.end() &&
        (old->second.NamedBuffers != ref.NamedBuffers ||
         old->second.UniformBufferHashes != ref.UniformBufferHashes ||
         old->second.LooseTextures != ref.LooseTextures ||
         old->second.LooseSamplers != ref.LooseSamplers))
      throw std::runtime_error("translucent pixel aliases disagree");
    pixels.emplace(unsigned(ref.ResourceIndex), ref);
  }
  if (pixels.empty())
    throw std::runtime_error("translucent donor pixel pass absent");
  TranslucentCompilation result;
  result.compiled.source_references=refs; result.uniforms= GraphUniforms(InlineFunctions(graph, Json()), textures);
  const auto patched = ReplaceOwnedUniforms(map, result.uniforms);
  result.compiled.material.material = material;
  result.compiled.material.game_material = gameMaterial;
  result.compiled.material.map_id_hash =
      Bytes(map.CookedIdHash.begin(), map.CookedIdHash.end());
  result.compiled.material.map_shaders = unsigned(indices.size());
  result.compiled.material.edits = patched.edits;
  for (const auto &item : pixels) {
    auto original = ReadPixelBindings(archive.Payload(indices[item.first]));
    RequireTranslucentBindings(original, item.second);
    if (original.layout_hashes[2] != Read32(map.Frozen, 472))
      throw std::runtime_error(
          "translucent Material header/frozen layout mismatch");
    unsigned materialBuffer = ~0u;
    for (const auto &binding : item.second.NamedBuffers)
      if (binding.second == "Material") {
        if (materialBuffer != ~0u)
          throw std::runtime_error("translucent Material uniform ambiguous");
        materialBuffer = binding.first;
      }
    if (materialBuffer != 2)
      throw std::runtime_error(
          "translucent Material uniform binding unavailable");
    auto rebound = RebindMaterialTextures(
        original, materialBuffer, unsigned(textures.size()),
        patched.layout_hash, false, item.second.LooseTextures,
        item.second.LooseSamplers);
    rebound.bindings.layout_hashes[materialBuffer] = patched.layout_hash;
    auto bindings =
        ResolveMaterialBindings(rebound.bindings, materialBuffer, {});
    for (std::size_t i = 0; i < textures.size(); ++i)
      bindings.textures[textures[i].name] = {int(rebound.slots[i].first),
                                             int(rebound.slots[i].second)};
    auto assembly = TranslucentUnlitProgram(graph, result.uniforms, original,
                                            materialBuffer, textureParams);
    auto compiled = ResolveAndCompilePixel(assembly, bindings.textures,
                                           bindings.varyings, rebound.bindings);
    result.compiled.material.shaders.push_back({item.first, compiled.payload});
    result.compiled.programs.push_back(std::move(compiled));
  }
  (void)WriteShaderSet3({result.compiled.material});
  return result;
}
} // namespace NativeBackend
} // namespace CustomMaterial
} // namespace P4MapKit
