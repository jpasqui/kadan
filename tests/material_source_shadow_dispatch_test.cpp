#include "../core/material_source_shadow_dispatch.h"
#include <iostream>
namespace M=P4MapKit::Material;
static int checked=0;
static void Require(bool value){++checked;if(!value)throw std::runtime_error("shadow dispatch assertion failed");}
int main(){try{
    M::MaterialPreparedProgram camera;camera.BaseBinding.Buffers={{0,"View"},{1,"Material"}};
    camera.Lines={"LDC.F32 R0.x, buf1[304]","MOV.F R0.w, {0.25, 0, 0, 0}","FLR.F.SAT R0.x, R0.x","MAX.F R0.y, R0.x, {0, 0, 0, 0}.x","LDC.F32 R0.z, buf1[308]","MOV.F R0.x, R0.z","MOV.F R0.w, R0.x","MUL.F32 R0.x, R0.w, R0.y","SLT.F32 R0.x, R0.x, {0.100000001, 0, 0, 0}","TRUNC.U.CC HC.x, R0.x","IF NE.x","MOV.U.CC RC.x, {1, 0, 0, 0}","KIL NE.x","ENDIF","MOV.F result_color0, {0, 0, 0, 0}.x"};
    auto shadow=camera;shadow.Lines[3]="MAX.F R0.y, R0.x, {1, 0, 0, 0}.x";
    Require(M::OriginalShadowDitherProgramCandidate(camera,shadow)==M::MaterialOriginalShadowDitherRoute::SampleSpan);
    Require(M::OriginalShadowDitherProgramCandidate(camera,camera)==M::MaterialOriginalShadowDitherRoute::None);
    auto other=shadow;other.Lines[0]="LDC.F32 R0.x, buf1[308]";Require(M::OriginalShadowDitherProgramCandidate(camera,other)==M::MaterialOriginalShadowDitherRoute::None);
    other=shadow;other.Lines[3]="MAX.F R0.y, -R0.x, {1, 0, 0, 0}.x";Require(M::OriginalShadowDitherProgramCandidate(camera,other)==M::MaterialOriginalShadowDitherRoute::None);
    other=shadow;other.Lines[3]="MAX.F R0.y, R0.x, {0.5, 0, 0, 0}.x";Require(M::OriginalShadowDitherProgramCandidate(camera,other)==M::MaterialOriginalShadowDitherRoute::None);
    other=shadow;other.BaseBinding.Buffers[2]="Material";other.Lines[0]="LDC.F32 R0.x, buf2[304]";Require(M::OriginalShadowDitherProgramCandidate(camera,other)==M::MaterialOriginalShadowDitherRoute::SampleSpan);
    auto immediate=camera;immediate.Lines.erase(immediate.Lines.begin()+4,immediate.Lines.begin()+6);auto immediateShadow=immediate;immediateShadow.Lines[3]=shadow.Lines[3];Require(M::OriginalShadowDitherProgramCandidate(immediate,immediateShadow)==M::MaterialOriginalShadowDitherRoute::ImmediateProducts);
    // A final full-coverage MAX is never dispatched as an internal dither factor.
    auto full=camera;full.Lines[7]="MAX.F R0.x, R0.w, {0, 0, 0, 0}.x";auto fullShadow=full;fullShadow.Lines[7]="MAX.F R0.x, R0.w, {1, 0, 0, 0}.x";Require(M::OriginalShadowDitherProgramCandidate(full,fullShadow)==M::MaterialOriginalShadowDitherRoute::None);
    std::cout<<checked<<" shadow dispatch assertions PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
