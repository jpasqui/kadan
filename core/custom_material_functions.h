// Function expansion on the graph boundary; no editor objects or asset reads.
#pragma once
#include "material_json.h"
#include <algorithm>
#include <set>
namespace P4MapKit
{
namespace CustomMaterial
{
namespace FunctionDetail
{
using Json = Material::Json;
inline const Json& get(const Json& n, const std::string& k)
{
    static const Json null;
    return n.Has(k) ? n.At(k) : null;
}
inline std::string text(const Json& n, const std::string& k, std::string d = "")
{
    auto& v = get(n, k);
    return v.kind == Json::Null ? d : v.Text();
}
inline bool flag(const Json& n, const std::string& k)
{
    auto& v = get(n, k);
    return v.kind == Json::Boolean  ? v.boolean
           : v.kind == Json::Number ? v.number != 0
           : v.kind == Json::String ? (v.string == "True" || v.string == "true" || v.string == "1")
                                    : false;
}
inline std::pair<std::string, std::string> declared(const Json& v)
{
    if (v.kind == Json::Array)
    {
        if (v.array.empty())
            throw std::runtime_error("empty function declaration");
        return {v.array[0].Text(), v.array.size() > 1 && v.array[1].kind != Json::Null ? v.array[1].Text() : ""};
    }
    return {v.Text(), ""};
}
inline std::string package(std::string s)
{
    auto a = s.find('\'');
    if (a != std::string::npos)
    {
        auto b = s.find('\'', a + 1);
        if (b == std::string::npos)
            throw std::runtime_error("invalid function package");
        s = s.substr(a + 1, b - a - 1);
    }
    if (s.size() > 1 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return s.substr(0, s.find('.'));
}
inline Json expand(Json graph, const Json& functions, std::set<std::string> stack, std::size_t depth)
{
    if (depth > 64)
        throw std::runtime_error("function nesting limit");
    auto& nodes = graph["nodes"];
    if (nodes.kind != Json::Object)
        throw std::runtime_error("function nodes must be an object");
    std::vector<std::string> calls;
    for (auto& p : nodes.object)
        if (text(p.second, "type") == "MaterialFunctionCall" && !flag(get(p.second, "props"), "Inlined"))
            calls.push_back(p.first);
    for (auto& id : calls)
    {
        auto call = nodes.At(id);
        auto& p = get(call, "props");
        auto& ins = get(call, "inputs");
        auto path = package(text(p, "MaterialFunction"));
        if (path.empty() || path == "None")
            throw std::runtime_error("material function missing");
        if (stack.count(path))
            throw std::runtime_error("material function calls itself: " + path);
        if (!functions.Has(path))
            throw std::runtime_error("material function not supplied: " + path);
        auto next = stack;
        next.insert(path);
        auto body = expand(functions.At(path), functions, next, depth + 1);
        std::vector<std::pair<std::string, std::string>> decl;
        for (auto& x : get(p, "FunctionInputs").array)
            decl.push_back(declared(x));
        struct Output
        {
            std::string id, name, guid;
            double priority;
        };
        std::vector<Output> results;
        for (auto& item : body.At("nodes").object)
        {
            auto n = item.second;
            auto& props = n["props"];
            if (props.kind == Json::Null)
                props = Json::MakeObject();
            auto& inputs = n["inputs"];
            if (inputs.kind == Json::Null)
                inputs = Json::MakeObject();
            for (auto& pin : inputs.object)
            {
                auto& l = pin.second;
                if (l.kind != Json::Array || l.array.size() < 2)
                    throw std::runtime_error("invalid function link");
                l.array[0] = id + "/" + l.array[0].Text();
            }
            auto kind = text(n, "type");
            if (kind == "FunctionInput" && !flag(props, "Inlined"))
            {
                props["Inlined"] = true;
                auto wanted = text(props, "InputName", "In"), guid = text(props, "Id");
                int at = -1;
                for (std::size_t j = 0; j < decl.size(); j++)
                    if (!guid.empty() && decl[j].second == guid)
                    {
                        at = int(j);
                        break;
                    }
                if (at < 0)
                    for (std::size_t j = 0; j < decl.size(); j++)
                        if (decl[j].first == wanted)
                        {
                            at = int(j);
                            break;
                        }
                Json link;
                if (at >= 0)
                {
                    link = get(ins, "FunctionInputs" + std::to_string(at));
                    if (link.kind == Json::Null)
                        link = get(ins, decl[at].first);
                }
                if (link.kind == Json::Null)
                    link = get(ins, wanted);
                if (link.kind != Json::Null)
                    inputs["Input"] = link;
            }
            else if (kind == "FunctionOutput" && !flag(props, "Inlined"))
            {
                props["Inlined"] = true;
                auto& v = get(props, "SortPriority");
                double priority = v.kind == Json::Number ? v.Numeric() : 0;
                results.push_back(
                    {id + "/" + item.first, text(props, "OutputName", "Result"), text(props, "Id"), priority});
            }
            auto newId = id + "/" + item.first;
            if (nodes.Has(newId))
                throw std::runtime_error("function node id collision: " + newId);
            nodes[newId] = n;
            if (nodes.object.size() > 100000)
                throw std::runtime_error("expanded node limit");
        }
        if (results.empty())
            throw std::runtime_error("material function has no output: " + path);
        std::vector<Output> ordered;
        auto& declaredOutputs = get(p, "FunctionOutputs");
        if (!declaredOutputs.array.empty())
        {
            for (auto& x : declaredOutputs.array)
            {
                auto wanted = declared(x);
                auto found = results.end();
                if (!wanted.second.empty())
                    found = std::find_if(results.begin(), results.end(),
                                         [&](const Output& r) { return r.guid == wanted.second; });
                if (found == results.end())
                    found = std::find_if(results.begin(), results.end(),
                                         [&](const Output& r) { return r.name == wanted.first; });
                if (found == results.end())
                    throw std::runtime_error("function output missing: " + wanted.first);
                ordered.push_back(*found);
            }
        }
        else
        {
            ordered = results;
            std::stable_sort(ordered.begin(), ordered.end(),
                             [](const Output& a, const Output& b) { return a.priority < b.priority; });
        }
        call["props"]["Inlined"] = true;
        call["inputs"] = Json::MakeObject();
        for (std::size_t j = 0; j < ordered.size(); j++)
        {
            auto l = Json::MakeArray();
            l.array = {Json(ordered[j].id), Json("")};
            call["inputs"]["Output" + std::to_string(j)] = l;
        }
        nodes[id] = call;
    }
    graph.object.erase("functions");
    return graph;
}
}
inline Material::Json InlineFunctions(const Material::Json& graph, const Material::Json& supplied = Material::Json())
{
    auto functions = supplied.kind == Material::Json::Null ? FunctionDetail::get(graph, "functions") : supplied;
    auto normalized = Material::Json::MakeObject();
    for (auto& p : functions.object)
        normalized[FunctionDetail::package(p.first)] = p.second;
    return FunctionDetail::expand(graph, normalized, {}, 0);
}
}
}
