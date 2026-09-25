Palette files (.map) in this folder, or in ~/.config/fract3d/palettes, are
loaded at startup and appear in the Palette list as "MAP: <name>".

The format is the one Fractint used: one color per line, "red green blue",
each 0-255. Up to 256 lines; shorter maps repeat. Anything after the three
numbers on a line is ignored, so Fractint .MAP files load as-is.

The Color tab's "Save palette as .map" button writes the current palette here.
