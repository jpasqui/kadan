#include "../core/material_source_shadow_span.h"
#include <iostream>
namespace M=P4MapKit::Material;
static int checked=0;
static void Require(bool value){++checked;if(!value)throw std::runtime_error("shadow span assertion failed");}
template<class F> static void Held(F call,const std::string& name){bool held=false;try{call();}catch(const M::Refused& e){held=e.name==name;}Require(held);}
int main(){try{
    M::MaterialPreparedProgram camera;camera.BaseBinding.Buffers={{0,"View"},{1,"Material"}};
    camera.Lines={"LDC.F32 R0.x, buf1[304]","MOV.F R0.w, {0.25, 0, 0, 0}","FLR.F.SAT R0.x, R0.x","MAX.F R0.y, R0.x, {0, 0, 0, 0}.x","LDC.F32 R0.z, buf1[308]","MOV.F R0.x, R0.z","MOV.F R0.w, R0.x","MUL.F32 R0.x, R0.w, R0.y","SLT.F32 R0.x, R0.x, {0.100000001, 0, 0, 0}","TRUNC.U.CC HC.x, R0.x","IF NE.x","MOV.U.CC RC.x, {1, 0, 0, 0}","KIL NE.x","ENDIF","MOV.F result_color0, {0, 0, 0, 0}.x"};
    auto shadow=camera;shadow.Lines[3]="MAX.F R0.y, R0.x, {1, 0, 0, 0}.x";
    const auto proof=M::CertifyOriginalShadowDitherSpan(camera,shadow,.1);Require(proof.FactorLine==3);Require(proof.MaskLine==8);
    Held([&]{M::CertifyOriginalShadowDitherFactor(camera,shadow,.1);},"material shadow factor anchor");
    Held([&]{M::CertifyOriginalShadowDitherSpan(camera,shadow,.2);},"material shadow span clip");
    Held([&]{M::CertifyOriginalShadowDitherSpan(camera,shadow,std::numeric_limits<double>::quiet_NaN());},"material shadow span clip");
    auto other=shadow;other.BaseBinding.Buffers[2]="Material";other.Lines[0]="LDC.F32 R0.x, buf2[304]";M::CertifyOriginalShadowDitherSpan(camera,other,.1);Require(true);
    other.Lines[0]="LDC.F32 R0.x, buf2[308]";Held([&]{M::CertifyOriginalShadowDitherSpan(camera,other,.1);},"material shadow span prefix");
    for(const auto& line:{"MAX.F R0.y, -R0.x, {1, 0, 0, 0}.x","MAX.F.SAT R0.y, R0.x, {1, 0, 0, 0}.x","MAX.F R0.y, R0.x, {0.5, 0, 0, 0}.x"}){other=shadow;other.Lines[3]=line;Held([&]{M::CertifyOriginalShadowDitherSpan(camera,other,.1);},"material shadow span anchor");}
    for(const auto& change:std::vector<std::pair<std::size_t,std::string>>{{2,"FLR.F R0.x, R0.x"},{4,"MOV.F R0.y, R0.z"},{4,"MOV.F R0.z, R0.y"},{7,"MUL.F32 R0.x, R0.w, -R0.y"},{7,"MUL.F32 R0.x, R0.w, R0.z"}}){auto changed=camera;changed.Lines[change.first]=change.second;other=changed;other.Lines[3]=shadow.Lines[3];Held([&]{M::CertifyOriginalShadowDitherSpan(changed,other,.1);},"material shadow span dependency");}
    other=shadow;other.BaseBinding.Buffers[2]="Pass";other.Lines.pop_back();other.Lines.insert(other.Lines.end(),{"LDC.F32X4 R0.w, buf2[432]","MUL.F32 R0.x, fragment.attrib[3], R0.w","MOV.F result_color0, {0, 0, 0, 0}.x","ADD.F32.SAT result.depth.z, R0.x, fragment.attrib[4].x"});M::CertifyOriginalShadowDitherSpan(camera,other,.1);Require(true);
    other.Lines.back()="ADD.F32 result.depth.z, R0.x, fragment.attrib[4].x";Held([&]{M::CertifyOriginalShadowDitherSpan(camera,other,.1);},"material shadow pixel depth tail");
    std::cout<<checked<<" shadow span assertions PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
