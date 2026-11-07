#include "../core/material_source_particle_lit_trace.h"
#include "../core/material_source_particle_lit_reflection.h"
#include <iostream>
namespace M=P4MapKit::Material;
static unsigned checks=0;
static void Check(bool value){if(!value)throw std::runtime_error("lit trace assertion failed after "+std::to_string(checks)+" checks");++checks;}
template<class F> static void Refuses(F test){try{test();}catch(const M::Refused&){++checks;return;}catch(const M::EvalError&){++checks;return;}throw std::runtime_error("lit trace mutation was admitted");}
int main(){try{
    auto arena=std::make_shared<M::SymbolicArena>();
    const std::vector<std::string> lines={"LDC.U64 D0.x, buf14[368]","MOV.F R0, { 0.1, 0.2, 0.3, 7 }","MOV.F R1.w, { 2, 2, 2, 2 }","TXL.F R2, R0, R1.w, handle(D0.x), ARRAYCUBE"};
    M::OriginalParticleLitSymbolicMachine machine(lines,arena);machine.run();const auto value=machine.reg.at("R2")[0];Check(M::is(value,"COMP"));const auto tex=M::Args(value)[0];Check(M::is(tex,"TEX"));const auto& args=M::Args(tex);Check(args.size()==9);Check(args[0].text=="TXL"&&args[1].text=="ARRAYCUBE");Check(args[2].number()==368&&args[3].kind==M::Value::Null);for(int c=0;c<4;++c)Check(args[std::size_t(c+4)].number()==std::array<double,4>{{.1,.2,.3,7.}}[std::size_t(c)]);Check(args[8].number()==2.);
    const std::vector<std::string> branching={lines[0],lines[1],lines[2],"LDC.F R4.x, buf0[0]","MOV.F.CC R4.x, R4.x","IF NE.x",lines[3],"ELSE","MOV.F R2, { 1, 2, 3, 4 }","ENDIF"};M::OriginalParticleLitSymbolicMachine branched(branching,arena);branched.run();Check(M::is(branched.reg.at("R2")[0],"SEL"));Check(M::Args(branched.reg.at("R2")[0])[1].expression.get()==value.expression.get());Refuses([&]{M::OriginalParticleLitSymbolicMachine partial(branching,arena);partial.run(7);});
    M::MaterialScheduledAnchor scheduled;scheduled.ValueAnchor=M::MaterialAnchor::Register(3,"R2","xyz");scheduled.Indices={3};M::OriginalParticleLitSymbolicMachine prefix(lines,arena);prefix.run(3);const auto later=M::ReadOriginalParticleLitScheduledValue(scheduled,prefix);Check(later.size()==3);for(int c=0;c<3;++c)Check(M::same(later[std::size_t(c)],machine.reg.at("R2")[std::size_t(c)]));scheduled.Indices={3,3};Refuses([&]{M::ReadOriginalParticleLitScheduledValue(scheduled,prefix);});
    for(const auto& bad:std::vector<std::string>{"TEX.F R2, R0, R1.w, handle(D0.x), ARRAYCUBE","TXL.F R2, R0, handle(D0.x), ARRAYCUBE","TXL.F R2, R0, R1.w, R9, ARRAYCUBE"})Refuses([&]{M::OriginalParticleLitSymbolicMachine wrong(lines,arena);wrong.run(3);wrong.step(3,bad);});
    P4MapKit::FMaterialShaderReference ref;ref.Frequency=3;ref.UniformBufferHashes={{0,0xf10cd51552e37c76ull},{2,0xdb2cde9471cc30d3ull}};
    P4MapKit::CustomMaterial::ShaderBindings header;header.layout_hashes={0x166705f7,0,0x0d480502};header.resource_bits=4;
    header.textures=P4MapKit::CustomMaterial::BuildResourceTable({{2,{{16,1},{18,2},{20,3}}}},3);header.samplers=P4MapKit::CustomMaterial::BuildResourceTable({{2,{{17,4},{19,5},{21,6}}}},3);
    const auto settings=M::Object({{"domain","MD_Surface"},{"blend_mode","BLEND_Opaque"},{"shading_model","MSM_DefaultLit"}});M::BindingContext context;context.Buffers={{0,"View"},{2,"OpaqueBasePass"}};M::InstallOriginalParticleLitReflection(*arena,context,ref,header,settings);M::Value out;
    Check(context.LeafOverride(arena->mk("IN",{"buffer",0,3624}),out));Check(M::same(out,M::source(*arena,"ReflectionField","ReflectionCubemapMaxMip",0)));
    for(int role=0;role<3;++role){std::vector<M::Value> a={"TXL",role==2?"ARRAYCUBE":"CUBE",328+8*(role+1),1352+8*(role+4),.1,.2,.3,7.};if(role==2)a.push_back(2.);const auto input=arena->mk("TEX",a);Check(context.ComponentOverride(input,out));Check(M::is(out,"REFLECTION_SAMPLE"));Check(M::Args(out)[0].text==std::array<std::string,3>{{"sky","sky_blend","reflection"}}[std::size_t(role)]);Check(M::Args(out).size()==(role==2?6u:5u));
        for(int mutation=0;mutation<2;++mutation){auto changed=a;if(mutation==0)changed[0]="TEX";else changed.push_back(0.);Refuses([&]{M::Value result;context.ComponentOverride(arena->mk("TEX",changed),result);});}
        a[3]=2000;Check(!context.ComponentOverride(arena->mk("TEX",a),out));
    }
    auto wrongRef=ref;wrongRef.UniformBufferHashes[2]^=1;Refuses([&]{M::BindingContext wrong;M::InstallOriginalParticleLitReflection(*arena,wrong,wrongRef,header,settings);});
    std::cout<<checks<<" trace and typed reflection checks\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
