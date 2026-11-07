#include "../core/material_game_reads.h"
#include <iostream>
using namespace P4MapKit::Material;
static unsigned checks=0;
static void Check(bool value){++checks;if(!value)throw std::runtime_error("game scene-read mapping failed");}
int main(){
    const auto original=Json::Parse(R"({"domain":"MD_Surface","blend_mode":"BLEND_Opaque","nodes":{"depth":{"type":"SceneDepth","inputs":{}},"color":{"type":"SceneColor","inputs":{}},"copy":{"type":"Custom","props":{"Code":"return Texture2DSample(SceneColorCopyTexture, View.MaterialTextureBilinearClampedSampler, UV);","OutputType":"CMOT_Float3"},"inputs":{"UV":["uv",0]}},"other":{"type":"Custom","props":{"Code":"return UV;"}},"stencil":{"type":"Custom","props":{"Code":"return float(SceneTexturesStruct.SceneStencilTexture.Load(int3(int2(Parameters.SvPosition.xy * View.BufferSizeAndInvSize.zw * View.BufferSizeAndInvSize.xy), 0)) STENCIL_COMPONENT_SWIZZLE & 2u);","OutputType":"CMOT_Float1"}},"uv":{"type":"TextureCoordinate"}}})");
    const auto opaque=KadanGameReads(original);
    Check(opaque.At("nodes").At("depth").At("type").Text()=="KadanSceneDepth");
    Check(opaque.At("nodes").At("color").At("type").Text()=="KadanSceneColor");
    Check(opaque.At("nodes").At("copy").At("type").Text()=="KadanGameCustom");
    Check(opaque.At("nodes").At("stencil").At("type").Text()=="KadanGameCustom");
    Check(StockGameReads(opaque).Dump()==original.Dump());
    auto transparent=original;transparent["blend_mode"]=Json("BLEND_Translucent");
    const auto mapped=KadanGameReads(transparent);
    Check(mapped.At("nodes").At("depth").At("type").Text()=="SceneDepth");
    Check(mapped.At("nodes").At("color").At("type").Text()=="SceneColor");
    Check(mapped.At("nodes").At("copy").At("type").Text()=="KadanGameCustom");
    Check(mapped.At("nodes").At("stencil").At("type").Text()=="KadanGameCustom");
    Check(mapped.At("nodes").At("other").At("type").Text()=="Custom");
    Check(StockGameReads(mapped).Dump()==transparent.Dump());
    Check(KadanGameReads(mapped).Dump()==mapped.Dump());
    const auto family=KadanGameReads(transparent,true);
    Check(family.At("nodes").At("depth").At("type").Text()=="KadanSceneDepth");
    Check(family.At("nodes").At("color").At("type").Text()=="KadanSceneColor");
    Check(StockGameReads(family).Dump()==transparent.Dump());
    std::cout<<checks<<" game scene-read checks passed\n";
}
