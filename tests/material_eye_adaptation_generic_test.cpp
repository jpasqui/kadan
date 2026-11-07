#include "../core/material_source_recovery.h"
#include "../core/material_source_particle_lit_bind.h"
#include <iostream>
using namespace P4MapKit;
using namespace P4MapKit::Material;
static unsigned checks=0;
static void Check(bool value,const char* what){++checks;if(!value)throw std::runtime_error(what);}
struct Fixture {
    FMaterialShaderReference Reference;
    CustomMaterial::ShaderBindings Header;
    BindingContext Binding;
    Json Settings=Object({{"domain","MD_Surface"},{"blend_mode","BLEND_Opaque"},{"shading_model","MSM_Subsurface"}});
    Fixture(int slot,bool translucent=false){
        Reference.Frequency=3;Reference.UniformBufferHashes={{0,0xf10cd51552e37c76ull},{2,translucent?0x9f079ec4ea0e07fcull:0xdb2cde9471cc30d3ull}};
        Header.layout_hashes={0x166705f7u,0,translucent?0x05ec0deeu:0x0d480502u};Header.resource_bits=1u<<2;
        Header.textures=CustomMaterial::BuildResourceTable({{2,{{translucent?77u:65u,unsigned(slot)}}}},3);
        Binding.Buffers={{0,"View"},{2,translucent?"TranslucentBasePass":"OpaqueBasePass"}};
        if(translucent)Settings["blend_mode"]="BLEND_Translucent";
    }
};
static Value Read(SymbolicArena& arena,int slot,int channel=0,double coordinate=0,const char* operation="TXF")
{return arena.mk("COMP",{arena.mk("TEX",{operation,"2D",328+8*slot,Value::null(),coordinate,0.0,0.0,0.0}),channel});}
static Value Bind(SymbolicArena& arena,Fixture& f,const Value& value,bool generic=true)
{
    auto context=f.Binding;
    if(generic)InstallOriginalMaterialSourceSceneInputs(arena,context,f.Reference,f.Header,f.Settings);
    else InstallOriginalParticleLitEyeAdaptation(arena,context,f.Reference,f.Header,f.Settings);
    Binder binder(arena,context);return BindMaterialComponent(binder,value);
}
static void Reject(const std::function<void()>& call,const std::string& message)
{bool refused=false;try{call();}catch(const std::exception& e){refused=std::string(e.what()).find(message)!=std::string::npos;}Check(refused,message.c_str());}
int main(){try{
    // These slots reproduce Dust and the two Ice programs' checked resource
    // contracts. They need the generic surface path, not DefaultLit dispatch.
    for(int slot:{2,3})for(bool translucent:{false,true}){
        SymbolicArena arena;Fixture f(slot,translucent);const auto raw=Read(arena,slot);
        Binder before(arena,f.Binding);const auto unbound=BindMaterialComponent(before,raw);
        Reject([&](){Lifter lifter(arena,Json::MakeObject());lifter.lift({unbound});},"a texture that is not the material's");
        const auto after=Bind(arena,f,raw);Check(same(after,source(arena,"EyeAdaptation")),"generic source exposes only the qualified EyeAdaptation source");
        Check(same(after,Bind(arena,f,raw,false)),"generic and existing particle APIs use the same checked handler");
        Lifter lifter(arena,Json::MakeObject());const auto link=lifter.lift({after});Check(lifter.nodes.At(link.array[0].Text()).At("type").Text()=="EyeAdaptation","generic result lifts to the real engine expression");
    }
    SymbolicArena arena;Fixture f(2);
    Reject([&](){Bind(arena,f,Read(arena,2,1));},"origin texel red-channel");
    Reject([&](){Bind(arena,f,Read(arena,2,0,1));},"origin texel red-channel");
    Reject([&](){Bind(arena,f,Read(arena,2,0,0,"TEX"));},"origin texel red-channel");
    auto bad=f;bad.Header.resource_bits=0;Reject([&](){Bind(arena,bad,Read(arena,2));},"active buffer bit");
    bad=f;bad.Reference.UniformBufferHashes[2]=1;Reject([&](){Bind(arena,bad,Read(arena,2));},"hashed buffer conflicts");
    bad=f;bad.Reference.Frequency=0;Reject([&](){Bind(arena,bad,Read(arena,2));},"pixel association");
    const auto unknown=Bind(arena,f,Read(arena,4));Reject([&](){Lifter lifter(arena,Json::MakeObject());lifter.lift({unknown});},"a texture that is not the material's");
    const auto prior=arena.mk("Prior",{});f.Binding.ComponentOverride=[prior](const Value& value,Value& out){if(same(value,prior)){out=7.0;return true;}return false;};
    Check(NumberIs(Bind(arena,f,prior),7),"generic scene input installation preserves previous component handlers");
    std::cout<<checks<<" generic EyeAdaptation checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
