#include "../core/material_bind.h"
#include "../core/material_json.h"
#include <iostream>
using namespace P4MapKit;
using namespace P4MapKit::Material;
static int checks=0;
static void Check(bool condition,const char*why) { ++checks;if(!condition)throw std::runtime_error(why); }
template<class F> static void Refuses(F action,const std::string&name) { bool refused=false;try { action(); }catch(const BindingRefused&e) { refused=e.name==name; }Check(refused,"named refusal"); }
static FMaterialPreshaderProgram Parameter(bool vector,int index) { FMaterialPreshaderProgram p;FMaterialPreshaderInstruction i;i.Opcode=vector?EMaterialPreshaderOpcode::VectorParameter:EMaterialPreshaderOpcode::ScalarParameter;i.Parameter=std::uint16_t(index);p.Instructions.push_back(i);p.PeakStack=1;return p; }
static Value Atom(const Json&j,const std::map<std::string,Value>&nodes) {
    if(j.kind==Json::Null)return Value::null();if(j.kind==Json::String)return j.string;if(j.kind==Json::Boolean)return j.boolean;if(j.kind==Json::Number)return j.number;
    if(j.kind==Json::Array) { std::vector<Value> items;for(const auto&item:j.array)items.push_back(Atom(item,nodes));return Value::tuple_value(items); }
    if(j.Has("ref"))return nodes.at(j.At("ref").Text());
    if(j.Has("float_bits")) { auto bits=std::stoull(j.At("float_bits").Text(),nullptr,16);double value;std::memcpy(&value,&bits,8);return value; }
    if(j.Has("integer"))return Value(std::int64_t(std::stoll(j.At("integer").Text())));
    throw EvalError("bad fixture atom");
}
static Json Encode(const Value&v,std::map<const Expression*,std::string>&seen,Json&formulas) {
    if(v.kind==Value::Expr) {
        auto at=seen.find(v.expression.get());if(at==seen.end()) { Json args=Json::MakeArray();for(const auto&a:v.expression->args)args.array.push_back(Encode(a,seen,formulas));std::string id="X"+std::to_string(seen.size());seen[v.expression.get()]=id;Json item=Json::MakeObject();item["id"]=id;item["op"]=v.expression->op;item["args"]=args;formulas.array.push_back(item);at=seen.find(v.expression.get()); }
        Json ref=Json::MakeObject();ref["ref"]=at->second;return ref;
    }
    if(v.kind==Value::Null)return Json();if(v.kind==Value::Text)return Json(v.text);if(v.kind==Value::Boolean)return Json(v.boolean);
    if(v.kind==Value::Tuple) { Json items=Json::MakeArray();for(const auto&i:v.tuple)items.array.push_back(Encode(i,seen,formulas));return items; }
    Json value=Json::MakeObject();if(v.kind==Value::Int)value["integer"]=v.integer.decimal();else if(v.kind==Value::Float) { std::uint64_t bits;std::memcpy(&bits,&v.real,8);std::ostringstream text;text<<std::hex<<std::setw(16)<<std::setfill('0')<<bits;value["float_bits"]=text.str(); }else throw EvalError("bad output atom");return value;
}
static int Fixtures() {
    std::string line;while(std::getline(std::cin,line)) { Json answer=Json::MakeObject();try {
        Json request=Json::Parse(line);SymbolicArena arena;BindingContext context;const auto&c=request.At("context");
        for(const auto&b:c.At("buffers").object)context.Buffers[std::stoi(b.first)]=b.second.Text();
        for(const auto&v:c.At("varyings").object)context.Varyings[v.first]=int(v.second.Numeric());
        for(const auto&name:c.At("scalars").array) { FMaterialUniformParameter p;p.Name=name.Text();context.Uniforms.Scalars.push_back(p); }
        for(const auto&name:c.At("vectors").array) { FMaterialUniformParameter p;p.Name=name.Text();context.Uniforms.Vectors.push_back(p); }
        for(const auto&programs:std::vector<std::pair<std::string,std::vector<FMaterialPreshaderProgram>*>>{{"vector_code",&context.Uniforms.VectorPrograms},{"scalar_code",&context.Uniforms.ScalarPrograms}})for(const auto&raw:c.At(programs.first).array) {
            std::vector<std::uint8_t> code;for(const auto&byte:raw.array)code.push_back(std::uint8_t(byte.Numeric()));FMaterialPreshaderProgram program;FReadError error;
            if(!ParseMaterialPreshader(code,context.Uniforms.Scalars.size(),context.Uniforms.Vectors.size(),0,program,error))throw EvalError(error.Code);programs.second->push_back(program);
        }
        for(const auto&collection:c.At("collections").array) { CollectionBinding item;item.Package=collection.At("package").Text();for(const auto&s:collection.At("scalars").array)item.Scalars.push_back(s.Text());for(const auto&v:collection.At("vectors").array)item.Vectors.push_back(v.Text());context.Collections.push_back(item); }
        for(const auto&t:c.At("textures").object)context.TextureParameters[std::stoi(t.first)]=t.second.Text();
        for(const auto&s:c.At("samplers").array)context.Samplers.insert(int(s.Numeric()));context.ViewSamplersKnown=true;
        for(const auto&s:c.At("view_samplers").object)context.ViewSamplers[std::stoi(s.first)]=int(s.second.Numeric());
        std::map<std::string,Value> nodes;for(const auto&f:request.At("formulas").array) { std::vector<Value> args;for(const auto&a:f.At("args").array)args.push_back(Atom(a,nodes));nodes[f.At("id").Text()]=arena.mk(f.At("op").Text(),args); }
        Binder binder(arena,context);std::vector<Value> outputs;for(const auto&v:request.At("values").array)outputs.push_back(binder.bind(Atom(v,nodes)));
        Json formulas=Json::MakeArray(),values=Json::MakeArray();std::map<const Expression*,std::string> seen;for(const auto&v:outputs)values.array.push_back(Encode(v,seen,formulas));answer["formulas"]=formulas;answer["values"]=values;
    }catch(const BindingRefused&e) { answer["refused"]=e.what();answer["refusal"]=e.name; }catch(const std::exception&e) { answer["refused"]=e.what();answer["refusal"]="fixture error"; }std::cout<<answer.Dump()<<'\n'; }
    return 0;
}
int main(int argc,char**) {
    if(argc>1)return Fixtures();
    try {
        SymbolicArena arena;BindingContext context;
        context.Buffers={{0,"View"},{1,"Primitive"},{2,"Material"},{3,"MaterialCollection0"},{4,"Unrecognized"}};
        context.Varyings={{"in_texcoord0",0},{"in_color0",1},{"in_texcoord4",4},{"in_texcoord9",9}};
        context.Collections.push_back({"/Game/TestCollection",{"A","B","C"},{"Color","Direction"},true});
        context.Uniforms.Scalars.resize(1);context.Uniforms.Scalars[0].Name="Scale";
        context.Uniforms.Vectors.resize(1);context.Uniforms.Vectors[0].Name="Tint";
        context.Uniforms.VectorPrograms.push_back(Parameter(true,0));context.Uniforms.ScalarPrograms.push_back(Parameter(false,0));
        context.TextureParameters={{0,"Albedo"},{1,"Cube"}};context.TextureEntries={{0,0},{1,1}};
        context.Uniforms.Textures.resize(2);context.Uniforms.Textures[1].TextureType=1;
        context.Samplers.insert(0);context.ViewSamplersKnown=true;context.ViewSamplers={{1,0},{2,1}};
        Binder binder(arena,context);
        auto input=[&](const std::string&kind,int index,int c) { return arena.mk("IN",{kind,index,c}); };
        for(int c=0;c<4;++c)Check(same(binder.bind(input("position",0,c)),source(arena,"SvPosition",Value::null(),c)),"position source");
        Check(same(binder.bind(input("facing",0,0)),source(arena,"Facing")),"facing source");
        for(int c=0;c<4;++c)Check(same(binder.bind(input("attrib",0,c)),source(arena,"TexCoord",c/2,c%2)),"packed texture coordinate");
        for(int c=0;c<4;++c)Check(same(binder.bind(input("attrib",1,c)),source(arena,"VertexColor",Value::null(),c)),"vertex color");
        Check(same(binder.bind(input("attrib",6,1)),source(arena,"Interpolant","interpolant 6",1)),"unknown varying retained");
        for(int c=0;c<16;++c)Check(same(binder.bind(input("buffer",0,704+4*c)),source(arena,"View","sv_to_translated_world",c)),"view matrix");
        for(int c=0;c<16;++c)Check(same(binder.bind(input("buffer",1,96+4*c)),source(arena,"PrimitiveMatrix","WorldToLocal",c)),"primitive matrix");
        Check(binder.bind(input("buffer",0,2180)).number()==1,"pre-exposure canonical unit");
        Check(binder.canonical_unit_fields.count({"View",2180})==1,"canonical domain recorded");
        Check(binder.bind(input("buffer",1,320)).number()==0,"known engine switch off");Check(!binder.assumed.at({"Primitive",320}).empty(),"known assumption evidence");
        Refuses([&] { binder.bind(input("buffer",4,0)); },"unmodeled engine input");
        Refuses([&] { binder.bind(input("buffer",0,700)); },"unmodeled engine input");
        Refuses([&] { binder.bind(input("buffer",1,97)); },"buffer word alignment");
        for(int c=0;c<4;++c)Check(same(binder.material_word(c*4),source(arena,"VectorParameter","Tint",c)),"vector parameter component");
        Check(same(binder.material_word(16),source(arena,"ScalarParameter","Scale")),"scalar parameter packing");
        Refuses([&] { binder.material_word(20); },"a word of the Material buffer with no value");
        BindingContext virtual_context=context;virtual_context.Uniforms.Textures[0].TextureType=4;
        Binder virtual_binder(arena,virtual_context);Refuses([&] { virtual_binder.material_word(0); },"virtual texture Material buffer layout");
        virtual_context.Uniforms.Textures[0].TextureType=0;virtual_context.Uniforms.VirtualStackCount=1;
        Binder virtual_stack(arena,virtual_context);Refuses([&] { virtual_stack.material_word(0); },"virtual texture Material buffer layout");
        for(int c=0;c<3;++c)Check(same(binder.collection_word(0,c*4),source(arena,"CollectionScalar",Value::tuple_value({"/Game/TestCollection",context.Collections[0].Scalars[c]}))),"collection scalar packing");
        for(int c=0;c<4;++c)Check(same(binder.collection_word(0,16+c*4),source(arena,"CollectionVector",Value::tuple_value({"/Game/TestCollection","Color"}),c)),"collection aligned vector packing");
        Refuses([&] { binder.collection_word(0,12); },"a word of a parameter collection with no parameter");
        Refuses([&] { binder.collection_word(1,0); },"a parameter collection that is not among the extracted files");
        auto raw_row=[&](int c,int mask) { Value terms[3];for(int r=0;r<3;++r) { auto p=input("position",0,r),m=input("buffer",0,704+4*(r*4+c));terms[r]=arena.op("MUL",mask&(1<<r)?std::vector<Value>{m,p}:std::vector<Value>{p,m}); }return arena.op("ADD",{input("buffer",0,704+4*(12+c)),arena.op("ADD",{terms[2],arena.op("ADD",{terms[1],terms[0]})})}); };
        for(int c=0;c<3;++c)for(int n=0;n<8;++n)for(int d=0;d<8;++d) {
            auto numerator=raw_row(c,n),denominator=raw_row(3,d),reciprocal=arena.mk("RCP",{denominator});
            for(const auto&v:std::vector<Value>{arena.op("DIV",{numerator,denominator}),arena.op("MUL",{numerator,reciprocal}),arena.op("MUL",{reciprocal,numerator})})Check(same(binder.bind(v),source(arena,"TranslatedWorldPosition",Value::null(),c)),"exact homogeneous matrix row");
        }
        for(int c=0;c<3;++c) {
            auto translated=arena.op("DIV",{raw_row(c,0),raw_row(3,0)}),back=arena.op("NEG",{input("buffer",0,1120+4*c)});
            Check(same(binder.bind(arena.op("ADD",{translated,back})),source(arena,"WorldPosition",Value::null(),c)),"absolute world position");
            Check(same(binder.bind(arena.op("ADD",{back,translated})),source(arena,"WorldPosition",Value::null(),c)),"absolute world position order");
        }
        for(int c=0;c<3;++c) {
            Value terms[3];for(int r=0;r<3;++r)terms[r]=arena.op("MUL",{input("buffer",1,96+4*(r*4+c)),input("attrib",9,r)});
            auto transformed=arena.op("ADD",{input("buffer",1,96+4*(12+c)),arena.op("ADD",{terms[2],arena.op("ADD",{terms[1],terms[0]})})});
            Vec position;for(int r=0;r<3;++r)position[r]=source(arena,"TranslatedWorldPositionNoOffsets",Value::null(),r);
            Check(same(binder.bind(transformed),arena.mk("COMP",{arena.mk("WORLD_TO_LOCAL_POSITION",{position[0],position[1],position[2]}),c})),"exact primitive MAD transform");
        }
        auto tex=[&](const std::string&op,const std::string&target,int texture,int sampler,const Value&lod) { return arena.mk("TEX",{op,target,texture,sampler,input("attrib",0,0),input("attrib",0,1),0.0,lod}); };
        auto sample=binder.bind(tex("TEX","2D",328,1352,0.0));Check(is(sample,"SAMPLE")&&sample.expression->args[1].text=="auto/SSM_FromTextureAsset","own sampler");
        auto shared=binder.bind(tex("TEX","2D",328,1360,0.0));Check(shared.expression->args[1].text=="auto/SSM_Wrap_WorldGroupSettings","view wrap sampler");
        auto clamp=binder.bind(tex("TEX","2D",328,1368,0.0));Check(clamp.expression->args[1].text=="auto/SSM_Clamp_WorldGroupSettings","view clamp sampler");
        auto mismatch=binder.bind(tex("TEX","2D",328,1376,0.0));Check(is(mismatch,"ENGINE"),"unknown sampler named");
        auto cube=binder.bind(tex("TEX","CUBE",336,1352,0.0));Check(is(cube,"CUBE_SAMPLE"),"checked cube texture identity");
        auto level=arena.op("ADD",{input("buffer",0,2452),2.0});auto lod=binder.bind(tex("TXL","2D",328,1352,level));Check(lod.expression->args[1].text=="levelauto/SSM_FromTextureAsset"&&lod.expression->args.back().number()==2,"explicit lod automatic bias");
        context.Uniforms.Textures[0].SamplerSource=2;
        Refuses([&] { binder.sampler_source("Albedo",Value(1360),&context.Uniforms.Textures[0]); },"settings disagree: sampler source");
        BindingContext unknown=context;unknown.Uniforms.Textures[0].SamplerSource=0;unknown.ViewSamplersKnown=false;unknown.AllowUnresolvedSyntheticSamplers=true;
        Binder unknown_binder(arena,unknown);Check(unknown_binder.sampler_source("Albedo",Value(1360),nullptr).empty(),"synthetic sampler provenance");
        BindingContext ao_context=context;ao_context.Uniforms.Textures[0].SamplerSource=0;ao_context.PrecomputedAOSlot=2;
        Binder ao(arena,ao_context);auto read=arena.mk("TEX",{"TEX","2D",344,1352,input("attrib",4,0),input("attrib",4,1),0.0,0.0});auto red=arena.mk("COMP",{read,0});
        Check(same(ao.bind(arena.op("MUL",{red,red})),source(arena,"PrecomputedAOMask")),"precomputed AO exact red square");
        Refuses([&] { ao.bind(tex("TEX","2D",344,1352,0.0)); },"precomputed AO inputs");
        BindingContext dynamic=context;dynamic.Uniforms.ScalarPrograms.clear();FMaterialPreshaderProgram runtime;FMaterialPreshaderInstruction instruction;instruction.Opcode=EMaterialPreshaderOpcode::TexelSize;runtime.Instructions.push_back(instruction);runtime.PeakStack=1;dynamic.Uniforms.ScalarPrograms.push_back(runtime);
        Binder no_runtime(arena,dynamic);Refuses([&] { no_runtime.material_word(16); },"material preshader input");
        dynamic.RuntimePreshader=[&](const FMaterialPreshaderInstruction&) { Vec v;for(int c=0;c<4;++c)v[c]=source(arena,"TextureProperty",Value::tuple_value({"Albedo","TMTM_TexelSize"}),c);return v; };
        Binder bound_runtime(arena,dynamic);Check(same(bound_runtime.material_word(16),source(arena,"TextureProperty",Value::tuple_value({"Albedo","TMTM_TexelSize"}),0)),"dynamic texture property retained");
        BindingContext overrides=context;overrides.LeafOverride=[&](const Value&v,Value&out) { const auto&a=v.expression->args;if(a[0].text=="buffer"&&a[1].number()==0&&a[2].number()==700) { out=source(arena,"ReflectionField","ExplicitEvidence");return true; }return false; };
        Binder explicit_leaf(arena,overrides);Check(same(explicit_leaf.bind(input("buffer",0,700)),source(arena,"ReflectionField","ExplicitEvidence")),"explicit checked policy extension");
        std::cout<<"material binding checks: "<<checks<<"\n";
        return 0;
    } catch(const std::exception&e) { std::cerr<<e.what()<<"\n";return 1; }
}
