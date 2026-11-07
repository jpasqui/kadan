# Switch 2 shaders

Research for shipping Kadan's own material shaders on Pikmin 4's Switch 2 builds: the 2.0.0 Switch 2 Edition
(build `D3A417650133546A11EDFB4EF0FE70B9`) and the Switch 2 demo (`FB43FD7D0A63D26A805B54D3EA308225`). Started
2026-11-06. Phase 1 covers the format, the instruction encoding, the compiler plan and one proof in the emulator. The
full compiler comes after.

Code: `core/own_sage_payload.h` (reader and writer), `tests/own_sage_payload_test.cpp` (host test, in
`tools/check.py`), `tests/own_sage_tool.cpp` (checks against the game's libraries, and the proof's mod),
`tests/own_sage_nvdisasm.py` (comparison with NVIDIA's disassembler). The game's libraries and everything read from
them are game data and aren't in this repository.

## Evidence levels

- **Round trip**: every program of a library is read and written back byte for byte.
- **nvdisasm**: NVIDIA's own disassembler (CUDA 13.1, `nvdisasm -b SM86`) agrees with the reader.
- **Observed**: a value's meaning is read from how it varies across the libraries, not from the game's loader.
  Treat these as leads.
- **In game**: seen in Eglantine with Tsuchi.

## 1. The library

| Build | Library | Pak | Maps | Programs |
| --- | --- | --- | --- | --- |
| 2.0.0 Switch 2 | `ShaderArchive-Carrot4_Chunk0-SF_GLSL_SAGE.ushaderbytecode` | `pakchunk0-Sage.pak` | 1,280 | 29,512 |
| 2.0.0 Switch 2 | `ShaderArchive-Global-SF_GLSL_SAGE.ushaderbytecode` | `pakchunk0-Sage.pak` | 151 | 5,130 |
| Demo | `ShaderArchive-Carrot4-SF_GLSL_SAGE.ushaderbytecode` | `Carrot4-Sage.pak` | 1,093 | 25,018 |
| Demo | `ShaderArchive-Global-SF_GLSL_SAGE.ushaderbytecode` | `Carrot4-Sage.pak` | 151 | 5,130 |

The two Global libraries are byte-identical. Both paks store the libraries uncompressed. The format name is
`SF_GLSL_SAGE`, so a mod's library is `ShaderArchive-<name>-SF_GLSL_SAGE.ushaderbytecode` (Tsuchi's Switch 2 tables
already say `shader_format: SF_GLSL_SAGE`).

The container is UE's `FSerializedShaderArchive` version 2, the same as on Switch 1: map hashes, shader hashes, map
entries (first index, count, first preload, preload count), shader entries (offset, size, uncompressed size,
frequency), preload entries, shader indices, then the payloads, LZ4 where size and uncompressed size differ. Kadan's
archive writer (`WriteOwnedShaderArchive`) writes it unchanged, and the game opened such a file in the proof below.

Frequencies are UE 4.27's: 0 vertex, 3 pixel, 4 geometry, 5 compute, 6 to 8 ray tracing (2 programs each, Global
only). Carrot4 in 2.0.0 holds 8,318 vertex, 20,921 pixel, 265 geometry and 8 compute programs.

A map lists its programs sorted by shader hash. The engine's shader map resource keeps the same order, so a
program's place in its map (what Tsuchi's `shaders[].place` names) is its index in the library's map.

Program names stay in the payload: `<master material>/<vertex factory>/<shader type>/<permutation>:<usf path>`, for
example `M_stg_Plants_Masked_TwoSided_00_Master/FLocalVertexFactory/TBasePassPSFNoLightMapPolicySkylight/0:/Engine/Private/BasePassPixelShader.usf`.
A material instance with static switches gets a map of its own, which still carries its master's name, so the map
belongs to an instance only through the instance's package (below).

## 2. The payload

Round trip: 29,512/29,512 (2.0.0 Carrot4), 25,018/25,018 (demo Carrot4), 5,129/5,130 (Global; the one left is a
ray tracing hit group with more than one code section, which the reader refuses).

A payload is Switch 1's `GLSL_SWITCH` payload with the bindings reordered and trimmed. In order:

| Field | Type | Notes |
| --- | --- | --- |
| marker | u32 | `0x474c534c`: the bytes `LSLG` |
| stage | u16 | `VS`, `PS`, `GS`, `CS` as on Switch 1; ray tracing `S1`, `S2` ... |
| resource table bits | u32 | the uniform buffers the tables below bind from |
| SRV, sampler, UAV tables | u32 arrays | UE's resource table: a head word per uniform buffer, then rows `buffer << 24 \| resource index << 8 \| slot`, ended by `0xffffffff` (the format `ReadResourceTable` already reads) |
| layout hashes | u32 array | one per uniform buffer slot; 0 for a struct without one |
| texture table | u32 array | as the SRV table |
| attribute mask | u32 | observed: vertex inputs read in vertex programs (`1`, `0x1f`, `0xd83f` ...); 0 in pixel programs |
| globals buffer | i32 | observed: the uniform buffer slot of the loose parameters, -1 for none |
| globals bytes | u32 | observed: its size; 0 exactly when the slot is -1, in every program |
| word 3 | u32 | the bytes of the program's handle table in `c[0x10]`: 16 per resource slot up to the highest slot the tables name or the code reads, at least 32 (`HandleTableBytes`; 1,001/1,001 pixel, 390/390 vertex, 7/7 geometry programs; compute programs differ) |
| counts | 4 × u8 | observed: texture, sampler, uniform buffer and storage slot counts, as Switch 1's first counts |
| word 4, byte 5 | u32, u8 | observed: 0 (3 in the compute programs), 0 |
| hash | 20 bytes | observed: SHA-1; SHA-1 of nothing in programs without varyings |
| name | FString | ANSI, the program name above |
| copy info | u32 | 0 everywhere |
| code | u32 size + bytes | section 3 |
| control | u32 size + bytes | NVN 2 control section: magic `0x98761234`, 224, 240 or 256 bytes |
| tail | 11 × u32 | `0, 1, 0, 0, control size, code size, -1, scratch, scratch, 0, 4`; the last word is the size of the optional data, as on Switch 1 |

What is gone compared with Switch 1: packed uniform buffers, packed global arrays, and the named input and output
varyings. The varyings now live only in the program header's attribute maps (section 3), so matching a pixel
program to a vertex program goes by generic attribute slot, not by name.

Layout hashes: `OpaqueBasePass` is `0x0d480502` and `TranslucentBasePass` `0x05ec0dee`, the same as on Switch 1.
`View` changed from `0x166705f7` to `0x1f8305f6`.

### Control section

224 bytes in vertex programs, 224 or 240 in pixel programs (a few 248 to 312), mostly 240 in compute programs. Fitted
on 5,925 programs (every 7th of 2.0.0 Carrot4, every 3rd of Global) with `own_sage_tool control`; "always" means every
one of them. Word numbers count 32-bit words from the start.

| Word | Value | Evidence |
| --- | --- | --- |
| 0–3 | `0x98761234`, `0xc9`, `0x14`, `0x000e0170` | always |
| 4–5, 10–11 | two 64-bit hashes of the program | distinct per program |
| 6–7 | `0x1ff2045f`, `0x00288000` | always |
| 12, 38, 53 | the control section's size | 5,924/5,925 (word 12) |
| 14 | where the instructions and their NOP padding end, from the start of the code: the constant table's offset | 466/466 programs with a constant table |
| 16 | the code size | 5,458/5,925 |
| 17 | the code size | always |
| 19 | registers: the highest register used + 3 (the count plus 2) | 685/738 against `nvdisasm` register numbers; the rest within the error of counting vector operand widths |
| 20 | local memory per thread: `0x200` (`0x400` once) when the program uses `STL`/`LDL`, else 0 | always |
| 21 | `0x10000` in pixel programs, else 0 | always |
| 22–24 | compute: the workgroup size (mostly 8, 8, 1); pixel: mostly 0, 0, `0x10000` | observed |
| 15, 25, 51, 55, 56–59 | small values by stage (compute word 51 = 9, 15, 21 ...; pixel words 56–57 `0x01010101`, `0x0100010d` in the 240-byte form) | meaning open |

A program rewritten in place keeps its code size, register budget (when it uses no more registers) and constant
table, so the game's control section stays valid; the proof below relies on that. Writing a program from scratch
needs words 14, 16, 17, 19 and 20 computed, the two hashes (unknown function; whether the driver checks them is
untested) and the open words copied from a game program of the same stage and shape.

## 3. Native code

The code section is what the GPU runs: for graphics stages a 128-byte program header (NVIDIA's Shader Program
Header, version 4, 32 words), then 128-bit instructions. Compute programs start straight with instructions. Every
program ends with `EXIT` and an unconditional branch to itself. After that come NOP padding, the program's own
constant table (for example a 4 × 4 dither matrix), and zeros. The code reads that table through a constant bank
the control section places, so a rewritten program keeps its code size and the table's offset.

Header (all 29,504 graphics headers are version 4): word 0 holds the type (1 vertex/geometry, 2 pixel), version,
shader type (1 vertex, 4 geometry, 5 pixel), MRT and kill bits as in Eden's `ProgramHeader`. Pixel programs keep
their output map in word 18 (4 bits per render target) and depth/sample mask in word 19. Pixel outputs are in
consecutive registers from R0, one per enabled component, render target 0 first. Every base-pass pixel program in
Carrot4 writes render target 0 only (`0xf`).

### Encoding

The instructions are NVIDIA's Volta-and-later encoding for an Ampere-class GPU (the console's T239). NVIDIA's
`nvdisasm -b SM86` reads them:

- All 178 opcodes that occur in the four libraries decode (3 samples each, 0 illegal).
- Whole programs disassemble without errors once the trailing constant table is left out: 539,702 instructions
  from every 40th program of 2.0.0 Carrot4 and 193,055 from every 10th program of Global.
- On those 732,757 instructions the reader agrees with `nvdisasm` on every guard predicate, 625,463 destination
  registers, 106,092 constant-bank operands and 27,680 immediates, with 0 disagreements (`tests/own_sage_nvdisasm.py`).

Field layout, as in Mesa NAK's `sm70_encode.rs`: opcode bits 0–11 (bit 9 immediate form, bit 11 constant form for
most ALU opcodes), guard predicate 12–15, destination 16–23, source A 24–31, source B 32–39 or immediate 32–63 or
constant `c[bits 54–58][bits 38–53]`, source C 64–71, scheduling control 105–125 (stall, yield, write barrier, read
barrier, wait mask, reuse). Uniform-datapath instructions (`ULDC`, `ULOP3` ...) use the same guard bits for a uniform
predicate.

Eglantine runs these programs by lowering each instruction to Maxwell (`src/shader_recompiler/frontend/ampere/lower_to_maxwell.h`,
GPL-3.0, encoding reference NAK). It's a working reference for what each opcode does, but Kadan can't copy it.

### How a game program reaches its inputs

Read from the game's base-pass programs and their binding tables (observed in many programs, not from the driver):

- Uniform buffer slot n is constant bank `c[n + 3]`: View at `c[0x3]`, the pass buffer at `c[0x4]`, the material
  buffer at `c[0x6]` in a translucent particle program with four slots.
- The texture or sampler in resource-table slot s has its bindless handle at `c[0x10][0x10 * s]`. A program loads
  both with `ULDC`, ORs them (`ULOP3.LUT 0xfc`), and passes the result to `TEX.SCR` / `TLD.SCR` / `TLD4.SCR` in a
  uniform register.
- Varyings come in through `IPA` from attribute addresses (`a[0x70]`–`a[0x7c]` position, generic attributes from
  `a[0x80]` in steps of 16), as the header's input map lists them.
- Vertex programs read their inputs with `ALD` (generic attribute n at `a[0x80 + 16n]`, `a[0x2fc]` vertex id,
  `a[0x2f8]` instance id) and write position (`a[0x70]`) and generic outputs with `AST`, all of them right before the
  single `EXIT`. Geometry programs get vertex handles with `ISBERD`, read with `ALD ..., handle`, write with `AST ...,
  handle` and `OUT.EMIT`/`OUT.CUT`. In every vertex and geometry program the header's input map (30 bytes from byte
  0x14) and output map (30 bytes from 0x32) are bitmaps of exactly the attribute words read and written, bit n for
  address 4n (`own_sage_tool stages`).

## 4. Compiler approach

**Recommendation: add an Ampere backend to Kadan's compiler, and port NAK's encoder tables into it.** Keep the
SPIR-V front end (`spirv_front.h`), the nvasm IR and its lowering, and the register allocator's design. Replace
instruction selection, scheduling and encoding.

Why:

- The front half carries over unchanged. `spirv_front.h` → nvasm IR knows nothing of Maxwell. The game's uniform
  buffer layouts plug in the same way once the 2.0.0 tables exist (section 5).
- The instruction mix is small. In every 40th 2.0.0 Carrot4 program, 59 instruction families occur and about 35
  cover 99% (`FMUL` 29%, `FFMA` 23%, `FADD` 10%, `MOV` 8%, `MUFU` 4%, then `FMNMX`, `BRA`, `FSEL`, `FSETP`, `ULDC`,
  `IPA`, `TLD`, `TEX` ...). That's close to the set `maxwell_mir.h` selects today.
- Scheduling carries over. Maxwell's control words and Volta's per-instruction control have the same fields
  (stall, yield, write and read barrier, wait mask, reuse), so `maxwell_ra.cpp`'s scheduling model moves to 128-bit
  words. NAK's SM80 latency tables (MIT) supply the numbers.
- Encoding is the part to port. NAK's `sm70_encode.rs` is MIT and covers every opcode above. Port the ones
  instruction selection emits into C++ (`compiler/ampere_encode.cpp`), with a host test that checks each against
  `nvdisasm -b SM86` text, the same as `tests/own_sage_nvdisasm.py`.
- New lowering is limited to: `IADD3`/`LOP3`/`IMAD` in place of `XMAD`/`LOP`/`ISCADD`, `BSSY`/`BSYNC`
  convergence in place of `SSY`/`SYNC`, the uniform datapath for bindless handles (`ULDC`, `ULOP3`), and `TEX.SCR`
  with a combined handle in place of bound texture slots.

### The encoder, started (2026-11-06)

`compiler/ampere_encode.h` ports NAK's field layout and covers every instruction family that occurs in Carrot4 and
Global except `SHFL`, `FSWZADD`, `ATOMG`, `REDUX`, `IMAD.WIDE`/`IMAD.HI`, `BRX`/`BRXU`, `PIXLD` and a few uniform ops
(`UIMAD`, `ULEA`, `UFLO`, `UBREV`, `UPOPC`, `UPRMT`, `UPLOP3`):

- ALU: `FADD`, `FMUL`, `FFMA`, `MOV`, `MUFU`, `FMNMX`, `FSEL`, `FSETP`, `FSET.BF`, `IADD3` (with carries and `.X`),
  `LOP3`, `IMAD` (with `.X`), `ISETP`, `SEL`, `SHF`, `IMNMX`, `PLOP3`, `POPC`, `FLO`, `IABS`, `PRMT`, `LEA`, `R2P`,
  `P2R`, with register, immediate, constant and uniform-register sources.
- Conversions: `F2I`, `I2F`, `I2FP`, `F2F`, `FRND`, `F2FP`, `I2I`.
- Control flow and special registers: `EXIT`, `BRA`, `KILL`, `NOP`, `BSSY`, `BSYNC`, `BREAK`, `BMOV`, `WARPSYNC`,
  `CALL`, `RET`, `S2R`, `CS2R`, `DEPBAR`; `IPA`.
- Textures: `TEX`, `TLD`, `TLD4`, `TXQ` with the handle in a register (`.B`), in a constant bank or in a uniform
  register. Bits 16–23 hold the first destination, 24–31 the coordinates, 32–39 the handle or extras, 64–71 the second
  destination (nvdisasm prints it first). Bit 60 (`.SCR`) is set on 95% of the game's texture instructions; the others
  are explicit-lod `CUBE`/`ARRAY_CUBE` lookups, which still write both destinations. nvdisasm also accepts bit 60 clear
  with the second destination `RZ`; what the hardware does with it is untested.
- Uniform datapath: `ULDC` (also indexed by a uniform register), `ULOP3`, `UIADD3`, `USHF`, `UMOV`, `R2UR`, `S2UR`,
  `UISETP`, `USEL`, `VOTEU`.
- Memory: `LDC`, `LDL`/`STL`, `LDS`/`STS`, `LDG`/`STG`, `ATOMS`.
- Half precision: `HADD2` (with `.F32`, the game's half-to-float conversion `HADD2.F32 R, -RZ, R.H0_H0`), `HSETP2`.
- Attribute memory and geometry output: `ALD` (`.64`/`.96`/`.128`, `.O`, `.P`, a vertex handle in bits 32–39), `AST`
  (handle in bits 64–71), `OUT` (`.EMIT`, `.CUT`, `.EMIT_THEN_CUT`, `.FINAL`), `ISBERD`. Address in bits 40–49, width
  74–75. The `.PHYS` forms with an offset register are not taken.
- Barriers and votes: `BAR.SYNC`/`BAR.ARV`, `VOTE`, `LEPC` and `BMOV.64 ATEXIT_PC` (register or 49-bit immediate).
- Surfaces: `SULD.P`, `SUST.P`, `SUATOM.D`, `SURED.D` with the handle in a register (bits 64–71), a constant bank or (`SULD`,
  `SUST`) a uniform register; nvdisasm refuses the uniform forms of `SUATOM` and `SURED`.

- `tests/ampere_encode_test.cpp` (host test, in `tools/check.py`): 279 cases whose words `nvdisasm -b SM86` printed
  exactly as expected (`tests/ampere_encode_dump.cpp`, `tests/ampere_encode_nvdisasm.ps1` and
  `tests/ampere_encode_pin.py` repeat that check and re-pin the words).
- `own_sage_tool reencode`: every instruction of these families in every 2.0.0 Carrot4 and Global program, read back
  into operands and encoded again, is bit-identical to the game's: 23,235,682 instructions, 0 different. 1,622
  instructions (0.007%, 16 of them in Carrot4) belong to the families not covered. Where the game's compiler differs from NAK, the
  encoder follows the game: unused sources are 0 rather than `RZ` (`FMUL`, `FADD`, `FSET`, conversions), `IMAD` and
  `LEA` carry `!PT` in their unused carry-in, `F2I` sets `.NTZ`, and `LDL`, `LDS`, `STL`, `STS`, `ATOMS` use the forms
  without the uniform address (bit 91 clear). `FMUL` also takes the scale field (bits 84–86) the game uses for `.D2`
  and the like.

Next: instruction selection from the nvasm IR, and scheduling.

Rejected:

- **Wrapping NAK whole.** NAK compiles NIR, so Kadan would also carry Mesa's C code, NIR, `spirv_to_nir`, Rust and
  meson inside a UE 4.26 Windows plugin. NAK targets nouveau's conventions (its program headers, constant banks
  and texture handles), and NVN 2's differ (section 3), so its graphics output would need patching anyway.
- **Eglantine's lowering.** GPL-3.0, and it goes the other way (Ampere to Maxwell).
- **The NVIDIA driver.** The desktop's RTX 3080 is SM86, and its GL driver would hand back SM86 code. Creators can't
  be required to have one, and the driver's binary format is undocumented.

## 5. What has to be recovered again from 2.0.0

### Uniform buffer layouts, layout hashes, resource indices (done 2026-11-06)

The layout reader (a separate research tool) now reads 2.0.0's program too. 2.0.0
was built with SDK 21 and pointer authentication, which broke the first run (0 of 4,146 calls decoded):

- functions return with `RETAB`, so the member functions never "returned";
- the linker turned many `ADRP+ADD` pairs into `NOP; ADR`, so names and the previous-member pointer were lost;
- the call the script took for the constructor (`0x9bfdd0`) grows the member array. The constructor is `0x827950`:
  x0 metadata, x2/x3 type name, x4 shader variable name, x5 static slot, w6 size, x7 the member array that the
  struct's `zzGetMembers` filled just before (it calls the member functions in a loop through `BLR`).

The script now decodes `ADR`, takes `RETAA`/`RETAB` as returns, and finds the constructor as the call given the
View type name in x2 and a size in w6. On 1.1.1 its four tables are byte-identical to the ones Kadan's headers were
made from. On 2.0.0 it decodes 546 of 550 metadata objects: 1,411 member offsets, 107 struct sizes, 240 resource
indices differing from stock and 19 layout hashes differing from stock. The tables and the headers
`make_game_layouts_header.py --out` writes from them are game data and aren't committed.

Checks:

- The recovered View hash is `528680438` = `0x1f8305f6`, the hash 2.0.0's programs carry. `OpaqueBasePass`
  (`0x0d480502`) and `TranslucentBasePass` (`0x05ec0dee`) also match the programs.
- Every layout hash in 2.0.0's two libraries is either in the recovered table (33 distinct, all found) or a
  per-material buffer (131 distinct, ending `08f7`, `0dff` or `00ff`). Those come from each material's size and
  resources through the engine's formula, which Kadan already has (`LayoutHash` in `custom_material_texture_list.h`).
- The View constructor's size argument is `0x1880`, the 6,272 bytes the tables give.

What changed from 1.1.1: View grew from 4,704 to 6,272 bytes (constant data 4,160 to 5,712). It gained UE 4.27's
physics field and virtual texture feedback members (11), lost `DepthOfFieldTiltAngles`, and 99 members after offset
2,848 moved. Members before that offset keep their offsets, including the ones Kadan's effect prelude reads
(`WorldToClip` 64, `PreExposure` 2,180, `GameTime` 2,440, `DirectionalLightDirection` 2,512). `OpaqueBasePass`,
`TranslucentBasePass`, `Primitive` and `ShadowDepthPass` are unchanged member for member.

Stock side: the "differs from stock" tables compare with UE 4.26, not 4.27, on purpose. Kadan compiles with the
4.26 editor, so its SPIR-V has 4.26's offsets, and the tables say where to move them. The absolute game offsets
(`game-layouts.tsv`) don't depend on the stock side.

Engine hashes (`material_engine_hashes.h`): `ViewLayout` becomes `0x1f8305f6`. The local, instanced and skinned
vertex factory hashes and the View buffer name hash occur in every Area001 instance with a map of its own, so they
are unchanged. The anisotropy shader hashes don't occur in those packages and are unchecked.

### Still open

| Item | Switch 1 source | 2.0.0 status |
| --- | --- | --- |
| Uniform buffer to constant bank, texture handles | Switch 1: binding through NVN texture IDs | observed `c[n + 3]` and `c[0x10][0x10 * slot]`; confirm on more programs and in the driver |
| Varying slots | named varyings in the payload | names are gone; derive UE's attribute order from the program headers' input and output maps |
| Control section | Switch 1's own layout | sizes, constant-table offset, register count and local memory fitted (section 2); the two hashes and the small words by stage are open |
| Payload words 3, 4 and the hash | — | meaning open |
| Material shader map id in 4.27 packages | 4.26 envelope in `material_shader_map.h` | the same envelope (cooked 1, valid 1, quality 4, feature level 3, map id hash, alignment `0xffffffff`, flags 1, frozen size) in the Area001 instances read; the frozen content's 4.27 layout is not checked |

## 6. In-game proof (2026-11-06, Eglantine)

**Result: a game material's pixel programs, rewritten by Kadan's code and shipped in a mod's `SF_GLSL_SAGE`
library through Tsuchi, draw flat magenta in Pikmin 4 2.0.0 Switch 2 Edition.** The player character E (front right)
is magenta; Shepherd and Oatchi keep their own shaders, and E's shadow is unchanged because only base-pass pixel
programs were replaced.

The pictures (before: run `sage-magenta-01`, frame 6001; after: run `sage-magenta-03`, frame 6601) show game
content and aren't in this repository.

How it was made (`own_sage_tool magenta`):

1. For each target material instance, read its package from 2.0.0's IoStore (`retoc to-legacy --version UE4_27`):
   the shader map id hash Tsuchi compares, and the resource hash that names its map in the game's library.
2. From that map, take every base-pass pixel program (name contains `/TBasePassPS`). Rewrite its code in place:
   `MOV` the output registers (render target 0 = 1, 0, 1, 1; all others 0), `EXIT`, branch to itself, then NOPs to the
   old length. The program header, control section, bindings and constant table stay. `nvdisasm` reads the result as
   intended.
3. Write the programs into `ShaderArchive-KadanSageMagenta-SF_GLSL_SAGE.ushaderbytecode` with Kadan's archive writer,
   and `mod.json` `"shaders"` rows (`material`, `game_material`, `map_id`, `map_shaders`, `archive_map`,
   `shaders[].place/hash`). This is the route Kadan's creator materials take on Switch 1 (Tsuchi
   `shader_materials.cpp`), with the library named by Tsuchi's Switch 2 `shader_format`.

The run boots 2.0.0 (`D3A41765`) in a Switch 2 emulator with Tsuchi and only this mod, from a save. The save
opens on Shepherd's dialog at a pipe, and nothing advances it. Tsuchi's log says `mod kadan.sage-magenta 1.0.0
running`; its admission check had compared every row's map id hash and program count with the live game's shader
maps and refused none.

What the three runs show:

| Run | Mod | Seen |
| --- | --- | --- |
| `sage-magenta-01` | 25 Area001 instances | admitted; nothing magenta: none of them is on screen in the dialog scene |
| `sage-magenta-02` | all 560 Area001 instances with maps of their own | admitted; nothing magenta: the visible ground and plants use instances without maps of their own (Tsuchi needs one) or aren't Area001's |
| `sage-magenta-03` | 727: Area001 without LODs, plus every `MI_chr_Avatar`, `MI_chr_Buddy`, `MI_chr_Npc` instance with a map of its own | the player character is magenta |

Not shown: which of the 727 materials drew E (the avatar instances are the candidates); Shepherd's and Oatchi's
meshes use instances whose own map isn't among them, or a child instance without one. The demo build (`FB43FD7D`)
was not run; its library has the same format. Kadan's export doesn't write this yet: the proof's mod comes from the
host tool, and `BuildOwnedShaderArchiveBundle` still names `GLSL_SWITCH`.

## 7. Compiling for SF_GLSL_SAGE (phase 2, started 2026-11-06)

`compiler/ampere_write.cpp` (`nvasm::ir::write_ampere`, header `compiler/ampere_write.h`) compiles NV_gpu_program5
pixel programs, the form Kadan's material emitter writes, into Switch 2 code:

1. Kadan's Maxwell instruction selection, optimization and register allocation run unchanged. Their machine IR
   already places uniform buffer n in bank `3 + n`, as 2.0.0's programs do, and builds texture handles in registers.
2. Each allocated instruction becomes one or two Ampere instructions (`compiler/ampere_encode.h`). Switch 1's handle
   words `c[2][32 + offset]` are moved to `c[0x10][0x10 * slot]` (`AmpereOptions::handles`). `IPA.MULTIPLY` becomes
   `IPA.PASS` and `FMUL.FTZ.RZ` by 1/w, through a scratch register when allocation gave both the same register.
   `F2F` rounding becomes `FRND`; `RRO` a move; `SSY`/`SYNC` become `BSSY`/`BSYNC` and a branch; `TEX` the bindless
   `TEX.B` with all components from the destination register (bit 60 clear; nvdisasm accepts it) and eviction
   priority normal, as the game's.
3. Scheduling is conservative, from NAK's SM80 latencies: a fixed-latency result is read no sooner than 6 cycles
   after issue (predicates 14); variable-latency instructions (MUFU, IPA, FRND, textures) get a write scoreboard
   that readers wait on and a read scoreboard that later writers of their sources wait on; at labels, branches,
   EXIT and KILL everything settles. Yield follows the game (set when the stall is 1 or 2).
4. Layout: program header version 4 (word 0 as 2.0.0's pixel programs, input and output maps as the Maxwell
   backend writes them), the instructions, a branch to itself, NOPs to 128 bytes, a zeroed 128-byte tail, 256-byte
   aligned. `OwnSage::SageControl` (core/own_sage_payload.h) writes the control section from a game program's of the
   same stage: words 14, 16, 17, 19 and 20 as fitted in section 2.

Checks: Kadan's `grid`, `rainbow` and `hologram` effect programs compile (`cel`, `blackhole`: `FSET` missing;
`lava`: loop breaks missing). The compiled `grid` (80 instructions) and a texture program disassemble cleanly with
`nvdisasm -b SM86`, including hand-checked `BSSY`, `BSYNC`, `SEL` and `FRND.FLOOR` words. Tools:
`tests/ampere_write_tool.cpp` (`compile` prints the listing; `mod` builds a Tsuchi mod from compiled programs for
game material instances, keeping each game program's bindings).

Kadan's export: `BuildOwnedShaderArchiveBundle` takes the family's shader format. Exporting own shaders for the
switch2 family stays refused (`FGameFamily::KadanShaders`): a material's own map also needs its vertex programs
(Kadan writes those for Switch 1 in `own_payload.h`), Sage payload descriptions from Kadan's binding records, and the
4.27 layout of the material's frozen shader map content. Those are the next steps after the in-game check below.

### In game: compiled programs draw, textures sample (2026-11-06)

All in Eglantine on 2.0.0 from a save, E's materials replaced through Tsuchi's mod.json "shaders":

| Run | Program | Seen |
| --- | --- | --- |
| `sage-compiled-03` | unlit green, compiled from scratch (own header, control section, size) | E green |
| `sage-compiled-02` | the same, three ways (game header and size; own header; all own) | E green in all three |
| `sage-compiled-05` | `Diffuse_01` sampled in screen space | E white with detail: the untinted diffuse |
| `sage-compiled-06` | `Normal_01` sampled in screen space | E yellow-green: a two-channel normal map |

Texture parameters are resolved as on Switch 1 (`ResolveMaterialBindings` and `Resolve` on the donor program's
tables; Sage tables have Switch 1's row format), and the handle words move to `c[0x10][0x10 * slot]`. Two findings
on the way: a lookup in the non-split form (bit 60 clear) sampled black, and the game never uses it for 2D, so
`TEX.B` is written in the split form with dst1 = dst0 + 2; and a run with only some of E's materials (`sage-compiled-01`)
showed nothing for reasons not found (no Tsuchi refusal, no rejected program), while the same materials drew in the
later runs.

The pictures show game content and aren't in this repository.

### Vertex programs

`ALD`, `AST`, `S2R`, `LDC`, `TLD`, `SHL`/`SHR` (`SHF`), `FSET` (the integer-mask form through `FSET.BF`, `>> 29` and
a negation), `IMNMX`, `F2I`, `I2F` (`I2FP`, fixed latency as in the game) and `XMAD` (emulated exactly with masks,
`IMAD` and `IADD3` through two registers above the allocation) translate. The vertex program header is word 0
`0x000c0481` with the input and output maps at the bytes the Maxwell header uses (docs/SWITCH2_VERTEX_NOTES.md). All 17
of Kadan's own vertex programs (`own_shaders/*.payload`) compile, and the three largest disassemble cleanly. A store
(`AST`) takes a read scoreboard but no write scoreboard; giving it one made nvdisasm refuse the program. Not done:
geometry programs (the one-pass point shadow program needs `VMAD`, `ISBERD`, `OUT`), and their payloads in game.

### Which hashes the game checks (2026-11-06)

Green unlit programs compiled from scratch on all 727 materials, one hash zeroed per run:

| Run | Zeroed | Seen |
| --- | --- | --- |
| `sage-compiled-07` | the payload's SHA-1 field and the control section's words 4-5 and 10-11 | E keeps the game's look |
| `sage-compiled-08` | the payload's SHA-1 field only | E keeps the game's look |
| `sage-compiled-09` | the control section's words 4-5 and 10-11 only | E green |

The control-section hashes are not checked: Kadan writes them as 0. The payload's SHA-1 field is the hash of the
program's input varyings (`core/sage_varyings.h`: per varying its 32-bit location, then its name and a NUL, in the cooker's container
order; SHA-1 of nothing without varyings). The engine pairs vertex and pixel programs by it, so a wrong value falls
back to the game's shaders silently (Tsuchi keeps them). A program compiled against a game donor keeps the donor's
value; Kadan's own vertex and pixel pairs compute it from their varyings.
