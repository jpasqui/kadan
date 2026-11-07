// NV_gpu_program5 assembly: lexer, parser and structured block tree.
#include "nvasm_program.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace nvasm::detail {

// ---------------------------------------------------------------- lexer

enum class Tok { Ident, Number, Punct };
struct Token {
    Tok kind;
    std::string_view text; // into the source
};
// A statement is a run of the lexer's single token array.
struct Statement {
    int line;
    const Token* tokens;
    size_t count;
};
struct Lexed {
    std::vector<Token> tokens;
    std::vector<std::pair<int, size_t>> statements; // line, first token; ends at the next statement
    Statement at(size_t i) const {
        const size_t first = statements[i].second;
        const size_t end = i + 1 < statements.size() ? statements[i + 1].second : tokens.size();
        return {statements[i].first, tokens.data() + first, end - first};
    }
};

bool ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

Lexed lex(std::string_view src, Stage& stage) {
    size_t pos = 0;
    int line = 1;
    while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) {
        if (src[pos] == '\n') ++line;
        ++pos;
    }
    static const std::pair<const char*, Stage> headers[] = {
        {"!!NVvp5.0", Stage::Vertex}, {"!!NVtcp5.0", Stage::TessControl}, {"!!NVtep5.0", Stage::TessEval},
        {"!!NVgp5.0", Stage::Geometry}, {"!!NVfp5.0", Stage::Fragment}, {"!!NVcp5.0", Stage::Compute}};
    bool found = false;
    for (auto& [text, s] : headers) {
        size_t n = std::strlen(text);
        if (src.substr(pos, n) == text) {
            stage = s;
            pos += n;
            found = true;
            break;
        }
    }
    if (!found) throw Error(line, "missing !!NV<stage>5.0 header");

    Lexed out;
    out.tokens.reserve(src.size() / 4);
    size_t first = 0; // current statement's first token
    int cur_line = line;
    auto add = [&](Tok kind, size_t b, size_t e) {
        if (out.tokens.size() == first) cur_line = line;
        out.tokens.push_back({kind, src.substr(b, e - b)});
    };
    bool ended = false;
    while (pos < src.size()) {
        char c = src[pos];
        if (c == '\n') { ++line; ++pos; continue; }
        if (std::isspace(static_cast<unsigned char>(c)) || c == '\0') { ++pos; continue; }
        if (c == '#') {
            while (pos < src.size() && src[pos] != '\n') ++pos;
            continue;
        }
        if (ended) throw Error(line, "text after END");
        if (c == ';') {
            if (out.tokens.size() == first) throw Error(line, "empty statement");
            out.statements.push_back({cur_line, first});
            first = out.tokens.size();
            ++pos;
            continue;
        }
        if (ident_start(c)) {
            size_t b = pos;
            while (pos < src.size() && ident_char(src[pos])) ++pos;
            add(Tok::Ident, b, pos);
            if (out.tokens.size() == first + 1 && out.tokens.back().text == "END") {
                ended = true;
                out.tokens.pop_back();
            }
            continue;
        }
        if (c == '0' && pos + 2 < src.size() && (src[pos + 1] == 'x' || src[pos + 1] == 'X') &&
            std::isxdigit(static_cast<unsigned char>(src[pos + 2]))) {
            size_t b = pos;
            pos += 2;
            while (pos < src.size() && std::isxdigit(static_cast<unsigned char>(src[pos]))) ++pos;
            add(Tok::Number, b, pos);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            size_t b = pos;
            while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) ++pos;
            // Targets such as 2D, 3D and 2DMS start with a digit.
            if (pos < src.size() && ident_start(src[pos]) &&
                !((src[pos] == 'e' || src[pos] == 'E') && pos + 1 < src.size() &&
                  (std::isdigit(static_cast<unsigned char>(src[pos + 1])) || src[pos + 1] == '-' || src[pos + 1] == '+'))) {
                while (pos < src.size() && ident_char(src[pos])) ++pos;
                add(Tok::Ident, b, pos);
                continue;
            }
            if (pos + 1 < src.size() && src[pos] == '.' && std::isdigit(static_cast<unsigned char>(src[pos + 1]))) {
                ++pos;
                while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) ++pos;
            }
            if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
                size_t e = pos + 1;
                if (e < src.size() && (src[e] == '-' || src[e] == '+')) ++e;
                if (e < src.size() && std::isdigit(static_cast<unsigned char>(src[e]))) {
                    pos = e;
                    while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) ++pos;
                }
            }
            add(Tok::Number, b, pos);
            continue;
        }
        if (std::strchr(",[]{}().|-+=", c)) {
            add(Tok::Punct, pos, pos + 1);
            ++pos;
            continue;
        }
        throw Error(line, std::string("unexpected character '") + c + "'");
    }
    if (!ended) throw Error(line, "missing END");
    if (out.tokens.size() != first) throw Error(cur_line, "statement without ';'");
    return out;
}

bool is_swizzle(std::string_view s) {
    if (s.empty() || s.size() > 4) return false;
    bool xyzw = s.find_first_not_of("xyzw") == std::string_view::npos;
    bool rgba = s.find_first_not_of("rgba") == std::string_view::npos;
    return xyzw || rgba;
}

int comp_index(char c) {
    switch (c) {
    case 'x': case 'r': return 0;
    case 'y': case 'g': return 1;
    case 'z': case 'b': return 2;
    case 'w': case 'a': return 3;
    }
    return -1;
}

const std::set<std::string, std::less<>> kTargets = {
    "1D", "2D", "3D", "CUBE", "RECT", "ARRAY1D", "ARRAY2D", "ARRAYCUBE", "SHADOW1D", "SHADOW2D", "SHADOWRECT",
    "SHADOWCUBE", "SHADOWARRAY1D", "SHADOWARRAY2D", "SHADOWARRAYCUBE", "BUFFER", "2DMS", "ARRAY2DMS"};

namespace {
// kTargets as a flat list: the parser asks about every identifier.
bool is_target(std::string_view s) {
    static constexpr std::string_view targets[] = {
        "1D", "2D", "3D", "CUBE", "RECT", "ARRAY1D", "ARRAY2D", "ARRAYCUBE", "SHADOW1D", "SHADOW2D", "SHADOWRECT",
        "SHADOWCUBE", "SHADOWARRAY1D", "SHADOWARRAY2D", "SHADOWARRAYCUBE", "BUFFER", "2DMS", "ARRAY2DMS"};
    for (auto t : targets)
        if (t == s) return true;
    return false;
}
} // namespace

bool is_cc_test(std::string_view s) {
    static constexpr std::string_view base[] = {"EQ", "GE", "GT", "LE", "LT", "NE", "FL", "TR", "NAN", "LEG",
                                                "CF", "NCF", "OF", "NOF", "SF", "NSF", "AB", "BLE"};
    if (s.size() < 2 || s.size() > 4) return false;
    auto known = [&](std::string_view t) {
        for (auto b : base)
            if (b == t) return true;
        return false;
    };
    if (known(s)) return true;
    if (s.back() == '0' || s.back() == '1') return known(s.substr(0, s.size() - 1));
    return false;
}

class Parser {
public:
    Parser(Statement st) : st_(st) {}

    bool done() const { return pos_ >= st_.count; }
    const Token& peek(size_t k = 0) const {
        static const Token none{Tok::Punct, ""};
        return pos_ + k < st_.count ? st_.tokens[pos_ + k] : none;
    }
    bool at(const char* text, size_t k = 0) const { return peek(k).text == text; }
    bool accept(const char* text) {
        if (!at(text)) return false;
        ++pos_;
        return true;
    }
    void expect(const char* text) {
        if (!accept(text)) fail(std::string("expected '") + text + "' before '" + std::string(peek().text) + "'");
    }
    std::string ident() {
        if (peek().kind != Tok::Ident) fail("expected identifier before '" + std::string(peek().text) + "'");
        return std::string(st_.tokens[pos_++].text);
    }
    long long integer() {
        bool neg = accept("-");
        if (peek().kind != Tok::Number) fail("expected number before '" + std::string(peek().text) + "'");
        // Tokens end at a non-digit, so strtoll stops at the token's end.
        long long v = std::strtoll(st_.tokens[pos_++].text.data(), nullptr, 10);
        return neg ? -v : v;
    }
    [[noreturn]] void fail(const std::string& message) const { throw Error(st_.line, message); }

    // "{ program.buffer[3] }" style single binding inside braces, or a bare binding.
    Binding binding_ref(int* last = nullptr) {
        Binding b;
        std::string path = ident();
        while (true) {
            if (accept("[")) {
                int first = static_cast<int>(integer());
                if (accept(".")) {
                    expect(".");
                    int second = static_cast<int>(integer());
                    if (last) *last = second;
                    else fail("range not allowed here");
                } else if (last) {
                    *last = first;
                }
                expect("]");
                if (path == "vertex") b.vertex = first, path += "[]";
                else b.index = first;
                continue;
            }
            if (at(".") && peek(1).kind == Tok::Ident && !is_swizzle(peek(1).text)) {
                ++pos_;
                path += "." + ident();
                continue;
            }
            break;
        }
        b.path = path;
        return b;
    }

    Operand operand() {
        Operand o;
        bool neg = false;
        if (accept("-")) neg = true;
        else accept("+");
        if (accept("|")) {
            o = primary();
            expect("|");
            o.abs = true;
        } else {
            o = primary();
        }
        o.neg = neg;
        return o;
    }

    Operand primary() {
        Operand o;
        if (accept("{")) {
            o.kind = Operand::Const;
            do {
                bool neg = accept("-");
                accept("+");
                if (peek().kind != Tok::Number) {
                    // inf/nan spellings would appear here; refuse rather than guess.
                    fail("unsupported constant '" + std::string(peek().text) + "'");
                }
                std::string text(neg ? "-" : "");
                text += st_.tokens[pos_++].text;
                o.consts.push_back(std::move(text));
            } while (accept(","));
            expect("}");
            if (o.consts.size() > 4) fail("constant vector longer than four");
            suffix(o);
            return o;
        }
        if (accept("(")) {
            o.kind = Operand::CCTest;
            o.name = ident();
            if (!is_cc_test(o.name)) fail("unknown condition test " + o.name);
            suffix(o);
            expect(")");
            return o;
        }
        if (peek().kind != Tok::Ident) fail("unexpected '" + std::string(peek().text) + "'");
        const std::string_view word = peek().text;
        if (word == "handle" && at("(", 1)) {
            pos_ += 2;
            o.kind = Operand::Handle;
            o.inner = std::make_shared<Operand>(operand());
            expect(")");
            if (accept(".")) {
                std::string c = ident();
                if (c.size() != 1 || comp_index(c[0]) < 0) fail("bad handle component");
                o.handle_comp = comp_index(c[0]);
            }
            return o;
        }
        if (word == "offset" && at("(", 1)) {
            pos_ += 2;
            o = operand();
            if (o.kind != Operand::Const) fail("texel offset must be constant");
            o.kind = Operand::Offset;
            expect(")");
            return o;
        }
        if (is_target(word)) {
            ++pos_;
            o.kind = Operand::Target;
            o.name = std::string(word);
            return o;
        }
        if (is_cc_test(word)) {
            ++pos_;
            o.kind = Operand::CCTest;
            o.name = std::string(word);
            suffix(o);
            return o;
        }
        if (word == "fragment" || word == "vertex" || word == "result" || word == "invocation" || word == "primitive") {
            o.kind = Operand::Bind;
            o.bind = binding_ref();
            suffix(o);
            predicate(o);
            return o;
        }
        ++pos_;
        o.kind = Operand::Var;
        o.name = std::string(word);
        if (accept("[")) {
            o.indexed = true;
            if (peek().kind == Tok::Number || at("-")) {
                o.index = static_cast<int>(integer());
            } else {
                o.index_reg = ident();
                expect(".");
                std::string c = ident();
                if (c.size() != 1 || comp_index(c[0]) < 0) fail("relative index needs one component");
                o.index_comp = comp_index(c[0]);
                if (accept("+")) o.index = static_cast<int>(integer());
                else if (accept("-")) o.index = -static_cast<int>(integer());
            }
            expect("]");
        }
        suffix(o);
        predicate(o);
        return o;
    }

    void suffix(Operand& o) {
        if (at(".") && peek(1).kind == Tok::Ident && is_swizzle(peek(1).text)) {
            ++pos_;
            o.suffix = ident();
        }
    }
    void predicate(Operand& o) {
        if (at("(") && peek(1).kind == Tok::Ident && is_cc_test(peek(1).text)) {
            ++pos_;
            o.pred = ident();
            if (accept(".")) {
                std::string s = ident();
                if (!is_swizzle(s)) fail("bad condition swizzle");
                o.pred_suffix = s;
            }
            expect(")");
        }
    }

private:
    const Statement st_;
    size_t pos_ = 0;
};

Program parse(std::string_view src) {
    Program p;
    const Lexed lexed = lex(src, p.stage);
    p.code.reserve(lexed.statements.size());
    for (size_t si = 0; si < lexed.statements.size(); ++si) {
        const Statement st = lexed.at(si);
        Parser ps(st);
        std::string head = ps.ident();
        std::string interp;
        if (head == "FLAT" || head == "NOPERSPECTIVE" || head == "CENTROID") {
            if (head == "CENTROID") ps.fail("CENTROID attributes are not supported yet");
            interp = head == "FLAT" ? "flat" : "noperspective";
            head = ps.ident();
            if (head != "ATTRIB") ps.fail("interpolation modifier on " + head);
        }
        if (head == "OPTION") {
            p.options.insert(ps.ident());
        } else if (head == "CBUFFER" || head == "STORAGE" || head == "SHARED") {
            std::string name = ps.ident();
            ps.expect("[");
            if (!ps.accept("]")) {
                ps.integer();
                ps.expect("]");
            }
            ps.expect("=");
            ps.expect("{");
            Binding b = ps.binding_ref();
            ps.expect("}");
            if (head == "CBUFFER") {
                if (b.path != "program.buffer" || b.index < 0) ps.fail("CBUFFER must bind program.buffer[N]");
                p.cbuffers[name] = b.index;
            } else if (head == "STORAGE") {
                if (b.path != "program.storage" || b.index < 0) ps.fail("STORAGE must bind program.storage[N]");
                p.storage[name] = b.index;
            } else {
                if (b.path != "program.sharedmem") ps.fail("SHARED must bind program.sharedmem");
                p.shared_name = name;
            }
        } else if (head == "SHARED_MEMORY") {
            p.shared_bytes = static_cast<uint32_t>(ps.integer());
        } else if (head == "TEMP" || head == "SHORT" || head == "LONG") {
            TempKind kind = TempKind::Float;
            if (head != "TEMP") {
                kind = head == "SHORT" ? TempKind::Short : TempKind::Long;
                if (ps.ident() != "TEMP") ps.fail(head + " must precede TEMP");
            }
            do {
                std::string name = ps.ident();
                Temp t{kind, 0, 0};
                if (ps.accept("[")) {
                    t.array = static_cast<int>(ps.integer());
                    ps.expect("]");
                }
                auto [it, fresh] = p.temps.try_emplace(name, t);
                if (fresh) {
                    it->second.id = int(p.temp_list.size());
                    p.temp_list.push_back(name);
                } else {
                    it->second.kind = t.kind;
                    it->second.array = t.array;
                }
            } while (ps.accept(","));
        } else if (head == "ATTRIB" || head == "OUTPUT") {
            std::string name = ps.ident();
            bool array = false;
            if (ps.accept("[")) {
                ps.expect("]");
                array = true;
            }
            ps.expect("=");
            AttribRange r;
            r.interpolation = interp;
            if (array) {
                ps.expect("{");
                int last = -1;
                Binding b = ps.binding_ref(&last);
                ps.expect("}");
                r.path = b.path;
                r.first = b.index;
                r.count = last - b.index + 1;
                if (r.count <= 0) ps.fail("empty binding range");
            } else {
                Binding b = ps.binding_ref();
                r.path = b.path;
                r.first = b.index;
                r.count = 1;
            }
            if (!interp.empty() && r.path == "fragment.attrib")
                for (int i = 0; i < r.count; ++i) p.interpolation[r.first + i] = interp;
            p.aliases[name] = r;
        } else if (head == "GROUP_SIZE") {
            for (int i = 0; i < 3 && !ps.done(); ++i) p.group[i] = static_cast<uint32_t>(ps.integer());
        } else if (head == "PRIMITIVE_IN") {
            p.prim_in = ps.ident();
        } else if (head == "PRIMITIVE_OUT") {
            p.prim_out = ps.ident();
        } else if (head == "VERTICES_OUT") {
            p.vertices_out = static_cast<int>(ps.integer());
        } else if (head == "INVOCATIONS") {
            p.invocations = static_cast<int>(ps.integer());
        } else {
            Instr in;
            in.line = st.line;
            in.op = std::move(head);
            while (ps.accept(".")) in.mods.push_back(ps.ident());
            in.ops.reserve(4);
            if (!ps.done()) {
                do in.ops.push_back(ps.operand());
                while (ps.accept(","));
            }
            if (!ps.done()) ps.fail("unexpected '" + std::string(ps.peek().text) + "' after operands");
            p.code.push_back(std::move(in));
            continue;
        }
        if (!ps.done()) ps.fail("unexpected '" + std::string(ps.peek().text) + "' in declaration");
    }
    // Name the registers once, so the passes after this look them up by index.
    p.temp_info.resize(p.temp_list.size());
    for (const auto& [name, t] : p.temps) p.temp_info[size_t(t.id)] = t;
    auto resolve = [&](auto& self, Operand& o) -> void {
        if (o.inner) self(self, *o.inner);
        if (o.kind != Operand::Var) return;
        auto t = p.temps.find(o.name);
        if (t != p.temps.end()) o.temp = t->second.id;
        if (!o.index_reg.empty()) {
            auto i = p.temps.find(o.index_reg);
            if (i != p.temps.end()) o.index_temp = i->second.id;
        }
    };
    for (auto& in : p.code)
        for (auto& o : in.ops) resolve(resolve, o);
    return p;
}

std::vector<Node> build_tree(std::vector<Instr>&& code) {
    std::vector<Node> root;
    struct Frame {
        Node node;
        bool in_else = false;
    };
    std::vector<Frame> stack;
    auto sink = [&]() -> std::vector<Node>& {
        if (stack.empty()) return root;
        return stack.back().in_else ? stack.back().node.other : stack.back().node.body;
    };
    for (auto& in : code) {
        if (in.op == "IF" || in.op == "REP") {
            Frame f;
            f.node.kind = in.op == "IF" ? Node::If : Node::Loop;
            f.node.ins = std::move(in);
            stack.push_back(std::move(f));
        } else if (in.op == "ELSE") {
            if (stack.empty() || stack.back().node.kind != Node::If || stack.back().in_else)
                throw Error(in.line, "ELSE without IF");
            stack.back().in_else = true;
        } else if (in.op == "ENDIF" || in.op == "ENDREP") {
            Node::Kind want = in.op == "ENDIF" ? Node::If : Node::Loop;
            if (stack.empty() || stack.back().node.kind != want) throw Error(in.line, in.op + " without opener");
            Node n = std::move(stack.back().node);
            stack.pop_back();
            sink().push_back(std::move(n));
        } else if (in.op == "CAL" || in.op == "RET" || in.op == "BRA") {
            throw Error(in.line, in.op + " (subroutines) are not supported");
        } else {
            Node n;
            n.ins = std::move(in);
            sink().push_back(std::move(n));
        }
    }
    if (!stack.empty()) throw Error(stack.back().node.ins.line, "unterminated block");
    return root;
}

} // namespace nvasm::detail
