#include "formula.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {

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

// ------------------------------------------------------------------ parser -> GLSL
// Every expression becomes a GLSL expression of type vec2 (a complex number).
struct Parser {
    std::vector<Tok> t;
    size_t p = 0;
    int parenDepth = 0;
    std::set<std::string> assigned, used;
    bool usesFn[4] = {false, false, false, false};
    bool usesP[3] = {false, false, false};

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
    void expect(const char* op) {
        if (!isOp(op)) throw ParseError(peek().line, std::string("expected '") + op + "'");
        next();
    }

    static std::string var(const std::string& n) { return "v_" + n; }

    std::string expression() { return assignment(); }

    std::string assignment() {
        // IDENT '=' expr   (right-associative: z = c = pixel)
        if (peek().type == T::Ident && t[p + 1].type == T::Op && t[p + 1].text == "=") {
            Tok id = next();
            next();  // '='
            checkAssignable(id);
            assigned.insert(id.text);
            std::string rhs = assignment();
            return "(" + var(id.text) + " = " + rhs + ")";
        }
        return logical();
    }

    void checkAssignable(const Tok& id) {
        static const std::set<std::string> ro = {"pixel", "p1", "p2", "p3", "pi", "e"};
        if (ro.count(id.text)) throw ParseError(id.line, "'" + id.text + "' is read-only");
        if (isFunction(id.text)) throw ParseError(id.line, "'" + id.text + "' is a function name");
    }

    std::string logical() {
        std::string a = comparison();
        while (isOp("&&") || isOp("||")) {
            std::string op = next().text;
            std::string b = comparison();
            a = "vec2(float((" + a + ").x != 0.0 " + op + " (" + b + ").x != 0.0), 0.0)";
        }
        return a;
    }

    std::string comparison() {
        std::string a = additive();
        for (const char* op : {"<=", ">=", "==", "!=", "<", ">"})
            if (isOp(op)) {
                next();
                std::string b = additive();
                // Fractint compares real parts
                return "vec2(float((" + a + ").x " + op + " (" + b + ").x), 0.0)";
            }
        return a;
    }

    std::string additive() {
        std::string a = term();
        while (isOp("+") || isOp("-")) {
            std::string op = next().text;
            a = "(" + a + " " + op + " " + term() + ")";
        }
        return a;
    }

    std::string term() {
        std::string a = unary();
        while (isOp("*") || isOp("/")) {
            bool mul = next().text == "*";
            std::string b = unary();
            a = (mul ? "cmul(" : "cdiv(") + a + ", " + b + ")";
        }
        return a;
    }

    std::string unary() {
        if (isOp("-")) {
            next();
            return "(-" + unary() + ")";
        }
        if (isOp("+")) {
            next();
            return unary();
        }
        return power();
    }

    std::string power() {
        std::string a = primary();
        if (isOp("^")) {
            next();
            std::string b = unary();  // right-associative
            return "frm_pow(" + a + ", " + b + ")";
        }
        return a;
    }

    static bool isFunction(const std::string& n) {
        for (int i = 0; i < kFormulaFunctionCount; i++)
            if (n == kFormulaFunctions[i]) return true;
        return n == "cabs" || n == "real" || n == "imag" || n == "fn1" || n == "fn2" || n == "fn3" || n == "fn4";
    }

    std::string primary() {
        Tok k = next();
        if (k.type == T::Num) {
            std::ostringstream o;
            o.precision(17);
            o << "vec2(" << k.num << ", 0.0)";
            return o.str();
        }
        if (k.type == T::Op && k.text == "(") {
            parenDepth++;
            std::string a = expression();
            if (isOp(",")) {  // complex literal (re, im)
                next();
                std::string b = expression();
                expect(")");
                parenDepth--;
                return "vec2((" + a + ").x, (" + b + ").x)";
            }
            expect(")");
            parenDepth--;
            return a;
        }
        if (k.type == T::Op && k.text == "|") {  // |x| is the squared modulus in Fractint
            parenDepth++;
            std::string a = additive();
            expect("|");
            parenDepth--;
            return "vec2(dot(" + a + ", " + a + "), 0.0)";
        }
        if (k.type == T::Ident) {
            if (isOp("(")) {  // function call
                if (!isFunction(k.text)) throw ParseError(k.line, "unknown function '" + k.text + "'");
                next();
                parenDepth++;
                std::string a = expression();
                expect(")");
                parenDepth--;
                if (k.text == "real") return "vec2((" + a + ").x, 0.0)";
                if (k.text == "imag") return "vec2((" + a + ").y, 0.0)";
                if (k.text == "cabs") return "vec2(length(" + a + "), 0.0)";
                if (k.text.size() == 3 && k.text.rfind("fn", 0) == 0) usesFn[k.text[2] - '1'] = true;
                return "frm_" + k.text + "(" + a + ")";
            }
            if (k.text == "pixel") return "v_pixel";
            if (k.text == "pi") return "vec2(3.14159265358979, 0.0)";
            if (k.text == "e") return "vec2(2.71828182845905, 0.0)";
            if (k.text == "p1" || k.text == "p2" || k.text == "p3") {
                usesP[k.text[1] - '1'] = true;
                return "uP" + k.text.substr(1);
            }
            if (isFunction(k.text)) throw ParseError(k.line, "'" + k.text + "' is a function: write " + k.text + "(...)");
            used.insert(k.text);
            return var(k.text);
        }
        if (k.type == T::End || k.type == T::RBrace) throw ParseError(k.line, "unexpected end of formula");
        if (k.type == T::Newline) throw ParseError(k.line, "the statement ends too early (something is missing before the end of the line)");
        throw ParseError(k.line, "unexpected '" + k.text + "'");
    }

    // statements up to ':' (init) or '}' (loop), separated by ',' or newlines
    std::vector<std::string> statements(bool untilColon, bool& sawColon) {
        std::vector<std::string> out;
        sawColon = false;
        for (;;) {
            while (peek().type == T::Newline || isOp(",")) next();
            if (peek().type == T::RBrace) return out;
            if (peek().type == T::End) throw ParseError(peek().line, "missing '}'");
            if (untilColon && isOp(":")) {
                next();
                sawColon = true;
                return out;
            }
            out.push_back(expression());
            const Tok& n = peek();
            if (n.type == T::End) throw ParseError(n.line, "missing '}' at the end of the formula");
            if (!(n.type == T::Newline || n.type == T::RBrace || (n.type == T::Op && (n.text == "," || n.text == ":"))))
                throw ParseError(n.line, "expected ',' or a new line after a statement, found '" + n.text + "'");
        }
    }
};

const char* kPrelude = R"(
// ---- complex functions for formulas (vec2 = a + bi)
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
vec2 frm_sinh(vec2 a) { return vec2(sinh(a.x) * cos(a.y), cosh(a.x) * sin(a.y)); }
vec2 frm_cosh(vec2 a) { return vec2(cosh(a.x) * cos(a.y), sinh(a.x) * sin(a.y)); }
vec2 frm_tan(vec2 a) { return cdiv(frm_sin(a), frm_cos(a)); }
vec2 frm_cotan(vec2 a) { return cdiv(frm_cos(a), frm_sin(a)); }
vec2 frm_tanh(vec2 a) { return cdiv(frm_sinh(a), frm_cosh(a)); }
vec2 frm_abs(vec2 a) { return abs(a); }
vec2 frm_conj(vec2 a) { return vec2(a.x, -a.y); }
vec2 frm_flip(vec2 a) { return a.yx; }
vec2 frm_ident(vec2 a) { return a; }
vec2 frm_recip(vec2 a) { return cdiv(vec2(1, 0), a); }
)";

}  // namespace

// "@p1 = (0.5, -0.2)", "@fn1 = cos", "@view = -0.5 0 3"
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
        if (key.size() == 2 && key[0] == 'p' && key[1] >= '1' && key[1] <= '3') {
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

TranspiledFormula transpileFormula(const std::string& source, const int fnChoice[4]) {
    TranspiledFormula r;
    try {
        Parser ps;
        ps.t = tokenize(source);
        // skip "Name(...)" up to the opening brace
        while (ps.t[ps.p].type != T::LBrace && ps.t[ps.p].type != T::End) ps.p++;
        if (ps.t[ps.p].type != T::LBrace) throw ParseError(ps.t[ps.p].line, "a formula looks like: Name { init : loop, test }");
        ps.p++;
        bool colon = false;
        std::vector<std::string> init = ps.statements(true, colon), loop;
        if (colon) {
            bool dummy;
            loop = ps.statements(false, dummy);
        } else {
            loop = std::move(init);  // no ':' - everything is the loop
            init.clear();
        }
        if (loop.empty()) throw ParseError(ps.t[ps.p].line, "the loop needs at least a bailout test, e.g. |z| <= 4");

        // Variables used but never assigned would silently be zero: report them.
        for (auto& u : ps.used)
            if (u != "z" && !ps.assigned.count(u))
                throw ParseError(1, "variable '" + u + "' is used but never assigned (pixel, p1, p2, p3, pi and e are built in)");
        std::vector<std::string> vars(ps.assigned.begin(), ps.assigned.end());
        if (std::find(vars.begin(), vars.end(), "z") == vars.end()) vars.insert(vars.begin(), "z");
        if (vars.size() > 6)
            throw ParseError(1, "at most 6 variables can carry over between iterations (this formula has " +
                                    std::to_string(vars.size()) + ")");

        std::ostringstream g;
        g << "// transpiled formula\nuniform vec2 uP1, uP2, uP3;\nvec2 v_pixel;\n";
        for (auto& v : vars) g << "vec2 v_" << v << " = vec2(0.0);\n";
        for (int i = 0; i < 4; i++) {
            int c = std::clamp(fnChoice ? fnChoice[i] : 0, 0, kFormulaFunctionCount - 1);
            g << "#define frm_fn" << (i + 1) << " frm_" << kFormulaFunctions[c] << "\n";
        }
        g << kPrelude;
        g << "void frm_init(vec2 pixel) {\n    v_pixel = pixel;\n";
        for (auto& s : init) g << "    " << s << ";\n";
        g << "}\nbool frm_step(int iter) {\n";
        for (size_t i = 0; i + 1 < loop.size(); i++) g << "    " << loop[i] << ";\n";
        g << "    return (" << loop.back() << ").x != 0.0;\n}\n";
        g << "vec2 frm_z() { return v_z; }\n";
        // state packing: 6 complex values in three uvec4s
        auto slot = [&](size_t i, bool save) {
            const char* reg[3] = {"s0", "s1", "s3"};
            const char* half[2] = {".xy", ".zw"};
            std::string r = std::string(reg[i / 2]) + half[i % 2];
            return save ? r + " = floatBitsToUint(v_" + vars[i] + ");\n" : "v_" + vars[i] + " = uintBitsToFloat(" + r + ");\n";
        };
        g << "void frm_save(out uvec4 s0, out uvec4 s1, out uvec4 s3) {\n    s0 = s1 = s3 = uvec4(0u);\n";
        for (size_t i = 0; i < vars.size(); i++) g << "    " << slot(i, true);
        g << "}\nvoid frm_load(vec2 pixel, uvec4 s0, uvec4 s1, uvec4 s3) {\n    v_pixel = pixel;\n";
        for (size_t i = 0; i < vars.size(); i++) g << "    " << slot(i, false);
        g << "}\n";
        r.glsl = g.str();
        r.stateVars = (int)vars.size();
        for (int i = 0; i < 4; i++) r.usesFn[i] = ps.usesFn[i];
        for (int i = 0; i < 3; i++) r.usesP[i] = ps.usesP[i];
        r.ok = true;
    } catch (const ParseError& e) {
        r.error = "line " + std::to_string(e.line) + ": " + e.what();
    }
    return r;
}
