// Portable checks of the selected original policy and creator-owned input integration.
// Actual archive/inventory inputs stay private. This test does not prove a renderer.
#include "native_custom_backend_opaque.h"
#include "custom_material_factory_inventory.h"
#include <fstream>
#include <filesystem>
#include <iostream>
using namespace P4MapKit::CustomMaterial;
using namespace P4MapKit::CustomMaterial::Lighting;
static Bytes Read(const char* path) { std::ifstream f(path,std::ios::binary|std::ios::ate); if(!f)throw std::runtime_error("missing input"); const auto size=f.tellg(); if(size<0 || size>std::streamoff(1ull<<31))throw std::runtime_error("input size bound"); Bytes out(std::size_t(size),0); f.seekg(0); if(!f.read(reinterpret_cast<char*>(out.data()),size))throw std::runtime_error("input read failed"); return out; }
static std::uint64_t Hash(const std::string& value) { const auto bytes=Unhex(value); if(bytes.size()!=8)throw std::runtime_error("hash shape"); std::uint64_t out=0; for(auto b:bytes)out=(out<<8)|b; return out; }
static void Require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
static Json Graph() { return Json::Parse(R"({"domain":"MD_Surface","blend_mode":"BLEND_Opaque","shading_model":"MSM_DefaultLit","render_settings":{"MaterialDecalResponse":"MDR_ColorRoughness"},"nodes":{"Tint":{"type":"VectorParameter","props":{"ParameterName":"Tint","DefaultValue":[0.1,0.5,0.9,1]}},"Metal":{"type":"ScalarParameter","props":{"ParameterName":"Metal","DefaultValue":0.7}},"Rough":{"type":"ScalarParameter","props":{"ParameterName":"Rough","DefaultValue":0.3}},"Spec":{"type":"ScalarParameter","props":{"ParameterName":"Spec","DefaultValue":0.4}},"AO":{"type":"ScalarParameter","props":{"ParameterName":"AO","DefaultValue":0.8}}},"outputs":{"BaseColor":["Tint","",0],"Metallic":["Metal","",0],"Roughness":["Rough","",0],"Specular":["Spec","",0],"AmbientOcclusion":["AO","",0],"EmissiveColor":["Tint","",0]}})"); }
int main(int argc,char** argv) { try {
 Require(argc==4,"archive, typed inventory and original source root required");
 auto graph=Graph(); if(!NativeBackend::OpaqueAuthoringIssue(graph).empty())throw std::runtime_error(NativeBackend::OpaqueAuthoringIssue(graph));
 auto bad=graph;bad["render_settings"]["MaterialDecalResponse"]="MDR_ColorNormalRoughness"; Require(!NativeBackend::OpaqueAuthoringIssue(bad).empty(),"unsupported authored decal response accepted");
 bad=graph;bad["render_settings"]=Json::MakeObject();Require(!NativeBackend::OpaqueAuthoringIssue(bad).empty(),"missing authored decal response defaulted");
 bad=graph;bad["render_settings"]["MaterialDecalResponse"]="MDR_Color";Require(NativeBackend::OpaqueAuthoringIssue(bad).empty(),"supported authored color response refused");
 bad=graph; bad["two_sided"]=true; Require(!NativeBackend::OpaqueAuthoringIssue(bad).empty(),"two-sided accepted");
 bad=graph; bad["outputs"]["WorldPositionOffset"]=Json::Parse("[\"Tint\"]"); Require(!NativeBackend::OpaqueAuthoringIssue(bad).empty(),"vertex output accepted");
 bad=graph; bad["shading_model"]="MSM_Unlit"; Require(!NativeBackend::OpaqueAuthoringIssue(bad).empty(),"unlit uses BaseColor lighting");
 bad["outputs"]=Json::MakeObject(); bad["outputs"]["EmissiveColor"]=graph.At("outputs").At("EmissiveColor"); Require(NativeBackend::OpaqueAuthoringIssue(bad).empty(),"emission-only unlit refused");
 ShaderBindings structural; structural.layout_hashes={0x166705f7u,0,0x0d480502u}; structural.resource_bits=5;
 unsigned refusals=0;
 for(int invalid:{-1,2,127}) {
  for(bool directional:{false,true}) {
   try { BuildOpaqueMaterialStages(graph,structural,{},0,directional?OriginalDecalResponse::Color:static_cast<OriginalDecalResponse>(invalid),{},Json(),directional?static_cast<OpaqueDirectionalResponse>(invalid):OpaqueDirectionalResponse::SurfaceShadow); throw std::runtime_error("invalid policy enum accepted"); }
   catch(const std::runtime_error& e) { Require(std::string(e.what())==(directional?"opaque directional response unavailable":"opaque decal response unavailable"),"wrong policy refusal"); ++refusals; }
  }
 }
 const std::filesystem::path sourceRoot=argv[3];
 const NativeBackend::TranslucentSourceResolver source=[&](const std::string& package,NativeBackend::TranslucentSource& out){
  if(package.compare(0,6,"/Game/")!=0)return false;
  const auto base=sourceRoot/"Carrot4/Content"/package.substr(6);
  out.header=Read((base.string()+".uasset").c_str());out.body=Read((base.string()+".uexp").c_str());return true;
 };
 unsigned retained=0,donorMaps=0,rejectedRetained=0;
 std::vector<P4MapKit::FMaterialShaderMapEnvelope> donorSourceMaps;
 for(const auto* package:{"/Game/Carrot4/Objects/Gimmicks/TossinObjects/BookEnd/MI_stg_Bookend_SI01","/Game/Carrot4/Terrain/Area/Area001/Kakiwari/stg_Area001_BayWindow/MI_stg_Area001_BayWindow01"}) {
  NativeBackend::TranslucentSource raw; Require(source(package,raw),"original source missing");
  const auto chain=NativeBackend::TranslucentParentChain(package,raw,source);
  P4MapKit::FMaterialCookedSettings settings; P4MapKit::FReadError error;
  Require(P4MapKit::ReadMaterialCookedSettings(chain,settings,error),"actual settings unreadable");
  Require(settings.Domain=="MD_Surface" && settings.BlendMode=="BLEND_Opaque" && settings.ShadingModel=="MSM_DefaultLit" && !settings.TwoSided,"actual source settings mismatch");
  const auto expected=std::string(package).find("Bookend")!=std::string::npos ? OriginalDecalResponse::Color : OriginalDecalResponse::ColorRoughness;
  Require(NativeBackend::ReadOpaqueDecalResponse(chain)==expected,"actual serialized decal response changed");
  const auto header=P4MapKit::ReadPackageHeader(raw.header,raw.body.size());
  std::size_t owner=header.Exports.size(); const auto leaf=std::string(package).substr(std::string(package).find_last_of('/')+1);
  for(std::size_t i=0;i<header.Exports.size();++i)if(header.Exports[i].Name==leaf) {Require(owner==header.Exports.size(),"ambiguous donor export");owner=i;}
  P4MapKit::FMaterialResourceBoundary boundary;P4MapKit::FMaterialShaderMapEnvelope map;std::vector<P4MapKit::FMaterialShaderReference> refs;
  Require(P4MapKit::DiscoverMaterialResourceBoundary(header,raw.body,owner,boundary,error),"actual donor boundary unreadable");
  Require(P4MapKit::ReadMaterialShaderMapEnvelope(raw.body,boundary.ResourceStart,boundary.ExportEnd,map,error) && P4MapKit::ReadMaterialShaderReferences(map,refs,error),"actual donor shader refs unreadable");
  for(const auto& ref:refs) {
   if(NativeBackend::IsReplacedOpaquePixel(ref))continue;
   bool consumes=false;for(const auto& role:ref.NamedBuffers)if(role.second=="Material")consumes=true;
   if(consumes) {
    Require(std::string(package).find("Bookend")!=std::string::npos,"selected BayWindow donor retains a Material consumer");
    try { NativeBackend::RequireOpaqueRetainedUniforms(ref);throw std::runtime_error("actual retained Material consumer accepted"); }
    catch(const std::runtime_error& e) { Require(std::string(e.what())=="An original retained shader consumes the replaced Material uniform buffer.","wrong actual retained refusal");++rejectedRetained; }
   }
   else {NativeBackend::RequireOpaqueRetainedUniforms(ref);++retained;}
  }
  if(std::string(package).find("BayWindow")!=std::string::npos)donorSourceMaps.push_back(map);
  ++donorMaps;
  auto omitted=chain; auto& fields=omitted.front().Properties.Fields; fields.erase(std::remove_if(fields.begin(),fields.end(),[](const std::pair<std::string,P4MapKit::FDecodedValue>& field){return field.first=="MaterialDecalResponse";}),fields.end());
  try { (void)NativeBackend::ReadOpaqueDecalResponse(omitted);throw std::runtime_error("omitted response accepted"); }
  catch(const std::runtime_error& e) { Require(std::string(e.what())=="The original opaque master must serialize a supported decal response.","wrong missing-source response"); }
 }
 P4MapKit::FMaterialShaderReference retainedRef;retainedRef.Frequency=0;retainedRef.NamedBuffers[0]="View";
 NativeBackend::RequireOpaqueRetainedUniforms(retainedRef);retainedRef.NamedBuffers[5]="Material";
 try { NativeBackend::RequireOpaqueRetainedUniforms(retainedRef);throw std::runtime_error("retained material consumer accepted"); }
 catch(const std::runtime_error& e) { Require(std::string(e.what())=="An original retained shader consumes the replaced Material uniform buffer.","wrong retained refusal");++retained; }
 const auto input=Read(argv[2]); const auto inventory=Json::Parse(std::string(input.begin(),input.end())); const auto archiveBytes=Read(argv[1]); const NativeBackend::ShaderArchive archive(archiveBytes);
 unsigned factoryCases=0;
 for(const auto& map:donorSourceMaps) {
  std::vector<P4MapKit::FMaterialShaderReference> refs;P4MapKit::FReadError error;Require(P4MapKit::ReadMaterialShaderReferences(map,refs,error),"factory source refs unreadable");
  const auto indices=archive.Map(Bytes(map.ResourceHash.begin(),map.ResourceHash.end()));
  for(const auto& ref:refs)if(ref.Frequency==0) {
   auto vertex=archive.Payload(indices[ref.ResourceIndex]);NativeBackend::RequireOpaqueRetainedVertex(ref,vertex);
   vertex[5]=0x50;try {NativeBackend::RequireOpaqueRetainedVertex(ref,vertex);throw std::runtime_error("wrong retained vertex stage accepted");}
   catch(const std::runtime_error& e) {Require(std::string(e.what())=="The original retained vertex payload has another shader stage.","wrong vertex-stage refusal");}
  }
  SetMaterial material;material.map_shaders=unsigned(indices.size());std::set<unsigned> seen;
  // Private actual payloads exercise inventory/header joins only. They are
  // never written as a creator artifact or a shader archive by this test.
  for(const auto& ref:refs)if(NativeBackend::IsReplacedOpaquePixel(ref) && seen.insert(unsigned(ref.ResourceIndex)).second)
   material.shaders.push_back({unsigned(ref.ResourceIndex),archive.Payload(indices[ref.ResourceIndex])});
  for(auto factory:{0x9f41d10d68bb52d8ull,0x7e6253dcc60d6ebbull}) {Require(CreatorFactoryStageIssue(material,refs,factory).empty(),"actual complete static factory refused");++factoryCases;}
  for(auto factory:{0x391c2b78c893a52aull,0xdd651790dd308138ull,0x872b1640e244ca06ull,0x1adefad780cd16c5ull,0x6d728306d56d412full,0x00080c76633abf12ull,0x2f7fb09897dda57cull}) {Require(!CreatorFactoryStageIssue(material,refs,factory).empty(),"unsupported factory nominally admitted");++factoryCases;}
  auto missing=refs;missing.erase(std::remove_if(missing.begin(),missing.end(),[](const P4MapKit::FMaterialShaderReference& ref){return ref.Frequency==0 && ref.VertexFactoryHash==0x9f41d10d68bb52d8ull;}),missing.end());
  Require(!CreatorFactoryStageIssue(material,missing,0x9f41d10d68bb52d8ull).empty(),"missing retained VS admitted");++factoryCases;
  auto changed=material;changed.shaders.clear();Require(!CreatorFactoryStageIssue(changed,refs,0x9f41d10d68bb52d8ull).empty(),"donor refs alone admit empty compile");++factoryCases;
  changed=material;changed.shaders[0].payload[5]=0x56;Require(!CreatorFactoryStageIssue(changed,refs,0x9f41d10d68bb52d8ull).empty(),"VS payload admitted as PS");++factoryCases;
  changed=material;changed.shaders.push_back(changed.shaders[0]);Require(!CreatorFactoryStageIssue(changed,refs,0x9f41d10d68bb52d8ull).empty(),"duplicate replacement admitted");++factoryCases;
  missing=refs;const auto place=material.shaders[0].place;missing.erase(std::remove_if(missing.begin(),missing.end(),[&](const P4MapKit::FMaterialShaderReference& ref){return ref.ResourceIndex==std::int32_t(place);}),missing.end());
  Require(!CreatorFactoryStageIssue(material,missing,0x9f41d10d68bb52d8ull).empty(),"unjoined replacement admitted");++factoryCases;
  changed=material;changed.map_shaders=1;Require(!CreatorFactoryStageIssue(changed,refs,0x9f41d10d68bb52d8ull).empty(),"stale map range admitted");++factoryCases;
 }
 Require(factoryCases>=15,"actual factory joins not exercised");
 const auto uniforms=GraphUniforms(graph,{}); unsigned programs=0,negative=0;
 for(const auto& row:inventory.At("policies").array) {
  if(Text(row,"template")=="masked")continue;
  const auto index=unsigned(row.At("archive_index").number); Require(archive.Frequency(index)==3,"inventory stage mismatch");
  const auto original=ReadPixelBindings(archive.Payload(index)); std::map<std::uint16_t,std::uint64_t> hashes;
  const auto& slots=row.At("named_uniform_buffer_slots").array; const auto& names=row.At("named_uniform_buffer_hashes").array;
  Require(slots.size()==names.size(),"uniform metadata shape"); for(std::size_t i=0;i<slots.size();++i)hashes[std::uint16_t(slots[i].number)]=Hash(names[i].Text());
  unsigned buffer=32; for(const auto& binding:row.At("named_uniform_buffers").array)if(binding.array.at(1).Text()=="Material") { Require(buffer==32,"ambiguous Material slot");buffer=unsigned(binding.array.at(0).number); }
  Require(buffer<32,"Material slot absent");
  const auto layout=UniformLayout(unsigned(uniforms.vectors.size()),unsigned(uniforms.scalars.size()),0);
  auto rebound=RebindMaterialTextures(original,buffer,0,layout.first,false,{},{}); rebound.bindings.layout_hashes.at(buffer)=layout.first;
  const auto response=Text(row,"template")=="opaque3"?OriginalDecalResponse::Color:OriginalDecalResponse::ColorRoughness;
  for(const auto& type:row.At("types").array) {
   const auto hash=Hash(Text(type,"hash"));
   Emitter seed; seed.owned_uniforms=true;seed.material_buffer=buffer;seed.material_parameters=uniforms.bindings;
   const auto program=OpaqueMaterialProgram(graph,rebound.bindings,hashes,hash,response,{},{},Json(),true,OpaqueDirectionalResponse::SurfaceShadow,seed);
   Require(program.find("{VAR:")==std::string::npos && program.find("{TEX:")==std::string::npos,"unresolved resource");
   Require(program.find("REP.S;")!=std::string::npos && program.find("CBUFFER buf2[]")!=std::string::npos,"local/policy lighting absent");
   Require(program.find("buf"+std::to_string(buffer)+"[16]")!=std::string::npos,"owned AO uniform missing");
   Require(program.find("buf"+std::to_string(buffer)+"[28]")!=std::string::npos,"owned specular uniform missing");
   ++programs;
   auto missing=rebound.bindings; missing.inputs.erase(std::remove_if(missing.inputs.begin(),missing.inputs.end(),[](const Varying& v){return v.name.find("in_texcoord7")==0;}),missing.inputs.end());
   try { (void)OpaqueMaterialProgram(graph,missing,hashes,hash,response,{},{},Json(),true,OpaqueDirectionalResponse::SurfaceShadow,seed); throw std::runtime_error("missing original fog varying accepted"); }
   catch(const std::runtime_error& e) { Require(std::string(e.what())=="forward geometry varying absent: in_texcoord7","missing fog failed elsewhere");++negative; }
  }
 }
 Require(programs>=8 && negative==programs && rejectedRetained>0,"insufficient actual policies");
 std::cout<<programs<<" actual-policy owned PBR programs, "<<negative<<" missing-fog refusals, "<<refusals<<" invalid enum refusals; "<<donorMaps<<" actual donor maps / "<<retained<<" safe retained-stage checks, "<<rejectedRetained<<" actual retained-Material refusals, "<<factoryCases<<" intrinsic factory cases; authoring cases PASS\n";
 return 0;
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
