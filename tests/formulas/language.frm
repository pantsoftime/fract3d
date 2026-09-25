; Formulas that exercise the whole formula language on the GPU; the tests compare
; each render with the CPU interpreter (tools/itercheck --frm).

; if / elseif / else / endif, and a Fractint-style complex literal
; @view = -0.3 0 3.2
Branches {
  z = 0, c = pixel :
  if (real(z) > 0)
    z = z*z + c
  elseif (imag(z) > 0)
    z = conj(z)*conj(z) + c
  else
    z = z*z - (0.1, 0.05) + c
  endif
  |z| <= 4
}

; whitesq (a checkerboard of two formulas) and maxit
; @view = -0.5 0 3
Checker {
  z = 0, c = pixel, k = maxit / 128 :
  if (whitesq)
    z = z*z + c
  else
    z = z*z*z + c*k
  endif
  |z| <= 4
}

; p4 and p5, and the inverse functions
; @p4 = (0.9, 0)
; @p5 = (0.2, 0.1)
; @view = 0 0 4
Inverse {
  z = pixel :
  z = p4 * asin(z) + p5 * atanh(z) + acos(z*p5) - asinh(z) + atan(z)*p5
  z = z*z + pixel,
  |z| <= 16
}

; rounding and the rest of the function list
; @view = 0 0 6
Mixed {
  z = pixel :
  z = cosxx(z) + cotanh(z)*0.1 + round(z*4)/16 + floor(z)*0.01 + ceil(z)*0.01 + trunc(z)*0.01
  z = z + acosh(z)*0.05 + sqrt(z)*0.1 + recip(z + 3)*0.1 + flip(z)*0.05 + cabs(z)*0.01 + one(z)*0.01 + zero(z)
  z = z*z + pixel,
  |z| <= 16
}
