#include "../core/material_graph.h"
#include <iostream>
using namespace P4MapKit::Material;
static void Check(bool value,const char* name){if(!value)throw std::runtime_error(name);}
template<class F>static void Held(F action,const char* name){bool refused=false;try{action();}catch(const Refused&){refused=true;}Check(refused,name);}
int main(){try{
    const auto graph=Json::Parse(R"({"format":2,"domain":"MD_Surface","blend_mode":"BLEND_Masked","shading_model":"MSM_DefaultLit","two_sided":false,"tangent_space_normal":true,"clip":0.3333,"nodes":{"camera":{"type":"Constant","props":{"R":0.2},"inputs":{}},"shadow":{"type":"Constant","props":{"R":1},"inputs":{}},"switch":{"type":"ShadowReplace","props":{},"inputs":{"Default":["camera",""],"Shadow":["shadow",""]}}},"outputs":{"OpacityMask":["switch",""]}})");
    Check(ValidateGraph(graph).node_types.count("ShadowReplace")==1,"stock shadow node admitted");
    auto missing=graph;missing["nodes"]["switch"]["inputs"].object.erase("Shadow");Held([&]{ValidateGraph(missing);},"required shadow input");
    auto extra=graph;extra["nodes"]["switch"]["inputs"]["Unexpected"]=graph.At("nodes").At("switch").At("inputs").At("Default");Held([&]{ValidateGraph(extra);},"exact stock field names");
    auto dangling=graph;dangling["nodes"]["switch"]["inputs"]["Shadow"]=Json::Parse(R"(["missing",""])");Held([&]{ValidateGraph(dangling);},"dangling shadow branch");
    auto cycle=graph;cycle["nodes"]["switch"]["inputs"]["Shadow"]=Json::Parse(R"(["switch",""])");Held([&]{ValidateGraph(cycle);},"shadow cycle");
    auto selector=graph;selector["nodes"]["switch"]["inputs"]["Shadow"]=Json::Parse(R"(["shadow","",0.5])");Held([&]{ValidateGraph(selector);},"integer branch output selector");
    std::cout<<"6 semantic checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
