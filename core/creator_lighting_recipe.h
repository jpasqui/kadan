// Closed creator lighting recipe. This is independent creator input, not a
// recovered game's sun/sky/defaults or proof of a completed lighting build.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace P4MapKit { namespace CreatorLighting {
struct Recipe {
    std::array<float,3> SunRotation{{-45,0,0}};
    std::array<std::uint8_t,4> SunColor{{255,255,255,255}};
    float SunIntensity=3,SunIndirectIntensity=1,SunSourceAngle=0.5357f;
    bool DirectSun=false;
    float SkyIntensity=1,SkyIndirectIntensity=1,SkyDistanceThreshold=150000;
    bool LowerHemisphereBlack=true,CaptureEmissiveOnly=false;
    float StaticLightingLevelScale=1,IndirectLightingQuality=2,IndirectLightingSmoothness=1;
    std::int32_t IndirectBounces=3,SkyBounces=1;
    bool AmbientOcclusion=false,CompressLightmaps=true;
    std::array<float,3> EnvironmentColor{{0,0,0}};
    float EnvironmentIntensity=1;
};
inline bool Validate(const Recipe& R,std::string& Error) {
    const auto Range=[&](float V,float Lo,float Hi,const char* Why) {
        if(!std::isfinite(V)||V<Lo||V>Hi) {Error=Why;return false;}return true;
    };
    for(float V:R.SunRotation) if(!Range(V,-360,360,"creator-lighting-rotation-invalid")) return false;
    if(R.SunColor[3]!=255) {Error="creator-lighting-alpha-unsupported";return false;}
    if(!Range(R.SunIntensity,0,100000,"creator-lighting-sun-intensity-invalid")||
       !Range(R.SunIndirectIntensity,0,100,"creator-lighting-sun-indirect-invalid")||
       !Range(R.SunSourceAngle,0,10,"creator-lighting-sun-angle-invalid")||
       !Range(R.SkyIntensity,0,100000,"creator-lighting-sky-intensity-invalid")||
       !Range(R.SkyIndirectIntensity,0,100,"creator-lighting-sky-indirect-invalid")||
       !Range(R.SkyDistanceThreshold,1,10000000,"creator-lighting-sky-distance-invalid")||
       !Range(R.StaticLightingLevelScale,0.1f,10,"creator-lighting-level-scale-invalid")||
       !Range(R.IndirectLightingQuality,0.1f,10,"creator-lighting-quality-invalid")||
       !Range(R.IndirectLightingSmoothness,0.1f,10,"creator-lighting-smoothness-invalid")||
       !Range(R.EnvironmentIntensity,0,100,"creator-lighting-environment-intensity-invalid")) return false;
    for(float V:R.EnvironmentColor) if(!Range(V,0,1,"creator-lighting-environment-color-invalid")) return false;
    if(R.IndirectBounces<0||R.IndirectBounces>100||R.SkyBounces<0||R.SkyBounces>100) {
        Error="creator-lighting-bounce-count-invalid";return false;
    }
    Error.clear();return true;
}
inline bool Canonical(const Recipe& R,std::vector<std::uint8_t>& Out,std::string& Error) {
    if(!Validate(R,Error)) return false;
    const char Domain[]="p4-creator-lighting-recipe/v1";
    std::vector<std::uint8_t> B(Domain,Domain+sizeof Domain);
    const auto Word=[&](std::uint32_t W) {for(unsigned I=0;I<4;++I) B.push_back(std::uint8_t(W>>(8*I)));};
    const auto Float=[&](float F) {std::uint32_t W=0;static_assert(sizeof F==sizeof W,"32-bit float required");std::memcpy(&W,&F,4);Word(W);};
    for(float V:R.SunRotation) Float(V);
    B.insert(B.end(),R.SunColor.begin(),R.SunColor.end());
    Float(R.SunIntensity);Float(R.SunIndirectIntensity);Float(R.SunSourceAngle);B.push_back(R.DirectSun?1:0);
    Float(R.SkyIntensity);Float(R.SkyIndirectIntensity);Float(R.SkyDistanceThreshold);
    B.push_back(R.LowerHemisphereBlack?1:0);B.push_back(R.CaptureEmissiveOnly?1:0);
    Float(R.StaticLightingLevelScale);Float(R.IndirectLightingQuality);Float(R.IndirectLightingSmoothness);
    Word(std::uint32_t(R.IndirectBounces));Word(std::uint32_t(R.SkyBounces));
    B.push_back(R.AmbientOcclusion?1:0);B.push_back(R.CompressLightmaps?1:0);
    for(float V:R.EnvironmentColor) Float(V);Float(R.EnvironmentIntensity);
    Out=std::move(B);return true;
}
}}
