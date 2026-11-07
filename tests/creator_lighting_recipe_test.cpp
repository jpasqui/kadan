#include "../core/creator_lighting_recipe.h"
#include <cstdio>
#include <limits>
#include <cstdlib>
using namespace P4MapKit::CreatorLighting;
static unsigned Checks=0;
static void Check(bool V) {++Checks;if(!V){std::fprintf(stderr,"failed %u\n",Checks);std::exit(1);}}
int main() {
    Recipe R;std::string Why;std::vector<std::uint8_t> Before,After;
    Check(Canonical(R,Before,Why));Check(!Before.empty());
    Recipe Same=R;Check(Canonical(Same,After,Why));Check(After==Before);
    Same.SunRotation[1]=-0.0f;Check(Canonical(Same,After,Why));Check(After!=Before);
    for(float Bad:{std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        for(unsigned Field=0;Field<17;++Field) {
            Recipe C=R;float* F[]={&C.SunRotation[0],&C.SunRotation[1],&C.SunRotation[2],&C.SunIntensity,&C.SunIndirectIntensity,&C.SunSourceAngle,
                &C.SkyIntensity,&C.SkyIndirectIntensity,&C.SkyDistanceThreshold,&C.StaticLightingLevelScale,&C.IndirectLightingQuality,
                &C.IndirectLightingSmoothness,&C.EnvironmentColor[0],&C.EnvironmentColor[1],&C.EnvironmentColor[2],&C.EnvironmentIntensity,&C.SunRotation[0]};
            *F[Field]=Bad;After=Before;Check(!Canonical(C,After,Why));Check(!Why.empty());Check(After==Before);
        }
    }
    for(int32_t Bad:{-1,101,std::numeric_limits<int32_t>::max()}) {
        Recipe C=R;C.IndirectBounces=Bad;After=Before;Check(!Canonical(C,After,Why));Check(After==Before);
        C=R;C.SkyBounces=Bad;Check(!Canonical(C,After,Why));Check(After==Before);
    }
    for(unsigned Alpha=0;Alpha<255;++Alpha) {Recipe C=R;C.SunColor[3]=uint8_t(Alpha);Check(!Validate(C,Why));}
    for(unsigned I=0;I<1000;++I) {
        Recipe C=R;C.SunIntensity=float(I)+0.25f;Check(Canonical(C,After,Why));Check(After!=Before);
        C.SunIntensity=-float(I)-0.25f;After=Before;Check(!Canonical(C,After,Why));Check(After==Before);
    }
    Recipe C=R;C.IndirectBounces=100;C.SkyBounces=0;C.DirectSun=true;C.CaptureEmissiveOnly=true;
    Check(Canonical(C,After,Why));Check(After!=Before);
    std::printf("creator lighting recipe: %u checks passed\n",Checks);
}
