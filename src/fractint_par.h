#pragma once
// Importing Fractint's own parameter files. A Fractint .PAR file holds any number
// of entries:
//
//   Seahorse { ; a comment
//     reset=2004 type=mandel center-mag=-0.7435/0.1314/1500 maxiter=512
//     inside=0 colors=000<30>zzz<223>000
//   }
//
// Each entry becomes a Fract3D view (PAR text). Everything Fractint could draw that
// Fract3D can also draw is carried over: the formula (built-in types and formula-file
// formulas), the view, iterations, bailout, inside/outside coloring and the palette.
// What can't be reproduced is listed in the warnings instead of being dropped silently.
#include <map>
#include <string>
#include <vector>

struct FractintEntry {
    std::string name;
    std::string comment;  // the first comment in the entry
    std::map<std::string, std::string> keys;  // key=value, keys lowercase
};

// All entries of a Fractint .PAR file (empty if the text isn't one).
std::vector<FractintEntry> parseFractintPar(const std::string& text);

struct FractintImport {
    bool ok = false;
    std::string par;                    // Fract3D PAR text for loadParText
    std::vector<std::string> warnings;  // what couldn't be carried over
    std::string error;                  // when !ok
};
FractintImport convertFractintEntry(const FractintEntry& e);

// Decodes Fractint's colors= value ("000<30>zzz..." - three base-64 digits per
// color, 0-63 each, "<n>" = n colors shaded between the neighbours). Returns 256
// RGB triples in 0..1, or an empty vector if the value isn't an encoded palette.
std::vector<float> decodeFractintColors(const std::string& v);
