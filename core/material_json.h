// Small bounded JSON boundary for portable material stages. No Unreal or Python dependency.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace P4MapKit
{
namespace Material
{
struct JsonError : std::runtime_error
{
    explicit JsonError(const std::string& s) : std::runtime_error(s)
    {
    }
};
struct Json
{
    enum Kind
    {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    } kind = Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Json> array;
    std::map<std::string, Json> object;
    Json() = default;
    Json(bool v) : kind(Boolean), boolean(v)
    {
    }
    Json(double v) : kind(Number), number(v)
    {
    }
    Json(int v) : kind(Number), number(v)
    {
    }
    Json(const char* v) : kind(String), string(v)
    {
    }
    Json(const std::string& v) : kind(String), string(v)
    {
    }
    explicit Json(const std::vector<Json>& v) : kind(Array), array(v)
    {
    }
    explicit Json(const std::map<std::string, Json>& v) : kind(Object), object(v)
    {
    }
    static Json MakeArray()
    {
        Json v;
        v.kind = Array;
        return v;
    }
    static Json MakeObject()
    {
        Json v;
        v.kind = Object;
        return v;
    }
    bool Has(const std::string& key) const
    {
        return kind == Object && object.count(key) != 0;
    }
    const Json& At(const std::string& key) const
    {
        if (kind != Object)
            throw JsonError("json-object-required");
        auto i = object.find(key);
        if (i == object.end())
            throw JsonError("json-missing-key: " + key);
        return i->second;
    }
    Json& operator[](const std::string& key)
    {
        if (kind == Null)
            kind = Object;
        if (kind != Object)
            throw JsonError("json-object-required");
        return object[key];
    }
    const Json& operator[](const std::string& key) const
    {
        return At(key);
    }
    std::string Text() const
    {
        if (kind != String)
            throw JsonError("json-string-required");
        return string;
    }
    double Numeric() const
    {
        if (kind != Number)
            throw JsonError("json-number-required");
        return number;
    }
    std::string Dump() const;
    static Json Parse(const std::string& text, std::size_t maxBytes = 64u * 1024u * 1024u,
                      std::size_t maxNodes = 1000000, std::size_t maxDepth = 128);
};

namespace JsonDetail
{
inline void Utf8(std::string& s, std::uint32_t c)
{
    if (c <= 0x7f)
        s.push_back(char(c));
    else if (c <= 0x7ff)
    {
        s.push_back(char(0xc0 | (c >> 6)));
        s.push_back(char(0x80 | (c & 63)));
    }
    else if (c <= 0xffff)
    {
        s.push_back(char(0xe0 | (c >> 12)));
        s.push_back(char(0x80 | ((c >> 6) & 63)));
        s.push_back(char(0x80 | (c & 63)));
    }
    else
    {
        s.push_back(char(0xf0 | (c >> 18)));
        s.push_back(char(0x80 | ((c >> 12) & 63)));
        s.push_back(char(0x80 | ((c >> 6) & 63)));
        s.push_back(char(0x80 | (c & 63)));
    }
}
inline bool ValidUtf8(const std::string& s)
{
    for (std::size_t i = 0; i < s.size();)
    {
        auto c = static_cast<unsigned char>(s[i++]);
        if (c < 128)
            continue;
        int n = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : -1;
        if (n < 0 || std::size_t(n) > s.size() - i)
            return false;
        std::uint32_t v = c & ((1u << (6 - n)) - 1);
        for (int j = 0; j < n; ++j)
        {
            c = static_cast<unsigned char>(s[i++]);
            if ((c & 0xc0) != 0x80)
                return false;
            v = (v << 6) | (c & 63);
        }
        if (v < (n == 1 ? 0x80u : n == 2 ? 0x800u : 0x10000u) || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff))
            return false;
    }
    return true;
}
inline std::string Quote(const std::string& s)
{
    if (!ValidUtf8(s))
        throw JsonError("json-invalid-utf8");
    std::ostringstream o;
    o << '"';
    const char* hex = "0123456789abcdef";
    for (unsigned char c : s)
    {
        switch (c)
        {
            case '"':
                o << "\\\"";
                break;
            case '\\':
                o << "\\\\";
                break;
            case '\b':
                o << "\\b";
                break;
            case '\f':
                o << "\\f";
                break;
            case '\n':
                o << "\\n";
                break;
            case '\r':
                o << "\\r";
                break;
            case '\t':
                o << "\\t";
                break;
            default:
                if (c < 32)
                    o << "\\u00" << hex[c >> 4] << hex[c & 15];
                else
                    o << char(c);
        }
    }
    o << '"';
    return o.str();
}
class Parser
{
    const std::string& s;
    std::size_t p = 0, count = 0, limit, depth;
    void Fail(const std::string& why) const
    {
        throw JsonError(why + " at byte " + std::to_string(p));
    }
    void Space()
    {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n'))
            ++p;
    }
    bool Take(char c)
    {
        Space();
        if (p < s.size() && s[p] == c)
        {
            ++p;
            return true;
        }
        return false;
    }
    std::uint32_t Hex()
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (p == s.size())
                Fail("json-truncated-unicode");
            char c = s[p++];
            int d = c >= '0' && c <= '9'   ? c - '0'
                    : c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                           : -1;
            if (d < 0)
                Fail("json-invalid-unicode");
            v = v * 16 + std::uint32_t(d);
        }
        return v;
    }
    std::string StringValue()
    {
        if (!Take('"'))
            Fail("json-string-required");
        std::string out;
        while (p < s.size())
        {
            unsigned char c = static_cast<unsigned char>(s[p++]);
            if (c == '"')
            {
                if (!ValidUtf8(out))
                    Fail("json-invalid-utf8");
                return out;
            }
            if (c < 32)
                Fail("json-control-character");
            if (c != '\\')
            {
                out.push_back(char(c));
                continue;
            }
            if (p == s.size())
                Fail("json-truncated-escape");
            char e = s[p++];
            switch (e)
            {
                case '"':
                case '\\':
                case '/':
                    out.push_back(e);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u':
                {
                    auto v = Hex();
                    if (v >= 0xd800 && v <= 0xdbff)
                    {
                        if (p + 2 > s.size() || s[p] != '\\' || s[p + 1] != 'u')
                            Fail("json-unpaired-surrogate");
                        p += 2;
                        auto w = Hex();
                        if (w < 0xdc00 || w > 0xdfff)
                            Fail("json-unpaired-surrogate");
                        v = 0x10000 + ((v - 0xd800) << 10) + (w - 0xdc00);
                    }
                    else if (v >= 0xdc00 && v <= 0xdfff)
                        Fail("json-unpaired-surrogate");
                    Utf8(out, v);
                    break;
                }
                default:
                    Fail("json-invalid-escape");
            }
        }
        Fail("json-unterminated-string");
        return out;
    }
    Json Value(std::size_t level)
    {
        if (level > depth)
            Fail("json-depth-limit");
        if (++count > limit)
            Fail("json-node-limit");
        Space();
        if (p == s.size())
            Fail("json-truncated");
        if (s[p] == '"')
            return Json(StringValue());
        if (Take('{'))
        {
            Json v = Json::MakeObject();
            if (Take('}'))
                return v;
            do
            {
                auto key = StringValue();
                if (!Take(':'))
                    Fail("json-colon-required");
                auto item = Value(level + 1);
                if (!v.object.emplace(key, std::move(item)).second)
                    Fail("json-duplicate-key: " + key);
                if (Take('}'))
                    return v;
            } while (Take(','));
            Fail("json-object-end-required");
        }
        if (Take('['))
        {
            Json v = Json::MakeArray();
            if (Take(']'))
                return v;
            do
            {
                v.array.push_back(Value(level + 1));
                if (Take(']'))
                    return v;
            } while (Take(','));
            Fail("json-array-end-required");
        }
        for (const auto& word :
             std::vector<std::pair<std::string, Json>>{{"null", Json()}, {"true", Json(true)}, {"false", Json(false)}})
        {
            if (s.compare(p, word.first.size(), word.first) == 0)
            {
                p += word.first.size();
                return word.second;
            }
        }
        auto start = p;
        if (s[p] == '-')
            ++p;
        if (p == s.size())
            Fail("json-number-required");
        if (s[p] == '0')
            ++p;
        else if (s[p] >= '1' && s[p] <= '9')
        {
            while (p < s.size() && s[p] >= '0' && s[p] <= '9')
                ++p;
        }
        else
            Fail("json-number-required");
        if (p < s.size() && s[p] == '.')
        {
            ++p;
            auto at = p;
            while (p < s.size() && s[p] >= '0' && s[p] <= '9')
                ++p;
            if (at == p)
                Fail("json-fraction-required");
        }
        if (p < s.size() && (s[p] == 'e' || s[p] == 'E'))
        {
            ++p;
            if (p < s.size() && (s[p] == '+' || s[p] == '-'))
                ++p;
            auto at = p;
            while (p < s.size() && s[p] >= '0' && s[p] <= '9')
                ++p;
            if (at == p)
                Fail("json-exponent-required");
        }
        std::istringstream input(s.substr(start, p - start));
        input.imbue(std::locale::classic());
        double n;
        input >> n;
        if (!input || !std::isfinite(n))
            Fail("json-nonfinite-number");
        return Json(n);
    }

public:
    Parser(const std::string& source, std::size_t nodes, std::size_t levels) : s(source), limit(nodes), depth(levels)
    {
    }
    Json Run()
    {
        auto v = Value(0);
        Space();
        if (p != s.size())
            Fail("json-trailing-input");
        return v;
    }
};
}
inline Json Json::Parse(const std::string& text, std::size_t maxBytes, std::size_t maxNodes, std::size_t maxDepth)
{
    if (text.size() > maxBytes)
        throw JsonError("json-byte-limit");
    return JsonDetail::Parser(text, maxNodes, maxDepth).Run();
}
inline std::string Json::Dump() const
{
    switch (kind)
    {
        case Null:
            return "null";
        case Boolean:
            return boolean ? "true" : "false";
        case String:
            return JsonDetail::Quote(string);
        case Number:
        {
            if (!std::isfinite(number))
                throw JsonError("json-nonfinite-number");
            std::ostringstream s;
            s.imbue(std::locale::classic());
            s << std::setprecision(std::numeric_limits<double>::max_digits10) << number;
            return s.str();
        }
        case Array:
        {
            std::string s = "[";
            for (const auto& v : array)
            {
                if (s.size() > 1)
                    s += ',';
                s += v.Dump();
            }
            return s + "]";
        }
        case Object:
        {
            std::string s = "{";
            for (const auto& p : object)
            {
                if (s.size() > 1)
                    s += ',';
                s += JsonDetail::Quote(p.first) + ":" + p.second.Dump();
            }
            return s + "}";
        }
    }
    throw JsonError("json-invalid-kind");
}
}
} // P4MapKit::Material
