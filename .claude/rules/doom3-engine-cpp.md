# id's Doom 3 engine C++

The `neo/` source is still at the repository root. The PS2 math headers and build
flags mentioned below are planned interfaces, not implemented files yet.

## Editing rules

- Keep id's code as close to unchanged as possible. Platform work belongs in `ps2`
  behind a seam. When an engine change is unavoidable, keep it minimal and tag it:
  `// [PS2_D3BFG]: <why>`.
- **No double FPU on the EE.** Unsuffixed constants are made float by `-fsingle-precision-constant`,
  and libm calls are replaced by `ps2/math/math.h`. Don't add `double` math or `<math.h>` double
  calls to engine code.
