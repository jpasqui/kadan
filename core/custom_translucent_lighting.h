// First-party Surface translucency lighting. No original shader instructions
// are emitted. The selected pass's native resources remain explicit inputs.
#pragma once
#include "custom_material_indirect.h"
#include "material_engine_hashes.h"
#include "custom_material_c4_controls.h"
#include "custom_material_reflection_policy.h"
#include "custom_material_directional_shadow.h"
#include "material_forward_lighting.h"
#include "custom_material_baked_shadow.h"
namespace P4MapKit { namespace CustomMaterial { namespace Lighting {
struct TranslucentSurfacePolicy { bool sky=false,cached=false,volume=false,lightmap=false,distanceField=false; };
inline TranslucentSurfacePolicy TranslucentSurfacePass(std::uint64_t type){
 switch(type){
 case 0x4507f03a199c6526ull:case 0x9134506f7bff0ffaull:return {};
 case 0xa8d53bcb8fca168dull:case 0xba9b0d30dcc48e73ull:return {true,false,false};
 case 0x666403914542b619ull:case 0x787d13a1ace9957dull:return {false,false,true};
 case 0x18a82ebde5d3d122ull:case 0x3c79a2188506dc5bull:return {true,false,true};
 case 0xe38d98e8b45169dbull:return {false,true,false};
 case 0x9f63684dbca4b663ull:return {true,true,false};
 // The lightmap policies (a static mesh with a baked lightmap; OWN_SHADER_MAPS.md stage 6).
 case 0x6d90922c199fe04full:return {false,false,false,true,false};
 case 0xdbbfd1d594df3611ull:return {true,false,false,true,false};
 case 0x28b73d7ae30b556cull:return {false,false,false,true,true};
 case 0x661e2d85a2c9dbe6ull:return {true,false,false,true,true};
 default:throw std::runtime_error("translucent Surface pixel policy unavailable");
 }
}
inline PolicyBindings ReadTranslucentSurfaceBindings(const ShaderBindings& h){
 if(h.layout_hashes.size()<3||h.layout_hashes[0]!=EngineHash::ViewLayout||h.layout_hashes[2]!=0x05ec0deeu||
    (h.resource_bits&5u)!=5u||!h.global_arrays.empty())throw std::runtime_error("translucent Surface View/BasePass layout unavailable");
 for(const auto& b:h.uniform_buffers)if(!b.empty())throw std::runtime_error("translucent Surface packed uniforms unavailable");
 const auto t=ReadResourceTable(h.textures),s=ReadResourceTable(h.samplers),r=ReadResourceTable(h.srv);PolicyBindings out;
 PolicyResourceNames(t,0,{{2,"VolumeIndirection"},{3,"VolumeAmbient"},{4,"VolumeSH0"},{5,"VolumeSH1"},{6,"VolumeSH2"},{7,"VolumeSH3"},{8,"VolumeSH4"},{9,"VolumeSH5"},{10,"VolumeBentNormal"},{11,"VolumeDirectionalShadow"}},63,out.textures);
 PolicyResourceNames(t,2,{{0,"DirectionalShadowMap"},{16,"SkyCubemap"},{18,"SkyBlendCubemap"},{20,"ReflectionCubemap"},{28,"IntegratedFog"}},63,out.textures);
 PolicyResourceNames(s,0,{{43,"SharedBilinearClamped"}},31,out.samplers);
 PolicyResourceNames(s,2,{{1,"DirectionalShadowMap"},{17,"SkyCubemap"},{19,"SkyBlendCubemap"},{21,"ReflectionCubemap"}},31,out.samplers);
 PolicyResourceNames(r,0,{{51,"SkyIrradiance"}},63,out.srvs);
 PolicyResourceNames(r,2,{{4,"LocalLights"},{5,"CulledLightCounts"},{6,"CulledLightIndices"}},63,out.srvs);
 return out;
}
inline Value SurfaceShadowTap(PolicyEmitter& p,Value uv){
 auto& e=p.emitter;const auto t=p.bindings.RequireTexture("DirectionalShadowMap"),s=p.bindings.RequireSampler("DirectionalShadowMap");
 e.uses_texture=true;e.handle.clear();e.emit("LDC.U64 D0.x, buf14["+std::to_string(328+8*t)+"]");e.emit("LDC.U64 D1.x, buf14["+std::to_string(1352+8*s)+"]");e.emit("OR.U64 D0.x, D0, D1");
 const auto coord=e.append(uv,F32({0}));const auto out=e.temp();e.emit("TXL.F "+out+".x, "+coord.text+".xyzz, handle(D0.x), 2D");return {out,1};
}
inline Value SurfaceShadowPCF(PolicyEmitter& p,Value projection){
 auto& e=p.emitter;auto size=p.Load(2,464,4),uv=e.op("DIV",2,{e.swizzle(projection,"rg"),e.swizzle(projection,"a")});
 const auto texel=e.op("MAD",2,{uv,e.swizzle(size,"rg"),F32({-.5})}),floor=e.per_component("FLR",texel);
 const auto center=e.op("ADD",2,{floor,F32({.5})}),fraction=e.per_component("FRC",texel),inverse=e.swizzle(size,"ba");
 std::array<Value,4> taps;const std::array<std::vector<double>,4> offsets={{{0,0},{1,0},{0,1},{1,1}}};
 for(unsigned i=0;i<4;++i){auto coord=e.op("MUL",2,{e.op("ADD",2,{center,Constant(offsets[i])}),inverse});auto depth=SurfaceShadowTap(p,coord);
  taps[i]=e.op("ADD",1,{e.op("MUL",1,{e.sub(1,depth,e.swizzle(projection,"b")),F32({4000})}),F32({1})},true);}
 auto row0=e.op("MAD",1,{e.swizzle(fraction,"r"),e.sub(1,taps[1],taps[0]),taps[0]});
 auto row1=e.op("MAD",1,{e.swizzle(fraction,"r"),e.sub(1,taps[3],taps[2]),taps[2]});
 return e.op("MAD",1,{e.swizzle(fraction,"g"),e.sub(1,row1,row0),row0});
}
inline Value SurfaceCascadedShadow(PolicyEmitter& p,Value world,Value sceneDepth){
 auto& e=p.emitter;const auto out=e.temp(),selected=e.temp();e.emit("MOV.F32 "+out+".x, {1, 0, 0, 0}.x");e.emit("MOV.F32 "+selected+".x, {0, 0, 0, 0}.x");
 // The game's forward descriptor holds four cascades. Select the first active
 // depth split, retaining an out-of-range UV's unshadowed result.
 const auto countWord=ShadowIntegerWord(p,2,112,false);const auto count=e.temp();e.emit("I2F.U "+count+".x, "+countWord.text+".x");const auto splits=p.Load(2,128,4);
 for(unsigned i=0;i<4;++i){auto active=e.op("MUL",1,{e.op("SGT",1,{{count,1},F32({double(i)})}),e.op("SLT",1,{sceneDepth,e.swizzle(splits,std::string(1,"rgba"[i]))})});
  active=e.op("MUL",1,{active,e.op("SEQ",1,{{selected,1},F32({0})})});IndirectCondition(e,active);e.emit("MOV.F32 "+selected+".x, {1, 0, 0, 0}.x");
  auto projected=e.op("MUL",4,{e.swizzle(world,"r"),p.Load(2,144+64*i,4)});
  projected=e.op("MAD",4,{e.swizzle(world,"g"),p.Load(2,160+64*i,4),projected});projected=e.op("MAD",4,{e.swizzle(world,"b"),p.Load(2,176+64*i,4),projected});projected=e.op("ADD",4,{p.Load(2,192+64*i,4),projected});
  const auto uv=e.op("DIV",2,{e.swizzle(projected,"rg"),e.swizzle(projected,"a")}),bounds=p.Load(2,400+16*i,4);
  auto inside=e.op("MUL",2,{e.op("SGE",2,{uv,e.swizzle(bounds,"rg")}),e.op("SLE",2,{uv,e.swizzle(bounds,"ba")})});
  IndirectCondition(e,e.op("MUL",1,{e.swizzle(inside,"r"),e.swizzle(inside,"g")}));const auto covered=SurfaceShadowPCF(p,projected);e.emit("MOV.F32 "+out+".x, "+Emitter::src(covered,1));e.emit("ENDIF");e.emit("ENDIF");}
 return {out,1};
}
inline Value SurfaceCachedIrradiance(PolicyEmitter& p,unsigned buffer,Value normal){
 auto& e=p.emitter;const auto out=e.temp();e.emit("MOV.F32 "+out+".xyz, {0, 0, 0, 0}.x");
 IndirectCondition(e,e.op("SGT",1,{p.Load(0,3936,1),F32({0})}));auto basis=VolumeIrradianceBasis(e,normal);std::array<Value,3> channels;
 for(unsigned c=0;c<3;++c){const auto linear=e.dot(4,p.Load(buffer,96+16*c,4),basis.linear),quadratic=e.dot(4,p.Load(buffer,144+16*c,4),basis.quadratic);
  channels[c]=e.op("MAD",1,{e.swizzle(p.Load(buffer,192,3),std::string(1,"rgb"[c])),basis.difference,e.op("ADD",1,{linear,quadratic})});}
 auto rgb=e.op("MAX",3,{e.append(e.append(channels[0],channels[1]),channels[2]),F32({0})});rgb=e.op("MUL",3,{rgb,p.Load(0,2960,3)});
 e.emit("MOV.F32 "+out+".xyz, "+rgb.text);e.emit("ENDIF");return {out,3};
}
inline unsigned SurfaceUniformRole(const FMaterialShaderReference& ref,std::uint64_t hash){
 unsigned out=32;for(auto b:ref.UniformBufferHashes)if(b.second==hash){if(out!=32)throw std::runtime_error("translucent Surface uniform role ambiguous");out=b.first;}
 if(out>14)throw std::runtime_error("translucent Surface uniform role absent");return out;
}
inline std::string TranslucentSurfaceProgram(const Json& graph,const OwnedUniforms& uniforms,const ShaderBindings& header,
 const FMaterialShaderReference& ref,unsigned materialBuffer,const std::map<std::string,std::string>& textureParameters,const std::map<std::string,std::pair<int,int>>& textureSlots={},const std::string& diagnosticStage=""){
 const auto kind=TranslucentSurfacePass(ref.TypeHash);auto roles=ReadTranslucentSurfaceBindings(header);
 if(SurfaceUniformRole(ref,EngineHash::ViewUniformBuffer)!=0||SurfaceUniformRole(ref,0x60929d69929b2b59ull)!=1)throw std::runtime_error("translucent Surface View/Primitive role mismatch");
 Emitter initial;initial.clips=true;initial.owned_uniforms=true;initial.material_parameters=uniforms.bindings;initial.material_buffer=materialBuffer;initial.uses_primitive=true;
 auto material=CompileTranslucentLitInputsWithEmitter(graph,std::move(initial),textureParameters);auto& e=material.emitter;PolicyEmitter p(e,roles);IndirectEmitter indirect(p);
 auto& pins=material.pins;const auto normal=pins.at("Normal"),view=e.camera_vector(),world=e.world_position();
 const auto base=pins.at("BaseColor"),metal=pins.at("Metallic"),spec=pins.at("Specular"),rough=pins.at("Roughness"),ao=pins.at("AmbientOcclusion");
 auto reflectance=Reflectance(e,base,metal,spec);const auto diffuse=e.temp(),specular=e.temp();e.emit("MOV.F32 "+diffuse+".xyz, "+reflectance.diffuse.text);e.emit("MOV.F32 "+specular+".xyz, "+reflectance.specular.text);
 IndirectCondition(e,e.op("SNE",1,{p.Load(0,3436,1),F32({0})}));auto absorbed=e.op("MAD",3,{reflectance.specular,F32({.449999988}),reflectance.diffuse});e.emit("MOV.F32 "+diffuse+".xyz, "+absorbed.text);e.emit("MOV.F32 "+specular+".xyz, {0, 0, 0, 0}.x");e.emit("ENDIF");reflectance={{diffuse,3},{specular,3}};
 // The lightmap policies read the lightmap coordinates in xy of their fourth interpolant and the shadow map
 // coordinates in zw (the distance field shadow policy).
 Value lightmapUV=F32({0,0}),shadowUV=F32({0,0});
 if(kind.lightmap){const auto coords=e.attrib("in_texcoord4",4);lightmapUV=e.swizzle(coords,"rg");shadowUV=e.swizzle(coords,"ba");}
 const auto indirectKind=kind.lightmap?IndirectKind::HighQuality:kind.volume?IndirectKind::Volumetric:IndirectKind::None;
 const auto noCache=EmitIndirectPolicy(indirect,header,ref.UniformBufferHashes,{indirectKind,kind.sky&&!kind.cached},{world,normal,lightmapUV});
 Value irradiance=noCache.irradiance,directionalShadow=noCache.directionalShadow;
 if(kind.cached){const auto cache=SurfaceUniformRole(ref,0xcafbf523acdd8b5cull);if(cache!=3||header.layout_hashes.at(cache)!=0x041d08fau)throw std::runtime_error("translucent Surface cached lighting layout unavailable");
  irradiance=SurfaceCachedIrradiance(p,cache,normal);
  const auto cacheShadow=e.temp();e.emit("MOV.F32 "+cacheShadow+".x, {1, 0, 0, 0}.x");IndirectCondition(e,e.op("MUL",1,{e.op("SGT",1,{p.Load(0,3936,1),F32({0})}),e.op("SGT",1,{p.Load(1,300,1),F32({0})})}));e.emit("MOV.F32 "+cacheShadow+".x, "+Emitter::src(p.Load(cache,80,1),1));e.emit("ENDIF");
  IndirectCondition(e,e.op("SGT",1,{p.Load(1,328,1),F32({0})}));e.emit("MOV.F32 "+cacheShadow+".x, "+Emitter::src(noCache.directionalShadow,1));e.emit("ENDIF");directionalShadow={cacheShadow,1};
  if(kind.sky){const auto direction=e.temp(),visibility=e.temp(),weight=e.temp();e.emit("MOV.F32 "+direction+".xyz, "+normal.text);e.emit("MOV.F32 "+visibility+".x, {1, 0, 0, 0}.x");e.emit("MOV.F32 "+weight+".x, {1, 0, 0, 0}.x");
   IndirectCondition(e,e.op("SNE",1,{p.Load(0,3476,1),F32({0})}));const auto rawDirection=e.temp();e.emit("MOV.F32 "+rawDirection+".xyz, "+normal.text);
   IndirectCondition(e,e.op("SGT",1,{p.Load(0,3936,1),F32({0})}));const auto bent=p.Load(cache,64,4);e.emit("MOV.F32 "+rawDirection+".xyz, "+bent.text);e.emit("MOV.F32 "+visibility+".x, "+Emitter::src(e.swizzle(bent,"a"),1));e.emit("ENDIF");
   const auto complement=e.sub(1,F32({1}),{visibility,1}),smooth=e.sub(1,F32({1}),e.op("MAD",1,{e.neg({visibility,1}),complement,complement})),dot=e.op("MOV",1,{e.dot(3,normal,{rawDirection,3})},true);
   const auto blended=e.op("MAD",3,{smooth,e.sub(3,normal,{rawDirection,3}),{rawDirection,3}}),w=e.op("ADD",1,{dot,e.op("MAD",1,{smooth,e.neg(dot),smooth})});
   e.emit("MOV.F32 "+direction+".xyz, "+blended.text);e.emit("MOV.F32 "+weight+".x, "+Emitter::src(w,1));e.emit("ENDIF");
   const auto sky=SkyIrradiance(indirect,ReadSkyCoefficients(indirect),{direction,3});irradiance=e.op("MAD",3,{sky,e.op("MUL",1,{{visibility,1},{weight,1}}),irradiance});}}

 const auto depthReg=e.temp();e.emit("RCP.F32 "+depthReg+".x, fragment.position.w");Value depth{depthReg,1};const auto csm=SurfaceCascadedShadow(p,world,depth);
 // The baked shadow: the distance field shadow from the lightmap's shadow texture, none for a lightmap without one
 // (as the opaque policies), else the indirect policy's directional shadow.
 const Value bakedSource=kind.distanceField?SampleBakedShadow(p,ReadBakedShadowBindings(header,ref.UniformBufferHashes),shadowUV):
     kind.lightmap?F32({0,0,0,0}):e.append(directionalShadow,F32({1,1,1}));
 const auto bakedReg=e.temp();e.emit("MOV.F32 "+bakedReg+", "+bakedSource.text);
 // The forward directional branch overrides its first precomputed channel;
 // local lights consume that same resulting four-channel shadow vector.
 ShadowIntegerCondition(e,ShadowIntegerWord(p,2,8,false));e.emit("MOV.F32 "+bakedReg+".x, {1, 0, 0, 0}.x");e.emit("ENDIF");
 const Value baked{bakedReg,4};const auto flags=ShadowIntegerWord(p,2,96,false);const auto channelMask=ShadowChannels(e,flags,0),previewMask=ShadowChannels(e,flags,4);
 const auto raw=e.temp();e.emit("MOV.F32 "+raw+".x, "+Emitter::src(ao,1));
 ShadowIntegerCondition(e,ShadowIntegerWord(p,2,8,false));const auto counts=e.dot(4,channelMask,F32({1,1,1,1})),coverage=e.dot(4,baked,channelMask);
 const auto stat=e.op("ADD",1,{e.op("MAD",1,{coverage,counts,e.neg(counts)}),F32({1})}),dyn=csm,fade=p.Load(2,104,2);
 auto amount=e.op("MAD",1,{depth,e.swizzle(fade,"r"),e.swizzle(fade,"g")},true);amount=e.op("MUL",1,{amount,amount});const auto joined=e.op("MIN",1,{e.op("MAD",1,{amount,e.sub(1,stat,dyn),dyn}),F32({1})});e.emit("MOV.F32 "+raw+".x, "+Emitter::src(joined,1));e.emit("ENDIF");
 const auto strength=e.op("MOV",1,{p.Load(0,2576,1)},true),surfaceShadow=e.op("ADD",1,{e.op("MAD",1,{{raw,1},strength,e.neg(strength)}),F32({1})});
 const auto directD=e.temp(),directS=e.temp();e.emit("MOV.F32 "+directD+".xyz, {0, 0, 0, 0}.x");e.emit("MOV.F32 "+directS+".xyz, {0, 0, 0, 0}.x");
 const auto primitiveChannels=ForwardWordLoad(p,1,380,1),lightChannels=ForwardWordOp(e,"SHR.U",1,{{flags.text,1},ForwardWordConstant(8)}),active=ForwardWordOp(e,"AND.U",1,{lightChannels,primitiveChannels});
 ShadowIntegerCondition(e,ShadowIntegerWord(p,2,8,false));ShadowIntegerCondition(e,{active.text,1});
 const auto core=DirectionalSurface(e,normal,view,p.Load(2,64,3),rough,reflectance.diffuse,reflectance.specular,F32({1}),p.Load(0,2568,1));const auto color=p.Load(2,80,3),specStrength=p.Load(0,2592,1);
 const auto sShadow=e.op("ADD",1,{specStrength,e.op("MAD",1,{surfaceShadow,e.neg(specStrength),surfaceShadow})});
 const auto d=e.op("MUL",3,{core.diffuse,e.op("MUL",3,{color,surfaceShadow})}),s=e.op("MUL",3,{core.specular,e.op("MUL",3,{color,sShadow})});
 e.emit("MOV.F32 "+directD+".xyz, "+d.text);e.emit("MOV.F32 "+directS+".xyz, "+s.text);e.emit("ENDIF");e.emit("ENDIF");
 const auto rect=p.Load(0,2080,2),inverseSize=e.swizzle(p.Load(0,2112,4),"ba"),pixel=e.op("MUL",2,{e.sub(2,{"fragment.position",4},rect),inverseSize});
 const ForwardLocalResources local{roles.RequireSRV("LocalLights"),roles.RequireSRV("CulledLightCounts"),roles.RequireSRV("CulledLightIndices"),unsigned(header.default_sampler)};
 const SurfaceLighting seed{{directD,3},{directS,3}};const auto direct=EmitClusteredLocalLighting(p,local,{world,pixel,depth,baked,e.op("MOV",4,{csm}),strength,ao,F32({1}),primitiveChannels},{normal,view,rough,reflectance.diffuse,reflectance.specular,F32({1}),p.Load(0,2568,1)},nullptr,&seed);
 const auto captureBuffer=SurfaceUniformRole(ref,ReflectionCaptureNameHash);if(header.layout_hashes.at(captureBuffer)!=0)throw std::runtime_error("translucent Surface reflection uniform layout unavailable");
 const auto brightness=e.dot(3,irradiance,F32({.300000012,.589999974,.109999999}));
 const auto reflection=EmitReflectionPolicy(p,{captureBuffer},{kind.sky,kind.volume||kind.cached||kind.lightmap},ReflectionResponseKind::OriginalOpaque,{normal,view,rough,reflectance.specular,F32({1}),ao,{raw,1},brightness});
 auto linear=e.op("ADD",3,{direct.diffuse,direct.specular});linear=e.op("ADD",3,{linear,reflection});linear=e.op("ADD",3,{linear,e.op("MUL",3,{irradiance,reflectance.diffuse})});
 linear=e.op("ADD",3,{linear,EmitC4Ambient(p,base,metal,ao)});linear=e.op("ADD",3,{linear,e.op("MAX",3,{pins.at("EmissiveColor"),F32({0})})});
 const auto fog=p.ApplyVolumetricFog(world,F32({0,0,0,1})),output=FogComposite(e,linear,fog,p.Load(0,2180,1));e.emit("MOV.F32 result_color0.xyz, "+output.text);e.emit("MOV.F32 result_color0.w, "+Emitter::src(pins.at("Opacity"),1));
 if(!diagnosticStage.empty()){Value stage;
  if(diagnosticStage=="direct-diffuse")stage=direct.diffuse;else if(diagnosticStage=="direct-specular")stage=direct.specular;else if(diagnosticStage=="reflection")stage=reflection;else if(diagnosticStage=="ambient")stage=EmitC4Ambient(p,base,metal,ao);else if(diagnosticStage=="irradiance")stage=irradiance;else throw std::runtime_error("translucent Surface diagnostic stage unavailable");
  e.emit("MOV.F32 result_color0.xyz, "+stage.text);e.emit("MOV.F32 result_color0.w, {0, 0, 0, 0}.x");}
 // Preserve each actual retained stage's UV and centroid-normal locations.
 std::map<std::string,int> varyings;for(const auto& v:header.inputs)varyings[v.name.substr(0,v.name.find("_centroid"))]=v.location;
 return ForwardReuseTemporaries(Resolve(indirect.Program(),textureSlots,varyings));
}
}}}
