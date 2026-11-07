// Native creator Surface/DefaultLit translucency compiler candidate.
// Actual editor/cook/runtime/game acceptance remains a separate release gate.
#pragma once
#include "mod_folders.h"
#include "native_custom_backend_translucent.h"
#include "material_engine_hashes.h"
#include "custom_translucent_lighting.h"
namespace P4MapKit { namespace CustomMaterial { namespace NativeBackend {
inline std::string TranslucentLitAuthoringIssue(const Json& graph,
 const std::map<std::string,std::string>& textureParams={},const std::vector<TextureParameter>& textures={}){
 try{
  if(Number(graph,"format",1)!=1&&Number(graph,"format",1)!=2)throw std::runtime_error("translucent lit graph version unavailable");
  for(const auto& setting:std::map<std::string,std::string>{{"domain","MD_Surface"},{"blend_mode","BLEND_Translucent"},{"shading_model","MSM_DefaultLit"}})
   if(graph.Has(setting.first)&&Text(graph,setting.first)!=setting.second)throw std::runtime_error("The lit translucent material has an unsupported "+setting.first+" setting.");
  if(Flag(graph,"two_sided")||Get(graph,"clip").kind!=Json::Null)throw std::runtime_error("translucent lit requires one-sided Surface without an opacity mask");
  const auto& render=Get(graph,"render_settings");if(render.Has("TranslucencyLightingMode")&&Text(render,"TranslucencyLightingMode")!="TLM_Surface")throw std::runtime_error("translucent lit lighting mode unavailable");
  for(const auto& k:std::vector<std::string>{"bEnableSeparateTranslucency","bUseTranslucencyVertexFog","bEnableMobileSeparateTranslucency","bApplyCloudFogging","bEnableResponsiveAA","bScreenSpaceReflections","bContactShadows","bDisableDepthTest","bOutputTranslucentVelocity","bAllowNegativeEmissiveColor","bFullyRough"})
   if(render.Has(k)){const auto& value=Get(render,k);if(value.kind!=Json::Boolean)throw std::runtime_error("The lit translucent material's "+k+" value is invalid.");if(value.boolean)throw std::runtime_error("Disable "+k+" for this lit translucent material.");}
  const std::map<std::string,double> numeric={{"RefractionDepthBias",0},{"TranslucencyDirectionalLightingIntensity",1},{"TranslucentShadowDensityScale",.5},{"TranslucentSelfShadowDensityScale",2},{"TranslucentSelfShadowSecondDensityScale",10},{"TranslucentSelfShadowSecondOpacity",0},{"TranslucentBackscatteringExponent",30},{"TranslucentShadowStartOffset",100}};
  const auto finiteNumber=[](const Json& value){if(value.kind!=Json::Number||!std::isfinite(value.number)||!std::isfinite(float(value.number)))throw std::runtime_error("translucent lit renderer numeric value malformed");return value.number;};
  for(const auto& setting:numeric)if(render.Has(setting.first)&&MaterialFloatBits(float(finiteNumber(Get(render,setting.first))))!=MaterialFloatBits(float(setting.second)))throw std::runtime_error("Keep the default "+setting.first+" value for this lit translucent material.");
  const auto& extinction=Get(render,"TranslucentMultipleScatteringExtinction");if(extinction.kind!=Json::Null){std::vector<double> actual;
   if(extinction.kind==Json::Object){for(const auto* c:{"R","G","B","A"})actual.push_back(finiteNumber(Get(extinction,c)));if(extinction.object.size()!=4)throw std::runtime_error("translucent lit extinction color malformed");}
   else if(extinction.kind==Json::Array){for(const auto& c:extinction.array)actual.push_back(finiteNumber(c));}else throw std::runtime_error("translucent lit extinction color malformed");const std::vector<double> expected={1,.833,.588,1};if(actual.size()!=4)throw std::runtime_error("translucent lit extinction color malformed");
   for(unsigned c=0;c<4;++c)if(MaterialFloatBits(float(actual[c]))!=MaterialFloatBits(float(expected[c])))throw std::runtime_error("Keep the default TranslucentMultipleScatteringExtinction value for this lit translucent material.");}

  const std::set<std::string> outputs={"BaseColor","Metallic","Specular","Roughness","AmbientOcclusion","EmissiveColor","Opacity","Normal"};
  for(const auto& pin:Get(graph,"outputs").object)if(pin.second.kind!=Json::Null&&!outputs.count(pin.first))throw std::runtime_error("The lit translucent compiler cannot export the connected "+pin.first+" input yet.");
  for(const auto& node:Get(graph,"nodes").object){const auto kind=Text(node.second,"type");const auto& p=Get(node.second,"props");
   if(kind=="CollectionParameter")throw std::runtime_error("translucent lit collection bindings unavailable");
   if(kind=="TextureSample"||kind=="TextureSampleParameter2D"||kind=="TextureObjectParameter"){
    if(Text(p,"SamplerSource","SSM_FromTextureAsset")!="SSM_FromTextureAsset"||Text(p,"MipValueMode","TMVM_None")!="TMVM_None")throw std::runtime_error("translucent lit texture sampler/mip mode unavailable");}}
  const auto own=GraphUniforms(InlineFunctions(graph,Json()),textures);Emitter e;e.owned_uniforms=true;e.material_parameters=own.bindings;e.material_buffer=5;
  const auto inputs=CompileTranslucentLitInputsWithEmitter(graph,std::move(e),textureParams);
  for(const auto& used:inputs.emitter.cache)if(used.first.compare(0,7,"attrib:")==0&&used.first!="attrib:in_texcoord0"&&used.first!="attrib:in_texcoord11"&&used.first!="attrib:in_texcoord10")throw std::runtime_error("The lit translucent compiler cannot provide the "+used.first+" input yet.");
  return {};
 }catch(const std::exception& e){return e.what();}
}
inline bool TranslucentLitNeedsTangentFrame(const Json& graph,const std::map<std::string,std::string>& textureParams,const std::vector<TextureParameter>& textures){
 const auto own=GraphUniforms(InlineFunctions(graph,Json()),textures);Emitter e;e.owned_uniforms=true;e.material_parameters=own.bindings;e.material_buffer=5;
 const auto inputs=CompileTranslucentLitInputsWithEmitter(graph,std::move(e),textureParams);return inputs.emitter.cache.count("attrib:in_texcoord10")!=0;
}
inline void AddTranslucentSurfaceTangentInput(ShaderBindings& header){
 for(const auto& in:header.inputs)if(in.location==0||in.name.find("in_texcoord10")==0)throw std::runtime_error("translucent lit tangent input conflicts with original pixel inputs");
 header.inputs.push_back({"in_texcoord10_centroid",0});
}
inline void RequireTranslucentSurfaceVertex(const FMaterialShaderReference& ref,const Bytes& payload){
 if(ref.Frequency!=0||ref.VertexFactoryHash!=EngineHash::LocalVertexFactory)throw std::runtime_error("translucent lit retained vertex factory unavailable");
 const std::set<std::uint64_t> types={0xe99e2d4408e39e36ull,0xe6b842b7733dcb47ull,0xba53fb0a20785ffcull,0x65b5d8cde5c1f164ull,0x1a4855b2c0466f87ull};
 if(!types.count(ref.TypeHash))throw std::runtime_error("translucent lit retained vertex type unavailable");
 for(const auto& role:ref.NamedBuffers)if((role.second=="Material"||role.second.compare(0,18,"MaterialCollection")==0))throw std::runtime_error("translucent lit vertex consumes replaced Material uniforms");
 const std::map<std::uint16_t,std::uint64_t> hashes={{0,EngineHash::ViewUniformBuffer},{1,0x60929d69929b2b59ull},{2,0x426d1240410b8fa2ull}};
 if(ref.UniformBufferHashes!=hashes||payload.size()<6||payload[4]!=0x53||payload[5]!=0x56)throw std::runtime_error("translucent lit retained vertex uniform identities unavailable");
 auto structural=payload;structural[5]=0x50;const auto h=ReadPixelBindings(structural);
 if(h.layout_hashes!=std::vector<std::uint32_t>{EngineHash::ViewLayout,0,0x064606e7}||!h.global_arrays.empty())throw std::runtime_error("translucent lit retained vertex layout unavailable");
 for(const auto& b:h.uniform_buffers)if(!b.empty())throw std::runtime_error("translucent lit retained packed vertex layout unavailable");
 const std::map<std::string,int> required={{"out_texcoord10_centroid",0},{"out_texcoord11_centroid",1},{"out_texcoord0",2}};std::map<std::string,int> actual;
 for(const auto& out:h.outputs)if(!actual.emplace(out.name,out.location).second)throw std::runtime_error("translucent lit retained vertex output ambiguous");
 if(actual!=required||h.outputs.size()!=3)throw std::runtime_error("translucent lit retained tangent/normal/UV outputs unavailable");
 const auto source=OriginalAssemblyMetadata(structural,h);std::smatch hand;
 if(!std::regex_search(source,hand,std::regex("MUL\\.F32 result\\.attrib\\[1\\]\\.w, (R[0-9]+), (R[0-9]+);")))throw std::runtime_error("translucent lit vertex tangent handedness output unavailable");
 const std::string tangent=hand[1].str(),primitive=hand[2].str();std::map<std::string,std::vector<std::string>> writes;std::istringstream lines(source);std::string line;
 const std::regex destination("^([A-Z][A-Z0-9]*\\.[A-Z0-9.]+) (R[0-9]+)(?:\\.([xyzw]+))?,");
 while(std::getline(lines,line)){std::smatch match;if(!std::regex_search(line,match,destination))continue;const auto mask=match[3].str();if((match[2].str()==tangent||match[2].str()==primitive)&&(mask.empty()||mask.find('w')!=std::string::npos))writes[match[2].str()].push_back(line);}
 if(writes[primitive]!=std::vector<std::string>{"LDC.F32X4 "+primitive+", buf1[64];"}||writes[tangent].size()!=1||writes[tangent][0].find("TXF.F "+tangent+", ")!=0||writes[tangent][0].find(", BUFFER;")==std::string::npos)throw std::runtime_error("translucent lit vertex tangent handedness producer unavailable");
 const auto vf=ReadResourceTable(h.srv);if(vf.at(2)!=std::vector<std::pair<unsigned,unsigned>>{{0,1},{3,0}})throw std::runtime_error("translucent lit vertex tangent/UV source resources unavailable");

}
inline TranslucentCompilation CompileTranslucentLitCandidate(const Json& graph,const std::string& material,
 const std::string& gameMaterial,const TranslucentSource& selected,const TranslucentSourceResolver& resolver,
 const ShaderArchive& archive,const std::map<std::string,std::string>& textureParams={},const std::vector<TextureParameter>& textures={}){
 const auto issue=TranslucentLitAuthoringIssue(graph,textureParams,textures);if(!issue.empty())throw std::runtime_error(issue);
 if(!AssetPath(material,true)||!P4MapKit::Export::ModFolderPackage(material))throw std::runtime_error("translucent lit creator path invalid");
 const auto chain=TranslucentParentChain(gameMaterial,selected,resolver);FMaterialCookedSettings settings;FReadError error;
 if(!ReadMaterialCookedSettings(chain,settings,error))throw std::runtime_error(error.Code);
 if(settings.Domain!="MD_Surface"||settings.BlendMode!="BLEND_Translucent"||settings.ShadingModel!="MSM_DefaultLit"||settings.TwoSided)throw std::runtime_error("translucent lit donor settings mismatch");
 const auto& master=chain.front().Properties;const auto mode=master.Field("TranslucencyLightingMode");std::string modeName;
 if(!mode||!MaterialEnum(*mode,modeName)||modeName!="TLM_Surface")throw std::runtime_error("translucent lit donor Surface lighting mode absent");
 for(const auto* flag:{"bEnableSeparateTranslucency","bUseTranslucencyVertexFog"}){const auto value=master.Field(flag);if(!value||value->Kind!=FDecodedValue::EKind::Boolean||value->Text!="false")throw std::runtime_error("translucent lit donor renderer setting mismatch");}
 const auto package=ReadPackageHeader(selected.header,selected.body.size());const auto leaf=gameMaterial.substr(gameMaterial.find_last_of('/')+1);std::size_t root=~std::size_t(0);
 for(std::size_t i=0;i<package.Exports.size();++i)if(package.Exports[i].Name==leaf){if(root!=~std::size_t(0))throw std::runtime_error("translucent lit source root ambiguous");root=i;}
 FMaterialResourceBoundary boundary;if(!DiscoverMaterialResourceBoundary(package,selected.body,root,boundary,error)||boundary.Class!="MaterialInstanceConstant")throw std::runtime_error("The original lit translucent material resource could not be read. "+error.Code);
 FMaterialShaderMapEnvelope map;if(!ReadMaterialShaderMapEnvelope(selected.body,boundary.ResourceStart,boundary.ExportEnd,map,error))throw std::runtime_error(error.Code);
 std::vector<FMaterialShaderReference> refs;if(!ReadMaterialShaderReferences(map,refs,error))throw std::runtime_error(error.Code);
 const auto indices=archive.Map(Bytes(map.ResourceHash.begin(),map.ResourceHash.end()));std::map<unsigned,FMaterialShaderReference> pixels;bool vertex=false;
 for(const auto& ref:refs){if(ref.ResourceIndex<0||std::size_t(ref.ResourceIndex)>=indices.size()||archive.Frequency(indices[ref.ResourceIndex])!=ref.Frequency)throw std::runtime_error("translucent lit archive association mismatch");
  if(ref.VertexFactoryHash!=EngineHash::LocalVertexFactory)continue;
  if(ref.Frequency==0){RequireTranslucentSurfaceVertex(ref,archive.Payload(indices[ref.ResourceIndex]));vertex=true;continue;}
  if(ref.Frequency!=3)throw std::runtime_error("translucent lit stage unavailable");
  const auto pass=Lighting::TranslucentSurfacePass(ref.TypeHash);auto prior=pixels.find(unsigned(ref.ResourceIndex));
  if(prior!=pixels.end()){const auto other=Lighting::TranslucentSurfacePass(prior->second.TypeHash);if(pass.sky!=other.sky||pass.cached!=other.cached||pass.volume!=other.volume||prior->second.NamedBuffers!=ref.NamedBuffers||prior->second.UniformBufferHashes!=ref.UniformBufferHashes)throw std::runtime_error("translucent lit pixel aliases disagree");}
  pixels.emplace(unsigned(ref.ResourceIndex),ref);}
 if(pixels.empty()||!vertex)throw std::runtime_error("translucent lit local pixel/vertex program absent");
 TranslucentCompilation result;result.compiled.source_references=refs; result.uniforms=GraphUniforms(InlineFunctions(graph,Json()),textures);const auto frozen=ReplaceOwnedUniforms(map,result.uniforms,true);
 auto& output=result.compiled.material;output.material=material;output.game_material=gameMaterial;output.map_id_hash=Bytes(map.CookedIdHash.begin(),map.CookedIdHash.end());output.map_shaders=unsigned(indices.size());output.edits=frozen.edits;
 const bool tangentFrame=TranslucentLitNeedsTangentFrame(graph,textureParams,textures);
 for(const auto& item:pixels){const auto payload=archive.Payload(indices[item.first]);auto h=ReadPixelBindings(payload);Lighting::ReadTranslucentSurfaceBindings(h);
  unsigned materialBuffer=32;for(const auto& role:item.second.NamedBuffers)if(role.second=="Material"){if(materialBuffer!=32)throw std::runtime_error("translucent lit Material binding ambiguous");materialBuffer=role.first;}
  if((materialBuffer!=5&&materialBuffer!=6)||h.layout_hashes.at(materialBuffer)!=Read32(map.Frozen,472))throw std::runtime_error("translucent lit Material frozen/header layout mismatch");
  if(tangentFrame)AddTranslucentSurfaceTangentInput(h);
  auto rebound=RebindMaterialTextures(h,materialBuffer,unsigned(textures.size()),frozen.layout_hash,false,item.second.LooseTextures,item.second.LooseSamplers);rebound.bindings.layout_hashes[materialBuffer]=frozen.layout_hash;
  auto bindings=ResolveMaterialBindings(rebound.bindings,materialBuffer,{});for(std::size_t i=0;i<textures.size();++i)bindings.textures[textures[i].name]={int(rebound.slots[i].first),int(rebound.slots[i].second)};
  auto assembly=Lighting::TranslucentSurfaceProgram(graph,result.uniforms,h,item.second,materialBuffer,textureParams,bindings.textures);
  const auto pass=Lighting::TranslucentSurfacePass(item.second.TypeHash);
  auto compiled=pass.sky?CompilePixelAssembly(assembly,rebound.bindings,ReadTranslucentSkyIrradianceStorageProof(payload,item.second.UniformBufferHashes,materialBuffer,tangentFrame)):CompilePixelAssembly(assembly,rebound.bindings);
  output.shaders.push_back({item.first,compiled.payload});result.compiled.programs.push_back(std::move(compiled));}
 (void)WriteShaderSet3({output});return result;
}
}}}
