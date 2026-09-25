#pragma once
// Fractint-style formula files (.frm): parse and transpile to GLSL.
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
// as in Fractint. Variables: pixel (the point being drawn), p1..p3 (parameters
// from the UI), z (what gets colored) and any names you assign. Functions: sin
// cos tan cotan sinh cosh tanh exp log sqr sqrt abs cabs conj real imag flip
// ident recip, and fn1..fn4, whose meaning is chosen in the UI.
//
// Smooth coloring uses the escape radius read from a "|z| <= N" or "cabs(z) < R"
// test; comment annotations inside the formula override it: "; @bailout = R",
// "; @power = 3" (degree of the map, default 2), "; @smooth = 0".
#include <string>
#include <vector>

struct FormulaDef {
    std::string name;
    std::string comment;  // lines of ';' text just before the formula
    std::string source;   // the full text, "Name { ... }"
    // Optional defaults from "; @p1 = (re, im)", "; @fn1 = cos",
    // "; @view = centerRe centerIm height" and "; @julia = Name" lines in the comment:
    bool hasP[3] = {false, false, false};
    float p[3][2] = {};
    int fn[4] = {-1, -1, -1, -1};
    bool hasView = false;
    double view[3] = {-0.5, 0.0, 3.0};
    std::string julia;  // "; @julia = Name": the formula that shows this one's Julia sets (c = p1)
};

// Splits a .frm file into its formulas.
std::vector<FormulaDef> parseFormulaFile(const std::string& text);

struct TranspiledFormula {
    bool ok = false;
    std::string error;  // "line N: message"
    std::string glsl;   // frm_init / frm_step / frm_z / frm_save / frm_load
    bool usesFn[4] = {false, false, false, false};
    bool usesP[3] = {false, false, false};
    int stateVars = 0;  // complex variables carried between iterations
    double bailout = 0;  // escape radius for smooth coloring (0: unknown - colors stay in whole bands)
    double power = 2;    // degree of the map, for smooth coloring
};

inline const char* kFormulaFunctions[] = {"sin",  "cos", "tan",  "cotan", "sinh", "cosh", "tanh",
                                          "exp",  "log", "sqr",  "sqrt",  "abs",  "conj", "ident",
                                          "recip", "flip"};
inline constexpr int kFormulaFunctionCount = 16;

// fnChoice[i] indexes kFormulaFunctions for fn1..fn4.
TranspiledFormula transpileFormula(const std::string& source, const int fnChoice[4]);
