# Vertex and geometry programs in the 2.0.0 SAGE libraries

Source: `own_sage_tool stages` over every program of Carrot4_Chunk0 (8,318 vertex, 265 geometry, 20,921 pixel) and
Global (104 vertex, 3 geometry, 2,281 pixel); output in `build/sage/stages-Carrot4_Chunk0.txt` and
`stages-Global.txt`. Whole-program disassembly of Carrot4 shader 1 (vertex) and 2 (geometry) with nvdisasm:
`build/sage/dis-vs1.txt`, `dis-gs2.txt` (`own_sage_tool code`). "All" below means every program of that stage in
both libraries; field names are NVIDIA's SPH names where the meaning is confirmed by the data.

## (a) Vertex programs

- Inputs: `ALD` only, no `IPA`. Generic vertex attribute n is `a[0x80 + 16n]` (the game reads up to `a[0x170]`,
  attribute 15), as `ALD`, `ALD.64/.96/.128` from the component's address. System values: `a[0x2fc]` vertex id (5,395
  programs), `a[0x2f8]` instance id (1,352). The vertex handle field is always RZ, and no load is `.O`.
  Many programs also fetch per-instance or skinning data themselves: `TLD.SCR.LZ ... 1D` from buffer handles at
  `c[0x10][...]`, indexed by the vertex id.
- Outputs: `AST` with handle RZ. Position is `a[0x70]..a[0x7c]` (`AST.128 a[0x70]` in 7,288 Carrot4 programs, `.96`/
  `.64` when the compiler knows w), generic outputs `a[0x80 + 16n]` as single components or `.64/.96/.128` runs;
  `a[0x64]` (render target array index, layer) in 373 programs. No clip distance or point size writes among the
  common ones.
- End: every vertex program has exactly one `EXIT`, and it is preceded directly by a run of 1 to 21 `AST`s (all
  8,422 programs; most often 4 to 7). Instructions that compute the outputs come before that run; the stores are
  sunk to the end. Code after the `EXIT` (out-of-line branch targets) is allowed.

## (b) Program header (SPH v4), vertex compared with pixel

- Word 0: `SphType` bits 0-4 (1 vertex/geometry, 2 pixel), `Version` 5-9 (4), `ShaderType` 10-13 (1 vertex, 4 geometry,
  5 pixel), `MrtEnable` 14 (pixel only, always 1), `KillsPixels` 15 (pixel), `DoesGlobalStore` 16 (7 Global pixel
  programs), `SassVersion` 17-20 (6 in all programs), `StreamOutMask` 28-31 (1 in all geometry programs, 0 elsewhere).
  Vertex word 0 is `0x000c0481` or `0x020c0481`; bit 25 (reserved in NVIDIA's older SPH) is set in 5,207 of 8,318
  Carrot4 and 103 of 104 Global vertex programs, never in geometry or pixel programs. It does not follow vertex id,
  instance id, textures, LDG, LDC or the vertex factory: meaning unknown. Pixel word 0 is `0x000c5482`
  (`0x000cd482` when it kills).
- Words 1-4 in vertex programs are 0 (no local memory, no geometry fields). Pixel programs use word 1's local memory
  size (0x10 in 4,498 Carrot4 programs, 0x20 in 16). Words 0x50-0x7f are 0 in all programs.
- Input map: 30 bytes from byte 0x14 (words 5-12, half of 12); output map: 30 bytes from byte 0x32 (words 12-19). Each
  is a bitmap of attribute words, LSB first, bit n = attribute address 4n: system values A (0x00-0x5c), system values
  B byte (0x60 primitive id, 0x64 layer, 0x68 viewport, 0x6c point size, 0x70-0x7c position), 16 bytes of generic
  attributes (4 bits per attribute, x first), 2 bytes color (0x280-0x2bc), 2 bytes system values C (0x2c0-0x2dc clip
  distances, ..., 0x2f8 instance id, 0x2fc vertex id), 5 bytes fixed-function texture (0x300-0x39c), 1 reserved. In
  all 8,422 vertex and 268 geometry programs both maps equal exactly the attribute words ALD reads and AST writes.
  (This is 6 bytes shorter than Eden's 36-byte VTG map with a 10-byte texture field; the game's headers settle it.)
- Examples (Carrot4):
  - shader 1, `M_chr_Pikmin_Instanced_Masked_00_Master/.../TBasePassVS...`: reads `ALD R0, a[0x2fc]`,
    `ALD.96 R12, a[0x80]`; input map byte 0x18 = 0x07 (generic 0 xyz), byte 0x2b = 0x80 (vertex id). Writes
    `AST.128` 0x70, 0x80, 0x90, 0xb0, 0xc0 and `AST.96 a[0xa0]`; output map byte 0x35 = 0xf0 (position), 0x36 = 0xff
    (generic 0, 1), 0x37 = 0xf7 (generic 2 xyz, 3), 0x38 = 0x0f (generic 4). Header words 0-14:
    `000c0481 0 0 0 0 0 00000007 0 0 0 80000000 0 0 f7fff000 0000000f`.
  - shader 7, `M_obj_Opaque_00_Master/TGPUSkinVertexFactoryDefault/...ShadowDepth...`: skinning inputs at generic 0-15
    (0x80-0x170) give input map words `033fff77 ff077000`; outputs at 0x70, 0x80-0xd8 give `7fff7000 00000077`.

## (c) Geometry programs

- Header: word 0 `0x100c1081` (type 1, version 4, shader type 4, stream-out mask 1). Word 2 bits 24-31
  `ThreadsPerInputPrimitive` = 1; word 3 bits 24-27 `OutputTopology` = 7 (triangle strip) in all; word 4 bits 0-11
  `MaxOutputVertexCount` = 18 (one-pass point light shadow: six cube faces x 3), 24, or 3 (Global FAtmosphereGS,
  FWriteToSliceGS). Maps as for vertex programs; a geometry program's inputs are the vertex program's outputs (input
  map byte 0x17 = 0xf0 when it reads `a[0x70]` position).
- Prologue (disassembled in Carrot4 shader 2; `LEPC` and `BMOV.64 ATEXIT_PC` occur once per geometry program, 268
  each, and nowhere else): `LEPC R0; MOV R3, <epilogue offset>; IADD3 R0, P0, R0, R3, RZ; IADD3.X R1, ...;
  BMOV.64 ATEXIT_PC, R0`, then `S2R R0, SR_INVOCATION_INFO`. The epilogue is `OUT.FINAL R0; BMOV.64 ATEXIT_PC, 0x0;
  EXIT` (in all 268 the instruction before the last EXIT is `BMOV.64 ATEXIT_PC, 0x0`, and each has one `OUT.FINAL`),
  so every other `EXIT` (1 to 3 per program) ends in `OUT.FINAL` with the current handle. In shader 2 the offset is
  the epilogue's offset from LEPC at offset 0.
- Vertex handles: `PRMT` takes bytes 0 and 2 of `SR_INVOCATION_INFO`; input vertex k of the primitive is
  `ISBERD Rh, [Rb*Rc + k]` (the index always comes from integer ALU). When the primitive has fewer vertices, the
  handle is replaced with `MOV Rh, 0x80000000`. Inputs are then `ALD[.128] Rd, a[addr], Rh` (all of the game's
  geometry `ALD`s carry a handle).
- Output: the handle starts as `MOV R0, RZ` (shader 2). Per vertex: `AST[.n] a[addr], Rdata, R0` for position, generics and
  `a[0x64]` (cube face as layer, written with RZ or a face-index register), then `OUT.EMIT R0, R0, RZ`, which
  returns the next handle in R0; `OUT.CUT R0, R0, RZ` after each triangle (Carrot4: 4,740 EMIT, 1,580 CUT, 265
  FINAL). Stream is always RZ (stream 0). No `.O` loads, and no `.PHYS` access anywhere (every ALD and AST in both libraries
  re-encodes with the offset register RZ).
