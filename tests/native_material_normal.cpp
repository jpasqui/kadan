#include "../core/material_normal.h"
#include <iostream>
using namespace P4MapKit;
using namespace P4MapKit::Material;
static int checks=0;
static void Check(bool value,const char*why) {++checks;if(!value)throw std::runtime_error(why);}
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
    std::string line;while(std::getline(std::cin,line)) {Json answer=Json::MakeObject();try {
        const auto request=Json::Parse(line);SymbolicArena arena;
        if(request.At("stage").Text()=="normal") {
            std::map<std::string,Value> nodes;for(const auto&f:request.At("formulas").array) {std::vector<Value> args;for(const auto&a:f.At("args").array)args.push_back(Atom(a,nodes));nodes[f.At("id").Text()]=arena.mk(f.At("op").Text(),args);}
            std::vector<Value> values;for(const auto&v:request.At("values").array)values.push_back(Atom(v,nodes));auto normal=MaterialNormal(arena,values);
            Json formulas=Json::MakeArray(),pin=Json::MakeArray();std::map<const Expression*,std::string> seen;for(const auto&v:normal.pin)pin.array.push_back(Encode(v,seen,formulas));
            answer["formulas"]=formulas;answer["values"]=pin;answer["has_pin"]=normal.has_pin;answer["tangent_space"]=normal.tangent_space;
        }else {
            std::vector<std::string> lines;for(const auto&l:request.At("code").array)lines.push_back(l.Text());auto code=ParseMaterialAnchorInstructions(lines);MaterialAnchors found;
            for(const auto&f:request.At("found").object) {MaterialAnchor anchor;anchor.line=std::size_t(f.second.At("line").Numeric());if(f.second.Has("constant")) {anchor.has_constant=true;anchor.constant=f.second.At("constant").Numeric();}if(f.second.Has("register"))anchor.register_name=f.second.At("register").Text();if(f.second.Has("comps"))anchor.comps=f.second.At("comps").Text();found[f.first]=anchor;}
            auto stage=request.At("stage").Text();auto anchors=stage=="ao"?CorrectMaterialAmbientOcclusion(code,found):stage=="scheduled"?ScheduledMaterialReflection(code,found):ForwardMaterialWorldNormal(code,found);
            Json out=Json::MakeObject();for(const auto&anchor:anchors) {Json item=Json::MakeObject();item["line"]=double(anchor.second.line);if(anchor.second.has_constant)item["constant"]=anchor.second.constant;if(!anchor.second.register_name.empty()) {item["register"]=anchor.second.register_name;item["comps"]=anchor.second.comps;}out[anchor.first]=item;}answer["found"]=out;
        }
    }catch(const std::exception&e) {answer["refused"]=e.what();}std::cout<<answer.Dump()<<'\n';}return 0;
}
int main(int argc,char**) {
    if(argc>1)return Fixtures();try {
        SymbolicArena arena;auto normalize=[&](const std::vector<Value>&input) {auto inverse=arena.mk("RSQ",{NormalDetail::dot(arena,input)});std::vector<Value> v;for(const auto&x:input)v.push_back(arena.mk("MUL",{inverse,x}));return v;};
        std::vector<Value> tangent;for(int c=0;c<3;++c)tangent.push_back(source(arena,"TangentZ",Value::null(),c));
        auto none=MaterialNormal(arena,normalize(tangent));Check(!none.has_pin&&none.tangent_space,"interpolated normal no pin");
        std::vector<Value> pin;for(int c=0;c<3;++c)pin.push_back(source(arena,"VectorParameter","Normal",c));
        auto normalized=normalize(pin);auto world=arena.mk("TOWORLD",normalized);std::vector<Value> transformed;for(int c=0;c<3;++c)transformed.push_back(arena.mk("COMP",{world,c}));
        auto result=MaterialNormal(arena,normalize(transformed));Check(result.has_pin&&result.tangent_space,"normalized tangent pin recovered");for(int c=0;c<3;++c)Check(same(result.pin[c],pin[c]),"pin expression preserved");
        auto signed_world=normalize(transformed);for(auto&v:signed_world)v=arena.mk("MUL",{v,source(arena,"TwoSidedSign")});auto signed_pin=MaterialNormal(arena,signed_world);Check(signed_pin.has_pin&&same(signed_pin.pin[0],pin[0]),"two sided turn removed");
        auto direct=MaterialNormal(arena,normalized);Check(direct.has_pin&&!direct.tangent_space,"world space pin recovered");
        bool refused=false;try {MaterialNormal(arena,pin);}catch(const Refused&) {refused=true;}Check(refused,"unnormalized world normal refused");
        auto code=ParseMaterialAnchorInstructions({"MAD A.xyz, Color, {2.0404}, {-0.3324}","MUL A.w, AO.w, A.w"});MaterialAnchors found;found["AmbientOcclusion"]=MaterialAnchor::Register(0,"AO","x");auto corrected=CorrectMaterialAmbientOcclusion(code,found);Check(corrected.at("AmbientOcclusion").comps=="w","AO scalar destination mask");
        code=ParseMaterialAnchorInstructions({"MOV Original.xyz, N.xyz","MOV Copy.xyz, Original.xyz","MOV Original.x, {0}","MUL result_color0.xyz, Copy.xyz, Color.xyz"});found.clear();found["NormalWorld"]=MaterialAnchor::Register(0,"Original","xyz");found["stop"].line=3;auto forwarded=ForwardMaterialWorldNormal(code,found);Check(forwarded.at("NormalWorld").register_name=="Copy"&&forwarded.at("NormalWorld").line==1,"complete held normal MOV carrier");
        std::cout<<"material normal checks: "<<checks<<'\n';return 0;
    }catch(const std::exception&e) {std::cerr<<e.what()<<'\n';return 1;}
}
