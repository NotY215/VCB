# VCB — Vayu Compiler Backend

Independent C++20 project. Replaces QBE + gcc with a direct x86-64
code generator and native linker frontend.

VCB lives at `E:\VCB`. It is NOT part of the Vayu repository. Once
complete, its binary and supporting files go into Vayu's `tools/`
folder and `vayuc` switches from QBE+gcc to VCB.

## Status

Phase 26 Part 1 — IR, parser, printer, `vcb dump`.
Phase 26 Part 2 — x86-64 codegen, PE + ELF writers, prebuilt runtime.

## Build

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build

Produces `build/bin/vcb.exe`.

## Usage

    vcb version
    vcb dump examples/hello.vcbir

## IR

Textual SSA form. One function per `func NAME(params) -> rettype { ... }`
block. Basic blocks with names and colon. Ops reference SSA values named
`%t0`, `%t1`, ... or parameter names.

Types: `i1 i8 i16 i32 i64 f32 f64 ptr<T>`.

Ops: `const.iN`, `const.fN`, `copy`, `add`, `sub`, `mul`, `div`, `mod`,
`neg`, `eq`, `ne`, `lt`, `le`, `gt`, `ge`, `and`, `or`, `xor`, `shl`,
`shr`, `call`, `ret`, `jmp`, `br`, `phi`, `alloca`, `load`, `store`,
`bitcast`, `sitof`, `fptosi`.

A sample function:

    func add(a: i64, b: i64) -> i64 {
    entry:
      i64 %t0 = add a, b
      ret %t0
    }

## Roadmap

- Part 1 (done): IR, parser, printer, `vcb dump`
- Part 2 (next): x86-64 codegen, PE writer, ELF writer, prebuilt runtime,
                 `vcb build <file.vcbir> -o out --target pe|elf`
- Post-VCB: bundle into Vayu's `tools/`, retire QBE+gcc