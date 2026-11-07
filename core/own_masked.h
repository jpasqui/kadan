#pragma once
// The mask passes of a masked material of Kadan's own (stage 7 of tools/navigation/OWN_SHADER_MAPS.md): the depth
// pass and the shadow depth passes cut by the material's opacity mask. A masked lit material carries the lit base pass
// (own_opaque.h, which now clips on the mask) and four pixel programs besides: the depth-only pixel shader and three
// shadow depth pixel shaders. Two of those (and the depth-only one) only clip; the perspective correct one also writes
// the depth: the interpolated shadow depth times ShadowParams.w (the shadow depth pass buffer's offset 432) plus the
// interpolated bias, as UE 4.26's ShadowDepthPixelShader does. Their vertex and geometry shaders are
// own_shaders/depth_passes.vert and onepass_point_shadow.geom. Written for one material texture, each description's
// header equals the game's (M_demo_Masked_00_Master) byte for byte (own_masked_tool check).
#include "own_opaque.h"
#include "native_custom_backend_depth.h"
namespace P4MapKit { namespace CustomMaterial { namespace OwnMasked {

constexpr std::uint32_t kShadowDepthPassLayout=184025585u;
constexpr unsigned kShadowParamsOffset=432;   // ShadowDepthPass.ShadowParams; w is the inverse max subject depth

struct Pass {
    const char* name;            // the shader class, or its type hash where the name is not known
    std::uint64_t type_hash;
    bool shadow_pass;            // binds the shadow depth pass buffer in slot 1 (the Material buffer in 2)
    bool writes_depth;
};
inline const std::vector<Pass>& Passes() {
    static const std::vector<Pass> all = {
        {"FDepthOnlyPS<false>",0x925355faf735b81cull,false,false},
        {"37a48ab631bfc57a",0x37a48ab631bfc57aull,false,false},
        {"7557e6252b46eb81",0x7557e6252b46eb81ull,false,false},
        {"4bbfae84d2dbe82a",0x4bbfae84d2dbe82aull,true,true},
    };
    return all;
}
inline unsigned MaterialSlot(const Pass& p) { return p.shadow_pass ? 2u : 1u; }

// The interpolants our depth and shadow depth vertex shaders write that a mask program may read: the tangent frame
// and the texture coordinates; the perspective correct shadow pass also the shadow depth (texcoord6) and bias
// (texcoord8).
inline std::vector<Varying> Inputs(const Pass& p) {
    std::vector<Varying> out{{"in_texcoord10_centroid",0},{"in_texcoord11_centroid",1},{"in_texcoord0",2}};
    if (p.writes_depth) { out.push_back({"in_texcoord6",3}); out.push_back({"in_texcoord8",4}); }
    return out;
}

// A pass's binding description with one material texture (the Material buffer's resource 0 in texture slot 0 and its
// sampler, resource 1, in sampler slot 0): View in slot 0, the shadow depth pass buffer in 1 when bound, the Material
// buffer last. No scene resources and no default sampler.
inline ShaderBindings OneTextureBindings(const Pass& p, std::uint32_t material_layout) {
    ShaderBindings h;
    const unsigned material = MaterialSlot(p), buffers = material + 1;
    h.textures = BuildResourceTable({{material,{{0,0}}}},buffers);
    h.samplers = BuildResourceTable({{material,{{1,0}}}},buffers);
    h.srv = BuildResourceTable({},buffers);
    h.uav = BuildResourceTable({},buffers);
    h.layout_hashes.assign(buffers,0);
    h.layout_hashes[0]=OwnOpaque::kViewLayout; h.layout_hashes[material]=material_layout;
    if (p.shadow_pass) h.layout_hashes[1]=kShadowDepthPassLayout;
    h.resource_bits = 1u<<material;
    h.default_sampler = -1;
    h.inputs = Inputs(p);
    h.inout_mask = p.writes_depth ? 0x8001 : 1;    // bit 15: the program writes the depth
    h.counts = {{1,1,std::uint8_t(buffers),0,0}};
    return h;
}

// The depth write of the perspective correct shadow pass, after the mask's clip.
inline std::string WriteShadowDepth(std::string program) {
    const std::string pass = "CBUFFER buf1[] = { program.buffer[1] };\n", view = "CBUFFER buf0[] = { program.buffer[0] };\n";
    if (program.find(pass)==std::string::npos) {
        const auto at = program.find(view);
        if (at==std::string::npos) throw std::runtime_error("mask program buffer declarations");
        program.insert(at+view.size(),pass);
    }
    const auto output = program.find("OUTPUT ");
    if (output==std::string::npos) throw std::runtime_error("mask program output declaration");
    program.insert(output,"TEMP P4D;\n");
    const auto end = program.rfind("END\n");
    if (end==std::string::npos || end+4!=program.size()) throw std::runtime_error("mask program end");
    program.insert(end,"LDC.F32X4 P4D.w, buf1["+std::to_string(kShadowParamsOffset)+"];\n"
        "MUL.F32 P4D.x, fragment.attrib[{VAR:in_texcoord6}], P4D.w;\n"
        "ADD.F32.SAT result.depth.z, P4D.x, fragment.attrib[{VAR:in_texcoord8}].x;\n");
    return program;
}

struct Program { const Pass* pass; NativeBackend::PixelProgram compiled; ShaderBindings bindings; };

// Every mask pass's program for a creator's masked graph, its parameters read from the Material buffer as the lit
// base pass programs read them (GraphUniforms).
inline std::vector<Program> Compile(const Json& graph, const std::vector<TextureParameter>& textures, std::uint32_t material_layout) {
    if (Get(graph,"clip").kind==Json::Null) throw std::runtime_error("mask passes are for a masked material");
    const auto uniforms = GraphUniforms(InlineFunctions(graph,Json()),textures);
    std::vector<Program> out;
    for (const auto& p : Passes()) try {
        const unsigned material = MaterialSlot(p);
        auto rebound = RebindMaterialTextures(OneTextureBindings(p,material_layout),material,unsigned(textures.size()),material_layout);
        rebound.bindings.layout_hashes.at(material)=material_layout;
        auto bindings = ResolveMaterialBindings(rebound.bindings,material,{});
        for (std::size_t i=0;i<textures.size();++i) bindings.textures[textures[i].name]={int(rebound.slots[i].first),int(rebound.slots[i].second)};
        Emitter emitter(false,Flag(graph,"two_sided")); emitter.owned_uniforms=true;
        emitter.material_buffer=material; emitter.material_parameters=uniforms.bindings;
        auto program = CompileMaskWithEmitter(graph,std::move(emitter),{}).program;
        if (p.writes_depth) program = WriteShadowDepth(std::move(program));
        const auto resolved = Resolve(program,bindings.textures,bindings.varyings);
        // The header lists the interpolants the program reads.
        std::vector<Varying> read;
        for (const auto& v : rebound.bindings.inputs)
            if (resolved.find("fragment.attrib["+std::to_string(v.location)+"]")!=std::string::npos) read.push_back(v);
        rebound.bindings.inputs = read;
        auto compiled = NativeBackend::CompilePixelAssembly(resolved,rebound.bindings);
        out.push_back({&p,std::move(compiled),rebound.bindings});
    } catch (const std::exception& error) { throw std::runtime_error(std::string(p.name) + ": " + error.what()); }
    return out;
}
} } }
