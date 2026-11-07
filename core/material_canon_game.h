// The game's facts the master graph rewrites read (material_graph_canon.h), from the game's own files as Kadan's
// import has them: the master's uniform expression set from its shader map (decoded to the text
// glsl_probe/uniform_sets.py gives it), its cooked texture defaults and settings, the sampler types of the textures
// it names and its instanced vertex program; and the recovered master's graph in UE's form from them
// (private/own-ue-route/kadan-graphs/make.py, the research route, in C++).
#pragma once
#include "material_masters.h"
#include "material_engine_hashes.h"
#include "material_prepare.h"
#include "material_source_particle_subuv.h"
#include "material_graph_canon.h"
namespace P4MapKit
{
namespace Material
{

// A preshader program as material_shadermap.decode_preshader writes it, with the runtime texture operands as
// material_water.decode_preshader names them (an operation and its record).
inline std::string MaterialCanonPreshaderText(const FMaterialPreshaderProgram& program, const FMaterialUniformSet& set)
{
    using Op = EMaterialPreshaderOpcode;
    const auto paramName = [](const FMaterialUniformParameter& p)
    {
        return p.NameIsNone ? std::string("?") : p.Name;
    };
    const auto preshaderName = [&](std::uint16_t index)
    {
        if (index >= set.PreshaderNames.size())
            throw MaterialCanonError("a preshader name outside its table");
        const auto& name = set.PreshaderNames[index];
        return name == "None" ? std::string("?") : name;
    };
    static const char* names[] = {"nop",
                                  "constantzero",
                                  "constant",
                                  "scalarparameter",
                                  "vectorparameter",
                                  "add",
                                  "sub",
                                  "mul",
                                  "div",
                                  "fmod",
                                  "min",
                                  "max",
                                  "clamp",
                                  "sin",
                                  "cos",
                                  "tan",
                                  "asin",
                                  "acos",
                                  "atan",
                                  "atan2",
                                  "dot",
                                  "cross",
                                  "sqrt",
                                  "length",
                                  "saturate",
                                  "abs",
                                  "floor",
                                  "ceil",
                                  "round",
                                  "trunc",
                                  "sign",
                                  "frac",
                                  "fractional",
                                  "log2",
                                  "log10",
                                  "componentswizzle",
                                  "appendvector",
                                  "texturesize",
                                  "texelsize",
                                  "externaltexturecoordinatescalerotation",
                                  "externaltexturecoordinateoffset",
                                  "runtimevirtualtextureuniform"};
    static const char* operations[] = {"TextureSize", "TexelSize", "ExternalTextureCoordinateScaleRotation",
                                       "ExternalTextureCoordinateOffset", "RuntimeVirtualTextureUniform"};
    std::vector<std::string> st;
    const auto pop = [&]
    {
        if (st.empty())
            throw MaterialCanonError("a preshader stack underflow");
        auto v = st.back();
        st.pop_back();
        return v;
    };
    for (const auto& i : program.Instructions)
    {
        const auto op = i.Opcode;
        const auto code = std::size_t(op);
        if (op == Op::Nop)
            continue;
        if (op == Op::ConstantZero)
        {
            st.push_back("0");
            continue;
        }
        if (op == Op::Constant)
        {
            std::string text = "(";
            for (std::size_t c = 0; c < 4; ++c)
            {
                float f;
                std::memcpy(&f, &i.ConstantBits[c], 4);
                text += (c ? ", " : "") + PyG(double(f));
            }
            st.push_back(text + ")");
            continue;
        }
        if (op == Op::ScalarParameter)
        {
            if (i.Parameter >= set.Scalars.size())
                throw MaterialCanonError("a scalar outside its table");
            st.push_back(paramName(set.Scalars[i.Parameter]));
            continue;
        }
        if (op == Op::VectorParameter)
        {
            if (i.Parameter >= set.Vectors.size())
                throw MaterialCanonError("a vector outside its table");
            st.push_back(paramName(set.Vectors[i.Parameter]));
            continue;
        }
        if (op == Op::Add || op == Op::Sub || op == Op::Mul || op == Op::Div)
        {
            const auto b = pop(), a = pop();
            const char* sym = op == Op::Add ? "+" : op == Op::Sub ? "-" : op == Op::Mul ? "*" : "/";
            st.push_back("(" + a + " " + sym + " " + b + ")");
            continue;
        }
        if (op == Op::Fmod || op == Op::Min || op == Op::Max || op == Op::Atan2 || op == Op::Dot || op == Op::Cross)
        {
            const auto b = pop(), a = pop();
            st.push_back(std::string(names[code]) + "(" + a + ", " + b + ")");
            continue;
        }
        if (op == Op::Clamp)
        {
            const auto c = pop(), b = pop(), a = pop();
            st.push_back("clamp(" + a + ", " + b + ", " + c + ")");
            continue;
        }
        if (op == Op::ComponentSwizzle)
        {
            auto v = pop() + ".";
            for (std::size_t c = 0; c < i.Components && c < 4; ++c)
                v += "rgba"[i.Swizzle[c]];
            st.push_back(v);
            continue;
        }
        if (op == Op::AppendVector)
        {
            const auto b = pop(), a = pop();
            st.push_back("append" + std::to_string(i.Components) + "(" + a + ", " + b + ")");
            continue;
        }
        if (code >= 37)
        {
            const auto operation = std::string(operations[code - 37]);
            std::string record =
                "{'operation': " + PyRepr(operation) + ", 'name': " + PyRepr(preshaderName(i.Parameter));
            if (op == Op::ExternalTextureCoordinateScaleRotation || op == Op::ExternalTextureCoordinateOffset)
            {
                static const char* hex = "0123456789abcdef";
                std::string guid;
                for (const auto b : i.ExternalGuid)
                {
                    guid += hex[b >> 4];
                    guid += hex[b & 15];
                }
                record += ", 'guid': " + PyRepr(guid) + ", 'texture_index': " + std::to_string(i.TextureIndex);
            }
            else
            {
                record += ", 'index': " + std::to_string(i.LayerIndex) +
                          ", 'association': " + std::to_string(int(i.Association)) +
                          ", 'texture_index': " + std::to_string(i.TextureIndex);
                if (op == Op::RuntimeVirtualTextureUniform)
                    record += ", 'vector_index': " + std::to_string(i.VectorIndex);
            }
            st.push_back(operation + "(" + record + "})");
            continue;
        }
        st.push_back(std::string(names[code]) + "(" + pop() + ")");
    }
    return st.empty() ? std::string("0") : st.back();
}
inline MaterialCanonUniformSet MaterialCanonUniformsOf(const FMaterialUniformSet& set)
{
    MaterialCanonUniformSet out;
    out.Known = true;
    for (const auto& p : set.VectorPrograms)
        out.Vectors.push_back(MaterialCanonPreshaderText(p, set));
    for (const auto& p : set.ScalarPrograms)
        out.Scalars.push_back(MaterialCanonPreshaderText(p, set));
    static const char* kinds[] = {"Standard2D", "Cube", "Array2D", "Volume", "Virtual"};
    for (std::size_t t = 0; t < 5; ++t)
    {
        std::vector<UniformOrder::TextureName> names;
        for (const auto& texture : set.Textures)
            if (texture.TextureType == t)
            {
                UniformOrder::TextureName name;
                name.None = false;
                name.Name = texture.Parameter.NameIsNone ? std::string("?") : texture.Parameter.Name;
                names.push_back(name);
            }
        if (!names.empty())
            out.Textures.emplace_back(kinds[t], names);
    }
    const auto value = [](std::uint32_t bits)
    {
        float f;
        std::memcpy(&f, &bits, 4);
        return double(f);
    };
    for (const auto& s : set.Scalars)
        out.ScalarDefaults[s.NameIsNone ? std::string("?") : s.Name] = value(s.DefaultBits[0]);
    for (const auto& v : set.Vectors)
    {
        std::vector<double> d;
        for (const auto bits : v.DefaultBits)
            d.push_back(value(bits));
        out.VectorDefaults[v.NameIsNone ? std::string("?") : v.Name] = d;
    }
    return out;
}

// One cooked property as material_cooked.material_properties reads it (a bool, a number, an enum's or a name's text).
inline bool MaterialCanonCookedValue(const FDecodedValue& value, Json& out)
{
    switch (value.Kind)
    {
        case FDecodedValue::EKind::Boolean:
        {
            bool b = false;
            if (!MaterialBool(value, b))
                return false;
            out = Json(b);
            return true;
        }
        case FDecodedValue::EKind::Number:
        {
            if (value.Type == "FloatProperty")
            {
                const float f = std::strtof(value.Text.c_str(), nullptr);
                out = Json(double(f));
                return true;
            }
            char* end = nullptr;
            const double d = std::strtod(value.Text.c_str(), &end);
            if (end == value.Text.c_str())
                return false;
            out = Json(d);
            return true;
        }
        case FDecodedValue::EKind::Enum:
        case FDecodedValue::EKind::Name:
        case FDecodedValue::EKind::String:
            out = Json(value.Text);
            return true;
        default:
            return false;
    }
}
// The stock UMaterial settings the game's masters change and the decompiled graphs leave out, and the settings only
// the game's engine has (C4...), from the master's cooked properties, where the graph does not say them (make.py).
inline Json MaterialCanonCookedSettings(Json graph, const FDecodedValue& master)
{
    static const char* stock[] = {"MaterialDecalResponse",
                                  "BlendableLocation",
                                  "BlendableOutputAlpha",
                                  "BlendablePriority",
                                  "DecalBlendMode",
                                  "RefractionMode",
                                  "TranslucencyDirectionalLightingIntensity",
                                  "TranslucencyLightingMode",
                                  "TranslucentShadowDensityScale",
                                  "Wireframe",
                                  "bAllowNegativeEmissiveColor",
                                  "bDisableDepthTest",
                                  "bEnableSeparateTranslucency",
                                  "bFullyRough",
                                  "bIsBlendable",
                                  "bIsSky",
                                  "bScreenSpaceReflections",
                                  "bUseEmissiveForDynamicAreaLighting",
                                  "bUseHQForwardReflections",
                                  "bUseLightmapDirectionality",
                                  "bUseTranslucencyVertexFog"};
    auto render = graph.Has("render_settings") && graph.At("render_settings").kind == Json::Object
                      ? graph.At("render_settings")
                      : Json::MakeObject();
    std::map<std::string, Json> cooked;
    for (const auto& field : master.Fields)
    {
        Json v;
        if (MaterialCanonCookedValue(field.second, v))
            cooked[field.first] = v;
    }
    for (const char* key : stock)
    {
        const auto f = cooked.find(key);
        if (f != cooked.end() && !render.Has(key))
            render[key] = f->second;
    }
    for (const auto& f : cooked)
        if (f.first.find("C4") != std::string::npos && !render.Has(f.first))
            render[f.first] = f.second;
    if (!render.object.empty())
        graph["render_settings"] = render;
    return graph;
}

// The texture defaults a master gives its texture parameters itself (material_cooked.default_textures).
inline std::map<std::string, std::string> MaterialCanonDefaultTextures(const FDecodedValue& master)
{
    std::map<std::string, std::string> out;
    const auto cached = master.Field("CachedExpressionData");
    const auto parameters = cached ? cached->Field("Parameters") : nullptr;
    if (!parameters)
        return out;
    const auto entries = parameters->Field("RuntimeEntries[2]");
    const auto infos = entries ? entries->Field("ParameterInfos") : nullptr;
    const auto values = parameters->Field("TextureValues");
    if (!infos || !values)
        return out;
    for (std::size_t i = 0; i < infos->Items.size() && i < values->Items.size(); ++i)
    {
        const auto name = infos->Items[i].Field("Name");
        const auto& value = values->Items[i];
        if (!name || name->Text.empty() ||
            (value.Kind != FDecodedValue::EKind::HardReference && value.Kind != FDecodedValue::EKind::SoftPath) ||
            value.Text.empty())
            continue;
        if (value.Kind == FDecodedValue::EKind::HardReference && value.ReferenceIndex == 0)
            continue;
        std::string object = value.Text;
        if (object.find('.') == std::string::npos)
            object += "." + object.substr(object.find_last_of('/') + 1);
        out[name->Text] = object;
    }
    return out;
}

// The facts of one master from the game's files (material_graph_canon.Game for that master).
inline MaterialCanonGame ReadMaterialCanonGame(MaterialMasterSources& sources, const std::string& masterPackage,
                                               const MaterialOriginalArchiveAccess& archive)
{
    MaterialCanonGame game;
    const auto resolve = sources.Resolver();
    MaterialSourceAsset asset;
    if (!resolve(MaterialSourcePackage(masterPackage), asset))
        return game;
    game.SamplerType = [resolve](const std::string& texture)
    {
        try
        {
            MaterialSourceAsset t;
            if (!resolve(MaterialSourcePackage(texture), t))
                return std::string();
            std::string type, kind;
            FDecodedValue props;
            FReadError error;
            if (!MaterialSourceReadObject(t,
                                          {"Texture2D", "MediaTexture", "TextureRenderTarget2D", "TextureCube",
                                           "Texture2DArray", "VolumeTexture"},
                                          type, props))
                return std::string();
            if (!ReadMaterialSamplerType(type, props, kind, error))
                return std::string();
            return kind;
        }
        catch (const std::exception&)
        {
            return std::string();
        }
    };
    // The master's uniform set: as uniform_sets.py, {"error"} when its shader map cannot be read.
    FMaterialShaderMapEnvelope map;
    std::vector<FMaterialShaderReference> references;
    bool readable = false;
    try
    {
        std::size_t index = asset.Package.Exports.size();
        for (std::size_t i = 0; i < asset.Package.Exports.size(); ++i)
        {
            std::string type;
            if (!asset.Package.Reference(asset.Package.Exports[i].Class, type))
                continue;
            type = type.substr(type.find_last_of('.') == std::string::npos ? 0 : type.find_last_of('.') + 1);
            if (type == "Material")
            {
                index = i;
                break;
            }
        }
        FMaterialResourceBoundary boundary;
        FReadError error;
        readable = index < asset.Package.Exports.size() && asset.Body &&
                   DiscoverMaterialResourceBoundary(asset.Package, *asset.Body, index, boundary, error) &&
                   ReadMaterialShaderMapEnvelope(*asset.Body, boundary.ResourceStart, boundary.ExportEnd, map, error) &&
                   ReadMaterialShaderReferences(map, references, error);
    }
    catch (const std::exception&)
    {
        readable = false;
    }
    if (readable)
    {
        try
        {
            game.Uniforms = MaterialCanonUniformsOf(map.Uniforms);
        }
        catch (const MaterialCanonError&)
        {
            readable = false;
        }
    }
    if (!readable)
    {
        game.Uniforms = MaterialCanonUniformSet();
        game.Uniforms.Known = true;
        game.Uniforms.Error = true;
    }
    try
    {
        const auto chain = ReadOriginalMaterialParentChain(asset, resolve);
        if (!chain.empty())
            game.DefaultTextures = MaterialCanonDefaultTextures(chain.front().Properties);
    }
    catch (const std::exception&)
    {
    }
    // The instanced vertex program (game_masters_dump.py: the first TBasePassVSFNoLightMapPolicy of the instanced
    // static mesh factory).
    if (readable && archive.MapIndices && archive.Payload)
        for (const auto& ref : references)
        {
            if (ref.Frequency != 0 || ref.VertexFactoryHash != EngineHash::InstancedStaticMeshVertexFactory ||
                ref.TypeHash != 0xe99e2d4408e39e36ull)
                continue;
            try
            {
                const auto indices = archive.MapIndices(map.ResourceHash);
                if (ref.ResourceIndex < 0 || std::size_t(ref.ResourceIndex) >= indices.size())
                    break;
                const auto payload = archive.Payload(indices[std::size_t(ref.ResourceIndex)]);
                const auto header = ReadOriginalParticleVertexBindings(payload);
                game.InstancedProgram = ReadOriginalMaterialProgramText(payload, header);
                game.HasInstancedProgram = true;
                for (const auto& output : header.outputs)
                    game.InstancedOutputs.emplace_back(output.name, int(output.location));
            }
            catch (const std::exception&)
            {
                game.HasInstancedProgram = false;
                game.InstancedProgram.clear();
                game.InstancedOutputs.clear();
            }
            break;
        }
    return game;
}
inline Json MaterialCanonGameJson(const MaterialCanonGame& game, const std::set<std::string>& textures)
{
    auto out = Json::MakeObject();
    out["uniform_set"] = MaterialCanonUniformSetJson(game.Uniforms);
    auto defaults = Json::MakeObject();
    for (const auto& d : game.DefaultTextures)
        defaults[d.first] = Json(d.second);
    out["default_textures"] = defaults;
    auto types = Json::MakeObject();
    for (const auto& t : textures)
    {
        const auto kind = game.SamplerType ? game.SamplerType(t) : std::string();
        types[t.substr(0, t.find('.'))] = kind.empty() ? Json() : Json(kind);
    }
    out["sampler_types"] = types;
    if (game.HasInstancedProgram)
    {
        out["instanced_program"] = Json(game.InstancedProgram);
        auto outputs = Json::MakeArray();
        for (const auto& o : game.InstancedOutputs)
            outputs.array.push_back(Json(std::vector<Json>{Json(o.first), Json(o.second)}));
        out["instanced_outputs"] = outputs;
    }
    return out;
}

// A recovered master's graph in UE's form: the cooked settings its graph leaves out, then the rewrites. The graph
// already has Kadan's game reads and the kit's finite +Inf (RecoverMaterialMaster). Why is set, and the graph kept as
// it was, where the rewrites have no form for it.
inline Json MaterialMasterGameForm(const Json& graph, MaterialMasterSources& sources, const std::string& masterPackage,
                                   const MaterialOriginalArchiveAccess& archive, std::string* why = nullptr)
{
    if (graph.kind != Json::Object || !graph.Has("nodes"))
        return graph;
    auto settled = graph;
    try
    {
        MaterialSourceAsset asset;
        const auto resolve = sources.Resolver();
        if (resolve(MaterialSourcePackage(masterPackage), asset))
        {
            const auto chain = ReadOriginalMaterialParentChain(asset, resolve);
            if (!chain.empty())
                settled = MaterialCanonCookedSettings(graph, chain.front().Properties);
        }
    }
    catch (const std::exception& error)
    {
        if (why)
            *why = std::string("the master's cooked settings cannot be read: ") + error.what();
        return graph;
    }
    try
    {
        return CanonMaterialGraph(settled, ReadMaterialCanonGame(sources, masterPackage, archive));
    }
    catch (const MaterialCanonError& error)
    {
        if (why)
            *why = std::string("the game's form has no rewrite for this graph: ") + error.what();
        return settled;
    }
}

// A master family as Kadan's import makes it: RecoverMaterialMaster, then the master's graph in UE's form of the
// game's map. Its instances set the graph's parameters by name, as before.
inline MaterialMasterFamily RecoverMaterialMasterInGameForm(const std::string& masterPackage,
                                                            const std::vector<std::string>& instancePackages,
                                                            MaterialMasterSources& sources,
                                                            const MaterialOriginalArchiveAccess& archive)
{
    auto family = RecoverMaterialMaster(masterPackage, instancePackages, sources, archive);
    if (family.HasGraph)
        family.Merge.Graph =
            MaterialMasterGameForm(family.Merge.Graph, sources, family.Master, archive, &family.GameForm);
    return family;
}
}
} // P4MapKit::Material
