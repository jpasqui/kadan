#include "../core/material_source_scene.h"
#include <iostream>
using namespace P4MapKit;using namespace P4MapKit::Material;
static unsigned checks=0;static void Check(bool ok,const char* message){++checks;if(!ok)throw EvalError(message);}
template<class F>static void Refuses(F work,const char* name){bool held=false;try{work();}catch(const Refused& error){held=error.name==name;}Check(held,"expected source scene refusal");}
int main(){try{
    MaterialPreparedProgram program;program.Reference.Frequency=3;program.Reference.UniformBufferHashes={{0,0xf10cd51552e37c76ull}};
    program.Header.layout_hashes={0x166705f7,0,0x0d480502};program.Header.resource_bits=5;
    program.Header.textures=CustomMaterial::BuildResourceTable({{2,{{36,6},{66,4}}}},3);
    program.Header.srv=CustomMaterial::BuildResourceTable({{2,{{47,8}}}},3);
    program.Header.samplers=CustomMaterial::BuildResourceTable({{0,{{43,2}}},{2,{{48,5},{67,3}}}},3);
    program.Lines={"LDC.U64 R0.x, buf0[360]","LDC.U64 R1.x, buf0[1368]","OR.U64 R0.x, R0.x, R1.x","TEX R2, UV, handle(R0.x), 2D"};
    const auto settings=Object({{"domain","MD_Surface"},{"blend_mode","BLEND_Opaque"}});SymbolicArena arena;
    const auto uv0=source(arena,"ScreenUV",Value::null(),0),uv1=source(arena,"ScreenUV",Value::null(),1);
    const auto depth=arena.mk("COMP",{arena.mk("SCENETEX",{6,uv0,uv1}),0});
    const auto copy=arena.mk("POW",{arena.mk("COMP",{arena.mk("SCENETEX",{4,uv0,uv1}),1}),4.});
    const auto prepared=PrepareOriginalMaterialSceneRewrites(program,settings);const auto values=prepared.SceneAndSun(arena,{depth,copy});
    Check(same(values[0],SceneDetail::component(arena,"DeviceDepth",uv0,uv1,0)),"automatic source scene depth identity");
    Check(same(values[1],simplify(arena,"MUL",{SceneDetail::component(arena,"ColorViewClamp",uv0,uv1,1),.1})),"automatic source scene copy and observed sampler");
    auto absent=program;absent.Header.layout_hashes.clear();absent.Lines={"UNPROVED R0, R7"};const auto lazy=PrepareOriginalMaterialSceneRewrites(absent,settings);
    Check(same(lazy.SceneAndSun(arena,{Value(.4)})[0],Value(.4)),"ordinary material does not demand scene metadata");
    const auto reciprocal=arena.mk("RCP",{source(arena,"SvPosition",Value::null(),3)});
    Check(same(lazy.SceneAndSun(arena,{reciprocal})[0],source(arena,"PixelDepth")),"ordinary pixel depth rewrite requires no unused scene policy");
    Refuses([&]{lazy.SceneAndSun(arena,{depth});},"material native scene layout");
    auto changed=program;changed.Header.layout_hashes[2]^=1;Refuses([&]{PrepareOriginalMaterialSceneRewrites(changed,settings).SceneAndSun(arena,{depth});},"material native scene layout");
    changed=program;changed.Lines[1]="MOV R1.x, R7.x";Refuses([&]{PrepareOriginalMaterialSceneRewrites(changed,settings).SceneAndSun(arena,{copy});},"material native scene handle");
    unsigned called=0;MaterialSurfaceRewrites extra;extra.SceneAndSun=[&](SymbolicArena&,const std::vector<Value>& input){++called;Check(same(input[0],values[0]),"additional source policy receives native scene rewrite");return input;};
    Check(same(PrepareOriginalMaterialSceneRewrites(program,settings,extra).SceneAndSun(arena,{depth})[0],values[0])&&called==1,"additional source callback composes once");
    const auto policy=ReadMaterialNativeSceneResourcePolicy(program.Reference,program.Header,"MD_Surface","BLEND_Opaque",true);
    Check(!policy.ResourcePolicy.ProductionAdmitted&&!policy.ResourcePolicy.SchedulingProved,"metadata rewrite cannot admit production or renderer access");
    std::cout<<"native original source scene checks="<<checks<<'\n';
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}return 0;}
