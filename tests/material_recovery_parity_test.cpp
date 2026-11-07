#include "../core/material_masters.h"
#include "../core/material_uniform_order.h"
#include <iostream>
using namespace P4MapKit;
using namespace P4MapKit::Material;
static unsigned checks=0;
static void Check(bool value,const char* what){++checks;if(!value)throw std::runtime_error(what);}
static FMaterialPreshaderInstruction Instruction(EMaterialPreshaderOpcode op,int parameter=0,float constant=0)
{FMaterialPreshaderInstruction out;out.Opcode=op;out.Parameter=std::uint16_t(parameter);std::uint32_t bits;std::memcpy(&bits,&constant,4);out.ConstantBits.fill(bits);return out;}
static FMaterialUniformSet Uniforms(bool far=false,bool off=false)
{
    using O=EMaterialPreshaderOpcode;FMaterialUniformSet out;
    for(const auto* name:{far?"CameraFarFadeStart":"CameraNearFadeStart",far?"CameraFarFadeEnd":"CameraNearFadeEnd","CameraFadeSizeScaleMin"}){FMaterialUniformParameter p;p.Name=name;out.Scalars.push_back(p);}
    float scale=.25f;std::memcpy(&out.Scalars[2].DefaultBits[0],&scale,4);
    const auto constant=Instruction(O::Constant,0,1),parameter=Instruction(O::ScalarParameter,2),subtract=Instruction(O::Sub);
    if(off){FMaterialPreshaderProgram p;p.Instructions={Instruction(O::Constant),Instruction(O::Constant),constant,parameter,subtract,Instruction(O::Max),Instruction(O::Mul)};out.ScalarPrograms.push_back(p);}
    else{FMaterialPreshaderProgram end,difference,scaleProgram;end.Instructions={Instruction(O::ScalarParameter,1)};difference.Instructions={Instruction(O::ScalarParameter),Instruction(O::ScalarParameter,1),subtract};scaleProgram.Instructions={constant,parameter,subtract};out.ScalarPrograms={end,difference,scaleProgram};}
    return out;
}
static Json Graph()
{return Json::Parse(R"({"format":2,"domain":"MD_Surface","blend_mode":"BLEND_Translucent","shading_model":"MSM_Unlit","two_sided":false,"clip":0.3333,"tangent_space_normal":true,"nodes":{"C":{"type":"Constant","inputs":{},"props":{"R":0.5}}},"outputs":{"EmissiveColor":["C",""]}})");}
static std::string Program(bool off=false)
{return off?"LDC.F32 R0.x, buf2[0]\n":"LDC.F32 R0.x, buf2[0]\nLDC.F32 R1.x, buf2[4]\nLDC.F32 R2.x, buf2[8]\nDIV.F32.SAT R3.x, R0.x, R1.x\nFLR.F R4.x, R3.x\nMAX.F R4.x, R4.x, R2.x\n";}
static MaterialRecoveryPlan AOPlan(const std::vector<std::string>& tail={"MOV.F R3.x, R2.w","MOV.F result.color, R3.x"})
{
    MaterialRecoveryPlan p;p.Settings=Object({{"shading_model","MSM_DefaultLit"}});p.Lines={"LDC.U64 D0.x, buf0[328]","MOV.F R0, fragment.attrib[0]","TEX.F R1, R0, handle(D0.x), 2D","MOV.F.SAT R2, R1"};p.Stop=p.Lines.size();p.Lines.insert(p.Lines.end(),tail.begin(),tail.end());p.Binding.TextureParameters[0]="Mask_01";p.Binding.AllowUnresolvedSyntheticSamplers=true;
    MaterialOutputRegister output;output.Pin="BaseColor";output.HasConstant=true;output.Components={0};p.Outputs.push_back(output);return p;
}
static MaterialRecoveryPlan DecalPlan(int usage=1,bool otherSetting=true)
{
    FMaterialCookedRecord master;master.Package="/Game/Test/Decal";master.Class="Material";
    const auto field=[&](const char* name,FDecodedValue::EKind kind,const char* value){master.Properties.Fields.emplace_back(name,FDecodedValue::Atom(kind,value));};
    field("MaterialDomain",FDecodedValue::EKind::Enum,"MD_DeferredDecal");field("BlendMode",FDecodedValue::EKind::Enum,"BLEND_Translucent");
    if(usage>=0)field("bUsedWithParticleSprites",FDecodedValue::EKind::Boolean,usage?"true":"false");
    if(otherSetting)field("bAllowNegativeEmissiveColor",FDecodedValue::EKind::Boolean,"true");
    FMaterialCookedSettings cooked;FReadError error;if(!ReadMaterialCookedSettings({master},cooked,error))throw std::runtime_error(error.Code);
    MaterialRecoveryPlan p;p.Settings=MaterialSourceSettings(cooked);p.Binding.Varyings["in_color0"]=0;
    p.Lines={"MOV.F R0.xyz, fragment.attrib[0]","DP3.F R1.x, R0, R0","RSQ.F R1.x, R1.x","MUL.F R2.xyz, R1.x, R0",
        "MOV.F R3.xyz, {0.2,0.3,0.4,0}","MOV.F R4.x, {0.5}","MOV.F R5.x, {0.6}","MOV.F R6.x, {0.7}","MOV.F R7.x, {0.8}","MOV.F result_color0.w, R7.x"};
    return p;
}
static MaterialAnchors DecalAnchors()
{
    MaterialAnchors out;out["stop"]=MaterialAnchor::Constant(9,0);out["Normal"]=MaterialAnchor::Register(9,"R2","xyz");
    out["BaseColor"]=out["EmissiveColor"]=MaterialAnchor::Register(9,"R3","xyz");
    for(const auto& item:std::vector<std::pair<std::string,std::string>>{{"Metallic","R4"},{"Specular","R5"},{"Roughness","R6"}})out[item.first]=MaterialAnchor::Register(9,item.second,"x");
    return out;
}
static int Run(int argc,char** argv)
{
    for(const auto* mode:{"DBM_Emissive","DBM_Stain"}){
        const auto decal=RecoverDecalMaterialExpressionGraph(DecalPlan(),DecalAnchors(),mode);const auto& render=decal.Graph.At("render_settings");
        Check(render.Has("bUsedWithParticleSprites")&&render.At("bUsedWithParticleSprites").boolean,"decal recovery retains serialized sprite usage");
        Check(render.Has("bAllowNegativeEmissiveColor")&&render.At("bAllowNegativeEmissiveColor").boolean,"decal recovery retains another cooked renderer setting");
        Check(render.At("DecalBlendMode").Text()==mode,"decal recovery applies its source blend contract");
    }
    for(int usage:{-1,0}){const auto decal=RecoverDecalMaterialExpressionGraph(DecalPlan(usage),DecalAnchors(),"DBM_Emissive");Check(!decal.Graph.At("render_settings").Has("bUsedWithParticleSprites"),"decal recovery does not invent absent or false usage");}
    auto noRender=DecalPlan(-1,false);Check(!noRender.Settings.Has("render_settings"),"fixture starts without render settings");
    const auto decal=RecoverDecalMaterialExpressionGraph(noRender,DecalAnchors(),"DBM_Stain");Check(decal.Graph.At("render_settings").object.size()==1,"decal recovery creates only its blend setting when none existed");
    auto malformed=DecalPlan();malformed.Settings["render_settings"]=Json::MakeArray();bool rejected=false;
    try{RecoverDecalMaterialExpressionGraph(malformed,DecalAnchors(),"DBM_Emissive");}catch(const Refused& error){rejected=error.name=="graph field type";}Check(rejected,"decal recovery refuses malformed renderer settings");
    const auto nearUniforms=Uniforms(),farUniforms=Uniforms(true),offUniforms=Uniforms(false,true);BindingContext context;context.Buffers={{0,"View"},{2,"Material"}};context.Uniforms=nearUniforms;
    const auto near=ReadMaterialCameraFadeVariant(Program(),context);Check(near.Matched&&near.Near&&!near.Far,"near fade words");
    context.Uniforms=farUniforms;const auto far=ReadMaterialCameraFadeVariant(Program(),context);Check(far.Matched&&!far.Near&&far.Far,"far fade words");
    context.Uniforms=offUniforms;const auto off=ReadMaterialCameraFadeVariant(Program(true),context);Check(off.Matched&&!off.Near&&!off.Far,"folded off words");
    context.Uniforms=nearUniforms;
    Check(!ReadMaterialCameraFadeVariant("LDC.F32 R0.x, buf2[0]\nLDC.F32 R1.x, buf2[4]\nLDC.F32 R2.x, buf2[8]\n",context).Matched,"enabled fade requires DIV/floor/max shape");
    Check(!ReadMaterialCameraFadeVariant(Program()+"LDC.F32 R4.x, buf0[2440]\n",context).Matched,"game time forbids fade inference");
    context.Buffers[3]="MaterialCollection0";Check(!ReadMaterialCameraFadeVariant(Program()+"LDC.F32 R4.x, buf3[0]\n",context).Matched,"collection forbids fade inference");context.Buffers.erase(3);
    const auto words=MaterialCameraFadeWords("LDC.F32 R0.z, buf2[8]\nLDC.F R1.yw, buf2[16]\nLDC.F32 R2.x, buf12[0]\n",2);Check(words==std::set<int>({8,20,28}),"scalar/vector word offsets and buffer identity");
    Check(!MaterialCameraFadeInstructionShape("MAX.F R0.x, R0.x, R1.x\nFLR.F R0.x, R0.x\nDIV.F32.SAT R0.x, R0.x, R1.x\n"),"instruction order matters");
    SymbolicArena arena;const auto scale=arena.op("SUB",{1.0,source(arena,"ScalarParameter","CameraFadeSizeScaleMin")});
    Check(!MatchMaterialCameraFadeWords(arena,{scale}).Matched,"scale alone is not the fade");
    Binder binder(arena,context);std::vector<Value> extra;for(int at:{0,4,8})extra.push_back(binder.material_word(at));extra.push_back(source(arena,"ScalarParameter","WPORadius"));Check(!MatchMaterialCameraFadeWords(arena,extra).Matched,"unrelated vertex parameter forbids fade inference");
    const auto ng=AddMaterialCameraFadeGraph(Graph(),near,nearUniforms),fg=AddMaterialCameraFadeGraph(Graph(),far,farUniforms),og=AddMaterialCameraFadeGraph(Graph(),off,offUniforms);
    Check(ValidateGraph(ng).node_types.count("Distance")==1,"recovered Distance is an admitted stock node");Check(ValidateGraph(og).node_types.count("Distance")==0,"off fade stays uniform");
    bool defaultFound=false;for(const auto& item:ng.At("nodes").object)if(item.second.At("type").Text()=="ScalarParameter"&&item.second.At("props").At("ParameterName").Text()=="CameraFadeSizeScaleMin")defaultFound=item.second.At("props").At("DefaultValue").Numeric()==.25;Check(defaultFound,"cooked scalar defaults");
    MaterialMasterPool pool;const auto zero=pool.Default("WorldPositionOffset");Check(pool.Nodes.At(zero.array[0].Text()).At("props").At("Constant").Dump()=="[0,0,0]","owner without WPO supplies zero");
    auto plan=AOPlan();auto bound=BindMaterialOutputs(plan);Check(AppendUnclaimedMaterialAmbientOcclusion(plan,bound),"one unclaimed live saturated channel is AO");Check(bound.Pins.back()=="AmbientOcclusion"&&IsExpr(bound.Values.back()[0],"COMP")&&Args(bound.Values.back()[0])[1].number()==3,"AO keeps texture alpha and removes UE's engine clamp");
    plan=AOPlan({"ADD.F R3.x, R2.w, R2.z","MOV.F result.color, R3.x"});bound=BindMaterialOutputs(plan);Check(!AppendUnclaimedMaterialAmbientOcclusion(plan,bound),"two unclaimed live channels stay ambiguous");
    plan=AOPlan({"MOV.F R2.w, {0}","MOV.F result.color, R2.w"});bound=BindMaterialOutputs(plan);Check(!AppendUnclaimedMaterialAmbientOcclusion(plan,bound),"dead saturated channel is not AO");
    plan=AOPlan();plan.Outputs[0].HasConstant=false;plan.Outputs[0].Register="R2";plan.Outputs[0].Components={3};bound=BindMaterialOutputs(plan);Check(!AppendUnclaimedMaterialAmbientOcclusion(plan,bound),"claimed material channel is not AO");
    plan=AOPlan({"ADD.F R3.x, R2.w, R4.x","MOV.F result.color, R3.x"});plan.Lines.insert(plan.Lines.begin()+4,"MOV.F R4.x, R2.w");++plan.Stop;bound=BindMaterialOutputs(plan);Check(AppendUnclaimedMaterialAmbientOcclusion(plan,bound),"duplicate carriers of one formula remain one AO candidate");
    const auto conditional=ParseMaterialAnchorInstructions({"IF NE.x","MOV.F R2.w, {0}","ENDIF","MOV.F result.color, R2.w"});Check(MaterialSurfaceLiveAt(conditional,0).count({"R2",'w'})==1,"conditional write preserves liveness");
    const auto loop=ParseMaterialAnchorInstructions({"REP {2}","MOV.F R1.x, R2.w","MOV.F R2.w, R0.x","ENDREP","MOV.F result.color, R1.x"});Check(MaterialSurfaceLiveAt(loop,0).count({"R2",'w'})==1,"loop-carried liveness reaches fixed point");
    if(argc>1&&std::string(argv[1])=="--graphs"){std::cout<<Object({{"near",ng},{"far",fg},{"off",og}}).Dump()<<'\n';return 0;}
    std::cout<<checks<<" material recovery parity checks passed\n";
    return 0;
}
int main(int argc,char** argv){try{return Run(argc,argv);}catch(const std::exception& e){std::cerr<<"after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
