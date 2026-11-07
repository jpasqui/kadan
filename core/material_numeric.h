// Portable NV_gpu_program5 material interpreter. Python oracle: nvasm_eval.py.
// Integer instructions truncate numbers, rather than reinterpreting float bits.
#pragma once
#if defined(_MSC_VER)
#pragma float_control(precise, on)
#endif
#if defined(__clang__)
#pragma clang fp contract(off)
#pragma clang fp reassociate(off)
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace P4MapKit
{
namespace Material
{
struct EvalError : std::runtime_error
{
    explicit EvalError(const std::string& s) : std::runtime_error(s)
    {
    }
};

// Sign/magnitude integers retain Python's unbounded ADD/MUL/NOT semantics.
struct Integer
{
    bool negative = false;
    std::vector<std::uint32_t> words;
    Integer(std::int64_t n = 0)
    {
        negative = n < 0;
        std::uint64_t u = negative ? std::uint64_t(0) - std::uint64_t(n) : std::uint64_t(n);
        if (u)
            words.push_back(std::uint32_t(u));
        if (u >> 32)
            words.push_back(std::uint32_t(u >> 32));
    }
    void normalize()
    {
        while (!words.empty() && !words.back())
            words.pop_back();
        if (words.empty())
            negative = false;
    }
    bool zero() const
    {
        return words.empty();
    }
    static int magnitude(const Integer& a, const Integer& b)
    {
        if (a.words.size() != b.words.size())
            return a.words.size() < b.words.size() ? -1 : 1;
        for (std::size_t i = a.words.size(); i; --i)
            if (a.words[i - 1] != b.words[i - 1])
                return a.words[i - 1] < b.words[i - 1] ? -1 : 1;
        return 0;
    }
    int compare(const Integer& b) const
    {
        if (negative != b.negative)
            return negative ? -1 : 1;
        return magnitude(*this, b) * (negative ? -1 : 1);
    }
    Integer negated() const
    {
        Integer r = *this;
        if (!zero())
            r.negative = !negative;
        return r;
    }
    static Integer add_magnitude(const Integer& a, const Integer& b)
    {
        Integer r;
        std::uint64_t carry = 0;
        for (std::size_t i = 0; i < std::max(a.words.size(), b.words.size()); ++i)
        {
            std::uint64_t n = carry + (i < a.words.size() ? a.words[i] : 0) + (i < b.words.size() ? b.words[i] : 0);
            r.words.push_back(std::uint32_t(n));
            carry = n >> 32;
        }
        if (carry)
            r.words.push_back(std::uint32_t(carry));
        return r;
    }
    static Integer sub_magnitude(const Integer& a, const Integer& b)
    {
        Integer r;
        std::uint64_t borrow = 0;
        for (std::size_t i = 0; i < a.words.size(); ++i)
        {
            std::uint64_t v = (i < b.words.size() ? b.words[i] : 0) + borrow;
            r.words.push_back(std::uint32_t(std::uint64_t(a.words[i]) - v));
            borrow = std::uint64_t(a.words[i]) < v;
        }
        r.normalize();
        return r;
    }
    Integer operator+(const Integer& b) const
    {
        Integer r;
        if (negative == b.negative)
        {
            r = add_magnitude(*this, b);
            r.negative = negative;
        }
        else
        {
            int c = magnitude(*this, b);
            r = c >= 0 ? sub_magnitude(*this, b) : sub_magnitude(b, *this);
            r.negative = c >= 0 ? negative : b.negative;
        }
        r.normalize();
        return r;
    }
    Integer operator-(const Integer& b) const
    {
        return *this + b.negated();
    }
    Integer operator*(const Integer& b) const
    {
        Integer r;
        r.words.assign(words.size() + b.words.size(), 0);
        for (std::size_t i = 0; i < words.size(); ++i)
        {
            std::uint64_t carry = 0;
            for (std::size_t j = 0; j < b.words.size(); ++j)
            {
                std::uint64_t n = std::uint64_t(words[i]) * b.words[j] + r.words[i + j] + carry;
                r.words[i + j] = std::uint32_t(n);
                carry = n >> 32;
            }
            if (i + b.words.size() < r.words.size())
                r.words[i + b.words.size()] = std::uint32_t(carry);
        }
        r.negative = negative != b.negative;
        r.normalize();
        return r;
    }
    Integer shifted_left(unsigned n) const
    {
        Integer r;
        r.negative = negative;
        r.words.assign(n / 32, 0);
        std::uint64_t carry = 0;
        for (auto w : words)
        {
            std::uint64_t t = (std::uint64_t(w) << (n % 32)) | carry;
            r.words.push_back(std::uint32_t(t));
            carry = t >> 32;
        }
        if (carry)
            r.words.push_back(std::uint32_t(carry));
        r.normalize();
        return r;
    }
    Integer shifted_right(unsigned n) const
    {
        if (negative)
            return ((*this).negated() + Integer(1).shifted_left(n) - Integer(1)).shifted_right(n).negated();
        Integer r;
        std::size_t skip = n / 32;
        for (std::size_t i = skip; i < words.size(); ++i)
        {
            std::uint64_t t = words[i];
            if (n % 32 && i + 1 < words.size())
                t |= std::uint64_t(words[i + 1]) << 32;
            r.words.push_back(std::uint32_t(t >> (n % 32)));
        }
        r.normalize();
        return r;
    }
    std::vector<std::uint32_t> twos(std::size_t size) const
    {
        std::vector<std::uint32_t> r = words;
        r.resize(size, 0);
        if (negative)
        {
            std::uint64_t carry = 1;
            for (auto& v : r)
            {
                std::uint64_t n = std::uint64_t(v ^ std::numeric_limits<std::uint32_t>::max()) + carry;
                v = std::uint32_t(n);
                carry = n >> 32;
            }
        }
        return r;
    }
    Integer bitwise(const Integer& b, char op) const
    {
        auto a = twos(std::max(words.size(), b.words.size()) + 1), bb = b.twos(a.size());
        for (std::size_t i = 0; i < a.size(); ++i)
            a[i] = op == '&' ? a[i] & bb[i] : op == '|' ? a[i] | bb[i] : a[i] ^ bb[i];
        Integer r;
        r.negative = (a.back() >> 31) != 0;
        r.words = a;
        if (r.negative)
        {
            std::uint64_t carry = 1;
            for (auto& v : r.words)
            {
                std::uint64_t n = std::uint64_t(v ^ std::numeric_limits<std::uint32_t>::max()) + carry;
                v = std::uint32_t(n);
                carry = n >> 32;
            }
        }
        r.normalize();
        return r;
    }
    std::uint32_t u32() const
    {
        std::uint32_t v = words.empty() ? 0 : words[0];
        return negative ? std::uint32_t(0) - v : v;
    }
    static Integer from_double(double d)
    {
        if (!std::isfinite(d))
            return Integer();
        bool neg = d < 0;
        d = std::trunc(std::fabs(d));
        if (d == 0)
            return Integer();
        int exp = 0;
        double fraction = std::frexp(d, &exp);
        std::uint64_t top = std::uint64_t(std::ldexp(fraction, 53));
        Integer r;
        r.words = {std::uint32_t(top), std::uint32_t(top >> 32)};
        r.normalize();
        r = exp >= 53 ? r.shifted_left(unsigned(exp - 53)) : r.shifted_right(unsigned(53 - exp));
        r.negative = neg;
        return r;
    }
    double number() const
    {
        if (zero())
            return 0.0;
        std::size_t bits = (words.size() - 1) * 32;
        std::uint32_t high = words.back();
        while (high)
        {
            ++bits;
            high >>= 1;
        }
        if (bits > 1024)
            throw EvalError("integer too large to convert to float");
        Integer a = *this;
        a.negative = false;
        std::size_t cut = bits > 53 ? bits - 53 : 0;
        Integer top = a.shifted_right(unsigned(cut));
        std::uint64_t n = top.words[0];
        if (top.words.size() > 1)
            n |= std::uint64_t(top.words[1]) << 32;
        if (cut)
        {
            Integer remainder = a - top.shifted_left(unsigned(cut)), half = Integer(1).shifted_left(unsigned(cut - 1));
            int c = remainder.compare(half);
            if (c > 0 || (c == 0 && (n & 1)))
                ++n;
        }
        double d = std::ldexp(double(n), int(cut));
        if (!std::isfinite(d))
            throw EvalError("integer too large to convert to float");
        return negative ? -d : d;
    }
    std::string key() const
    {
        std::ostringstream s;
        s << (negative ? '-' : '+');
        for (auto w : words)
            s << std::hex << std::setw(8) << std::setfill('0') << w;
        return s.str();
    }
    std::string decimal() const
    {
        if (zero())
            return "0";
        auto v = words;
        std::string result;
        while (!v.empty())
        {
            std::uint64_t rem = 0;
            for (std::size_t i = v.size(); i; --i)
            {
                std::uint64_t n = (rem << 32) | v[i - 1];
                v[i - 1] = std::uint32_t(n / 10);
                rem = n % 10;
            }
            result.push_back(char('0' + rem));
            while (!v.empty() && !v.back())
                v.pop_back();
        }
        if (negative)
            result.push_back('-');
        std::reverse(result.begin(), result.end());
        return result;
    }
    std::size_t bit_length() const
    {
        if (zero())
            return 0;
        std::size_t bits = (words.size() - 1) * 32;
        auto high = words.back();
        while (high)
        {
            ++bits;
            high >>= 1;
        }
        return bits;
    }
    static std::pair<Integer, Integer> divmod_positive(Integer a, const Integer& b)
    {
        if (b.zero())
            throw EvalError("integer division by zero");
        a.negative = false;
        Integer divisor = b;
        divisor.negative = false;
        Integer q;
        if (a.compare(divisor) < 0)
            return {q, a};
        auto shift = a.bit_length() - divisor.bit_length();
        divisor = divisor.shifted_left(unsigned(shift));
        q.words.assign(shift / 32 + 1, 0);
        for (std::size_t i = shift + 1; i; --i)
        {
            if (a.compare(divisor) >= 0)
            {
                a = a - divisor;
                q.words[(i - 1) / 32] |= std::uint32_t(1) << ((i - 1) % 32);
            }
            divisor = divisor.shifted_right(1);
        }
        q.normalize();
        return {q, a};
    }
    double divided_number(const Integer& b) const
    {
        if (b.zero())
            throw EvalError("integer division by zero");
        bool sign = negative != b.negative;
        if (zero())
            return sign ? -0.0 : 0.0;
        Integer a = *this, denominator = b;
        a.negative = denominator.negative = false;
        std::int64_t exponent = std::int64_t(a.bit_length()) - std::int64_t(denominator.bit_length());
        if (exponent > 1024)
            throw EvalError("integer division result too large for a float");
        if (exponent < -1075)
            return sign ? -0.0 : 0.0;
        bool below = exponent >= 0 ? a.compare(denominator.shifted_left(unsigned(exponent))) < 0
                                   : a.shifted_left(unsigned(-exponent)).compare(denominator) < 0;
        if (below)
            --exponent;
        int scale = int(std::max(std::int64_t(-1074), exponent - 52));
        if (scale < 0)
            a = a.shifted_left(unsigned(-scale));
        else
            denominator = denominator.shifted_left(unsigned(scale));
        auto qr = divmod_positive(a, denominator);
        int c = qr.second.shifted_left(1).compare(denominator);
        if (c > 0 || (c == 0 && (qr.first.u32() & 1)))
            qr.first = qr.first + Integer(1);
        double d = std::ldexp(qr.first.number(), scale);
        if (!std::isfinite(d))
            throw EvalError("integer division result too large for a float");
        return sign ? -d : d;
    }
    double logarithm() const
    {
        if (negative || zero())
            throw EvalError("math domain error");
        try
        {
            return std::log(number());
        }
        catch (const EvalError&)
        {
            auto bits = bit_length();
            unsigned cut = unsigned(bits - 53);
            Integer a = *this, top = shifted_right(cut);
            Integer remainder = a - top.shifted_left(cut), half = Integer(1).shifted_left(cut - 1);
            int c = remainder.compare(half);
            if (c > 0 || (c == 0 && (top.u32() & 1)))
                top = top + Integer(1);
            double fraction = std::ldexp(top.number(), -53);
            return std::log(fraction) + double(bits) * std::log(2.0);
        }
    }
};
struct Expression;
struct Value
{
    enum Kind
    {
        Float,
        Int,
        Boolean,
        Handle,
        Text,
        Expr,
        Null,
        Tuple
    } kind = Float;
    double real = 0.0;
    Integer integer;
    bool boolean = false;
    std::vector<std::pair<int, int>> handle;
    std::string text;
    std::vector<Value> tuple;
    std::shared_ptr<const Expression> expression;
    Value() = default;
    Value(double n) : kind(Float), real(n)
    {
    }
    Value(int n) : kind(Int), integer(n)
    {
    }
    Value(std::int64_t n) : kind(Int), integer(n)
    {
    }
    Value(const Integer& n) : kind(Int), integer(n)
    {
    }
    Value(bool n) : kind(Boolean), boolean(n)
    {
    }
    Value(const char* s) : kind(Text), text(s)
    {
    }
    Value(const std::string& s) : kind(Text), text(s)
    {
    }
    static Value word_handle(int b, int offset)
    {
        Value v;
        v.kind = Handle;
        v.handle.push_back({b, offset});
        return v;
    }
    static Value null()
    {
        Value v;
        v.kind = Null;
        return v;
    }
    static Value tuple_value(const std::vector<Value>& items)
    {
        Value v;
        v.kind = Tuple;
        v.tuple = items;
        return v;
    }
    double number() const
    {
        if (kind == Float)
            return real;
        if (kind == Int)
            return integer.number();
        if (kind == Boolean)
            return boolean ? 1.0 : 0.0;
        throw EvalError("a texture handle in arithmetic");
    }
    Integer as_int() const
    {
        if (kind == Int)
            return integer;
        if (kind == Boolean)
            return Integer(boolean ? 1 : 0);
        if (kind == Float)
            return Integer::from_double(real);
        throw EvalError("a texture handle in arithmetic");
    }
    bool truth() const
    {
        if (kind == Float)
            return real != 0;
        if (kind == Int)
            return !integer.zero();
        if (kind == Boolean)
            return boolean;
        throw EvalError("condition is not a number");
    }
};
using Vec = std::array<Value, 4>;
inline Vec splat(const Value& v)
{
    return {{v, v, v, v}};
}
inline double nan()
{
    return std::numeric_limits<double>::quiet_NaN();
}
inline double infinity()
{
    return std::numeric_limits<double>::infinity();
}
inline double divide(double a, double b)
{
    if (b == 0)
        return a == 0 || std::isnan(a) ? nan() : std::copysign(infinity(), a) * std::copysign(1.0, b);
    return a / b;
}
inline int compare_numbers(const Value& a, const Value& b)
{
    if (a.kind != Value::Float && b.kind != Value::Float)
        return a.as_int().compare(b.as_int());
    if (a.kind == Value::Float && b.kind == Value::Float)
        return a.real < b.real ? -1 : a.real > b.real ? 1 : 0;
    if (b.kind == Value::Float)
        return -compare_numbers(b, a);
    if (std::isinf(a.real))
        return a.real < 0 ? -1 : 1;
    int c = Integer::from_double(a.real).compare(b.as_int());
    if (c)
        return c;
    return a.real > std::trunc(a.real) ? 1 : a.real < std::trunc(a.real) ? -1 : 0;
}
inline bool has_nan(const Value& v)
{
    return v.kind == Value::Float && std::isnan(v.real);
}
inline double logarithm(const Value& v)
{
    return v.kind == Value::Float ? std::log(v.real) : v.as_int().logarithm();
}
inline Value numeric_operation(const std::string& name, const std::vector<Value>& args)
{
    for (const auto& a : args)
        if (a.kind == Value::Handle || a.kind == Value::Text || a.kind == Value::Expr)
            throw EvalError("a texture handle in arithmetic");
    if (args.empty())
        throw EvalError("operation without operands: " + name);
    const Value& a = args[0];
    auto b = [&]() -> const Value&
    {
        if (args.size() < 2)
            throw EvalError("missing operand: " + name);
        return args[1];
    };
    auto numeric = [&](char operation) -> Value
    {
        const auto& v = b();
        if (a.kind != Value::Float && v.kind != Value::Float)
        {
            Integer x = a.as_int(), y = v.as_int();
            return operation == '+' ? x + y : operation == '-' ? x - y : x * y;
        }
        double x = a.number(), y = v.number();
        return operation == '+' ? x + y : operation == '-' ? x - y : x * y;
    };
    if (name == "ADD")
        return numeric('+');
    if (name == "SUB")
        return numeric('-');
    if (name == "MUL")
        return numeric('*');
    if (name == "NEG")
        return a.kind == Value::Float ? Value(-a.real) : Value(a.as_int().negated());
    if (name == "ABS" || name == "U.ABS")
    {
        if (a.kind == Value::Float)
            return name == "U.ABS" && !std::isfinite(a.real) ? a : Value(std::fabs(a.real));
        Integer i = a.as_int();
        i.negative = false;
        return i;
    }
    if (name == "TOINT" || name == "ARRAYINDEX")
        return a.as_int();
    if (name == "SAT")
    {
        if (has_nan(a))
            return a;
        return compare_numbers(a, Value(0.0)) < 0 ? Value(0.0) : compare_numbers(a, Value(1.0)) > 0 ? Value(1.0) : a;
    }
    if (name == "MIN" || name == "MAX")
    {
        if (has_nan(a))
            return b();
        if (has_nan(b()))
            return a;
        int c = compare_numbers(a, b());
        return name == "MIN" ? (c <= 0 ? a : b()) : (c >= 0 ? a : b());
    }
    if (name == "TRUNC.U" || name == "TRUNC.S")
    {
        Value lo = name == "TRUNC.U" ? 0.0 : -2147483648.0, hi = name == "TRUNC.U" ? 4294967295.0 : 2147483647.0;
        if (has_nan(a))
            return Integer();
        if (compare_numbers(a, lo) < 0)
            return lo.as_int();
        if (compare_numbers(a, hi) > 0)
            return hi.as_int();
        return a.as_int();
    }
    if (name == "I2F.U")
        return double(a.as_int().u32());
    if (name == "I2F.S")
        return a.as_int().number();
    if (name.substr(0, 2) == "T.")
    {
        std::string t = name.substr(2);
        if (t == "TR")
            return true;
        if (t == "FL")
            return false;
        if (t == "NE")
            return a.truth();
        if (t == "EQ")
            return !a.truth();
        if (has_nan(a))
            return false;
        int c = compare_numbers(a, Value(0));
        if (t == "GT")
            return c > 0;
        if (t == "GE")
            return c >= 0;
        if (t == "LT")
            return c < 0;
        if (t == "LE")
            return c <= 0;
    }
    if (name.substr(0, 2) == "F." || name.substr(0, 2) == "S." || name.substr(0, 3) == "UC.")
    {
        bool f = name[0] == 'F', u = name.substr(0, 3) == "UC.";
        std::string op = name.substr(u ? 3 : 2);
        Value aa = a, bb = b();
        if (u)
        {
            if (aa.kind == Value::Int)
                aa = Integer(std::int64_t(aa.integer.u32()));
            if (bb.kind == Value::Int)
                bb = Integer(std::int64_t(bb.integer.u32()));
        }
        bool n = has_nan(aa) || has_nan(bb);
        int c = n ? 0 : compare_numbers(aa, bb);
        bool r = op == "SGT"   ? !n && c > 0
                 : op == "SLT" ? !n && c < 0
                 : op == "SGE" ? !n && c >= 0
                 : op == "SLE" ? !n && c <= 0
                 : op == "SEQ" ? !n && c == 0
                 : op == "SNE" ? n || c != 0
                               : false;
        return f ? Value(r ? 1.0 : 0.0) : Value(r ? -1 : 0);
    }
    if (name.substr(0, 2) == "I.")
    {
        Integer x = a.as_int();
        std::string op = name.substr(2);
        if (op == "NOT")
            return x.negated() - Integer(1);
        Integer y = b().as_int();
        if (op == "ADD")
            return x + y;
        if (op == "SUB")
            return x - y;
        if (op == "MUL")
            return x * y;
        if (op == "MAD")
        {
            if (args.size() != 3)
                throw EvalError("MAD operands");
            return x * y + args[2].as_int();
        }
        if (op == "AND")
            return x.bitwise(y, '&');
        if (op == "OR")
            return x.bitwise(y, '|');
        if (op == "XOR")
            return x.bitwise(y, '^');
        if (op == "SHL")
            return Integer(std::int64_t(x.shifted_left(y.u32() & 31).u32()));
        if (op == "SHR.U")
            return Integer(std::int64_t(x.u32() >> (y.u32() & 31)));
        if (op == "SHR.S")
            return x.shifted_right(y.u32() & 31);
        if (op == "MIN")
            return x.compare(y) <= 0 ? x : y;
        if (op == "MAX")
            return x.compare(y) >= 0 ? x : y;
        if (op == "DIV")
            return y.zero() ? Integer() : Integer::from_double(x.divided_number(y));
        if (op == "MOD")
            return y.zero() ? Integer() : Integer::from_double(std::fmod(x.number(), y.number()));
    }
    if (name == "U.FRC" && a.kind != Value::Float)
        return Integer();
    if (name == "U.SSG" && a.kind != Value::Float)
    {
        int c = a.as_int().compare(Integer());
        return c > 0 ? 1.0 : c < 0 ? -1.0 : 0.0;
    }
    if (name == "RSQ" && !has_nan(a) && compare_numbers(a, Value(0)) < 0)
        return nan();
    if (name == "LG2")
    {
        if (has_nan(a) || compare_numbers(a, Value(0)) < 0)
            return nan();
        if (!a.truth())
            return -infinity();
        return logarithm(a) / std::log(2.0);
    }
    if (name == "EX2")
    {
        try
        {
            return std::pow(2.0, a.number());
        }
        catch (const EvalError&)
        {
            return infinity();
        }
    }
    if (name == "DIV" && a.kind != Value::Float && b().kind != Value::Float)
    {
        auto ai = a.as_int(), bi = b().as_int();
        if (!bi.zero())
            return ai.divided_number(bi);
        if (ai.zero())
            return nan();
        return divide(ai.number(), 0.0);
    }
    if (name == "POW")
    {
        const auto& bv = b();
        if (has_nan(a) || has_nan(bv))
            return nan();
        Value absolute = numeric_operation("ABS", {a});
        int bsign = compare_numbers(bv, Value(0));
        if (!absolute.truth())
            return bsign == 0 ? 1.0 : bsign > 0 ? 0.0 : infinity();
        if (absolute.kind == Value::Float && absolute.real == infinity())
            return bsign == 0 ? 1.0 : bsign > 0 ? infinity() : 0.0;
        double power = logarithm(absolute) / std::log(2.0) * bv.number();
        return std::pow(2.0, power);
    }
    double x = a.number();
    if (name == "P.SQRT")
        return std::sqrt(x);
    if (name == "P.TAN")
        return std::tan(x);
    if (name == "P.ASIN")
        return std::asin(x);
    if (name == "P.ACOS")
        return std::acos(x);
    if (name == "P.ATAN")
        return std::atan(x);
    if (name == "P.ATAN2")
        return std::atan2(x, b().number());
    if (name == "P.FMOD")
        return b().number() == 0 ? 0.0 : std::fmod(x, b().number());
    if (name == "P.LOG10")
        return x > 0 ? std::log10(x) : -infinity();
    if (name == "P.EXP")
        return std::exp(x);
    if (name == "P.FROUND32")
    {
        volatile float f = static_cast<float>(x);
        return double(f);
    }
    if (name.substr(0, 2) == "U.")
    {
        if (!std::isfinite(x))
            return a;
        std::string op = name.substr(2);
        if (op == "MOV")
            return a;
        if (op == "FRC")
        {
            double fl = std::floor(x);
            return x - (fl == 0 ? 0.0 : fl);
        }
        if (op == "FLR")
        {
            double r = std::floor(x);
            return r == 0 ? 0.0 : r;
        }
        if (op == "CEIL")
        {
            double r = std::ceil(x);
            return r == 0 ? 0.0 : r;
        }
        if (op == "TRUNC")
        {
            double r = std::trunc(x);
            return r == 0 ? 0.0 : r;
        }
        if (op == "SSG")
            return x > 0 ? 1.0 : x < 0 ? -1.0 : 0.0;
        if (op == "ROUND")
        {
            double fl = std::floor(x), r = x - fl;
            double out = r < 0.5 ? fl : r > 0.5 ? fl + 1 : std::fmod(fl, 2) == 0 ? fl : fl + 1;
            return out == 0 ? 0.0 : out;
        }
    }
    if (name == "RSQ")
        return std::isnan(x) || x < 0 ? nan() : x == 0 ? infinity() : x == infinity() ? 0.0 : 1 / std::sqrt(x);
    if (name == "RCP")
        return divide(1, x);
    if (name == "LG2")
        return std::isnan(x) || x < 0 ? nan() : x == 0 ? -infinity() : std::log(x) / std::log(2.0);
    if (name == "EX2")
        return std::pow(2.0, x);
    if (name == "SIN")
        return std::isfinite(x) ? std::sin(x) : nan();
    if (name == "COS")
        return std::isfinite(x) ? std::cos(x) : nan();
    if (name == "DIV")
        return divide(x, b().number());
    if (name == "POW")
    {
        x = std::fabs(x);
        double y = b().number();
        if (std::isnan(x) || std::isnan(y))
            return nan();
        if (x == 0)
            return y == 0 ? 1.0 : y > 0 ? 0.0 : infinity();
        if (x == infinity())
            return y == 0 ? 1.0 : y > 0 ? infinity() : 0.0;
        return std::pow(2.0, std::log(x) / std::log(2.0) * y);
    }
    throw EvalError("operation " + name);
}

struct NumericEnv
{
    Vec position = {{0.5, 0.5, 0.5, 1.0}};
    Value facing = 1.0;
    std::map<int, Vec> attribs;
    std::map<int, std::map<int, Value>> buffers;
    std::function<std::map<int, Vec>(double, double)> attrib_function;
    std::function<Vec(int, int, const std::array<double, 2>&)> texture;
    std::function<Vec(const std::string&, const std::string&, int, int, const Vec&)> other;
    Vec attrib(int i) const
    {
        auto table = attrib_function ? attrib_function(position[0].number(), position[1].number()) : attribs;
        auto it = table.find(i);
        if (it == table.end())
            throw EvalError("no interpolant " + std::to_string(i));
        return it->second;
    }
    Value word(int buffer, int offset) const
    {
        auto b = buffers.find(buffer);
        if (b == buffers.end())
            return 0.0;
        auto w = b->second.find(offset);
        return w == b->second.end() ? Value(0.0) : w->second;
    }
    NumericEnv moved(int dx, int dy) const
    {
        NumericEnv r = *this;
        r.position[0] = position[0].number() + dx;
        r.position[1] = position[1].number() + dy;
        return r;
    }
};
inline std::string trim(const std::string& s)
{
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
inline bool starts(const std::string& s, const std::string& p)
{
    return s.compare(0, p.size(), p) == 0;
}
inline std::vector<std::string> split_operands(const std::string& s)
{
    std::vector<std::string> r;
    int depth = 0;
    std::string cur;
    for (char c : s)
    {
        if (c == '{' || c == '(' || c == '[')
            ++depth;
        if (c == '}' || c == ')' || c == ']')
            --depth;
        if (c == ',' && !depth)
        {
            r.push_back(trim(cur));
            cur.clear();
        }
        else
            cur += c;
    }
    if (!trim(cur).empty())
        r.push_back(trim(cur));
    return r;
}
inline std::vector<std::string> instructions(const std::string& program)
{
    std::vector<std::string> out;
    std::istringstream stream(program);
    std::string line;
    while (std::getline(stream, line))
    {
        line = trim(line);
        while (!line.empty() && line.back() == ';')
            line.pop_back();
        line = trim(line);
        bool declaration = false;
        for (const auto& p :
             {"!!", "OPTION", "CBUFFER", "ATTRIB", "TEMP", "LONG TEMP", "SHORT TEMP", "OUTPUT", "PARAM"})
            if (starts(line, p))
                declaration = true;
        if (line.empty() || line == "END" || declaration)
            continue;
        if (line.find('{') != std::string::npos)
            for (const auto& p : {"VAR:", "TEX:", "SAMP:", "LAST:"})
                if (line.find(p) != std::string::npos)
                    throw EvalError("resolve the program first (material_graph.resolve)");
        out.push_back(line);
    }
    return out;
}

// This base implements operand/instruction syntax once for numeric and symbolic machines.
class NumericMachine
{
public:
    std::vector<std::string> lines;
    NumericEnv env;
    std::map<std::string, Vec> reg;
    Vec cc = splat(0.0);
    bool killed = false;
    std::vector<std::size_t> kills;
    NumericMachine(std::vector<std::string> l, NumericEnv e = NumericEnv()) : lines(std::move(l)), env(std::move(e))
    {
    }
    virtual ~NumericMachine() = default;
    virtual Value operation(const std::string& name, const std::vector<Value>& a)
    {
        return numeric_operation(name, a);
    }
    virtual Value select(const Value& t, const Value& a, const Value& b)
    {
        return t.truth() ? a : b;
    }
    virtual Vec input(const std::string& kind, int i, int k = 0)
    {
        if (kind == "position")
            return env.position;
        if (kind == "facing")
            return splat(env.facing);
        if (kind == "attrib")
            return env.attrib(i);
        return splat(env.word(i, k));
    }
    Vec registered(const std::string& name) const
    {
        auto it = reg.find(name);
        return it == reg.end() ? splat(0.0) : it->second;
    }
    virtual Vec indexed(const std::string& name, const Value& at)
    {
        return registered(name + "[" + at.as_int().decimal() + "]");
    }
    virtual Vec read(std::string text)
    {
        static const std::regex swizzle_pattern("^(.*?)\\.([xyzw]+)$"),
            attrib_pattern("^fragment\\.attrib\\[(\\d+)\\]$"),
            index_pattern("^([A-Za-z_]\\w*)\\[([A-Za-z_]\\w*)\\.([xyzw])\\]$"),
            register_pattern("^[A-Za-z_]\\w*(\\[\\d+\\])?$");
        text = trim(text);
        bool neg = starts(text, "-");
        if (neg)
            text = trim(text.substr(1));
        bool absolute = text.size() >= 2 && text.front() == '|' && text.back() == '|';
        if (absolute)
            text = trim(text.substr(1, text.size() - 2));
        std::smatch m;
        std::string swizzle;
        if (std::regex_match(text, m, swizzle_pattern) && !m[1].str().empty() &&
            m[1].str().substr(m[1].str().size() > 8 ? m[1].str().size() - 8 : 0) != "fragment")
        {
            swizzle = m[2];
            text = m[1];
        }
        Vec v = splat(0.0);
        if (starts(text, "{"))
        {
            std::string literal = text;
            while (!literal.empty() && (literal.front() == '{' || literal.front() == '}'))
                literal.erase(literal.begin());
            while (!literal.empty() && (literal.back() == '{' || literal.back() == '}'))
                literal.pop_back();
            auto parts = split_operands(literal);
            if (parts.size() > 4)
                throw EvalError("literal vector has more than four components");
            for (std::size_t i = 0; i < parts.size(); ++i)
            {
                try
                {
                    char* end = nullptr;
                    double literal_number = std::strtod(parts[i].c_str(), &end);
                    if (end == parts[i].c_str() || *end)
                        throw EvalError("literal " + parts[i]);
                    v[i] = literal_number;
                }
                catch (const std::invalid_argument&)
                {
                    throw EvalError("literal " + parts[i]);
                }
            }
        }
        else if (text == "fragment.position" || text == "fragment_position")
            v = input("position", 0);
        else if (text == "fragment.facing")
            v = input("facing", 0);
        else if (std::regex_match(text, m, attrib_pattern))
            v = input("attrib", std::stoi(m[1]));
        else if (std::regex_match(text, m, index_pattern))
            v = indexed(m[1], registered(m[2])[component(m[3].str()[0])]);
        else
        {
            if (!std::regex_match(text, register_pattern))
                throw EvalError("operand " + text);
            v = registered(text);
        }
        if (!swizzle.empty())
        {
            if (swizzle.size() != 1 && swizzle.size() != 4)
                throw EvalError("a swizzle has 1 or 4 components: " + swizzle);
            Vec old = v;
            for (int i = 0; i < 4; ++i)
                v[i] = old[component(swizzle[swizzle.size() == 1 ? 0 : i])];
        }
        for (auto& x : v)
        {
            if (absolute)
                x = operation("ABS", {x});
            if (neg)
                x = operation("NEG", {x});
        }
        return v;
    }
    static int component(char c)
    {
        auto p = std::string("xyzw").find(c);
        if (p == std::string::npos)
            throw EvalError("component");
        return int(p);
    }
    virtual Vec condition(const std::string& text)
    {
        static const std::regex pattern("^(NE|EQ|GT|GE|LT|LE|TR|FL)(?:\\.([xyzw]+))?$");
        std::smatch m;
        std::string s = trim(text);
        if (!std::regex_match(s, m, pattern))
            throw EvalError("condition " + text);
        std::string swizzle = m[2].matched ? m[2].str() : "xyzw";
        if (swizzle.size() != 1 && swizzle.size() != 4)
            throw EvalError("a swizzle has 1 or 4 components: " + swizzle);
        Vec r;
        for (int i = 0; i < 4; ++i)
            r[i] = operation("T." + m[1].str(), {cc[component(swizzle[swizzle.size() == 1 ? 0 : i])]});
        return r;
    }
    virtual void write(const std::string& dst, Vec v, const std::vector<std::string>& mods)
    {
        static const std::regex pattern("^(\\w+(?:\\[\\d+\\])?)(?:\\.([xyzw]+))?(?:\\s*\\(([^)]*)\\))?$");
        std::smatch m;
        std::string d = trim(dst);
        if (!std::regex_match(d, m, pattern))
            throw EvalError("destination " + dst);
        std::string name = m[1], mask = m[2].matched ? m[2].str() : "xyzw";
        Vec allowed = m[3].matched ? condition(m[3]) : splat(true);
        if (has(mods, "SAT"))
            for (auto& x : v)
                x = operation("SAT", {x});
        Vec current = registered(name);
        for (char c : mask)
        {
            int k = component(c);
            current[k] = select(allowed[k], v[k], current[k]);
            if (has(mods, "CC"))
                cc[k] = select(allowed[k], v[k], cc[k]);
        }
        reg[name] = current;
    }
    static bool has(const std::vector<std::string>& mods, const std::string& s)
    {
        return std::find(mods.begin(), mods.end(), s) != mods.end();
    }
    std::pair<int, int> handle(const std::string& text)
    {
        static const std::regex pattern("^handle\\((\\w+)(?:\\.([xyzw]))?\\)$");
        std::smatch m;
        if (!std::regex_match(text, m, pattern))
            throw EvalError("texture " + text);
        auto v = registered(m[1])[component(m[2].matched ? m[2].str()[0] : 'x')];
        if (v.kind != Value::Handle || v.handle.empty())
            throw EvalError("a texture read without a handle");
        std::vector<int> words;
        for (const auto& w : v.handle)
            words.push_back(w.second);
        std::sort(words.begin(), words.end());
        int texture = -1, sampler = -1;
        for (int w : words)
        {
            if (w < 1352 && texture < 0)
                texture = w;
            if (w >= 1352 && sampler < 0)
                sampler = w;
        }
        return {texture, sampler};
    }
    virtual void record_kill(std::size_t i, const Value& dead)
    {
        if (dead.truth())
        {
            killed = true;
            kills.push_back(i);
        }
    }
    virtual Vec texture_read(const std::string& op, const std::string& target, int t, int s, const Vec& coordinates)
    {
        if ((op == "TEX" || op == "TXB" || op == "TXL") && target == "2D")
        {
            if (!env.texture)
                throw EvalError("texture read without a texture function");
            return env.texture(t, s, {{coordinates[0].number(), coordinates[1].number()}});
        }
        return env.other ? env.other(op, target, t, s, coordinates) : splat(0.0);
    }
    virtual Vec derivative(std::size_t index, const std::string& op, const std::string& source)
    {
        NumericMachine other(lines, env.moved(op == "DDX" ? 1 : 0, op == "DDY" ? 1 : 0));
        other.run(index);
        Vec a = read(source), b = other.read(source), r;
        for (int i = 0; i < 4; ++i)
            r[i] = numeric_operation("SUB", {b[i], a[i]});
        return r;
    }
    virtual void step(std::size_t index, const std::string& line)
    {
        auto space = line.find(' ');
        std::string head = line.substr(0, space), rest = space == std::string::npos ? "" : line.substr(space + 1);
        std::vector<std::string> mods;
        auto dot = head.find('.');
        std::string op = head.substr(0, dot);
        while (dot != std::string::npos)
        {
            auto next = head.find('.', dot + 1);
            mods.push_back(head.substr(dot + 1, next == std::string::npos ? next : next - dot - 1));
            dot = next;
        }
        auto operands = split_operands(rest);
        for (const auto& r :
             {"REP", "ENDREP", "BRK", "CAL", "RET", "LOADIM", "STOREIM", "LDB", "STB", "ATOM", "ATOMIM", "ATOMB"})
            if (op == r)
                throw EvalError("instruction " + op + " (the lighting part of a shader is not run)");
        auto operand = [&](std::size_t i) -> std::string
        {
            if (i >= operands.size())
                throw EvalError("missing operands: " + head);
            return operands[i];
        };
        if (op == "KIL")
        {
            Value dead;
            try
            {
                dead = condition(operand(0))[0];
            }
            catch (const EvalError&)
            {
                auto v = read(operand(0));
                dead = operation("F.SLT", {v[0], 0.0});
                for (int i = 1; i < 4; ++i)
                    dead = operation("MAX", {dead, operation("F.SLT", {v[i], 0.0})});
                dead = operation("T.NE", {dead});
            }
            record_kill(index, dead);
            return;
        }
        if (op == "LDC")
        {
            static const std::regex buffer_pattern("^buf(\\d+)\\[(\\d+)\\]$"), kind_pattern("^[FSU](32|16|8)");
            std::smatch m;
            auto source = operand(1);
            if (!std::regex_match(source, m, buffer_pattern))
                throw EvalError("LDC source " + source);
            int b = std::stoi(m[1]), offset = std::stoi(m[2]);
            if (has(mods, "U64"))
            {
                write(operand(0), splat(Value::word_handle(b, offset)), {});
                return;
            }
            std::string kind = "F32";
            for (auto& k : mods)
                if (std::regex_search(k, kind_pattern))
                {
                    kind = k;
                    break;
                }
            int wide = kind.size() >= 2 && kind.substr(kind.size() - 2) == "X4"   ? 4
                       : kind.size() >= 2 && kind.substr(kind.size() - 2) == "X2" ? 2
                                                                                  : 1;
            Vec v = splat(0.0);
            for (int i = 0; i < wide; ++i)
            {
                v[i] = input("buffer", b, offset + 4 * i)[0];
                if (kind[0] != 'F')
                    v[i] = operation("TOINT", {v[i]});
            }
            if (wide == 1)
                v = splat(v[0]);
            write(operand(0), v, mods);
            return;
        }
        if (op == "OR" && has(mods, "U64"))
        {
            auto a = read(operand(1)), b = read(operand(2));
            Vec v;
            for (int i = 0; i < 4; ++i)
            {
                v[i].kind = Value::Handle;
                v[i].handle = a[i].handle;
                v[i].handle.insert(v[i].handle.end(), b[i].handle.begin(), b[i].handle.end());
            }
            write(operand(0), v, {});
            return;
        }
        for (const auto& t : {"TEX", "TXB", "TXL", "TXF", "TXD", "TXP", "TXFMS", "TXQ", "TXG"})
            if (op == t)
            {
                auto h = handle(operand(2));
                write(operand(0),
                      texture_read(op, operands.size() > 3 ? operand(3) : "2D", h.first, h.second, read(operand(1))),
                      mods);
                return;
            }
        if (op == "DDX" || op == "DDY")
        {
            write(operand(0), derivative(index, op, operand(1)), mods);
            return;
        }
        std::vector<Vec> src;
        for (std::size_t i = 1; i < operands.size(); ++i)
            src.push_back(read(operands[i]));
        auto get = [&](int j, int k) -> Value
        {
            if (std::size_t(j) >= src.size())
                throw EvalError("missing operands: " + head);
            return src[j][k];
        };
        bool integer = has(mods, "S") || has(mods, "U");
        Vec v;
        std::string operation_name;
        if (op == "MOV")
            v = src.at(0);
        else if (op == "DP2" || op == "DP3" || op == "DP4")
        {
            Value total = 0;
            for (int i = 0; i < op[2] - '0'; ++i)
                total = operation("ADD", {total, operation("MUL", {get(0, i), get(1, i)})});
            v = splat(total);
        }
        else if (op == "RSQ" || op == "RCP" || op == "SIN" || op == "COS" || op == "EX2" || op == "LG2")
            v = splat(operation(op, {get(0, 0)}));
        else if (op == "POW")
            v = splat(operation(op, {get(0, 0), get(1, 0)}));
        else
            for (int i = 0; i < 4; ++i)
            {
                if (op == "MAD")
                    v[i] = integer ? operation("I.MAD", {get(0, i), get(1, i), get(2, i)})
                                   : operation("ADD", {operation("MUL", {get(0, i), get(1, i)}), get(2, i)});
                else if (op == "LRP")
                    v[i] = operation("ADD", {operation("MUL", {get(0, i), get(1, i)}),
                                             operation("MUL", {operation("SUB", {1.0, get(0, i)}), get(2, i)})});
                else if (op == "DIV" && !integer)
                    v[i] = operation("DIV", {get(0, i), get(1, 0)});
                else
                {
                    if (op == "TRUNC" && integer)
                        operation_name = has(mods, "U") ? "TRUNC.U" : "TRUNC.S";
                    else if (op == "I2F")
                        operation_name = has(mods, "U") ? "I2F.U" : "I2F.S";
                    else if (op == "SGT" || op == "SLT" || op == "SGE" || op == "SLE" || op == "SEQ" || op == "SNE")
                        operation_name = (integer ? (has(mods, "U") ? "UC." : "S.") : "F.") + op;
                    else if (integer &&
                             (op == "AND" || op == "OR" || op == "XOR" || op == "SHL" || op == "ADD" || op == "SUB" ||
                              op == "MUL" || op == "MIN" || op == "MAX" || op == "MOD" || op == "DIV" || op == "NOT"))
                        operation_name = "I." + op;
                    else if (integer && op == "SHR")
                        operation_name = has(mods, "U") ? "I.SHR.U" : "I.SHR.S";
                    else if (op == "FRC" || op == "FLR" || op == "CEIL" || op == "TRUNC" || op == "ABS" ||
                             op == "SSG" || op == "ROUND")
                        operation_name = "U." + op;
                    else
                        operation_name = op;
                    bool unary = op == "TRUNC" || op == "I2F" || op == "NOT" || starts(operation_name, "U.");
                    v[i] = unary ? operation(operation_name, {get(0, i)})
                                 : operation(operation_name, {get(0, i), get(1, i)});
                }
            }
        write(operand(0), v, mods);
    }
    std::size_t skip(std::size_t pc, const std::vector<std::string>& until) const
    {
        int depth = 0;
        for (std::size_t k = pc + 1; k < lines.size(); ++k)
        {
            if (starts(lines[k], "IF "))
                ++depth;
            else if (depth == 0 && has(until, lines[k]))
                return k;
            else if (lines[k] == "ENDIF")
                --depth;
        }
        throw EvalError("IF without ENDIF");
    }
    virtual NumericMachine& run(std::size_t stop = std::numeric_limits<std::size_t>::max())
    {
        for (std::size_t pc = 0; pc < lines.size() && pc < stop; ++pc)
        {
            if (starts(lines[pc], "IF "))
            {
                if (!condition(lines[pc].substr(3))[0].truth())
                    pc = skip(pc, {"ELSE", "ENDIF"});
            }
            else if (lines[pc] == "ELSE")
                pc = skip(pc, {"ENDIF"});
            else if (lines[pc] != "ENDIF")
                step(pc, lines[pc]);
        }
        return *this;
    }
};
inline NumericMachine trace(const std::vector<std::string>& lines, const NumericEnv& env = NumericEnv(),
                            std::size_t stop = std::numeric_limits<std::size_t>::max())
{
    NumericMachine m(lines, env);
    m.run(stop);
    return m;
}
inline NumericMachine run(const std::string& program, const NumericEnv& env = NumericEnv())
{
    return trace(instructions(program), env);
}
}
}
namespace p4
{
namespace mapkit
{
namespace material = ::P4MapKit::Material;
}
}
