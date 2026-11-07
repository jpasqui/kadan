// Offline fixture only; product calls the same native library in-process.
#include "native_custom_backend.h"
#include "native_custom_backend_policy.h"
#include "native_custom_backend_opaque_template.h"
#include "native_custom_material_instance_cook.h"
#include "custom_material_bindings.h"
#include "native_custom_backend_template.h"
#include "custom_material_lighting.h"
#include "custom_material_policy_emitter.h"
#include "custom_material_reflection.h"
#include "custom_material_skylight.h"
#include "custom_material_lightmap.h"
#include "custom_material_reflection_response.h"
#include "custom_material_reflection_policy.h"
#include "custom_material_shadow.h"
#include "custom_material_directional_shadow.h"
#include "custom_material_baked_shadow.h"
#include "custom_material_directional.h"
#include "custom_material_c4_controls.h"
#include "custom_material_indirect.h"
#include "custom_material_dbuffer.h"
#include "custom_material_opaque_policy.h"
#include "custom_material_final_composition.h"
#include "material_forward_lighting.h"
#include "material_forward_geometry.h"
#include <fstream>
#include <iostream>
#include <memory>
#include <cmath>
using namespace P4MapKit::CustomMaterial;
using P4MapKit::Material::Json;
Bytes ReadFixture(const std::string& Path) {
    std::ifstream File(Path,std::ios::binary);if(!File)throw std::runtime_error("fixture file unavailable");
    File.seekg(0,std::ios::end);auto Size=File.tellg();if(Size<0||Size>(1ll<<31))throw std::runtime_error("fixture file size");
    File.seekg(0);Bytes Data(static_cast<std::size_t>(Size));if(!File.read(reinterpret_cast<char*>(Data.data()),Size))throw std::runtime_error("fixture read failed");return Data;
}
const NativeBackend::ShaderArchive& CachedFixtureArchive(const std::string& Path) {
    static std::string Name;static Bytes Data;static std::unique_ptr<NativeBackend::ShaderArchive> Archive;
    if(!Archive||Name!=Path){Archive.reset();Data=ReadFixture(Path);Archive.reset(new NativeBackend::ShaderArchive(Data));Name=Path;}
    return *Archive;
}
P4MapKit::FMaterialShaderReference FixtureReference(const Json& Request,const NativeBackend::ShaderArchive& Archive,std::size_t Index) {
    const auto Body=ReadFixture(Request["template"].Text());
    P4MapKit::FMaterialShaderMapEnvelope Map;P4MapKit::FReadError Error;
    if(!P4MapKit::ReadMaterialShaderMapEnvelope(Body,std::size_t(Number(Request,"resource_start")),std::size_t(Number(Request,"export_end")),Map,Error))
        throw std::runtime_error("diagnostic fixture map: "+Error.Code);
    std::vector<P4MapKit::FMaterialShaderReference> References;
    if(!P4MapKit::ReadMaterialShaderReferences(Map,References,Error))throw std::runtime_error("diagnostic fixture references: "+Error.Code);
    const auto Indices=Archive.Map(Bytes(Map.ResourceHash.begin(),Map.ResourceHash.end()));
    const auto WantedType=Text(Request,"type_hash");
    bool Found=false;P4MapKit::FMaterialShaderReference Selected;
    for(const auto& Ref:References){
        if(Ref.ResourceIndex<0||std::size_t(Ref.ResourceIndex)>=Indices.size())throw std::runtime_error("diagnostic fixture resource index");
        if(Indices[Ref.ResourceIndex]!=Index||Ref.Frequency!=3)continue;
        if(!WantedType.empty()&&Ref.TypeHash!=std::stoull(WantedType,nullptr,16))continue;
        if(Found&&Selected.UniformBufferHashes!=Ref.UniformBufferHashes)throw std::runtime_error("diagnostic fixture ambiguous uniform hashes");
        Selected=Ref;Found=true;
    }
    if(!Found)throw std::runtime_error("diagnostic fixture archive/reference association absent");
    return Selected;
}
std::map<std::uint16_t,std::uint64_t> FixtureUniformHashes(const Json& Request,const NativeBackend::ShaderArchive& Archive,std::size_t Index) {
    return FixtureReference(Request,Archive,Index).UniformBufferHashes;
}
std::string FixtureFinalComposition(const Json& Request,const ShaderBindings& Header,const P4MapKit::FMaterialShaderReference& Ref) {
    using namespace Lighting;
    RequireIndirectView(Ref.UniformBufferHashes);
    ValidateDiagnosticBuffer(Header,{13,true});
    Emitter Em;PolicyEmitter Policy(Em,Header);
    auto Load=[&](unsigned Offset,int Size){return Policy.Load(13,Offset,Size);};
    const auto Stage=Text(Request,"stage"),Output=Text(Request,"output");Value Result;
    if(Stage=="opaque-final-stage") {
        if(Get(Request,"has_irradiance_producer").kind!=Json::Boolean||Get(Request,"has_emissive_producer").kind!=Json::Boolean)
            throw std::runtime_error("final fixture requires explicit source presence");
        OpaqueFinalInputs Inputs{Load(0,3),Load(16,3),Load(32,3),Load(48,3),Load(64,3),{Load(80,3),Load(96,1)},
            Load(112,1),Load(128,3),Flag(Request,"has_irradiance_producer"),Flag(Request,"has_emissive_producer")};
        if(Output=="linear")Result=Em.append(EmitOpaqueLinearComposition(Em,Inputs),F32({0}));
        else if(Output=="final")Result=EmitOpaqueFinalColor(Policy,Inputs,Load(144,4));
        else throw std::runtime_error("final fixture output unavailable");
    } else if(Stage=="opaque-ambient-stage") {
        if(Output!="ambient")throw std::runtime_error("ambient fixture output unavailable");
        Result=Em.append(EmitOpaqueAmbient(Policy,Load(0,3),Load(16,1),Load(32,1)),F32({0}));
    } else {
        OpaqueReflectionTerms Terms;
        if(Stage=="opaque-reflection-response-terms")
            Terms=EmitOpaqueReflectionResponseTerms(Em,Load(0,3),Load(16,3),Load(32,1),Load(48,1),Load(64,1),Load(80,1),Load(96,1),Load(112,1));
        else if(Stage=="opaque-reflection-policy-terms") {
            const OpaqueReflectionInputs Inputs{Load(0,3),Load(16,3),Load(32,1),Load(48,3),Load(64,1),Load(80,1),Load(96,1),Load(112,1)};
            Terms=EmitOpaqueReflectionPolicyTerms(Policy,ReadReflectionCaptureBinding(Header,Ref.UniformBufferHashes),OriginalReflectionPolicy(Ref.TypeHash),Inputs);
        } else throw std::runtime_error("final fixture stage unavailable");
        if(Output=="radiance")Result=Em.append(Terms.radianceBeforeWeight,F32({0}));
        else if(Output=="weight")Result=Em.append(Em.op("MOV",3,{Terms.shadowBlend}),F32({0}));
        else throw std::runtime_error("reflection fixture output unavailable");
    }
    Em.emit("MOV.F32 result_color0, "+Result.text);
    return Resolve(Policy.Program(),{},{});
}
int main() {
    std::string Line;
    while (std::getline(std::cin,Line)) {
        Json Reply=Json::MakeObject();
        try {
            Json Request=Json::Parse(Line,1u<<20);
            if(Text(Request,"command")=="original-instance-tail") {
                const auto Header=ReadFixture(Text(Request,"header")),Body=ReadFixture(Text(Request,"body"));
                const auto Package=P4MapKit::ReadPackageHeader(Header,Body.size());
                if(!Package.Error.Code.empty()||Package.Exports.size()!=1)throw std::runtime_error("original instance owner");
                const auto& Export=Package.Exports.front();std::string Class;
                if(!Package.Reference(Export.Class,Class)||Class!="/Script/Engine.MaterialInstanceConstant")throw std::runtime_error("original instance class");
                std::size_t Position=Export.Offset;P4MapKit::FDecodedValue Properties;P4MapKit::FPropertyDecoder Decoder(Package,Body);
                if(!Decoder.TypedProperties(Position,Export.Offset+Export.Size,Properties))throw std::runtime_error(Decoder.Error.Code);
                bool Static=false;const auto Field=Properties.Field("bHasStaticPermutationResource");
                if(Field&&!P4MapKit::MaterialBool(*Field,Static))throw std::runtime_error("original static flag type");
                const auto End=Export.Offset+Export.Size;Bytes Tail(Body.begin()+Position,Body.begin()+End);
                Reply["ok"]=Json(true);Reply["package"]=Json(Package.Package);Reply["static_flag_serialized"]=Json(Field!=nullptr);Reply["static_flag"]=Json(Static);
                Reply["property_end"]=Json(int(Position));Reply["export_end"]=Json(int(End));Reply["native_tail_size"]=Json(int(Tail.size()));Reply["native_tail_sha256"]=Json(Hex(Sha256(Tail)));
                std::cout<<Reply.Dump()<<'\n';continue;
            }
            if(Text(Request,"command")=="fresh-instance-cook") {
                NativeBackend::FreshOpaqueInstanceProperties Expected;
                if(Get(Request,"inherited_clip").kind!=Json::Null)Expected.InheritedClip=float(Get(Request,"inherited_clip").Numeric());
                for(const auto& Texture:Get(Request,"cached_textures").array)Expected.CachedTextures.push_back(Texture.Text());
                const auto Parsed=NativeBackend::ReadFreshOpaqueInstanceCook(ReadFixture(Text(Request,"header")),ReadFixture(Text(Request,"body")),Text(Request,"package"),Text(Request,"parent"),Expected);
                Reply["ok"]=Json(true);Reply["package"]=Json(Parsed.package);Reply["parent"]=Json(Parsed.parent);
                Reply["summary_folder"]=Json(Parsed.summary_folder);
                Reply["header_sha256"]=Json(Parsed.header_sha256);Reply["body_sha256"]=Json(Parsed.body_sha256);Reply["property_end"]=Json(int(Parsed.property_end));Reply["export_end"]=Json(int(Parsed.export_end));
                Reply["production_admitted"]=Json(false);std::cout<<Reply.Dump()<<'\n';continue;
            }
            if(Text(Request,"command")=="opaque-research-template") {
                auto source=[](const Json& input){NativeBackend::OpaqueResearchSource raw;raw.header=ReadFixture(Text(input,"header"));raw.body=ReadFixture(Text(input,"body"));return raw;};
                const auto& Archive=CachedFixtureArchive(Text(Request,"archive"));
                const auto Selected=source(Get(Request,"source"));
                const NativeBackend::OpaqueResearchSourceResolver Resolve=[&](const std::string& package,NativeBackend::OpaqueResearchSource& raw){
                    const auto& row=Get(Get(Request,"parents"),package);if(row.kind==Json::Null)return false;raw=source(row);return true;
                };
                auto Result=NativeBackend::CompileOpaqueResearchTemplate(Get(Request,"graph"),Text(Request,"material"),Text(Request,"game_material"),Selected,Resolve,Archive,Text(Request,"mesh","static"));
                Reply["ok"]=Json(true);Reply["production_admitted"]=Json(Result.production_admitted);Reply["recipe"]=Result.recipe;
                Reply["recipe_source"]=Json(Result.recipe.Dump());
                Reply["recipe_digest"]=Json(Result.recipe_digest);Reply["set"]=Json(Hex(WriteShaderSet3({Result.compiled.material})));Reply["passes"]=Json(int(Result.compiled.programs.size()));
                std::cout<<Reply.Dump()<<'\n';continue;
            }
            if(Text(Request,"command")=="reference-fixture") {
                const auto Index=Number(Request,"archive_index",-1);
                if(Index<0||Index>0x7fffffff||std::floor(Index)!=Index)throw std::runtime_error("reference fixture archive index");
                const auto& Archive=CachedFixtureArchive(Request["archive"].Text());
                const auto Ref=FixtureReference(Request,Archive,std::size_t(Index));
                Json Hashes=Json::MakeObject();
                for(const auto& Pair:Ref.UniformBufferHashes){std::ostringstream Value;Value<<std::hex<<Pair.second;Hashes[std::to_string(Pair.first)]=Json(Value.str());}
                Reply["ok"]=Json(true);Reply["uniform_hashes"]=Hashes;Reply["resource_index"]=Json(Ref.ResourceIndex);Reply["frequency"]=Json(Ref.Frequency);
                std::cout<<Reply.Dump()<<'\n';continue;
            }
            if(Text(Request,"command")=="opaque-source-fixture") {
                const auto Index=Number(Request,"archive_index",-1);
                if(Index<0||Index>0x7fffffff||std::floor(Index)!=Index)throw std::runtime_error("source fixture archive index");
                const auto& Archive=CachedFixtureArchive(Request["archive"].Text());
                auto Ref=FixtureReference(Request,Archive,std::size_t(Index));
                auto Payload=Archive.Payload(std::size_t(Index));
                if(Text(Request,"source_mutation")=="uniform-name")Ref.UniformBufferHashes[0]^=1;
                if(Text(Request,"source_mutation")=="control-load") {
                    const auto Binding=ReadPixelBindings(Payload);const auto Text=NativeBackend::OriginalAssemblyMetadata(Payload,Binding);
                    const auto Found=Text.find("[2592]");if(Found==std::string::npos)throw std::runtime_error("mutation requires source control");
                    const auto CodeAt=ShaderHeader(Binding).size();const auto ControlAt=CodeAt+8+Read32(Payload,CodeAt);
                    const auto At=ControlAt+0x878+Found;Payload[At+4]='0';
                }
                const auto Proof=NativeBackend::ReadOpaqueDirectionalSourceProof(Payload,Ref);
                Reply["ok"]=Json(true);Reply["directional_response"]=Json(Proof.Response()==NativeBackend::OriginalOpaqueDirectionalResponse::SurfaceShadow?"SurfaceShadow":"SeparateSpecularShadow");
                Reply["directional_response_source_sha256"]=Json(Proof.AssemblySha256());Reply["directional_control_loads"]=Json(int(Proof.ControlLoads()));
                std::cout<<Reply.Dump()<<'\n';continue;
            }
            if(Request.Has("command")&&Request["command"].Text()=="template") {
                auto MaterialBytes=ReadFixture(Request["template"].Text());auto ArchiveBytes=ReadFixture(Request["archive"].Text());
                NativeBackend::ShaderArchive Archive(ArchiveBytes);RenderRequest Render;Render.production=false;Render.lighting="diagnostic-sun";
                Render.blend=Text(Request,"blend","Opaque");Render.mesh=Text(Request,"mesh","static");
                NativeBackend::TemplateOptions Options;std::map<std::string,std::string> Parameters;
                for(const auto& Pair:Get(Request,"texture_parameters").object)Parameters.emplace(Pair.first,Pair.second.Text());
                if(Request.Has("textures")){Options.own_texture_list=true;Render.custom_texture_list=true;Options.share_sampler=Flag(Request,"share_sampler",false);for(const auto& Texture:Get(Request,"textures").array)Options.textures.push_back({Texture.Text(),0});Render.texture_count=unsigned(Options.textures.size());}
                auto Set=NativeBackend::CompileDiagnosticTemplate(Request["graph"],Render,Request["material"].Text(),Request["game_material"].Text(),
                    MaterialBytes,std::size_t(Number(Request,"resource_start")),std::size_t(Number(Request,"export_end")),Archive,Parameters,Options);
                Reply["ok"]=Json(true);Reply["set"]=Json(Hex(WriteShaderSet3({Set.material})));Reply["passes"]=Json(int(Set.programs.size()));
                std::cout<<Reply.Dump()<<'\n';continue;
            }
            ShaderBindings Bindings;
            if (Request.Has("template_payload")) Bindings=ReadPixelBindings(Unhex(Request["template_payload"].Text()));
            else for (int I=0;I<32;++I) Bindings.inputs.push_back({"in_texcoord"+std::to_string(I),I});
            std::string Source=Request.Has("command")&&Request["command"].Text()=="lighting-block"?Lighting::BlockProgram(Request["block"].Text()):
                Request.Has("program")?Request["program"].Text():std::string();
            if(Request.Has("command")&&Request["command"].Text()=="graph-depth")
                Source=Resolve(CompileGraph(Request["graph"],{},true).program,{},{});
            const std::string Command=Text(Request,"command");
            std::unique_ptr<NativeBackend::StorageReadProof> StorageProof;
            if(Command=="opaque-policy-stage"||Command=="opaque-policy-program") {
                const auto Index=Number(Request,"archive_index",-1);
                if(Index<0||Index>0x7fffffff||std::floor(Index)!=Index)throw std::runtime_error("opaque fixture archive index");
                const auto& Archive=CachedFixtureArchive(Request["archive"].Text());
                if(Archive.Frequency(std::size_t(Index))!=3)throw std::runtime_error("opaque fixture requires pixel shader");
                const auto Payload=Archive.Payload(std::size_t(Index));Bindings=ReadPixelBindings(Payload);
                const auto Ref=FixtureReference(Request,Archive,std::size_t(Index));
                const auto SourceProof=NativeBackend::ReadOpaqueDirectionalSourceProof(Payload,Ref);
                const auto Directional=SourceProof.Response()==NativeBackend::OriginalOpaqueDirectionalResponse::SurfaceShadow?
                    Lighting::OpaqueDirectionalResponse::SurfaceShadow:Lighting::OpaqueDirectionalResponse::SeparateSpecularShadow;
                const auto DirectionalName=Directional==Lighting::OpaqueDirectionalResponse::SurfaceShadow?"SurfaceShadow":"SeparateSpecularShadow";
                if(Request.Has("directional_response")&&Text(Request,"directional_response")!=DirectionalName)throw std::runtime_error("opaque fixture directional assertion mismatches original source");
                const auto Response=Text(Request,"decal_response");
                if(Response!="Color"&&Response!="ColorRoughness")throw std::runtime_error("opaque fixture source response unavailable");
                const auto Decal=Response=="Color"?Lighting::OriginalDecalResponse::Color:Lighting::OriginalDecalResponse::ColorRoughness;
                Source=Command=="opaque-policy-program"?
                    Lighting::OpaqueMaterialProgram(Request["graph"],Bindings,Ref.UniformBufferHashes,Ref.TypeHash,Decal,{}, {},Json(),true,Directional):
                    Lighting::OpaqueMaterialStageProgram(Request["graph"],Bindings,Ref.UniformBufferHashes,Ref.TypeHash,Decal,Text(Request,"output"),true,Directional);
                if(Lighting::OriginalIndirectPolicy(Ref.TypeHash).sky)
                    StorageProof.reset(new NativeBackend::StorageReadProof(NativeBackend::ReadSkyIrradianceStorageProof(Payload,Ref.UniformBufferHashes)));
                Reply["original_policy_bindings_only"]=Json(true);
                Reply["directional_response"]=Json(DirectionalName);Reply["directional_response_source_sha256"]=Json(SourceProof.AssemblySha256());
                Reply["directional_control_loads"]=Json(int(SourceProof.ControlLoads()));
            }
            if(Command=="geometry-policy-stage") {
                const auto Index=Number(Request,"archive_index",-1);
                if(Index<0||Index>0x7fffffff||std::floor(Index)!=Index)throw std::runtime_error("geometry fixture archive index");
                const auto& Archive=CachedFixtureArchive(Request["archive"].Text());
                if(Archive.Frequency(std::size_t(Index))!=3)throw std::runtime_error("geometry fixture requires pixel shader");
                Bindings=ReadPixelBindings(Archive.Payload(std::size_t(Index)));
                const auto Ref=FixtureReference(Request,Archive,std::size_t(Index));
                Source=Lighting::ForwardGeometryBlockProgram(Bindings,Text(Request,"output"),Ref.UniformBufferHashes);
                Reply["original_geometry_bindings_only"]=Json(true);
            }
            const bool PolicyStage=Command=="fog-policy-stage"||Command=="nearest-capture-stage"||Command=="sky-policy-stage"||Command=="forward-policy-stage"||Command=="hq-policy-stage"||Command=="reflection-response-stage"||Command=="reflection-policy-stage"||Command=="shadow-policy-stage"||Command=="directional-shadow-stage"||Command=="sampled-directional-shadow-stage"||Command=="baked-shadow-stage"||Command=="directional-lighting-stage"||Command=="c4-controls-stage"||Command=="indirect-policy-stage"||Command=="dbuffer-policy-stage"||Command=="final-composition-stage";
            if(PolicyStage) {
                const auto Index=Number(Request,"archive_index",-1);
                if(Index<0||Index>0x7fffffff||std::floor(Index)!=Index)throw std::runtime_error("fog fixture archive index");
                const auto& Archive=CachedFixtureArchive(Request["archive"].Text());
                if(Archive.Frequency(std::size_t(Index))!=3)throw std::runtime_error("fog fixture requires pixel shader");
                const auto Payload=Archive.Payload(std::size_t(Index));Bindings=ReadPixelBindings(Payload);
                const auto Original=NativeBackend::OriginalAssemblyMetadata(Payload,Bindings);
                if(std::regex_search(Original,std::regex("(^|[^A-Za-z0-9_])buf13([^A-Za-z0-9_]|$)|program\\.buffer\\s*\\[\\s*13\\s*\\]")))
                    throw std::runtime_error("diagnostic input buffer13 already referenced by original program");
                if(Command=="fog-policy-stage")Source=Lighting::FogStageProgram(Bindings,{13,true});
                else if(Command=="nearest-capture-stage")Source=Lighting::NearestCaptureStageProgram(Bindings,FixtureUniformHashes(Request,Archive,std::size_t(Index)),{13,true});
                else if(Command=="sky-policy-stage")Source=Lighting::SkyStageProgram(Bindings,{13,true},Flag(Request,"dynamic_output"));
                else if(Command=="hq-policy-stage")Source=Lighting::HQStageProgram(Bindings,FixtureUniformHashes(Request,Archive,std::size_t(Index)),{13,true});
                else if(Command=="reflection-response-stage")Source=Lighting::ReflectionResponseStageProgram(Bindings,{13,true},Flag(Request,"masked")?Lighting::ReflectionResponseKind::OriginalMasked:Lighting::ReflectionResponseKind::OriginalOpaque);
                else if(Command=="reflection-policy-stage"){
                    const auto Ref=FixtureReference(Request,Archive,std::size_t(Index));
                    Source=Lighting::ReflectionPolicyProgram(Bindings,Ref.UniformBufferHashes,Ref.TypeHash,{13,true},Flag(Request,"masked")?Lighting::ReflectionResponseKind::OriginalMasked:Lighting::ReflectionResponseKind::OriginalOpaque);
                }
                else if(Command=="shadow-policy-stage")Source=Lighting::ForwardShadowStageProgram(Bindings,{13,true});
                else if(Command=="directional-shadow-stage")Source=Lighting::DirectionalShadowStageProgram(Bindings,{13,true});
                else if(Command=="sampled-directional-shadow-stage")Source=Lighting::SampleDirectionalShadowStageProgram(Bindings,{13,true});
                else if(Command=="baked-shadow-stage")Source=Lighting::BakedShadowStageProgram(Bindings,FixtureUniformHashes(Request,Archive,std::size_t(Index)),{13,true});
                else if(Command=="directional-lighting-stage")Source=Lighting::DirectionalLightingStageProgram(Bindings,{13,true},Flag(Request,"specular_output"));
                else if(Command=="c4-controls-stage")Source=Lighting::C4ControlsStageProgram(Bindings,{13,true},Text(Request,"output"));
                else if(Command=="final-composition-stage")Source=FixtureFinalComposition(Request,Bindings,FixtureReference(Request,Archive,std::size_t(Index)));
                else if(Command=="dbuffer-policy-stage") {
                    const auto Response=Text(Request,"decal_response");
                    if(Response!="Color"&&Response!="ColorRoughness")throw std::runtime_error("dbuffer fixture source response unavailable");
                    Source=Lighting::DBufferStageProgram(Bindings,FixtureUniformHashes(Request,Archive,std::size_t(Index)),{13,true},
                        Response=="Color"?Lighting::OriginalDecalResponse::Color:Lighting::OriginalDecalResponse::ColorRoughness,Text(Request,"output"));
                }
                else if(Command=="indirect-policy-stage") {
                    const auto Ref=FixtureReference(Request,Archive,std::size_t(Index));
                    const auto Response=Text(Request,"response","original-single");
                    if(Response!="original-single"&&Response!="original-two-sided-surface")throw std::runtime_error("indirect fixture source response unavailable");
                    Source=Lighting::IndirectStageProgram(Bindings,Ref.UniformBufferHashes,Ref.TypeHash,{13,true},Text(Request,"output"),
                        Response=="original-single"?Lighting::IndirectResponse::OriginalSingle:Lighting::IndirectResponse::OriginalTwoSidedSurface);
                    if(Lighting::OriginalIndirectPolicy(Ref.TypeHash).sky)
                        StorageProof.reset(new NativeBackend::StorageReadProof(NativeBackend::ReadSkyIrradianceStorageProof(Payload,Ref.UniformBufferHashes)));
                }
                else Source=Lighting::ForwardClusteredBlockProgram(Bindings,Text(Request,"output"),{13,true},true,Flag(Request,"seed_initial"));
                // Buffer13 contains explicit own WORLD-position/fog inputs for
                // this standalone proof. It is never a production source bind.
                Bindings.uniform_buffers.resize(std::max<std::size_t>(14,Bindings.uniform_buffers.size()));
                Reply["diagnostic_input_uniform_buffer"]=Json(13);
                Reply["diagnostic_input_slot_originally_absent"]=Json(true);
            }
            if(Command=="storage-read-fixture") {
                const auto Index=Number(Request,"archive_index",-1);
                if(Index<0||Index>0x7fffffff||std::floor(Index)!=Index)throw std::runtime_error("storage fixture archive index");
                const auto& Archive=CachedFixtureArchive(Request["archive"].Text());
                if(Archive.Frequency(std::size_t(Index))!=3)throw std::runtime_error("storage fixture requires pixel shader");
                const auto OriginalPayload=Archive.Payload(std::size_t(Index));
                Bindings=ReadPixelBindings(OriginalPayload);
                auto UniformHashes=FixtureUniformHashes(Request,Archive,std::size_t(Index));
                if(Text(Request,"storage_mutation")=="uniform-name")UniformHashes[0]^=1;
                StorageProof.reset(new NativeBackend::StorageReadProof(NativeBackend::ReadSkyIrradianceStorageProof(OriginalPayload,UniformHashes)));
                if(Text(Request,"storage_mutation")=="layout")Bindings.layout_hashes[0]^=1;
                Reply["storage_read_slot"]=Json(int(StorageProof->Slot()));
            }
            auto Program=StorageProof?NativeBackend::CompilePixelAssembly(Source,Bindings,*StorageProof):NativeBackend::CompilePixelAssembly(Source,Bindings);
            Reply["ok"]=Json(true); Reply["code"]=Json(Hex(Program.code)); Reply["control"]=Json(Hex(Program.control));
            Reply["payload"]=Json(Hex(Program.payload)); Reply["code_sha256"]=Json(Program.code_sha256);
            Reply["control_sha256"]=Json(Program.control_sha256); Reply["payload_sha1"]=Json(Program.payload_sha1);
            if(PolicyStage||Command=="geometry-policy-stage"||Command=="opaque-policy-stage"||Command=="opaque-policy-program"||Command=="lighting-block"||Command=="graph-depth")Reply["assembly"]=Json(Source);
            Reply["program_size"]=Json(int(Program.program_size)); Reply["registers"]=Json(int(Program.registers)); Reply["scratch"]=Json(int(Program.scratch));
        } catch(const std::exception& Error) { Reply["ok"]=Json(false); Reply["error"]=Json(Error.what()); }
        std::cout << Reply.Dump() << '\n';
    }
}
