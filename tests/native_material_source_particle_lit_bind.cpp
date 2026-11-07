#include "../core/material_source_particle_lit_bind.h"
#include <iostream>
namespace M=P4MapKit::Material;
static unsigned checks=0;
static void Check(bool value){if(!value)throw std::runtime_error("lit binding assertion failed");++checks;}
template<class F> static void Refuses(F test){try{test();}catch(const M::Refused&){++checks;return;}catch(const M::BindingRefused&){++checks;return;}throw std::runtime_error("lit binding mutation was admitted");}
int main(){
    M::SymbolicArena arena;M::BindingContext context;context.Buffers={{0,"View"}};
    P4MapKit::FMaterialShaderReference ref;ref.Frequency=3;ref.UniformBufferHashes={{0,0xf10cd51552e37c76ull},{2,0xdb2cde9471cc30d3ull}};
    P4MapKit::CustomMaterial::ShaderBindings header;header.layout_hashes={0x166705f7,0,0x0d480502};header.resource_bits=4;
    header.textures=P4MapKit::CustomMaterial::BuildResourceTable({{2,{{36,5},{65,6},{66,7}}}},3);
    header.samplers=P4MapKit::CustomMaterial::BuildResourceTable({{2,{{48,3},{67,4}}}},3);
    const auto settings=M::Object({{"domain","MD_Surface"},{"blend_mode","BLEND_Translucent"},{"shading_model","MSM_DefaultLit"}});
    M::InstallOriginalParticleLitViewRows(arena,context,ref,header);
    for(const auto& field:std::vector<std::tuple<const char*,int,int>>{{"TranslatedWorldToView",192,16},{"TranslatedWorldToCameraView",320,16},{"ViewToClip",448,16},{"ClipToTranslatedWorld",640,16},{"BufferSizeAndInvSize",2128,4}})
        for(int c=0;c<std::get<2>(field);++c){M::Value out;Check(context.LeafOverride(arena.mk("IN",{"buffer",0,std::get<1>(field)+4*c}),out));Check(M::same(out,M::source(arena,"WaterViewRow",M::Value::tuple_value({std::get<0>(field),c/4}),c%4)));}
    M::Value untouched;Check(!context.LeafOverride(arena.mk("IN",{"buffer",0,1072}),untouched));
    Refuses([&]{M::Value out;context.LeafOverride(arena.mk("IN",{"buffer",0,321}),out);});
    for(int mutation=0;mutation<3;++mutation){auto r=ref;auto h=header;M::BindingContext c;c.Buffers={{0,"View"}};if(mutation==0)r.UniformBufferHashes[0]^=1;else if(mutation==1)h.layout_hashes[0]^=1;else r.Frequency=0;M::InstallOriginalParticleLitViewRows(arena,c,r,h);Refuses([&]{M::Value out;c.LeafOverride(arena.mk("IN",{"buffer",0,352}),out);});}
    M::InstallOriginalParticleLitEyeAdaptation(arena,context,ref,header,settings);
    const auto eye=arena.mk("TEX",{"TXF","2D",328+6*8,M::Value::null(),0.,0.,0.,0.});M::Value out;
    Check(context.ComponentOverride(arena.mk("COMP",{eye,0}),out));Check(M::same(out,M::source(arena,"EyeAdaptation")));
    Refuses([&]{M::Value v;context.ComponentOverride(arena.mk("COMP",{eye,1}),v);});
    const auto moved=arena.mk("TEX",{"TXF","2D",328+6*8,M::Value::null(),1.,0.,0.,0.});
    Refuses([&]{M::Value v;context.ComponentOverride(arena.mk("COMP",{moved,0}),v);});
    M::InstallOriginalParticleLitSceneSamples(arena,context,ref,header,settings);const M::Vec coords{{.2,.3,0.,0.}};
    const auto depth=arena.mk("TEX",{"TXL","2D",328+5*8,1352+3*8,.2,.3,0.,0.});
    Check(context.TextureOverride(depth,coords,out));Check(M::same(out,arena.mk("SCENETEX",{5,.2,.3})));
    const auto copy=arena.mk("TEX",{"TEX","2D",328+7*8,1352+4*8,.2,.3,0.,0.});Check(context.TextureOverride(copy,coords,out));
    for(int mutation=0;mutation<4;++mutation){auto a=M::Args(depth);if(mutation==0)a[0]="TEX";else if(mutation==1)a[1]="CUBE";else if(mutation==2)a[3]=1352+4*8;else a[7]=1.;const auto bad=arena.mk("TEX",a);Refuses([&]{M::Value v;context.TextureOverride(bad,coords,v);});}
    Check(!context.TextureOverride(arena.mk("TEX",{"TXL","2D",328+9*8,1352+3*8,.2,.3,0.,0.}),coords,out));
    std::cout<<checks<<" exact binding and refusal checks\n";return 0;
}
