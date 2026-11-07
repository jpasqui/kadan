#include "../core/material_source_complements.h"
#include <iostream>
namespace M=P4MapKit::Material;
using P4MapKit::EMaterialPreshaderOpcode;
static int checked=0;
static void Require(bool value){++checked;if(!value)throw std::runtime_error("complement assertion failed");}
template<class F> static void Refusal(F call,const std::string& name){bool held=false;try{call();}catch(const M::Refused& e){held=e.name==name;}Require(held);}
int main(){try{
    M::MaterialPreparedProgram camera,shadow;camera.BaseBinding.Buffers={{0,"View"},{1,"Material"}};
    camera.Lines={"LDC.F32 R0.x, buf1[176]","MAX.F R0.x, R0, {0, 0, 0, 0}","SLT.F32 R0.x, R0, {0.333299994, 0, 0, 0}","TRUNC.U.CC HC.x, R0","IF NE.x","MOV.U.CC RC.x, {1, 0, 0, 0}","KIL NE.x","ENDIF","MOV.F result_color0, {0, 0, 0, 0}.x"};
    shadow=camera;shadow.BaseBinding.Buffers={{0,"View"},{2,"Material"}};shadow.Lines[0]="LDC.F32 R0.x, buf2[176]";shadow.Lines[1]="MAX.F R0.x, R0, {1, 0, 0, 0}";
    M::CertifyOriginalFullShadowCoverage(camera,shadow,.3333);Require(true);
    Require(M::OriginalShadowUsesCameraMask(camera,camera));Require(!M::OriginalShadowUsesCameraMask(camera,shadow));
    auto changed=shadow;changed.Lines[0]="LDC.F32 R0.x, buf2[180]";Refusal([&]{M::CertifyOriginalFullShadowCoverage(camera,changed,.3333);},"material shadow coverage prefix");
    changed=shadow;changed.Lines[1]="MAX.F R0.x, -R0, {1, 0, 0, 0}";Refusal([&]{M::CertifyOriginalFullShadowCoverage(camera,changed,.3333);},"material shadow coverage anchor");
    changed=shadow;changed.Lines[1]="MAX.F R0.x, R0, {-1, 0, 0, 0}";Refusal([&]{M::CertifyOriginalFullShadowCoverage(camera,changed,.3333);},"material shadow coverage anchor");
    Refusal([&]{M::CertifyOriginalFullShadowCoverage(camera,shadow,.5);},"material shadow camera clip");
    Refusal([&]{M::CertifyOriginalFullShadowCoverage(camera,shadow,2);},"material shadow coverage clip");
    changed=shadow;changed.BaseBinding.Buffers[1]="Pass";changed.Lines.pop_back();changed.Lines.insert(changed.Lines.end(),{"LDC.F32X4 R0.w, buf1[432]","MUL.F32 R0.x, fragment.attrib[3], R0.w","MOV.F result_color0, {0, 0, 0, 0}.x","ADD.F32.SAT result.depth.z, R0.x, fragment.attrib[4].x"});
    M::CertifyOriginalFullShadowCoverage(camera,changed,.3333);Require(true);
    changed.BaseBinding.Buffers[1]="Material";Refusal([&]{M::CertifyOriginalFullShadowCoverage(camera,changed,.3333);},"material shadow pixel depth tail");
    M::MaterialPreparedProgram distortion;distortion.BaseBinding.Buffers={{0,"View"},{2,"Material"}};distortion.Header.layout_hashes={0x166705f7u,0x098f0609u};
    distortion.Header.textures=P4MapKit::CustomMaterial::BuildResourceTable({{1,{{1,0}}}},3);distortion.Header.samplers=P4MapKit::CustomMaterial::BuildResourceTable({{1,{{13,0}}}},3);
    distortion.Lines={"DP3.F32 R0.z, R0, R0","RSQ.F32 R0.z, R0.z","MUL.F32 R0.zw, R0.z, R0.xyxy","LDC.F32 R0.x, buf2[256]","MAD.F32 R1.xy, R0.x, R0.zwzw, -R0.zwzw"};
    Require(M::OriginalRefractionCoefficientByte(distortion)==256);
    auto other=distortion;other.Lines.back()="MAD.F32 R1.xy, R0.x, R0.zwzw, R0.zwzw";Refusal([&]{M::OriginalRefractionCoefficientByte(other);},"material refraction coefficient anchor");
    other=distortion;other.Lines[1]="RSQ.F32 R0.z, R1.z";Refusal([&]{M::OriginalRefractionCoefficientByte(other);},"material refraction normal anchor");
    other=distortion;other.Header.layout_hashes[1]=0;Refusal([&]{M::OriginalRefractionCoefficientByte(other);},"material refraction buffer ABI");
    other=distortion;other.Header.samplers=P4MapKit::CustomMaterial::BuildResourceTable({{1,{{12,0}}}},3);Refusal([&]{M::OriginalRefractionCoefficientByte(other);},"material refraction depth ABI");
    P4MapKit::FMaterialUniformSet uniforms;uniforms.VectorPrograms.resize(9);uniforms.ScalarPrograms.resize(29);uniforms.Scalars.resize(17);uniforms.Scalars[16].Name="XluCutWaterHeight";
    P4MapKit::FMaterialPreshaderInstruction direct;direct.Opcode=EMaterialPreshaderOpcode::ScalarParameter;direct.Parameter=16;uniforms.ScalarPrograms[28].Instructions={direct};
    Require(M::OriginalComplementScalar(uniforms,256)=="XluCutWaterHeight");
    uniforms.ScalarPrograms[28].Instructions.push_back(direct);Refusal([&]{M::OriginalComplementScalar(uniforms,256);},"material complementary scalar program");uniforms.ScalarPrograms[28].Instructions.pop_back();
    uniforms.Scalars[16].LayerIndex=0;Refusal([&]{M::OriginalComplementScalar(uniforms,256);},"material complementary scalar identity");uniforms.Scalars[16].LayerIndex=-1;
    uniforms.Scalars[16].DefaultBits[0]=0x3f800000;Refusal([&]{M::OriginalComplementScalar(uniforms,256);},"material complementary scalar identity");
    Refusal([&]{M::OriginalComplementScalar(uniforms,255);},"material complementary uniform word");
    auto graph=M::Object({{"format",2},{"domain","MD_Surface"},{"blend_mode","BLEND_Masked"},{"shading_model","MSM_DefaultLit"},{"two_sided",false},{"tangent_space_normal",true},{"clip",.3333},{"nodes",M::Json::MakeObject()},{"outputs",M::Json::MakeObject()}});
    graph["nodes"]["camera"]=M::OriginalComplementNode("Constant",M::Json::MakeObject(),M::Object({{"R",0.}}));graph["nodes"]["full"]=M::OriginalComplementNode("Constant",M::Json::MakeObject(),M::Object({{"R",1.}}));graph["nodes"]["switch"]=M::OriginalComplementNode("ShadowReplace",M::Object({{"Default",M::OriginalComplementLink("camera")},{"Shadow",M::OriginalComplementLink("full")}}),M::Json::MakeObject());graph["outputs"]["OpacityMask"]=M::OriginalComplementLink("switch");
    M::ValidateOriginalComplementGraph(graph);Require(true);
    auto badGraph=graph;badGraph["nodes"]["switch"]["inputs"]["Extra"]=M::OriginalComplementLink("full");Refusal([&]{M::ValidateOriginalComplementGraph(badGraph);},"material complementary selector node");
    badGraph=graph;badGraph["nodes"]["switch"]["inputs"]["Shadow"]=M::OriginalComplementLink("switch");Refusal([&]{M::ValidateOriginalComplementGraph(badGraph);},"graph cycle");
    badGraph=graph;badGraph["nodes"]["switch"]["inputs"]["Shadow"]=M::OriginalComplementLink("missing");Refusal([&]{M::ValidateOriginalComplementGraph(badGraph);},"graph dangling link");
    std::cout<<checked<<" complementary assertions PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
