// The recovered master graph rewrites (material_graph_canon.h) against values material_graph_canon.py and
// material_uniform_order.py give for the same graphs. Every master is compared with the Python by
// tools/navigation/material_graph_canon_parity.py; these are the small cases that pin each piece.
#include "../core/material_graph_canon.h"
#include "../core/material_game_reads.h"
#include <cstdlib>
#include <iostream>
using namespace P4MapKit::Material;
static unsigned checks=0;
static void Check(bool value,const char* what){++checks;if(!value){std::cerr<<"graph rewrite check failed: "<<what<<'\n';std::exit(1);}}
static std::vector<std::string> List(const std::vector<std::string>& v){return v;}
int main(){
    // Python's random.Random(7) and (11): uniform, choice and random draw the same numbers.
    Canon::PyRandom seven(7);
    Check(seven.Uniform(-2.0,2.0)==-0.7046689406673505,"seed 7 first uniform");
    Check(seven.Uniform(-2.0,2.0)==-1.3966033043019923,"seed 7 second uniform");
    Check(seven.Uniform(-2.0,2.0)==0.603737892159415,"seed 7 third uniform");
    Check(seven.Below(3)==0,"seed 7 choice");Check(seven.Random()==0.8212742919913083,"seed 7 random");
    Canon::PyRandom eleven(11);Check(eleven.Random()==0.4523795535098186,"seed 11 random");Check(eleven.Below(3)==2,"seed 11 choice");
    // '%g' and repr as Python writes them.
    Check(PyG(1e6)=="1e+06"&&PyG(0.5)=="0.5"&&PyG(-1)=="-1"&&PyG(6.283185307179586)=="6.28319","%g");
    Check(PyRepr("Heightfield")=="'Heightfield'"&&PyRepr("it's")=="\"it's\"","repr");

    const auto rim=Json::Parse(R"({"format":2,"domain":"MD_Surface","blend_mode":"BLEND_Translucent","shading_model":"MSM_DefaultLit","two_sided":false,"clip":0.3333,"tangent_space_normal":true,
        "nodes":{"X":{"type":"ScalarParameter","inputs":{},"props":{"ParameterName":"Rim","DefaultValue":0.2}},"P":{"type":"ScalarParameter","inputs":{},"props":{"ParameterName":"Power","DefaultValue":2.0}},
        "Neg":{"type":"Multiply","inputs":{"A":["X",""],"B":["MinusOne",""]},"props":{}},"MinusOne":{"type":"Constant","inputs":{},"props":{"R":-1.0}},
        "NegP":{"type":"Multiply","inputs":{"A":["Neg",""],"B":["P",""]},"props":{}},"Sum":{"type":"Add","inputs":{"A":["NegP",""],"B":["P",""]},"props":{}},
        "Rimmed":{"type":"Add","inputs":{"A":["X",""],"B":["Sum",""]},"props":{}}},"outputs":{"EmissiveColor":["Rimmed",""],"Opacity":["P",""]}})");
    // The uniform expression set UE makes: Add/Mul hash both operands first, constants are never accessed.
    const auto predicted=PredictUniformSet(CanonGraphFrom(rim));
    Check(predicted.Vectors.empty(),"rim vectors");
    Check(predicted.Scalars==List({"Rim","(Rim * (-1, -1, -1, -1))","Power","((Rim * (-1, -1, -1, -1)) * Power)","(((Rim * (-1, -1, -1, -1)) * Power) + Power)",
        "(Rim + (((Rim * (-1, -1, -1, -1)) * Power) + Power))","RefractionDepthBias"}),"rim scalars in access order");
    // x + (-x) p + p is UE's Lerp(x, 1, p); without the game's set every rewrite is kept.
    const MaterialCanonGame none;
    Check(CanonMaterialGraph(rim,none).Dump()==Json::Parse(R"({"blend_mode":"BLEND_Translucent","clip":0.3333,"domain":"MD_Surface","format":2,"nodes":{
        "P":{"inputs":{},"props":{"DefaultValue":2.0,"ParameterName":"Power"},"type":"ScalarParameter"},
        "Rimmed":{"inputs":{"A":["X",""],"Alpha":["P",""]},"props":{"ConstB":1.0},"type":"LinearInterpolate"},
        "X":{"inputs":{},"props":{"DefaultValue":0.2,"ParameterName":"Rim"},"type":"ScalarParameter"}},"outputs":{"EmissiveColor":["Rimmed",""],"Opacity":["P",""]},
        "shading_model":"MSM_DefaultLit","tangent_space_normal":true,"two_sided":false})").Dump(),"rim as a Lerp");

    const auto scroll=Json::Parse(R"({"format":2,"domain":"MD_Surface","blend_mode":"BLEND_Translucent","shading_model":"MSM_DefaultLit","two_sided":false,"clip":0.3333,"tangent_space_normal":true,
        "nodes":{"UV":{"type":"TextureCoordinate","inputs":{},"props":{"CoordinateIndex":0}},
        "U":{"type":"ScalarParameter","inputs":{},"props":{"ParameterName":"U","DefaultValue":0.1}},"V":{"type":"ScalarParameter","inputs":{},"props":{"ParameterName":"V","DefaultValue":0.2}},
        "Speed":{"type":"AppendVector","inputs":{"A":["U",""],"B":["V",""]},"props":{}},"Time":{"type":"Time","inputs":{},"props":{}},
        "Step":{"type":"Multiply","inputs":{"A":["Time",""],"B":["Speed",""]},"props":{}},"Moved":{"type":"Add","inputs":{"A":["UV",""],"B":["Step",""]},"props":{}},
        "Tex":{"type":"TextureSampleParameter2D","inputs":{"Coordinates":["Moved",""]},"props":{"ParameterName":"Base","Texture":"/Game/T_Base.T_Base","SamplerSource":"SSM_FromTextureAsset"}}},
        "outputs":{"BaseColor":["Tex","RGB"],"Opacity":["Tex","A"]}})");
    // A printf argument list is accessed right to left: append(U, V) reads V first.
    const auto scrolled=PredictUniformSet(CanonGraphFrom(scroll));
    Check(scrolled.Vectors==List({"append1(U, V)"})&&scrolled.Scalars==List({"V","U","RefractionDepthBias"}),"scroll set");
    Check(scrolled.Textures.size()==1&&scrolled.Textures[0].first=="Standard2D"&&scrolled.Textures[0].second.size()==1&&scrolled.Textures[0].second[0].Name=="Base","scroll textures");
    // The scroll becomes UE's Panner.
    const auto canon=CanonMaterialGraph(scroll,none);
    Check(canon.At("nodes").At("Moved").At("type").Text()=="Panner","scroll as a Panner");
    Check(!canon.At("nodes").Has("Step"),"the expanded scroll is gone");
    // The game's XluCut clips around AmbientOcclusion with the two scalars of the decompiler's stand-in mask (an If on
    // the world height less a scalar, chained to an If on the enable scalar), which the clip replaces.
    const auto cut=Json::Parse(R"({"format":2,"domain":"MD_Surface","blend_mode":"BLEND_Translucent","shading_model":"MSM_DefaultLit","two_sided":false,"clip":0.3333,"tangent_space_normal":true,
        "render_settings":{"C4MaterialParam3":"C4XC_Enable"},"nodes":{
        "E":{"type":"ScalarParameter","inputs":{},"props":{"ParameterName":"bEnableXluCut","DefaultValue":0.0}},
        "H":{"type":"ScalarParameter","inputs":{},"props":{"ParameterName":"XluCutWaterHeight","DefaultValue":0.0}},
        "P":{"type":"WorldPosition","inputs":{},"props":{}},
        "One":{"type":"Constant","inputs":{},"props":{"R":1.0}},"Zero":{"type":"Constant","inputs":{},"props":{"R":0.0}},
        "Below":{"type":"Subtract","inputs":{"A":["P","B"],"B":["H",""]},"props":{}},
        "On":{"type":"If","inputs":{"A":["One",""],"B":["E",""],"AGreaterThanB":["E",""],"ALessThanB":["One",""]},"props":{}},
        "Mask":{"type":"If","inputs":{"A":["Below",""],"B":["Zero",""],"AGreaterThanB":["One",""],"ALessThanB":["On",""]},"props":{}},
        "Alpha":{"type":"Constant","inputs":{},"props":{"R":0.5}},
        "Masked":{"type":"Multiply","inputs":{"A":["Alpha",""],"B":["Mask",""]},"props":{}}},
        "outputs":{"Opacity":["Masked",""]}})");
    const auto clipped=CanonMaterialGraph(cut,none);
    const auto& clip=clipped.At("nodes").At(clipped.At("outputs").At("AmbientOcclusion").array[0].Text());
    Check(clip.At("props").At("Code").Text()==Canon::XluCutCode(),"XluCut on the AO pin");
    Check(clip.At("inputs").At("Enable").array[0].Text()=="E"&&clip.At("inputs").At("Height").array[0].Text()=="H","XluCut scalars from the stand-in");
    Check(!clipped.At("nodes").Has("Mask")&&!clipped.At("nodes").Has("On"),"the stand-in mask is replaced");
    // A graph with the flag and neither a recovered clip nor its stand-in has no form: the scalars are not invented.
    auto bare=scroll;bare["render_settings"]=Json::Parse(R"({"C4MaterialParam3":"C4XC_Enable"})");
    bool refused=false;
    try{CanonMaterialGraph(bare,none);}catch(const MaterialCanonError&){refused=true;}
    Check(refused,"XluCut without its scalars refused");
    Check(KadanGameReads(Json::Parse(R"({"domain":"MD_Surface","blend_mode":"BLEND_Opaque","nodes":{"c":{"type":"Custom","props":{"Code":"return ResolvedView.C4GlobalClippingPlane.x;"}}}})"))
        .At("nodes").At("c").At("type").Text()=="KadanGameCustom","XluCut Custom is a game read");

    // With the game's set, a form is kept only where it brings the set closer: a * (1 / x) as the game's a / x.
    const auto reciprocal=Json::Parse(R"({"format":2,"domain":"MD_Surface","blend_mode":"BLEND_Opaque","shading_model":"MSM_Unlit","two_sided":false,"clip":0.3333,"tangent_space_normal":true,
        "nodes":{"A":{"type":"ScalarParameter","inputs":{},"props":{"ParameterName":"A","DefaultValue":1.0}},"X":{"type":"ScalarParameter","inputs":{},"props":{"ParameterName":"X","DefaultValue":2.0}},
        "One":{"type":"Constant","inputs":{},"props":{"R":1.0}},"R":{"type":"Divide","inputs":{"A":["One",""],"B":["X",""]},"props":{}},
        "M":{"type":"Multiply","inputs":{"A":["A",""],"B":["R",""]},"props":{}}},"outputs":{"EmissiveColor":["M",""]}})");
    Check(PredictUniformSet(CanonGraphFrom(reciprocal)).Scalars==List({"X","A","((1, 1, 1, 1) / X)","(A * ((1, 1, 1, 1) / X))"}),"reciprocal set");
    const auto set=MaterialCanonGameFromJson(Json::Parse(R"J({"uniform_set":{"vectors":[],"scalars":["A","X","(A / X)"],"textures":{},"scalar_defaults":{"A":1.0,"X":2.0},"vector_defaults":{}}})J"));
    Check(CanonMaterialGraph(reciprocal,set).Dump()==Json::Parse(R"({"blend_mode":"BLEND_Opaque","clip":0.3333,"domain":"MD_Surface","format":2,"nodes":{
        "A":{"inputs":{},"props":{"DefaultValue":1.0,"ParameterName":"A"},"type":"ScalarParameter"},"M":{"inputs":{"A":["A",""],"B":["X",""]},"props":{},"type":"Divide"},
        "X":{"inputs":{},"props":{"DefaultValue":2.0,"ParameterName":"X"},"type":"ScalarParameter"}},"outputs":{"EmissiveColor":["M",""]},
        "shading_model":"MSM_Unlit","tangent_space_normal":true,"two_sided":false})").Dump(),"the game's division");

    // Preshader text: parsed and evaluated as UE's preshader computes it.
    const auto tree=UniformOrder::Parse("(append1(U, V) * (2, 2, 2, 2)).rg",{"U","V"});
    Check(tree.Kind=="swz"&&tree.Name=="rg"&&tree.Args[0].Kind=="op"&&tree.Args[0].Args[0].Kind=="append"&&tree.Args[0].Args[1].Kind=="const","parse");
    const auto value=UniformOrder::Evaluate(UniformOrder::Parse("max(frac((U / (0, 0, 0, 0))), (0.5, 0.5, 0.5, 0.5))",{"U"}),{{"U",3.25}},{});
    Check(value.size()==1&&value[0]==0.5,"evaluate with UE's guarded division");

    // Customized UVs from the game's instanced vertex program: per-instance data with its default, and a constant.
    const std::string program="MIN.S R1.x, R0, {3, 0, 0, 0};\nTXF.F R2.x, R1.x, handle(D0.y), BUFFER;\nELSE;\nMOV.F R2.x, {0.25, 0, 0, 0}.x;\nENDIF;\n"
        "MOV.F result.attrib[3].x, R2.x;\nMOV.F result.attrib[3].y, {0.5, 0, 0, 0}.x;\n";
    const auto uvs=Canon::InstanceCustomUvs(program,{{"out_texcoord1",3}});
    Check(uvs.size()==1&&uvs.count(2)&&uvs.at(2)[0].Kind==Canon::UvPart::CustomData&&uvs.at(2)[0].Index==3&&uvs.at(2)[0].Value==0.25&&
        uvs.at(2)[1].Kind==Canon::UvPart::Fixed&&uvs.at(2)[1].Value==0.5,"instance custom data UV");

    // Rewrites added by the research route must run in the production import as well.
    const auto guided=[](std::vector<std::string> vectors,std::vector<std::string> scalars){MaterialCanonGame game;game.Uniforms.Known=true;game.Uniforms.Vectors=std::move(vectors);game.Uniforms.Scalars=std::move(scalars);return game;};
    auto coat=CanonGraphFrom(Json::Parse(R"({"shading_model":"MSM_ClearCoat","nodes":{
        "C":{"type":"ScalarParameter","props":{"ParameterName":"ClearCoat_Scale"}},"Sat":{"type":"Saturate","inputs":{"Input":["C",""]}}},"outputs":{"ClearCoat":["Sat",""]}})"));
    Check(Canon::ClearCoatSaturate(coat,guided({}, {"ClearCoat_Scale"}))==1,"clear coat engine saturation removed");
    Check(PredictUniformSet(coat).Scalars==List({"ClearCoat_Scale"}),"clear coat scalar set");
    auto lerp=CanonGraphFrom(Json::Parse(R"({"blend_mode":"BLEND_Translucent","shading_model":"MSM_Unlit","nodes":{
        "Lo":{"type":"ScalarParameter","props":{"ParameterName":"Opacity_RimMin"}},"Hi":{"type":"ScalarParameter","props":{"ParameterName":"Opacity_Intensity"}},
        "Cam":{"type":"CameraVectorWS"},"P":{"type":"ComponentMask","inputs":{"Input":["Cam",""]},"props":{"R":true}},
        "D":{"type":"Subtract","inputs":{"A":["Hi",""],"B":["Lo",""]}},"M":{"type":"Multiply","inputs":{"A":["P",""],"B":["D",""]}},
        "S":{"type":"Add","inputs":{"A":["Lo",""],"B":["M",""]}}},"outputs":{"Opacity":["S",""]}})"));
    const auto lerpOriginal=lerp;const auto lerpGame=guided({}, {"Opacity_Intensity","Opacity_RimMin","RefractionDepthBias"});
    Check(Canon::LerpDifference(lerp,lerpGame)==1&&lerp.Nodes.Find("S")->Type=="LinearInterpolate","expanded parameter lerp");
    Check(PredictUniformSet(lerp).Scalars==lerpGame.Uniforms.Scalars,"lerp removes folded endpoint difference");
    auto vectorAlpha=lerpOriginal;vectorAlpha.Nodes.Find("M")->Inputs.Set("A",MakeLink("Cam",""));
    Check(Canon::LerpDifference(vectorAlpha,none)==0,"vector alpha prevents scalar lerp rewrite");

    auto channels=CanonGraphFrom(Json::Parse(R"({"shading_model":"MSM_Unlit","nodes":{
        "U":{"type":"ScalarParameter","props":{"ParameterName":"CenterU"}},"V":{"type":"ScalarParameter","props":{"ParameterName":"CenterV"}},"As":{"type":"ScalarParameter","props":{"ParameterName":"Aspect"}},
        "UV":{"type":"TextureCoordinate","props":{"CoordinateIndex":0}},"Su":{"type":"Subtract","inputs":{"A":["UV","R"],"B":["U",""]}},
        "Sv":{"type":"Subtract","inputs":{"A":["UV","G"],"B":["V",""]}},"Mu":{"type":"Multiply","inputs":{"A":["Su",""],"B":["As",""]}},
        "A2":{"type":"Multiply","inputs":{"A":["Mu",""],"B":["Mu",""]}},"B2":{"type":"Multiply","inputs":{"A":["Sv",""],"B":["Sv",""]}},
        "Sum":{"type":"Add","inputs":{"A":["B2",""],"B":["A2",""]}}},"outputs":{"EmissiveColor":["Sum",""]}})"));
    const auto channelGame=guided({"append1(CenterU, CenterV)","append1(Aspect, (1, 1, 1, 1))"},{"CenterV","CenterU","Aspect"});
    Check(Canon::ChannelPairs(channels,channelGame)==2,"paired channels and identity channel form vector operations");
    Check(PredictUniformSet(channels).Vectors==channelGame.Uniforms.Vectors,"paired channel set");

    const auto scaledJson=Json::Parse(R"({"shading_model":"MSM_Unlit","nodes":{
        "U":{"type":"ScalarParameter","props":{"ParameterName":"Indirect Tiling U"}},"V":{"type":"ScalarParameter","props":{"ParameterName":"Indirect Tiling V"}},
        "D":{"type":"DynamicParameter","props":{"ParameterIndex":0}},"Ap":{"type":"AppendVector","inputs":{"A":["U",""],"B":["V",""]}},
        "Dd":{"type":"AppendVector","inputs":{"A":["D","",2],"B":["D","",2]}},"M":{"type":"Multiply","inputs":{"A":["Ap",""],"B":["Dd",""]}},
        "UV":{"type":"TextureCoordinate","props":{"CoordinateIndex":0}},"S":{"type":"Add","inputs":{"A":["UV",""],"B":["M",""]}},
        "R":{"type":"ComponentMask","inputs":{"Input":["S",""]},"props":{"R":true}}},"outputs":{"EmissiveColor":["R",""]}})");
    auto scaled=CanonGraphFrom(scaledJson);const auto scaledGame=guided({}, {"Indirect Tiling U","Indirect Tiling V"});
    Check(Canon::ScaledAppends(scaled,scaledGame)==1,"dynamic scalar distributed into parameter append");
    Check(PredictUniformSet(scaled).Vectors.empty()&&PredictUniformSet(scaled).Scalars==scaledGame.Uniforms.Scalars,"scaled append set");
    auto keepAppend=CanonGraphFrom(scaledJson);
    Check(Canon::ScaledAppends(keepAppend,guided({"append1(Indirect Tiling U, Indirect Tiling V)"},scaledGame.Uniforms.Scalars))==0,"game append prevents distribution");
    Check(Canon::ScaledAppends(keepAppend,none)==0,"scaled append requires game evidence");
    auto invalidOutput=CanonGraphFrom(scaledJson);invalidOutput.Nodes.Find("Dd")->Inputs.Set("A",Json(std::vector<Json>{Json("D"),Json(""),Json(4)}));
    invalidOutput.Nodes.Find("Dd")->Inputs.Set("B",*invalidOutput.Nodes.Find("Dd")->Inputs.Find("A"));
    Check(Canon::ScaledAppends(invalidOutput,scaledGame)==0,"invalid dynamic output prevents distribution");

    auto smooth=CanonGraphFrom(Json::Parse(R"({"shading_model":"MSM_Unlit","nodes":{
        "Hi":{"type":"ScalarParameter","props":{"ParameterName":"Edge High"}},"Lo":{"type":"ScalarParameter","props":{"ParameterName":"Edge Low"}},
        "Cam":{"type":"CameraVectorWS"},"D":{"type":"ComponentMask","inputs":{"Input":["Cam",""]},"props":{"R":true}},
        "W":{"type":"Subtract","inputs":{"A":["Hi",""],"B":["Lo",""]}},"Q":{"type":"Divide","inputs":{"A":["D",""],"B":["W",""]}},
        "T":{"type":"Saturate","inputs":{"Input":["Q",""]}},"TT":{"type":"Multiply","inputs":{"A":["T",""],"B":["T",""]}},
        "M2":{"type":"Multiply","inputs":{"A":["T",""]},"props":{"ConstB":-2}},"P":{"type":"Add","inputs":{"A":["M2",""]},"props":{"ConstB":3}},
        "S":{"type":"Multiply","inputs":{"A":["TT",""],"B":["P",""]}}},"outputs":{"EmissiveColor":["S",""]}})"));
    Check(Canon::SmoothstepForm(smooth,guided({}, {"Edge Low","Edge High"}))==1&&smooth.Nodes.Find("S")->Type=="SmoothStep","wetness polynomial becomes smoothstep");
    Check(PredictUniformSet(smooth).Scalars==List({"Edge Low","Edge High"}),"smoothstep removes folded edge difference");
    std::cout<<checks<<" graph rewrite checks passed\n";
}
