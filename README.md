# VCB — Vayu Compiler Backend

A compact, single-executable, QBE-inspired compiler backend written in C++20.

## Build (Visual Studio)

1. File → New → Project → **Empty Project (C++)**, name it `VCB`.
2. Add the six `.cpp` files in `src/` and the single header in `include/`.
3. Add `test/tests.cpp` as a second project (`vcb-tests`).
4. Project Properties:
    - C/C++ → Language → C++ Language Standard: **/std:c++20**
    - C/C++ → Optimization: **/O2 /GL /fp:fast /arch:AVX2**
    - C/C++ → Code Generation → Runtime Library: **Multi-threaded (/MT)**
    - Linker → Optimization: **/LTCG**
    - C/C++ → General → Additional Include Directories: `$(ProjectDir)include`
    - C/C++ → Warning Level: **/W4**
5. Build Release | x64. Output: one `.exe`, self-contained.

## Build (CMake)

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## IR syntax (QBE-inspired)

```
export function i32 main() {
entry:
    %1 = add i32 5, 3
    %2 = mul i32 %1, 2
    ret i32 %2
}
```

Supported ops: `add sub mul div rem and or xor shl shr sar neg not
ceq cne clt cle cgt cge load store alloc trunc zext sext jmp jnz
ret call phi copy`.

Types: `void i8 i16 i32 i64 f32 f64 ptr`.

## Pipeline

```
IR text ──▶ parseIR ──▶ optimize ──▶ lower ──▶ codegen ──▶ x86-64 asm
                       (const-fold,   (peephole)
                        DCE, CSE,
                        simplify)
```

Output is GNU-as-compatible Intel-syntax assembly. Assemble with:

```
vcb input.vcb -o out.s
gcc out.s -o out
```