#include "../core/material_rewrite.h"
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
        const auto request=Json::Parse(line);SymbolicArena arena;Lifter lifter(arena);std::map<std::string,Value> nodes;
        for(const auto&f:request.At("formulas").array) {std::vector<Value> args;for(const auto&a:f.At("args").array)args.push_back(Atom(a,nodes));nodes[f.At("id").Text()]=arena.mk(f.At("op").Text(),args);}
        std::vector<Value> values;for(const auto&v:request.At("values").array)values.push_back(Atom(v,nodes));
        auto stage=request.At("stage").Text();
        if(stage=="frame"||stage=="combined")values=frame_transforms(arena,values);
        if(stage=="facing"||stage=="combined")values=facing_signs(arena,values);
        auto samples=normal_samples(lifter,values);
        Json formulas=Json::MakeArray(),outputs=Json::MakeArray(),normal=Json::MakeArray();std::map<const Expression*,std::string> seen;
        for(const auto&v:values)outputs.array.push_back(Encode(v,seen,formulas));
        std::vector<std::string> sample_ids;for(const auto&read:samples) {Value v;v.kind=Value::Expr;v.expression=read;sample_ids.push_back(Encode(v,seen,formulas).At("ref").Text());}
        std::sort(sample_ids.begin(),sample_ids.end());for(const auto&id:sample_ids)normal.array.push_back(id);
        answer["formulas"]=formulas;answer["values"]=outputs;answer["normal_samples"]=normal;
    }catch(const std::exception&e) {answer["error"]=e.what();}std::cout<<answer.Dump()<<'\n';}return 0;
}
int main(int argc,char**) {
    if(argc>1)return Fixtures();try {
        SymbolicArena arena;Lifter lifter(arena);
        auto sample=arena.mk("SAMPLE",{"Normal","auto/SSM_FromTextureAsset",0.0,0.0});
        auto packed=[&](int c) {return arena.mk("COMP",{sample,c});};
        auto unpack=[&](int c) {return arena.mk("ADD",{arena.mk("MUL",{packed(c),2}),-1});};
        auto nx=unpack(0),ny=unpack(1);
        Check(normal_samples(lifter,{nx,ny}).count(sample.expression)==1,"normal sample selection");
        Check(normal_samples(lifter,{nx,ny,packed(0)}).empty(),"raw channel defeats normal selection");
        Check(normal_samples(lifter,{nx,ny,arena.mk("MUL",{packed(2),3})}).empty(),"packed B use defeats normal selection");
        auto tx=source(arena,"TangentX",Value::null(),0),tz=source(arena,"TangentZ",Value::null(),0);
        auto transformed=frame_transforms(arena,{arena.mk("ADD",{arena.mk("MUL",{tx,2.0}),arena.mk("MUL",{tz,3.0})})});
        Check(same(transformed[0],arena.mk("COMP",{arena.mk("TOWORLD",{2.0,0.0,3.0}),0})),"tangent to world sparse rows");
        auto vector=arena.mk("SRC",{"VectorParameter","Direction"});
        auto rowdot=[&](const std::string&name) {std::vector<Value> terms;for(int c=0;c<3;++c)terms.push_back(arena.mk("MUL",{source(arena,name,Value::null(),c),arena.mk("COMP",{vector,c})}));return arena.mk("ADD",{arena.mk("ADD",{terms[0],terms[1]}),terms[2]});};
        auto dotz=rowdot("TangentZ"),dotx=rowdot("TangentX");
        Check(same(frame_transforms(arena,{dotz})[0],dotz),"isolated TangentZ dot retained");
        auto dots=frame_transforms(arena,{dotz,dotx});
        for(int c=0;c<2;++c)Check(is(dots[c],"COMP")&&is(dots[c].expression->args[0],"TOTANGENT"),"paired frame row dot");
        auto facing=source(arena,"Facing"),unknown=source(arena,"Unmodeled");
        auto sign=arena.mk("SEL",{arena.mk("F.SGE",{facing,0}),1.0,-1.0});
        Check(same(facing_signs(arena,{sign})[0],source(arena,"TwoSidedSign")),"facing sign recognition");
        auto lazy=arena.mk("MUL",{facing,arena.mk("SEL",{true,1.0,unknown})});
        Check(same(facing_signs(arena,{lazy})[0],source(arena,"TwoSidedSign")),"facing lazy unreachable input");
        auto bad=arena.mk("MUL",{facing,unknown});Check(same(facing_signs(arena,{bad})[0],bad),"unknown facing input retained");
        Value deep=tx;for(int i=0;i<12000;++i)deep=arena.mk("ADD",{deep,1.0});
        RewriteMemo memo;std::size_t visits=0;auto copy=rewrite(arena,deep,[&](const Value&v) {++visits;return v;},memo);
        Check(same(copy,deep)&&visits==12002,"iterative DAG rewrite");
        auto shared=arena.mk("MUL",{deep,deep});memo.clear();visits=0;rewrite(arena,shared,[&](const Value&v) {++visits;return v;},memo);Check(visits==12003,"shared DAG rewritten once");
        std::cout<<"material rewrite checks: "<<checks<<'\n';return 0;
    }catch(const std::exception&e) {std::cerr<<e.what()<<'\n';return 1;}
}
