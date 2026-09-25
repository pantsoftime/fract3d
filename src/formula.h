#pragma once
// Fractint-style formula files (.frm): parse, transpile to GLSL, and interpret on
// the CPU.
//
//   Name { init statements : loop statements, bailout condition }
//
//   Spider {  ; comments start with a semicolon
//     z = c = pixel :
//     z = z*z + c
//     c = c/2 + z,
//     |z| <= 4
//   }
//
// All values are complex. Statements are separated by commas or newlines; the
// last loop statement is the "keep iterating" test. |x| is the squared modulus,
// as in Fractint. Built-in values: pixel (the point being drawn), p1..p5
// (parameters from the UI), maxit (the iteration limit), whitesq (1 on alternate
// pixels, like a checkerboard's white squares), pi and e. z is what gets colored;
// any other name you assign is a variable. Branches:
//
//   if (cond)  statements  elseif (cond)  statements  else  statements  endif
//
// Functions: sin cos tan cotan sinh cosh tanh cotanh cosxx exp log sqr sqrt abs
// conj flip ident recip zero one asin acos atan asinh acosh atanh floor ceil trunc
// round real imag cabs, and fn1..fn4, whose meaning is chosen in the UI.
//
// Smooth coloring uses the escape radius read from a "|z| <= N" or "cabs(z) < R"
// test; comment annotations inside the formula override it: "; @bailout = R",
// "; @power = 3" (degree of the map, default 2), "; @smooth = 0".
#include <complex>
#include <memory>
#include <string>
#include <vector>

inline constexpr int kFormulaParams = 5;  // p1..p5

struct FormulaDef {
    std::string name;
    std::string comment;  // lines of ';' text just before the formula
    std::string source;   // the full text, "Name { ... }"
    // Optional defaults from "; @p1 = (re, im)", "; @fn1 = cos",
    // "; @view = centerRe centerIm height" and "; @julia = Name" lines in the comment:
    bool hasP[kFormulaParams] = {};
    float p[kFormulaParams][2] = {};
    int fn[4] = {-1, -1, -1, -1};
    bool hasView = false;
    double view[3] = {-0.5, 0.0, 3.0};
    std::string julia;  // the formula that shows this one's Julia sets (c = p1)
};

// Splits a .frm file into its formulas.
std::vector<FormulaDef> parseFormulaFile(const std::string& text);

struct FormulaAst;  // the parsed formula (formula.cpp)

struct TranspiledFormula {
    bool ok = false;
    std::string error;  // "line N: message"
    std::string glsl;   // frm_init / frm_step / frm_z / frm_save / frm_load
    bool usesFn[4] = {false, false, false, false};
    bool usesP[kFormulaParams] = {};
    bool usesWhitesq = false;
    int stateVars = 0;   // complex variables carried between iterations
    double bailout = 0;  // escape radius for smooth coloring (0: unknown - colors stay in whole bands)
    double power = 2;    // degree of the map, for smooth coloring
    std::shared_ptr<const FormulaAst> ast;  // for FormulaVM
    int fn[4] = {0, 0, 0, 0};               // the functions fn1..fn4 stand for
};

// Append only: PAR files store fn choices as indexes into this list.
inline const char* kFormulaFunctions[] = {"sin",   "cos",   "tan",   "cotan", "sinh", "cosh",  "tanh",  "exp",
                                          "log",   "sqr",   "sqrt",  "abs",   "conj", "ident", "recip", "flip",
                                          "cotanh", "cosxx", "zero", "one",   "asin", "acos",  "atan",  "asinh",
                                          "acosh", "atanh", "floor", "ceil",  "trunc", "round", "real",  "imag",
                                          "cabs"};
inline constexpr int kFormulaFunctionCount = 33;

// fnChoice[i] indexes kFormulaFunctions for fn1..fn4.
TranspiledFormula transpileFormula(const std::string& source, const int fnChoice[4]);

// Runs a transpiled formula on the CPU, in double precision: the orbit viewer and
// tests use it. The same semantics as the GPU code (which runs in floats).
class FormulaVM {
public:
    using C = std::complex<double>;
    explicit FormulaVM(const TranspiledFormula& f);
    void setParams(const C p[kFormulaParams], int maxIter) {
        for (int i = 0; i < kFormulaParams; i++) p_[i] = p[i];
        maxit_ = maxIter;
    }
    void init(C pixel, bool whitesq = false);  // runs the init section
    bool step();                               // one loop iteration; false once the test fails
    C z() const;

private:
    std::shared_ptr<const FormulaAst> ast_;
    int fn_[4];
    std::vector<C> vars_;
    C pixel_, p_[kFormulaParams];
    double maxit_ = 100;
    bool whitesq_ = false;
    friend struct Evaluator;
};
