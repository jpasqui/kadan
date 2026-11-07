#include "../core/material_source_shadow_factor.h"
#include <iostream>
namespace M=P4MapKit::Material;
static int checked=0;
static void Require(bool value){++checked;if(!value)throw std::runtime_error("shadow factor assertion failed");}
template<class F> static void Held(F call,const std::string& name){bool held=false;try{call();}catch(const M::Refused& e){held=e.name==name;}Require(held);}
int main(){try{
    M::MaterialPreparedProgram camera;camera.BaseBinding.Buffers={{0,"View"},{1,"Material"}};
    camera.Lines={"LDC.F32 R0.x, buf1[304]","MOV.F R0.w, {0.25, 0, 0, 0}","FLR.F.SAT R0.y, R0.x","CEIL.F.SAT R0.x, R0.x","MAX.F R0.y, R0.y, {0, 0, 0, 0}.x","MUL.F32 R0.x, R0.w, R0.x","MUL.F32 R0.x, R0.x, R0.y","SLT.F32 R0.x, R0.x, {0.100000001, 0, 0, 0}","TRUNC.U.CC HC.x, R0.x","IF NE.x","MOV.U.CC RC.x, {1, 0, 0, 0}","KIL NE.x","ENDIF","MOV.F result_color0, {0, 0, 0, 0}.x"};
    auto shadow=camera;shadow.Lines[4]="MAX.F R0.y, R0.y, {1, 0, 0, 0}.x";
    const auto proof=M::CertifyOriginalShadowDitherFactor(camera,shadow,.1);Require(proof.FactorLine==4);Require(proof.MaskLine==7);
    Held([&]{M::CertifyOriginalFullShadowCoverage(camera,shadow,.1);},"material shadow coverage anchor");
    Held([&]{M::CertifyOriginalShadowDitherFactor(camera,shadow,.2);},"material shadow factor clip");
    Held([&]{M::CertifyOriginalShadowDitherFactor(camera,shadow,std::numeric_limits<double>::infinity());},"material shadow factor clip");
    auto other=shadow;other.BaseBinding.Buffers={{0,"View"},{2,"Material"}};other.Lines[0]="LDC.F32 R0.x, buf2[304]";M::CertifyOriginalShadowDitherFactor(camera,other,.1);Require(true);
    other.Lines[0]="LDC.F32 R0.x, buf2[308]";Held([&]{M::CertifyOriginalShadowDitherFactor(camera,other,.1);},"material shadow factor prefix");
    for(const auto& line:{"MAX.F R0.y, R0.y, {0, 0, 0, 0}.x","MAX.F R0.y, -R0.y, {1, 0, 0, 0}.x","MAX.F.SAT R0.y, R0.y, {1, 0, 0, 0}.x","MAX.F R0.x, R0.y, {1, 0, 0, 0}.x"}){other=shadow;other.Lines[4]=line;Held([&]{M::CertifyOriginalShadowDitherFactor(camera,other,.1);},"material shadow factor anchor");}
    for(const auto& change:std::vector<std::pair<std::size_t,std::string>>{{2,"FLR.F R0.y, R0.x"},{3,"CEIL.F R0.x, R0.x"},{5,"MUL.F32 R0.x, R0.w, -R0.x"},{6,"MUL.F32 R0.x, R0.x, R0.w"}}){auto changedCamera=camera;changedCamera.Lines[change.first]=change.second;other=changedCamera;other.Lines[4]=shadow.Lines[4];Held([&]{M::CertifyOriginalShadowDitherFactor(changedCamera,other,.1);},"material shadow factor dependency");}
    other=shadow;other.BaseBinding.Buffers[2]="Pass";other.Lines.pop_back();other.Lines.insert(other.Lines.end(),{"LDC.F32X4 R0.w, buf2[432]","MUL.F32 R0.x, fragment.attrib[3], R0.w","MOV.F result_color0, {0, 0, 0, 0}.x","ADD.F32.SAT result.depth.z, R0.x, fragment.attrib[4].x"});M::CertifyOriginalShadowDitherFactor(camera,other,.1);Require(true);
    other.Lines.back()="ADD.F32 result.depth.z, R0.x, fragment.attrib[4].x";Held([&]{M::CertifyOriginalShadowDitherFactor(camera,other,.1);},"material shadow pixel depth tail");
    std::cout<<checked<<" shadow factor assertions PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
