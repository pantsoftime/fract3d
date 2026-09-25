#include "formula.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

// ------------------------------------------------------------------ syntax tree
// Parsed once; turned into GLSL for the GPU, or evaluated directly by FormulaVM.
struct Node {
    enum Kind { Num, Var, Pixel, Param, Maxit, Whitesq, Neg, Bin, Assign, Call, Mod2, Pair } kind = Num;
    std::complex<double> num;
    int idx = 0;     // Var: variable; Param: 0..4; Call: function (kFormulaFunctions, or kFn1 + k for fn1..fn4)
    std::string op;  // Bin: + - * / ^ < <= > >= == != && ||
    std::vector<std::unique_ptr<Node>> kids;
};
struct Stmt {
    std::unique_ptr<Node> expr;  // an expression statement, or...
    // ...an if / elseif / else block: (condition, body) pairs, then the else body
    std::vector<std::pair<std::unique_ptr<Node>, std::vector<Stmt>>> branches;
    std::vector<Stmt> elseBody;
};
struct FormulaAst {
    std::vector<std::string> vars;  // variable names; z is among them
    int zIndex = 0;
    std::vector<Stmt> init, loop;   // the loop's last statement is the bailout test
};

namespace {
constexpr int kFn1 = 1000;

// ------------------------------------------------------------------ tokens
enum class T { Num, Ident, Op, Newline, LBrace, RBrace, End };
struct Tok {
    T type;
    std::string text;
    double num = 0;
    int line = 1;
};

struct ParseError : std::runtime_error {
    int line;
    ParseError(int l, const std::string& m) : std::runtime_error(m), line(l) {}
};

std::vector<Tok> tokenize(const std::string& s) {
    std::vector<Tok> out;
    int line = 1;
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (c == ';') {  // comment to end of line
            while (i < s.size() && s[i] != '\n') i++;
            continue;
        }
        if (c == '\n') {
            out.push_back({T::Newline, "\n", 0, line});
            line++;
            i++;
            continue;
        }
        if (isspace((unsigned char)c)) {
            i++;
            continue;
        }
        if (c == '\\' && i + 1 < s.size() && s[i + 1] == '\n') {  // explicit line continuation
            i += 2;
            line++;
            continue;
        }
        if (isdigit((unsigned char)c) || (c == '.' && i + 1 < s.size() && isdigit((unsigned char)s[i + 1]))) {
            char* end = nullptr;
            double v = strtod(s.c_str() + i, &end);
            size_t len = end - (s.c_str() + i);
            out.push_back({T::Num, s.substr(i, len), v, line});
            i += len;
            continue;
        }
        if (isalpha((unsigned char)c) || c == '_') {
            size_t j = i;
            while (j < s.size() && (isalnum((unsigned char)s[j]) || s[j] == '_')) j++;
            std::string id = s.substr(i, j - i);
            std::transform(id.begin(), id.end(), id.begin(), ::tolower);  // Fractint is case-insensitive
            out.push_back({T::Ident, id, 0, line});
            i = j;
            continue;
        }
        if (c == '{') { out.push_back({T::LBrace, "{", 0, line}); i++; continue; }
        if (c == '}') { out.push_back({T::RBrace, "}", 0, line}); i++; continue; }
        static const char* two[] = {"==", "!=", "<=", ">=", "&&", "||"};
        bool matched = false;
        for (auto* op : two)
            if (s.compare(i, 2, op) == 0) {
                out.push_back({T::Op, op, 0, line});
                i += 2;
                matched = true;
                break;
            }
        if (matched) continue;
        if (std::string("+-*/^(),:=<>|").find(c) != std::string::npos) {
            out.push_back({T::Op, std::string(1, c), 0, line});
            i++;
            continue;
        }
        throw ParseError(line, std::string("unexpected character '") + c + "'");
    }
    out.push_back({T::End, "", 0, line});
    return out;
}

int functionIndex(const std::string& n) {
    for (int i = 0; i < kFormulaFunctionCount; i++)
        if (n == kFormulaFunctions[i]) return i;
    if (n.size() == 3 && n[0] == 'f' && n[1] == 'n' && n[2] >= '1' && n[2] <= '4') return kFn1 + (n[2] - '1');
    return -1;
}
bool isKeyword(const std::string& n) { return n == "if" || n == "elseif" || n == "else" || n == "endif"; }

// ------------------------------------------------------------------ parser
struct Parser {
    std::vector<Tok> t;
    size_t p = 0;
    int parenDepth = 0;
    std::map<std::string, int> varIndex;
    std::vector<std::string> varNames;
    std::set<int> assigned;
    std::map<int, int> firstUse;  // variable -> line where it's read
    bool usesFn[4] = {false, false, false, false};
    bool usesP[kFormulaParams] = {};
    bool usesWhitesq = false;

    const Tok& peek() {
        // inside parentheses and |...| newlines are just whitespace
        while (parenDepth > 0 && t[p].type == T::Newline) p++;
        return t[p];
    }
    Tok next() {
        peek();
        return t[p++];
    }
    bool isOp(const char* op) { return peek().type == T::Op && peek().text == op; }
    bool isWord(const char* w) { return peek().type == T::Ident && peek().text == w; }
    void expect(const char* op) {
        if (!isOp(op)) throw ParseError(peek().line, std::string("expected '") + op + "'");
        next();
    }
    int var(const std::string& n) {
        auto it = varIndex.find(n);
        if (it != varIndex.end()) return it->second;
        varNames.push_back(n);
        return varIndex[n] = (int)varNames.size() - 1;
    }
    static std::unique_ptr<Node> make(Node::Kind k) {
        auto n = std::make_unique<Node>();
        n->kind = k;
        return n;
    }
    static std::unique_ptr<Node> bin(const std::string& op, std::unique_ptr<Node> a, std::unique_ptr<Node> b) {
        auto n = make(Node::Bin);
        n->op = op;
        n->kids.push_back(std::move(a));
        n->kids.push_back(std::move(b));
        return n;
    }

    std::unique_ptr<Node> expression() { return assignment(); }

    std::unique_ptr<Node> assignment() {
        // IDENT '=' expr   (right-associative: z = c = pixel)
        if (peek().type == T::Ident && t[p + 1].type == T::Op && t[p + 1].text == "=") {
            Tok id = next();
            next();  // '='
            static const std::set<std::string> ro = {"pixel", "p1", "p2", "p3", "p4", "p5", "pi", "e", "maxit", "whitesq"};
            if (ro.count(id.text)) throw ParseError(id.line, "'" + id.text + "' is read-only");
            if (functionIndex(id.text) >= 0) throw ParseError(id.line, "'" + id.text + "' is a function name");
            if (isKeyword(id.text)) throw ParseError(id.line, "'" + id.text + "' is a keyword");
            auto n = make(Node::Assign);
            n->idx = var(id.text);
            assigned.insert(n->idx);
            n->kids.push_back(assignment());
            return n;
        }
        return logical();
    }

    std::unique_ptr<Node> logical() {
        auto a = comparison();
        while (isOp("&&") || isOp("||")) {
            std::string op = next().text;
            a = bin(op, std::move(a), comparison());
        }
        return a;
    }

    std::unique_ptr<Node> comparison() {
        auto a = additive();
        for (const char* op : {"<=", ">=", "==", "!=", "<", ">"})
            if (isOp(op)) {
                next();
                return bin(op, std::move(a), additive());  // Fractint compares real parts
            }
        return a;
    }

    std::unique_ptr<Node> additive() {
        auto a = term();
        while (isOp("+") || isOp("-")) {
            std::string op = next().text;
            a = bin(op, std::move(a), term());
        }
        return a;
    }

    std::unique_ptr<Node> term() {
        auto a = unary();
        while (isOp("*") || isOp("/")) {
            std::string op = next().text;
            a = bin(op, std::move(a), unary());
        }
        return a;
    }

    std::unique_ptr<Node> unary() {
        if (isOp("-")) {
            next();
            auto n = make(Node::Neg);
            n->kids.push_back(unary());
            return n;
        }
        if (isOp("+")) {
            next();
            return unary();
        }
        return power();
    }

    std::unique_ptr<Node> power() {
        auto a = primary();
        if (isOp("^")) {
            next();
            return bin("^", std::move(a), unary());  // right-associative
        }
        return a;
    }

    std::unique_ptr<Node> primary() {
        Tok k = next();
        if (k.type == T::Num) {
            auto n = make(Node::Num);
            n->num = k.num;
            return n;
        }
        if (k.type == T::Op && k.text == "(") {
            parenDepth++;
            auto a = expression();
            if (isOp(",")) {  // complex literal (re, im)
                next();
                auto n = make(Node::Pair);
                n->kids.push_back(std::move(a));
                n->kids.push_back(expression());
                expect(")");
                parenDepth--;
                return n;
            }
            expect(")");
            parenDepth--;
            return a;
        }
        if (k.type == T::Op && k.text == "|") {  // |x| is the squared modulus in Fractint
            parenDepth++;
            auto n = make(Node::Mod2);
            n->kids.push_back(additive());
            expect("|");
            parenDepth--;
            return n;
        }
        if (k.type == T::Ident) {
            if (isOp("(")) {  // function call
                int f = functionIndex(k.text);
                if (f < 0) throw ParseError(k.line, "unknown function '" + k.text + "'");
                next();
                parenDepth++;
                auto n = make(Node::Call);
                n->idx = f;
                n->kids.push_back(expression());
                expect(")");
                parenDepth--;
                if (f >= kFn1) usesFn[f - kFn1] = true;
                return n;
            }
            if (k.text == "pixel") return make(Node::Pixel);
            if (k.text == "maxit") return make(Node::Maxit);
            if (k.text == "whitesq") {
                usesWhitesq = true;
                return make(Node::Whitesq);
            }
            if (k.text == "pi" || k.text == "e") {
                auto n = make(Node::Num);
                n->num = k.text == "pi" ? 3.14159265358979323846 : 2.71828182845904523536;
                return n;
            }
            if (k.text.size() == 2 && k.text[0] == 'p' && k.text[1] >= '1' && k.text[1] <= '5') {
                auto n = make(Node::Param);
                n->idx = k.text[1] - '1';
                usesP[n->idx] = true;
                return n;
            }
            if (functionIndex(k.text) >= 0) throw ParseError(k.line, "'" + k.text + "' is a function: write " + k.text + "(...)");
            if (isKeyword(k.text)) throw ParseError(k.line, "'" + k.text + "' can't be used here");
            auto n = make(Node::Var);
            n->idx = var(k.text);
            firstUse.emplace(n->idx, k.line);
            return n;
        }
        if (k.type == T::End || k.type == T::RBrace) throw ParseError(k.line, "unexpected end of formula");
        if (k.type == T::Newline) throw ParseError(k.line, "the statement ends too early (something is missing before the end of the line)");
        throw ParseError(k.line, "unexpected '" + k.text + "'");
    }

    // Statements until ':' (when untilColon), '}', or one of the given keywords
    // (elseif/else/endif, which end an if block). Separated by ',' or newlines.
    std::vector<Stmt> block(bool untilColon, bool& sawColon, bool inIf) {
        std::vector<Stmt> out;
        sawColon = false;
        for (;;) {
            while (peek().type == T::Newline || isOp(",")) next();
            if (peek().type == T::RBrace) {
                if (inIf) throw ParseError(peek().line, "'if' without 'endif'");
                return out;
            }
            if (peek().type == T::End) throw ParseError(peek().line, "missing '}'");
            if (untilColon && isOp(":")) {
                next();
                sawColon = true;
                return out;
            }
            if (isWord("elseif") || isWord("else") || isWord("endif")) {
                if (!inIf) throw ParseError(peek().line, "'" + peek().text + "' without 'if'");
                return out;
            }
            Stmt s;
            if (isWord("if")) {
                next();
                auto cond = expression();
                bool dummy;
                auto body = block(false, dummy, true);
                s.branches.push_back({std::move(cond), std::move(body)});
                for (;;) {
                    if (isWord("elseif")) {
                        next();
                        auto c = expression();
                        auto b = block(false, dummy, true);
                        s.branches.push_back({std::move(c), std::move(b)});
                    } else if (isWord("else")) {
                        next();
                        s.elseBody = block(false, dummy, true);
                    } else {
                        expect_word("endif");
                        break;
                    }
                }
            } else {
                s.expr = expression();
            }
            out.push_back(std::move(s));
            const Tok& n = peek();
            if (n.type == T::End) throw ParseError(n.line, "missing '}' at the end of the formula");
            bool ok = n.type == T::Newline || n.type == T::RBrace || (n.type == T::Op && (n.text == "," || n.text == ":")) ||
                      (n.type == T::Ident && (n.text == "elseif" || n.text == "else" || n.text == "endif" || n.text == "if"));
            if (!ok) throw ParseError(n.line, "expected ',' or a new line after a statement, found '" + n.text + "'");
        }
    }
    void expect_word(const char* w) {
        if (!isWord(w)) throw ParseError(peek().line, std::string("expected '") + w + "'");
        next();
    }
};

// ------------------------------------------------------------------ GLSL
const char* kPrelude = R"(
// ---- complex functions for formulas (vec2 = a + bi)
vec2 frm_mod2(vec2 a) { return vec2(dot(a, a), 0.0); }
vec2 frm_exp(vec2 a) { return exp(a.x) * vec2(cos(a.y), sin(a.y)); }
vec2 frm_log(vec2 a) { return vec2(log(length(a)), atan(a.y, a.x)); }
vec2 frm_sqr(vec2 a) { return csqr(a); }
vec2 frm_sqrt(vec2 a) {
    float r = length(a);
    vec2 s = vec2(sqrt(max(0.5 * (r + a.x), 0.0)), sqrt(max(0.5 * (r - a.x), 0.0)));
    return a.y < 0.0 ? vec2(s.x, -s.y) : s;
}
vec2 frm_pow(vec2 a, vec2 b) {
    if (b.y == 0.0 && b.x == floor(b.x) && abs(b.x) <= 32.0) {  // exact integer powers
        int n = int(abs(b.x));
        vec2 r = vec2(1, 0);
        for (int i = 0; i < n; i++) r = cmul(r, a);
        return b.x < 0.0 ? cdiv(vec2(1, 0), r) : r;
    }
    if (dot(a, a) == 0.0) return vec2(0);
    return frm_exp(cmul(b, frm_log(a)));
}
vec2 frm_sin(vec2 a) { return vec2(sin(a.x) * cosh(a.y), cos(a.x) * sinh(a.y)); }
vec2 frm_cos(vec2 a) { return vec2(cos(a.x) * cosh(a.y), -sin(a.x) * sinh(a.y)); }
vec2 frm_cosxx(vec2 a) { return vec2(cos(a.x) * cosh(a.y), sin(a.x) * sinh(a.y)); }  // Fractint's old buggy cos
vec2 frm_sinh(vec2 a) { return vec2(sinh(a.x) * cos(a.y), cosh(a.x) * sin(a.y)); }
vec2 frm_cosh(vec2 a) { return vec2(cosh(a.x) * cos(a.y), sinh(a.x) * sin(a.y)); }
vec2 frm_tan(vec2 a) { return cdiv(frm_sin(a), frm_cos(a)); }
vec2 frm_cotan(vec2 a) { return cdiv(frm_cos(a), frm_sin(a)); }
vec2 frm_tanh(vec2 a) { return cdiv(frm_sinh(a), frm_cosh(a)); }
vec2 frm_cotanh(vec2 a) { return cdiv(frm_cosh(a), frm_sinh(a)); }
vec2 frm_abs(vec2 a) { return abs(a); }
vec2 frm_conj(vec2 a) { return vec2(a.x, -a.y); }
vec2 frm_flip(vec2 a) { return a.yx; }
vec2 frm_ident(vec2 a) { return a; }
vec2 frm_recip(vec2 a) { return cdiv(vec2(1, 0), a); }
vec2 frm_zero(vec2 a) { return vec2(0.0); }
vec2 frm_one(vec2 a) { return vec2(1.0, 0.0); }
vec2 frm_asin(vec2 a) { return cmul(vec2(0, -1), frm_log(cmul(vec2(0, 1), a) + frm_sqrt(vec2(1, 0) - cmul(a, a)))); }
vec2 frm_acos(vec2 a) { return cmul(vec2(0, -1), frm_log(a + cmul(vec2(0, 1), frm_sqrt(vec2(1, 0) - cmul(a, a))))); }
vec2 frm_atan(vec2 a) { return cmul(vec2(0, 0.5), frm_log(cdiv(vec2(0, 1) + a, vec2(0, 1) - a))); }
vec2 frm_asinh(vec2 a) { return frm_log(a + frm_sqrt(cmul(a, a) + vec2(1, 0))); }
vec2 frm_acosh(vec2 a) { return frm_log(a + cmul(frm_sqrt(a + vec2(1, 0)), frm_sqrt(a - vec2(1, 0)))); }
vec2 frm_atanh(vec2 a) { return 0.5 * frm_log(cdiv(vec2(1, 0) + a, vec2(1, 0) - a)); }
vec2 frm_floor(vec2 a) { return floor(a); }
vec2 frm_ceil(vec2 a) { return ceil(a); }
vec2 frm_trunc(vec2 a) { return trunc(a); }
vec2 frm_round(vec2 a) { return sign(a) * floor(abs(a) + 0.5); }  // halves away from zero, like C
vec2 frm_real(vec2 a) { return vec2(a.x, 0.0); }
vec2 frm_imag(vec2 a) { return vec2(a.y, 0.0); }
vec2 frm_cabs(vec2 a) { return vec2(length(a), 0.0); }
)";

std::string glslNum(double v) {
    if (!std::isfinite(v)) v = v < 0 ? -3.4e38 : 3.4e38;  // "1e999" in a formula: as big as a float gets
    char b[40];
    snprintf(b, sizeof b, "%.9g", v);
    std::string s = b;
    if (s.find_first_of(".eE") == std::string::npos) s += ".0";  // a float literal
    return s;
}

std::string glsl(const Node& n, const FormulaAst& a) {
    auto k = [&](int i) { return glsl(*n.kids[i], a); };
    switch (n.kind) {
    case Node::Num: return "vec2(" + glslNum(n.num.real()) + ", " + glslNum(n.num.imag()) + ")";
    case Node::Var: return "v_" + a.vars[n.idx];
    case Node::Pixel: return "v_pixel";
    case Node::Param: return "uP" + std::to_string(n.idx + 1);
    case Node::Maxit: return "vec2(uFrmMaxit, 0.0)";
    case Node::Whitesq: return "v_whitesq";
    case Node::Neg: return "(-" + k(0) + ")";
    case Node::Assign: return "(v_" + a.vars[n.idx] + " = " + k(0) + ")";
    case Node::Mod2: return "frm_mod2(" + k(0) + ")";
    case Node::Pair: return "vec2((" + k(0) + ").x, (" + k(1) + ").x)";
    case Node::Call: {
        std::string f = n.idx >= kFn1 ? "fn" + std::to_string(n.idx - kFn1 + 1) : kFormulaFunctions[n.idx];
        return "frm_" + f + "(" + k(0) + ")";
    }
    case Node::Bin:
        if (n.op == "+" || n.op == "-") return "(" + k(0) + " " + n.op + " " + k(1) + ")";
        if (n.op == "*") return "cmul(" + k(0) + ", " + k(1) + ")";
        if (n.op == "/") return "cdiv(" + k(0) + ", " + k(1) + ")";
        if (n.op == "^") return "frm_pow(" + k(0) + ", " + k(1) + ")";
        if (n.op == "&&" || n.op == "||")
            return "vec2(float((" + k(0) + ").x != 0.0 " + n.op + " (" + k(1) + ").x != 0.0), 0.0)";
        return "vec2(float((" + k(0) + ").x " + n.op + " (" + k(1) + ").x), 0.0)";
    }
    return "vec2(0.0)";
}

void glslBlock(std::ostringstream& g, const std::vector<Stmt>& b, const FormulaAst& a, const std::string& ind, size_t count) {
    for (size_t i = 0; i < count; i++) {
        const Stmt& s = b[i];
        if (s.expr) {
            g << ind << glsl(*s.expr, a) << ";\n";
            continue;
        }
        for (size_t k = 0; k < s.branches.size(); k++) {
            g << ind << (k ? "} else if (" : "if (") << "(" << glsl(*s.branches[k].first, a) << ").x != 0.0) {\n";
            glslBlock(g, s.branches[k].second, a, ind + "    ", s.branches[k].second.size());
        }
        if (!s.elseBody.empty()) {
            g << ind << "} else {\n";
            glslBlock(g, s.elseBody, a, ind + "    ", s.elseBody.size());
        }
        g << ind << "}\n";
    }
}

// The escape radius, when the bailout test is the usual  |z| <= N  (Fractint's |z|
// is the squared modulus, so the radius is sqrt(N)) or  cabs(z) <= R.  0 if it isn't.
double detectBailout(const Node& test, int z) {
    if (test.kind != Node::Bin || (test.op != "<=" && test.op != "<")) return 0;
    const Node &l = *test.kids[0], &r = *test.kids[1];
    if (r.kind != Node::Num || r.num.imag() != 0 || r.num.real() <= 0) return 0;
    auto isZ = [&](const Node& n) { return n.kind == Node::Var && n.idx == z; };
    if (l.kind == Node::Mod2 && isZ(*l.kids[0])) return std::sqrt(r.num.real());
    if (l.kind == Node::Call && l.idx == functionIndex("cabs") && isZ(*l.kids[0])) return r.num.real();
    return 0;
}

}  // namespace

static void applyAnnotations(FormulaDef& d, const std::string& text) {
    std::istringstream is(text);
    std::string line;
    while (std::getline(is, line)) {
        size_t e = line.find('=');
        if (e == std::string::npos) continue;
        std::string key = line.substr(1, e - 1), val = line.substr(e + 1);
        key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());
        std::replace_if(val.begin(), val.end(), [](char c) { return c == '(' || c == ')' || c == ','; }, ' ');
        std::istringstream vs(val);
        if (key.size() == 2 && key[0] == 'p' && key[1] >= '1' && key[1] <= '5') {
            int i = key[1] - '1';
            vs >> d.p[i][0] >> d.p[i][1];
            d.hasP[i] = true;
        } else if (key.size() == 3 && key.rfind("fn", 0) == 0 && key[2] >= '1' && key[2] <= '4') {
            std::string f;
            vs >> f;
            for (int k = 0; k < kFormulaFunctionCount; k++)
                if (f == kFormulaFunctions[k]) d.fn[key[2] - '1'] = k;
        } else if (key == "view") {
            vs >> d.view[0] >> d.view[1] >> d.view[2];
            d.hasView = d.view[2] > 0;
        } else if (key == "julia") {
            vs >> d.julia;
        }
    }
}
std::vector<FormulaDef> parseFormulaFile(const std::string& text) {
    std::vector<FormulaDef> out;
    std::istringstream is(text);
    std::string line, comment, cur, name, pendingAnnotations;
    int depth = 0;
    while (std::getline(is, line)) {
        if (depth == 0) {
            size_t a = line.find_first_not_of(" \t\r");
            if (a == std::string::npos) {
                comment.clear();
                continue;
            }
            if (line[a] == ';') {  // description lines before a formula
                size_t b = line.find_first_not_of("; \t", a);
                std::string text = b == std::string::npos ? "" : line.substr(b);
                if (text.rfind("@", 0) == 0) pendingAnnotations += text + "\n";  // defaults, not prose
                else comment += text + "\n";
                continue;
            }
            size_t brace = line.find('{');
            if (brace == std::string::npos) continue;
            name = line.substr(a, brace - a);
            size_t paren = name.find('(');  // "Name(XAXIS)": the symmetry hint isn't needed
            if (paren != std::string::npos) name = name.substr(0, paren);
            while (!name.empty() && isspace((unsigned char)name.back())) name.pop_back();
            cur.clear();
        }
        cur += line + "\n";
        for (char c : line.substr(0, line.find(';'))) {
            if (c == '{') depth++;
            if (c == '}') depth--;
        }
        if (depth <= 0 && !cur.empty() && cur.find('{') != std::string::npos) {
            FormulaDef d;
            d.name = name;
            d.comment = comment;
            d.source = cur;
            applyAnnotations(d, pendingAnnotations);
            out.push_back(d);
            cur.clear();
            comment.clear();
            pendingAnnotations.clear();
            depth = 0;
        }
    }
    return out;
}
static bool findAnnotation(const std::string& src, const char* key, double& out) {
    std::string k = std::string("@") + key;
    for (size_t at = src.find(k); at != std::string::npos; at = src.find(k, at + 1)) {
        size_t semi = src.rfind(';', at), nl = src.rfind('\n', at);
        if (semi == std::string::npos || (nl != std::string::npos && nl > semi)) continue;  // only inside comments
        size_t e = src.find_first_not_of(" \t", at + k.size());
        if (e == std::string::npos || src[e] != '=') continue;
        char* end = nullptr;
        double v = strtod(src.c_str() + e + 1, &end);
        if (end != src.c_str() + e + 1 && std::isfinite(v)) {
            out = v;
            return true;
        }
    }
    return false;
}

TranspiledFormula transpileFormula(const std::string& source, const int fnChoice[4]) {
    TranspiledFormula r;
    for (int i = 0; i < 4; i++) r.fn[i] = std::clamp(fnChoice ? fnChoice[i] : 0, 0, kFormulaFunctionCount - 1);
    try {
        Parser ps;
        ps.t = tokenize(source);
        // skip "Name(...)" up to the opening brace
        while (ps.t[ps.p].type != T::LBrace && ps.t[ps.p].type != T::End) ps.p++;
        if (ps.t[ps.p].type != T::LBrace) throw ParseError(ps.t[ps.p].line, "a formula looks like: Name { init : loop, test }");
        ps.p++;
        auto ast = std::make_shared<FormulaAst>();
        bool colon = false;
        ast->init = ps.block(true, colon, false);
        if (colon) {
            bool dummy;
            ast->loop = ps.block(false, dummy, false);
        } else {
            ast->loop = std::move(ast->init);  // no ':' - everything is the loop
            ast->init.clear();
        }
        if (ast->loop.empty()) throw ParseError(ps.t[ps.p].line, "the loop needs at least a bailout test, e.g. |z| <= 4");
        if (!ast->loop.back().expr)
            throw ParseError(ps.t[ps.p].line, "the loop must end with the bailout test (an expression), not with an if block");
        ast->zIndex = ps.var("z");
        // Variables used but never assigned would silently be zero: report them.
        for (auto& [v, line] : ps.firstUse)
            if (v != ast->zIndex && !ps.assigned.count(v))
                throw ParseError(line, "variable '" + ps.varNames[v] + "' is used but never assigned (pixel, p1..p5, maxit, whitesq, pi and e are built in)");
        ast->vars = ps.varNames;
        if (ast->vars.size() > 6)
            throw ParseError(1, "at most 6 variables can carry over between iterations (this formula has " +
                                    std::to_string(ast->vars.size()) + ")");

        // Smooth coloring needs the escape radius and the degree of the map: the radius
        // comes from a standard bailout test (or @bailout), the degree from @power (default 2).
        r.bailout = detectBailout(*ast->loop.back().expr, ast->zIndex);
        double v;
        if (findAnnotation(source, "bailout", v) && v > 1) r.bailout = v;
        if (findAnnotation(source, "power", v) && v > 1) r.power = v;
        if (findAnnotation(source, "smooth", v) && v == 0) r.bailout = 0;

        std::ostringstream g;
        char consts[160];
        snprintf(consts, sizeof consts, "const float FRM_BAILOUT = %.9g;  // escape radius (0: unknown, no smooth coloring)\nconst float FRM_POWER = %.9g;\n",
                 r.bailout, r.power);
        g << "// transpiled formula\nuniform vec2 uP1, uP2, uP3, uP4, uP5;\nuniform float uFrmMaxit;\n"
          << "vec2 v_pixel, v_whitesq = vec2(0.0);\n" << consts;
        for (auto& name : ast->vars) g << "vec2 v_" << name << " = vec2(0.0);\n";
        for (int i = 0; i < 4; i++) g << "#define frm_fn" << (i + 1) << " frm_" << kFormulaFunctions[r.fn[i]] << "\n";
        g << kPrelude;
        g << "void frm_init(vec2 pixel) {\n    v_pixel = pixel;\n";
        for (auto& name : ast->vars) g << "    v_" << name << " = vec2(0.0);\n";
        glslBlock(g, ast->init, *ast, "    ", ast->init.size());
        g << "}\nbool frm_step(int iter) {\n";
        glslBlock(g, ast->loop, *ast, "    ", ast->loop.size() - 1);
        g << "    return (" << glsl(*ast->loop.back().expr, *ast) << ").x != 0.0;\n}\n";
        g << "vec2 frm_z() { return v_z; }\n";
        // state packing: 6 complex values in three uvec4s
        auto slot = [&](size_t i, bool save) {
            const char* reg[3] = {"s0", "s1", "s3"};
            const char* half[2] = {".xy", ".zw"};
            std::string rg = std::string(reg[i / 2]) + half[i % 2];
            return save ? rg + " = floatBitsToUint(v_" + ast->vars[i] + ");\n" : "v_" + ast->vars[i] + " = uintBitsToFloat(" + rg + ");\n";
        };
        g << "void frm_save(out uvec4 s0, out uvec4 s1, out uvec4 s3) {\n    s0 = s1 = s3 = uvec4(0u);\n";
        for (size_t i = 0; i < ast->vars.size(); i++) g << "    " << slot(i, true);
        g << "}\nvoid frm_load(vec2 pixel, uvec4 s0, uvec4 s1, uvec4 s3) {\n    v_pixel = pixel;\n";
        for (size_t i = 0; i < ast->vars.size(); i++) g << "    " << slot(i, false);
        g << "}\n";
        r.glsl = g.str();
        r.stateVars = (int)ast->vars.size();
        for (int i = 0; i < 4; i++) r.usesFn[i] = ps.usesFn[i];
        for (int i = 0; i < kFormulaParams; i++) r.usesP[i] = ps.usesP[i];
        r.usesWhitesq = ps.usesWhitesq;
        r.ast = ast;
        r.ok = true;
    } catch (const ParseError& e) {
        r.error = "line " + std::to_string(e.line) + ": " + e.what();
    }
    return r;
}

// ------------------------------------------------------------------ CPU interpreter
// Mirrors the GLSL above operation by operation (in doubles).
using C = std::complex<double>;
static C cmulC(C a, C b) { return {a.real() * b.real() - a.imag() * b.imag(), a.real() * b.imag() + a.imag() * b.real()}; }
static C cdivC(C a, C b) {
    double d = std::norm(b);
    return C(a.real() * b.real() + a.imag() * b.imag(), a.imag() * b.real() - a.real() * b.imag()) / d;
}
static C logC(C a) { return {std::log(std::abs(a)), std::atan2(a.imag(), a.real())}; }
static C expC(C a) { return std::exp(a.real()) * C(std::cos(a.imag()), std::sin(a.imag())); }
static C sqrtC(C a) {
    double r = std::abs(a);
    C s(std::sqrt(std::max(0.5 * (r + a.real()), 0.0)), std::sqrt(std::max(0.5 * (r - a.real()), 0.0)));
    return a.imag() < 0 ? std::conj(s) : s;
}
static C sinC(C a) { return {std::sin(a.real()) * std::cosh(a.imag()), std::cos(a.real()) * std::sinh(a.imag())}; }
static C cosC(C a) { return {std::cos(a.real()) * std::cosh(a.imag()), -std::sin(a.real()) * std::sinh(a.imag())}; }
static C sinhC(C a) { return {std::sinh(a.real()) * std::cos(a.imag()), std::cosh(a.real()) * std::sin(a.imag())}; }
static C coshC(C a) { return {std::cosh(a.real()) * std::cos(a.imag()), std::sinh(a.real()) * std::sin(a.imag())}; }
static C powC(C a, C b) {
    if (b.imag() == 0 && b.real() == std::floor(b.real()) && std::abs(b.real()) <= 32) {
        int n = (int)std::abs(b.real());
        C r(1, 0);
        for (int i = 0; i < n; i++) r = cmulC(r, a);
        return b.real() < 0 ? cdivC(C(1, 0), r) : r;
    }
    if (std::norm(a) == 0) return 0;
    return expC(cmulC(b, logC(a)));
}
static double roundC(double x) { return x < 0 ? -std::floor(-x + 0.5) : std::floor(x + 0.5); }  // halves away from zero

static C applyFunction(int f, C a) {
    const C I(0, 1), one(1, 0);
    switch (f) {
    case 0: return sinC(a);
    case 1: return cosC(a);
    case 2: return cdivC(sinC(a), cosC(a));
    case 3: return cdivC(cosC(a), sinC(a));
    case 4: return sinhC(a);
    case 5: return coshC(a);
    case 6: return cdivC(sinhC(a), coshC(a));
    case 7: return expC(a);
    case 8: return logC(a);
    case 9: return cmulC(a, a);
    case 10: return sqrtC(a);
    case 11: return {std::abs(a.real()), std::abs(a.imag())};
    case 12: return std::conj(a);
    case 13: return a;
    case 14: return cdivC(one, a);
    case 15: return {a.imag(), a.real()};
    case 16: return cdivC(coshC(a), sinhC(a));
    case 17: return {std::cos(a.real()) * std::cosh(a.imag()), std::sin(a.real()) * std::sinh(a.imag())};
    case 18: return 0;
    case 19: return one;
    case 20: return cmulC(C(0, -1), logC(cmulC(I, a) + sqrtC(one - cmulC(a, a))));
    case 21: return cmulC(C(0, -1), logC(a + cmulC(I, sqrtC(one - cmulC(a, a)))));
    case 22: return cmulC(C(0, 0.5), logC(cdivC(I + a, I - a)));
    case 23: return logC(a + sqrtC(cmulC(a, a) + one));
    case 24: return logC(a + cmulC(sqrtC(a + one), sqrtC(a - one)));
    case 25: return 0.5 * logC(cdivC(one + a, one - a));
    case 26: return {std::floor(a.real()), std::floor(a.imag())};
    case 27: return {std::ceil(a.real()), std::ceil(a.imag())};
    case 28: return {std::trunc(a.real()), std::trunc(a.imag())};
    case 29: return {roundC(a.real()), roundC(a.imag())};
    case 30: return {a.real(), 0};
    case 31: return {a.imag(), 0};
    case 32: return {std::abs(a), 0};
    }
    return a;
}

struct Evaluator {
    FormulaVM& vm;
    C eval(const Node& n) {
        switch (n.kind) {
        case Node::Num: return n.num;
        case Node::Var: return vm.vars_[n.idx];
        case Node::Pixel: return vm.pixel_;
        case Node::Param: return vm.p_[n.idx];
        case Node::Maxit: return vm.maxit_;
        case Node::Whitesq: return vm.whitesq_ ? 1.0 : 0.0;
        case Node::Neg: return -eval(*n.kids[0]);
        case Node::Assign: return vm.vars_[n.idx] = eval(*n.kids[0]);
        case Node::Mod2: return std::norm(eval(*n.kids[0]));
        case Node::Pair: return C(eval(*n.kids[0]).real(), eval(*n.kids[1]).real());
        case Node::Call: return applyFunction(n.idx >= kFn1 ? vm.fn_[n.idx - kFn1] : n.idx, eval(*n.kids[0]));
        case Node::Bin: {
            C a = eval(*n.kids[0]), b = eval(*n.kids[1]);
            const std::string& o = n.op;
            if (o == "+") return a + b;
            if (o == "-") return a - b;
            if (o == "*") return cmulC(a, b);
            if (o == "/") return cdivC(a, b);
            if (o == "^") return powC(a, b);
            double x = a.real(), y = b.real();
            bool r = o == "<" ? x < y : o == "<=" ? x <= y : o == ">" ? x > y : o == ">=" ? x >= y : o == "==" ? x == y
                   : o == "!=" ? x != y : o == "&&" ? (x != 0 && y != 0) : (x != 0 || y != 0);
            return r ? 1.0 : 0.0;
        }
        }
        return 0;
    }
    void run(const std::vector<Stmt>& b, size_t count) {
        for (size_t i = 0; i < count; i++) {
            const Stmt& s = b[i];
            if (s.expr) {
                eval(*s.expr);
                continue;
            }
            bool done = false;
            for (auto& [cond, body] : s.branches)
                if (eval(*cond).real() != 0) {
                    run(body, body.size());
                    done = true;
                    break;
                }
            if (!done) run(s.elseBody, s.elseBody.size());
        }
    }
};

FormulaVM::FormulaVM(const TranspiledFormula& f) : ast_(f.ast) {
    std::copy(f.fn, f.fn + 4, fn_);
    vars_.assign(ast_ ? ast_->vars.size() : 1, 0);
}

void FormulaVM::init(C pixel, bool whitesq) {
    pixel_ = pixel;
    whitesq_ = whitesq;
    std::fill(vars_.begin(), vars_.end(), C(0));
    if (ast_) Evaluator{*this}.run(ast_->init, ast_->init.size());
}

bool FormulaVM::step() {
    if (!ast_) return false;
    Evaluator ev{*this};
    ev.run(ast_->loop, ast_->loop.size() - 1);
    return ev.eval(*ast_->loop.back().expr).real() != 0;
}

FormulaVM::C FormulaVM::z() const { return ast_ ? vars_[ast_->zIndex] : C(0); }
