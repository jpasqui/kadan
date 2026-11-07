#include "../core/material_fixed_source.h"
#include "../core/material_lifter.h"
#include <iostream>
using namespace P4MapKit;using namespace P4MapKit::Material;
static int checks=0;
static void Check(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
template<class F>static void Refuses(F action,const std::string& category){
    ++checks;try{action();}catch(const Refused& error){if(error.name==category)return;throw;}
    throw std::runtime_error("expected refusal: "+category);
}
static FDecodedValue Fields(std::initializer_list<std::pair<std::string,FDecodedValue>> values){FDecodedValue result;result.Fields=values;return result;}
static FDecodedValue Items(std::initializer_list<FDecodedValue> values){FDecodedValue result;result.Kind=FDecodedValue::EKind::Array;result.Items=values;return result;}
static FDecodedValue Reference(const std::string& path,bool soft=false){auto result=FDecodedValue::Atom(soft?FDecodedValue::EKind::SoftPath:FDecodedValue::EKind::HardReference,path);result.ReferenceIndex=-1;return result;}
static void Put(std::vector<std::uint8_t>& bytes,std::uint32_t value){for(int i=0;i<4;++i)bytes.push_back(std::uint8_t(value>>(8*i)));}
// Texture settings travel through the actual native tagged-property decoder.
static MaterialSourceAsset Texture(const std::string& package,const std::string& type="Texture2D",
    const std::string& compression="TC_Default",bool srgb=true,bool malformed=false,bool badSetting=false){
    MaterialSourceAsset result;result.Package.Package=package;
    result.Package.Imports={{"","","/Script/Engine",0},{"","",type,-1}};
    std::vector<std::uint8_t> bytes;
    auto name=[&](const std::string& text){auto at=std::find(result.Package.Names.begin(),result.Package.Names.end(),text);
        if(at==result.Package.Names.end()){result.Package.Names.push_back(text);at=result.Package.Names.end()-1;}Put(bytes,std::uint32_t(at-result.Package.Names.begin()));Put(bytes,0);};
    name("CompressionSettings");name("ByteProperty");Put(bytes,8);Put(bytes,0);name("TextureCompressionSettings");bytes.push_back(0);name(compression);
    name("SRGB");name(badSetting?"IntProperty":"BoolProperty");Put(bytes,badSetting?4:0);Put(bytes,0);
    if(!badSetting)bytes.push_back(srgb?1:0);bytes.push_back(0);if(badSetting)Put(bytes,2);name("None");
    if(malformed)bytes.pop_back();FPackageExport record;record.Name="Asset";record.Class=-2;record.Size=bytes.size();result.Package.Exports.push_back(record);
    result.Body=std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));return result;
}
static MaterialOriginalUserData User(){MaterialOriginalUserData user;
    user.Chain.push_back({"/Game/Master","Material",Fields({{"CachedExpressionData",Fields({{"ReferencedTextures",Items({Reference("/Game/Unused.Unused"),Reference("/Game/Color.Color"),Reference("/Game/Normal.Normal",true)})}})}})});return user;}
static FMaterialUniformTexture Uniform(const std::string& name,int index,int kind=0,int sampler=0){FMaterialUniformTexture result;
    result.Parameter.Name=name;result.TextureIndex=index;result.TextureType=std::uint8_t(kind);result.SamplerSource=std::uint8_t(sampler);return result;}
static Value BoundSample(SymbolicArena& arena,const BindingContext& context,int slot,int sampler,const std::string& op="TEX",double lod=0){
    Binder binder(arena,context);return binder.bind(arena.mk("TEX",{op,"2D",328+slot*8,1352+sampler*8,.25,.5,0,lod}));}
static int SelfTest(){
    auto user=User();FMaterialShaderMapEnvelope map;map.Uniforms.Textures={Uniform("NamedParameter",-1),Uniform("None",1),Uniform("?",2),Uniform("",-1,1)};
    int calls=0;MaterialSourceAssetResolver resolve=[&](const std::string& package,MaterialSourceAsset& asset){++calls;
        if(package=="/Game/Color")asset=Texture(package);else if(package=="/Game/Normal")asset=Texture(package,"Texture2D","TC_Normalmap");else return false;return true;};
    auto fixed=PrepareOriginalMaterialFixedTextures(user,map,resolve);
    Check(calls==2&&fixed.UniformEntries.size()==2,"resolve actual indices only");
    Check(fixed.UniformEntries.at(1).Package=="/Game/Color"&&fixed.UniformEntries.at(2).SamplerType=="SAMPLERTYPE_Normal","typed reference and sampler");
    Check(fixed.UnsupportedUniformEntries.count(3)==1,"unused cube deferred");
    Check(fixed.Defaults.At("textures").At("/Game/Color").Text()=="/Game/Color","defaults use package asset identity");
    BindingContext context;context.Uniforms=map.Uniforms;context.TextureEntries={{4,2},{9,1},{10,0}};
    context.TextureParameters={{4,"?"},{9,"None"},{10,"NamedParameter"}};context.Samplers={3};context.ViewSamplersKnown=true;context.ViewSamplers={{7,0},{8,1}};
    context.MaterialSharedSamplers={{1600,"SSM_Wrap_WorldGroupSettings"}};
    ApplyOriginalMaterialFixedTextures(context,fixed);
    Check(context.TextureParameters.at(4)=="/Game/Normal"&&context.TextureParameters.at(9)=="/Game/Color"&&context.TextureParameters.at(10)=="NamedParameter","compiled mapping overrides slot ordering");
    Check(context.Samplers==std::set<int>{3}&&context.ViewSamplers.at(7)==0&&context.MaterialSharedSamplers.at(1600)=="SSM_Wrap_WorldGroupSettings","sampler maps preserved");
    SymbolicArena arena;
    for(const auto& sampler:std::vector<std::pair<int,std::string>>{{3,"SSM_FromTextureAsset"},{7,"SSM_Wrap_WorldGroupSettings"},{8,"SSM_Clamp_WorldGroupSettings"}}){
        const auto sample=BoundSample(arena,context,9,sampler.first);Check(is(sample,"SAMPLE")&&sample.expression->args[0].text=="/Game/Color","binder carries exact asset");
        Lifter lifter(arena,fixed.Defaults);auto link=lifter.lift({arena.mk("COMP",{sample,0}),arena.mk("COMP",{sample,1}),arena.mk("COMP",{sample,2})});
        const auto& node=lifter.nodes.At(link.array[0].Text());Check(node.At("type").Text()=="TextureSample","fixed sample is not parameter");
        const auto& props=node.At("props");Check(props.At("Texture").Text()=="/Game/Color"&&props.At("SamplerSource").Text()==sampler.second&&props.At("SamplerType").Text()=="SAMPLERTYPE_Color","graph asset and shared sampler");
    }
    for(const auto& mode:std::vector<std::pair<std::string,std::string>>{{"TXB","plain"},{"TXL","level"}}){const auto sample=BoundSample(arena,context,9,3,mode.first);
        Check(sample.expression->args[1].text==mode.second+"/SSM_FromTextureAsset","native sampling mode");Lifter lifter(arena,fixed.Defaults);Check(lifter.source(sample).second==4,"fixed mip graph");}
    const auto normal=BoundSample(arena,context,4,3);Lifter normalLifter(arena,fixed.Defaults);Check(normalLifter.source(normal,true).second==4,"normal texture typed source");
    const auto color=BoundSample(arena,context,9,3);Refuses([&]{Lifter lifter(arena,fixed.Defaults);lifter.source(color,true);},"fixed texture sampler type");
    auto wrongDefaults=fixed.Defaults;wrongDefaults["fixed_textures"]["/Game/Color"]=Json();Refuses([&]{Lifter lifter(arena,wrongDefaults);lifter.source(color);},"fixed texture sampler type");
    auto instance=user;instance.Chain.push_back({"/Game/Instance","MaterialInstanceConstant",Fields({{"CachedReferencedTextures",Items({Reference("/Game/Normal.Normal"),Reference("/Game/Normal.Normal")})}})});
    auto one=map;one.Uniforms.Textures={Uniform("None",1)};
    Check(PrepareOriginalMaterialFixedTextures(instance,one,resolve).UniformEntries.at(0).Package=="/Game/Normal","nonempty instance table wins");
    instance.Chain.back().Properties=Fields({{"CachedReferencedTextures",Items({})}});
    Check(PrepareOriginalMaterialFixedTextures(instance,one,resolve).UniformEntries.at(0).Package=="/Game/Color","empty instance falls back master");
    instance.Chain.back().Properties=Fields({});Check(PrepareOriginalMaterialFixedTextures(instance,one,resolve).UniformEntries.at(0).Package=="/Game/Color","missing instance falls back master");
    instance.Chain.back().Properties=Fields({{"CachedReferencedTextures",FDecodedValue::Atom(FDecodedValue::EKind::String,"wrong")}});
    Refuses([&]{PrepareOriginalMaterialFixedTextures(instance,one,resolve);},"fixed texture reference table");
    for(int index:{-1,3,INT32_MAX}){auto bad=one;bad.Uniforms.Textures[0].TextureIndex=index;Refuses([&]{PrepareOriginalMaterialFixedTextures(user,bad,resolve);},"fixed texture index");}
    auto badUser=user;badUser.Chain.front().Properties=Fields({});Refuses([&]{PrepareOriginalMaterialFixedTextures(badUser,one,resolve);},"fixed texture reference table");
    for(const auto& bad:std::vector<FDecodedValue>{FDecodedValue::Atom(FDecodedValue::EKind::String,"/Game/Color.Color"),FDecodedValue::Atom(FDecodedValue::EKind::Null,""),Reference("/Engine/Color.Color"),Reference("/Game/Color"),Reference("/Game/../Color.Color"),Reference("/Game//Color.Color"),Reference("/Game/Color.Color:Part"),Reference("/Game/Color."),Reference("/Game/Color.Color.More"),Reference("/Game/Color/Name.Obj/")}){
        ++checks;try{OriginalMaterialFixedPackage(bad);}catch(const Refused&){continue;}throw std::runtime_error("invalid fixed path accepted");}
    auto null=Reference("/Game/Color.Color");null.ReferenceIndex=0;Refuses([&]{OriginalMaterialFixedPackage(null);},"fixed texture asset reference");
    auto sub=Reference("/Game/Color.Color",true);sub.SubPath="Part";Refuses([&]{OriginalMaterialFixedPackage(sub);},"fixed texture asset reference");
    Check(OriginalMaterialFixedPackage(Reference("/Game/Color.DifferentObject"))=="/Game/Color","object suffix stripped, not inferred");
    Refuses([&]{PrepareOriginalMaterialFixedTextures(user,one,{});},"fixed texture source asset");
    Refuses([&]{PrepareOriginalMaterialFixedTextures(user,one,[](const std::string&,MaterialSourceAsset& asset){asset=Texture("/Game/Other");return true;});},"fixed texture asset identity");
    Refuses([&]{PrepareOriginalMaterialFixedTextures(user,one,[](const std::string& package,MaterialSourceAsset& asset){asset=Texture(package,"StaticMesh");return true;});},"fixed texture source class");
    Refuses([&]{PrepareOriginalMaterialFixedTextures(user,one,[](const std::string& package,MaterialSourceAsset& asset){asset=Texture(package,"Texture2D","TC_Default",true,false,true);return true;});},"fixed texture sampler type");
    ++checks;try{PrepareOriginalMaterialFixedTextures(user,one,[](const std::string& package,MaterialSourceAsset& asset){asset=Texture(package,"Texture2D","TC_Default",true,true);return true;});throw std::runtime_error("truncated texture accepted");}catch(const Refused& error){Check(error.name=="material source properties","texture decoding refusal");}
    auto bad=context;bad.TextureEntries={{1,1},{2,3}};bad.TextureParameters={{1,"None"}};const auto before=bad.TextureParameters;
    Refuses([&]{ApplyOriginalMaterialFixedTextures(bad,fixed);},"fixed texture kind");Check(bad.TextureParameters==before,"apply refusal is atomic");
    bad=context;bad.TextureEntries={{0,999}};Refuses([&]{ApplyOriginalMaterialFixedTextures(bad,fixed);},"fixed texture compiled index");
    bad=context;bad.TextureEntries={{-1,1}};Refuses([&]{ApplyOriginalMaterialFixedTextures(bad,fixed);},"fixed texture compiled index");
    bad=context;bad.Uniforms.Textures[1].TextureIndex=2;Refuses([&]{ApplyOriginalMaterialFixedTextures(bad,fixed);},"fixed texture uniform identity");
    bad=context;bad.TextureParameters[9]="Conflicting";Refuses([&]{ApplyOriginalMaterialFixedTextures(bad,fixed);},"fixed texture compiled identity");
    ApplyOriginalMaterialFixedTextures(context,fixed);Check(context.TextureParameters.at(9)=="/Game/Color","apply idempotent");
    auto named=map;named.Uniforms.Textures={Uniform("Albedo",-1)};Check(PrepareOriginalMaterialFixedTextures({},named,{}).UniformEntries.empty(),"named parameters need no fixed table");
    auto absent=one;absent.Uniforms.Textures[0].Parameter.Name="SerializedIgnored";absent.Uniforms.Textures[0].Parameter.NameIsNone=true;
    Check(PrepareOriginalMaterialFixedTextures(user,absent,resolve).UniformEntries.size()==1,"raw None identity authoritative");
    std::cout<<"native fixed source checks="<<checks<<"\n";return 0;
}
static int Fixtures(){std::string line;while(std::getline(std::cin,line)){Json answer=Json::MakeObject();try{
    const auto request=Json::Parse(line);MaterialOriginalUserData user;auto refs=FDecodedValue::Atom(FDecodedValue::EKind::Array,"");
    for(const auto& item:request.At("references").array)refs.Items.push_back(Reference(item.Text()));
    user.Chain.push_back({"/Game/Master","Material",Fields({{"CachedExpressionData",Fields({{"ReferencedTextures",refs}})}})});
    if(request.Has("instance_references")){auto cached=FDecodedValue::Atom(FDecodedValue::EKind::Array,"");for(const auto& item:request.At("instance_references").array)cached.Items.push_back(Reference(item.Text(),true));user.Chain.push_back({"/Game/Instance","MaterialInstanceConstant",Fields({{"CachedReferencedTextures",cached}})});}
    FMaterialShaderMapEnvelope map;for(const auto& item:request.At("uniforms").array)map.Uniforms.Textures.push_back(Uniform(item.At("name").Text(),int(item.At("index").Numeric()),int(item.At("kind").Numeric())));
    MaterialSourceAssetResolver resolver=[&](const std::string& package,MaterialSourceAsset& asset){const auto& assets=request.At("assets");if(!assets.Has(package))return false;const auto& settings=assets.At(package);asset=Texture(package,settings.At("class").Text(),settings.At("compression").Text(),settings.At("srgb").boolean);return true;};
    const auto result=PrepareOriginalMaterialFixedTextures(user,map,resolver);BindingContext context;context.Uniforms=map.Uniforms;
    for(const auto& item:request.At("slots").object)context.TextureEntries[std::stoi(item.first)]=std::size_t(item.second.Numeric());
    ApplyOriginalMaterialFixedTextures(context,result);answer["defaults"]=result.Defaults;answer["bindings"]=Json::MakeObject();for(const auto& item:context.TextureParameters)answer["bindings"][std::to_string(item.first)]=item.second;
    }catch(const Refused& error){answer["refusal"]=error.name;answer["reason"]=error.what();}catch(const std::exception& error){answer["refusal"]="fixture error";answer["reason"]=error.what();}std::cout<<answer.Dump()<<'\n';}return 0;}
int main(int argc,char**){try{return argc>1?Fixtures():SelfTest();}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
