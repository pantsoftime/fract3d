; Fract3D formula file - the same idea as Fractint's .FRM files.
;
; A formula is:   Name { initialization : iteration, bailout test }
; Lines like "; @p1 = (0.5, -0.2)", "; @fn1 = cos" or "; @view = x y height"
; just before a formula set its starting parameters and view.
; Everything is a complex number. Statements are separated by commas or new
; lines. The last statement of the loop is the "keep going" test: while it's
; true (non-zero), iteration continues. |x| means the squared modulus x*x+y*y,
; just as in Fractint. pixel is the point being drawn; p1, p2, p3 are
; parameters you can set in the panel; fn1..fn4 are functions you choose there.
;
; Copy any of these into the formula editor, change them, and press Compile.
; Your own formulas can go in ~/.config/fract3d/formulas/*.frm.

; The Mandelbrot set, written out the long way.
; @view = -0.6 0 3
Mandel {
  z = 0, c = pixel :
  z = z*z + c,
  |z| <= 4
}

; A Julia set: the constant c comes from p1.
; @p1 = (-0.8, 0.156)
; @view = 0 0 2.6
Julia {
  z = pixel, c = p1 :
  z = z*z + c,
  |z| <= 4
}

; The Spider: c is updated every step too, so it chases z around.
; @view = -0.3 0 3
Spider {
  z = c = pixel :
  z = z*z + c
  c = c/2 + z,
  |z| <= 4
}

; Manowar: each step also adds the previous z, so the orbit has a memory.
; @view = -0.4 0 1.6
Manowar {
  z = z1 = 0, c = pixel :
  t = z
  z = z*z + z1 + c
  z1 = t,
  |z| <= 4
}

; Nova: Newton's method for z^3 = 1 with a Mandelbrot-style "+ c" kick.
; It stops when z stops moving, so the colors show how fast it converged.
; @view = -0.4 0 2.5
Nova {
  z = 1, c = pixel :
  zold = z
  z = z - (z*z*z - 1) / (3*z*z) + c,
  |z - zold| > 0.000001
}

; A Mandelbrot set built on any function: pick fn1 in the panel.
; sin, cos, exp and cosh give very different worlds. Needs a larger bailout.
; @fn1 = cos
; @view = 0 0 8
FnMandel {
  z = 0, c = pixel :
  z = fn1(z) + c,
  |z| <= 64
}

; Burning Ship, formula style: abs() folds z into the first quadrant.
; (It appears upside down compared with the built-in, which flips it.)
; @view = -0.5 -0.5 3.5
BurningShip {
  z = 0, c = pixel :
  z = sqr(abs(z)) + c,
  |z| <= 4
}

; The Phoenix: the previous z feeds back through p1's imaginary part.
; @p1 = (0.5667, -0.5)
; @view = 0 0 3
Phoenix {
  z = pixel, y = 0 :
  t = z*z + real(p1) + imag(p1) * y
  y = z
  z = t,
  |z| <= 4
}

; Sine Julia: z -> p1 * sin(z). It escapes along the imaginary axis,
; so the test watches only the imaginary part.
; @p1 = (1, 0.4)
; @view = 0 0 8
SineJulia {
  z = pixel :
  z = p1 * sin(z),
  |imag(z)| < 2500
}

; Exponential Mandelbrot: z -> exp(z) + c. Shows spiky "hairs".
; @view = -1.5 0 6
ExpMandel {
  z = 0, c = pixel :
  z = exp(z) + c,
  |real(z)| < 50
}

; A power you choose: z^p1 + c. Fractional powers tear the set along a branch cut.
; @p1 = (2.5, 0)
PowerMandel {
  z = 0, c = pixel :
  z = z^p1 + c,
  |z| <= 16
}

; Magnet II (from renormalization theory in physics): convergence to 1
; stops the iteration as well as escape.
; @view = 1.2 0 5
Magnet2 {
  z = 0, c = pixel :
  n = z*z*z + 3*(c - 1)*z + (c - 1)*(c - 2)
  d = 3*z*z + 3*(c - 2)*z + (c - 1)*(c - 2) + 1
  z = sqr(n / d),
  |z| <= 100 && |z - 1| > 0.00001
}
