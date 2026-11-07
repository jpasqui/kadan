#include "../core/material_stage.h"
#include <iostream>
using namespace P4MapKit::Material;
static int checks=0;
static void Check(bool value,const char* name){++checks;if(!value)throw std::runtime_error(name);}
template<class F>static void Reject(F action){bool hit=false;try{action();}catch(const std::exception&){hit=true;}Check(hit,"unsafe boundary accepted");}
static Json Graph(){return Object({{"format",2},{"domain","MD_Surface"},{"blend_mode","BLEND_Opaque"},{"shading_model","MSM_DefaultLit"},{"clip",.3333},{"two_sided",false},{"tangent_space_normal",true},{"nodes",Object({{"C",Object({{"type","Constant"},{"inputs",Json::MakeObject()},{"props",Object({{"R",.5}})}})}})},{"outputs",Object({{"Roughness",Array({"C",""})}})}});}
int main()
{
    try{
        Check(Json::Parse("{\"text\":\"\\ud83d\\ude03\",\"n\":-0.0}").At("n").Dump()=="-0","signed zero survives JSON");
        for(const auto* invalid:{"{\"x\":0,\"x\":1}","{\"x\":NaN}","{\"x\":1e999}","[01]","[+1]","\"\\ud800\"","\"\\udc00\"","\"\\u0000\n\""})Reject([&]{Json::Parse(invalid);});
        std::string malformed="\"";malformed.push_back(char(0xc0));malformed.push_back(char(0xaf));malformed+='"';Reject([&]{Json::Parse(malformed);});
        Reject([&]{Json::Parse("[1,2]",4);});Reject([&]{Json::Parse("[1,2]",100,2);});Reject([&]{Json::Parse("[[[]]]",100,100,1);});
        auto graph=Graph();Check(ValidateGraph(graph).nodes==1,"minimal graph boundary");auto dangling=graph;dangling["outputs"]["Roughness"]=Array({"missing",""});Reject([&]{ValidateGraph(dangling);});
        auto cycle=graph;cycle["nodes"]["C"]=Object({{"type","Add"},{"props",Json::MakeObject()},{"inputs",Object({{"A",Array({"C",""})}})}});Reject([&]{ValidateGraph(cycle);});
        auto mask=graph;mask["outputs"]["Roughness"]=Array({"C","Q"});Reject([&]{ValidateGraph(mask);});auto index=graph;index["outputs"]["Roughness"]=Array({"C","",.5});Reject([&]{ValidateGraph(index);});
        auto nonfinite=graph;nonfinite["nodes"]["C"]["props"]["R"]=std::numeric_limits<double>::infinity();Reject([&]{ValidateGraph(nonfinite);});
        auto sample=graph;sample["nodes"]["C"]=Object({{"type","TextureSampleParameter2D"},{"inputs",Json::MakeObject()},{"props",Object({{"ParameterName","Texture"},{"SamplerSource","SSM_FromTextureAsset"}})}});Check(!ValidateGraph(sample).renderer_requirements.empty(),"unknown sampler cannot qualify renderer");Reject([&]{ValidateGraph(sample,true);});
        sample["nodes"]["C"]["props"]["SamplerType"]="SAMPLERTYPE_Color";Check(ValidateGraph(sample,true).nodes==1,"explicit sampler passes file contract");
        auto custom=graph;custom["nodes"]["C"]=Object({{"type","Custom"},{"inputs",Json::MakeObject()},{"props",Object({{"Code","return A;"},{"OutputType","CMOT_Float1"},{"Inputs",Array({Object({{"InputName","A"}})})}})}});Reject([&]{ValidateGraph(custom);});
        std::map<std::string,Value> empty;Reject([&]{StageAtom(Object({{"ref","X1"}}),empty);});Reject([&]{StageAtom(Object({{"float_bits","ffff"}}),empty);});Reject([&]{StageAtom(Object({{"integer","--1"}}),empty);});
        const auto negativeInf=StageAtom(Object({{"float_bits","fff0000000000000"}}),empty);Check(negativeInf.kind==Value::Float&&std::isinf(negativeInf.real)&&negativeInf.real<0,"typed nonfinite formula literal retained before policy fold");
        std::cout<<"native material boundary checks="<<checks<<"\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}return 0;
}
