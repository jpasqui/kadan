#include "../core/material_forward_lighting.h"
#include <iostream>
using namespace P4MapKit::CustomMaterial;using namespace P4MapKit::CustomMaterial::Lighting;
int main(){try{
    ShaderBindings header;header.layout_hashes={0x166705f7,0,0x0d480502};header.resource_bits=5;
    Emitter emitter;PolicyEmitter policy(emitter,header);
    ClusteredLocalInputs inputs{F32({0,0,0}),F32({0,0}),F32({1}),F32({1,1,1,1}),F32({1,1,1,1}),F32({1}),F32({1}),F32({1}),ForwardWordConstant(7)};
    SurfaceInputs surface{F32({0,0,1}),F32({0,0,1}),F32({.4}),F32({.2,.3,.4}),F32({.04,.05,.06}),F32({1}),F32({1})};
    int checks=0;
    for(const auto& initial:std::vector<SurfaceLighting>{{F32({0,0}),F32({0,0,0})},{F32({0,0,0}),F32({0,0,0,0})},{{"",3},F32({0,0,0})}}){
        bool refused=false;try{EmitClusteredLocalLighting(policy,{0,0,0,0},inputs,surface,nullptr,&initial);}catch(const std::runtime_error& error){const std::string reason=error.what();refused=reason.find("initial")!=std::string::npos;}
        if(!refused||!emitter.lines.empty())throw std::runtime_error("initial accumulator shape refusal failed or emitted partial program");++checks;
    }
    const SurfaceLighting initial{F32({0,0,0}),F32({0,0,0})};
    for(const auto& ao:std::vector<Value>{F32({.2,.8}),{"",1}}){bool refused=false;try{EmitClusteredLocalLighting(policy,{0,0,0,0},inputs,surface,nullptr,&initial,&ao);}catch(const std::runtime_error& error){refused=std::string(error.what()).find("occlusion")!=std::string::npos;}if(!refused||!emitter.lines.empty())throw std::runtime_error("accumulation occlusion shape refusal emitted code");++checks;}
    const Value ao=F32({.2});bool refused=false;try{EmitClusteredLocalLighting(policy,{0,0,0,0},inputs,surface,nullptr,nullptr,&ao);}catch(const std::runtime_error& error){refused=std::string(error.what()).find("occlusion")!=std::string::npos;}if(!refused||!emitter.lines.empty())throw std::runtime_error("accumulation occlusion requires initial accumulators");++checks;
    std::cout<<"forward initial/occlusion refusal checks="<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
