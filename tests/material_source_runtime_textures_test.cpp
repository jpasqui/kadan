#include "../core/material_source_runtime_textures.h"
#include <iostream>
using namespace P4MapKit;
namespace M=P4MapKit::Material;
static int checks=0;
static void Check(bool value){if(!value)throw std::runtime_error("runtime-texture-check-failed");++checks;}
template<class F> static void Refuses(F action){bool refused=false;try{action();}catch(const M::BindingRefused&){refused=true;}catch(const M::Refused&){refused=true;}Check(refused);}
static M::MaterialOriginalRuntimeTextureContract Texel(){
    M::MaterialOriginalRuntimeTextureContract c;c.Uniforms.PreshaderNames={"Heightfield"};FMaterialPreshaderInstruction i;i.Opcode=EMaterialPreshaderOpcode::TexelSize;i.TextureIndex=0;
    FMaterialPreshaderProgram p;p.Instructions={i};c.Uniforms.VectorPrograms={p};FMaterialUniformTexture t;t.Parameter.Name="Heightfield";t.TextureIndex=0;c.Uniforms.Textures={t};return c;
}
static M::MaterialOriginalRuntimeTextureContract External(){
    M::MaterialOriginalRuntimeTextureContract c;c.Uniforms.PreshaderNames={"MediaTexture"};c.Uniforms.ExternalTextureCount=1;
    M::MaterialOriginalExternalTexture e;e.Name="MediaTexture";e.TextureIndex=0;c.External={e};
    for(const auto op:{EMaterialPreshaderOpcode::ExternalTextureCoordinateScaleRotation,EMaterialPreshaderOpcode::ExternalTextureCoordinateOffset}){FMaterialPreshaderInstruction i;i.Opcode=op;i.TextureIndex=0;FMaterialPreshaderProgram p;p.Instructions={i};c.Uniforms.VectorPrograms.push_back(p);}
    c.ExternalTextures[0]="MediaTexture";c.ExternalSamplers[0]="MediaTexture";return c;
}
static M::Value Row(M::SymbolicArena& a,const std::string& name,const M::Value& x,const M::Value& y,int row){return a.op("ADD",{M::OriginalExternalTextureSource(a,name,false,row),a.op("ADD",{a.op("MUL",{x,M::OriginalExternalTextureSource(a,name,true,row*2)}),a.op("MUL",{M::OriginalExternalTextureSource(a,name,true,row*2+1),y})})});}
int main(){try{
    auto texel=Texel();auto i=texel.Uniforms.VectorPrograms[0].Instructions[0];M::SymbolicArena arena;M::BindingContext context;context.Uniforms=texel.Uniforms;
    M::Binder unbound(arena,context);Refuses([&]{unbound.material_word(0);});
    M::InstallOriginalMaterialRuntimeTextures(arena,context,texel);const auto v=context.RuntimePreshader(i);
    Check(M::same(v[0],M::source(arena,"TextureTexelSize","Heightfield",0)));Check(M::same(v[1],M::source(arena,"TextureTexelSize","Heightfield",1)));Check(v[2].number()==1&&v[3].number()==0);
    M::Binder bound(arena,context);Check(M::same(bound.material_word(0),v[0]));Check(bound.material_word(8).number()==1&&bound.material_word(12).number()==0);
    Refuses([&]{M::InstallOriginalMaterialRuntimeTextures(arena,context,texel);});
    auto changed=i;changed.TextureIndex=1;Refuses([&]{context.RuntimePreshader(changed);});
    changed=i;changed.LayerIndex=0;Refuses([&]{context.RuntimePreshader(changed);});
    changed=i;changed.Parameter=1;Refuses([&]{context.RuntimePreshader(changed);});
    auto bad=texel;bad.Uniforms.Textures[0].TextureType=1;Refuses([&]{M::ValidateOriginalRuntimeTextureInstruction(bad,i);});
    bad=texel;bad.Uniforms.Textures.push_back(bad.Uniforms.Textures[0]);Refuses([&]{M::ValidateOriginalRuntimeTextureInstruction(bad,i);});
    bad=texel;bad.Uniforms.VectorPrograms[0].Instructions[0].Opcode=EMaterialPreshaderOpcode::TextureSize;i.Opcode=EMaterialPreshaderOpcode::TextureSize;Refuses([&]{M::ValidateOriginalRuntimeTextureInstruction(bad,i);});
    auto ext=External();M::BindingContext media;M::InstallOriginalMaterialRuntimeTextures(arena,media,ext);
    const auto rotation=media.RuntimePreshader(ext.Uniforms.VectorPrograms[0].Instructions[0]);Check(M::same(rotation[3],M::OriginalExternalTextureSource(arena,"MediaTexture",true,3)));
    const auto offset=media.RuntimePreshader(ext.Uniforms.VectorPrograms[1].Instructions[0]);Check(M::same(offset[1],M::OriginalExternalTextureSource(arena,"MediaTexture",false,1)));
    auto operand=ext.Uniforms.VectorPrograms[0].Instructions[0];operand.ExternalGuid[0]=1;Refuses([&]{media.RuntimePreshader(operand);});
    auto many=ext;many.External.push_back(many.External[0]);Refuses([&]{M::ValidateOriginalRuntimeTextureInstruction(many,ext.Uniforms.VectorPrograms[0].Instructions[0]);});
    const auto x=M::source(arena,"TexCoord",0,0),y=M::source(arena,"TexCoord",0,1);M::Vec coords{{Row(arena,"MediaTexture",x,y,0),Row(arena,"MediaTexture",x,y,1),0,0}};
    auto raw=arena.mk("TEX",{"TEX","2D",328,1352,x,y,0,0});M::Value result;Check(media.TextureOverride(raw,coords,result));Check(M::same(result,arena.mk("SAMPLE",{"MediaTexture","plain/SSM_FromTextureAsset",x,y})));
    auto swapped=coords;swapped[1]=Row(arena,"MediaTexture",y,x,1);Refuses([&]{media.TextureOverride(raw,swapped,result);});
    swapped=coords;swapped[0]=arena.op("ADD",{swapped[0],0.1});Refuses([&]{media.TextureOverride(raw,swapped,result);});
    swapped=coords;swapped[0]=Row(arena,"OtherRegistry",x,y,0);Refuses([&]{media.TextureOverride(raw,swapped,result);});
    Refuses([&]{media.TextureOverride(arena.mk("TEX",{"TXL","2D",328,1352,x,y,0,0}),coords,result);});
    Refuses([&]{media.TextureOverride(arena.mk("TEX",{"TEX","2D",328,1360,x,y,0,0}),coords,result);});
    M::BindingContext conflict;conflict.TextureParameters[0]="OtherRegistry";Refuses([&]{M::InstallOriginalMaterialRuntimeTextures(arena,conflict,ext);});Check(!conflict.RuntimePreshader);
    int fallback=0;M::BindingContext chained;chained.TextureOverride=[&](const M::Value&,const M::Vec&,M::Value&){++fallback;return false;};M::InstallOriginalMaterialRuntimeTextures(arena,chained,ext);
    Check(!chained.TextureOverride(arena.mk("TEX",{"TEX","2D",336,1352,x,y,0,0}),coords,result)&&fallback==1);
    std::vector<M::Value> terms;auto deep=x;for(int n=0;n<1000;++n)deep=arena.mk("ADD",{deep,x});Check(!M::OriginalExternalCoordinateTerms(deep,terms));
    FMaterialShaderMapEnvelope map;map.Frozen.resize(544);map.Uniforms.ExternalTextureCount=1;map.ScriptNames[512]="MediaTexture";
    const auto put=[&](std::size_t at,std::uint64_t bits,int bytes){for(int n=0;n<bytes;++n)map.Frozen[at+n]=std::uint8_t(bits>>(8*n));};put(256,513,8);put(264,1,4);put(268,1,4);
    const auto records=M::ReadOriginalMaterialExternalTextures(map);Check(records.size()==1&&records[0].Name=="MediaTexture"&&records[0].TextureIndex==0);
    auto invalid=map;invalid.ScriptNames.clear();Refuses([&]{M::ReadOriginalMaterialExternalTextures(invalid);});invalid=map;invalid.Uniforms.ExternalTextureCount=0;Refuses([&]{M::ReadOriginalMaterialExternalTextures(invalid);});invalid=map;invalid.Frozen.resize(530);Refuses([&]{M::ReadOriginalMaterialExternalTextures(invalid);});
    Check(!texel.ProductionAdmitted&&!ext.ProductionAdmitted);std::cout<<checks<<" runtime texture checks PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
