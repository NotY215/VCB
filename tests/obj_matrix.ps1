# tests\obj_matrix.ps1 -- Phase 28.1 + 28.3 COFF object + link matrix.
#
# For each examples\<name>.vcbir:
#   1. vcb emit-obj <name>.vcbir -o <name>.obj
#   2. vcb link <name>.obj -o <name>.exe --target pe --lib tools\kernel32.lib
#   3. run <name>.exe, compare stdout against expected.
#
# Usage:  powershell -NoProfile -File tests\obj_matrix.ps1 [-NoRun]

param(
    [switch]$NoRun
)

# NOTE: "Continue", not "Stop".  vcb writes its "wrote ..." status
# line to stderr by design.  Under ErrorActionPreference = "Stop",
# PowerShell 5.1 raises NativeCommandError on any native command that
# writes to stderr, even when `*> $null` is used.  Every native call
# below is followed by an explicit $LASTEXITCODE check, so real
# failures still fail.
$ErrorActionPreference = "Continue"

$root   = Split-Path -Parent $PSScriptRoot
$vcb    = Join-Path $root "build\x64-Release\bin\vcb.exe"
$klib   = Join-Path $root "tools\kernel32.lib"
$outDir = Join-Path $root "_out\obj_matrix"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

if (-not (Test-Path $vcb))  { Write-Host "missing $vcb -- build VCB first" -ForegroundColor Red; exit 2 }
if (-not (Test-Path $klib)) { Write-Host "missing $klib -- copy kernel32.lib into tools\" -ForegroundColor Red; exit 2 }

$tests = @(
    @{ name = "t01_empty";          out = "" },
    @{ name = "t02_arith";          out = "12`n2`n35`n1`n2" },
    @{ name = "t03_print_int";      out = "12345`n-67890`n0" },
    @{ name = "t04_print_str";      out = "hello, vayu`n" },
    @{ name = "t05_print_bool";     out = "true`nfalse" },
    @{ name = "t06_list_create";    out = "0`n[]" },
    @{ name = "t07_list_push";      out = "3`n[10, 20, 30]" },
    @{ name = "t08_multiple_calls"; out = "300`n400" },
    @{ name = "t09_str_const";      out = "hello world`nhello vayu" },
    @{ name = "t10_large_text";     out = "1`n2`n3`n4`n5`n6`n7`n8`n9`n10" },
    @{ name = "t11_multi_import";   out = "42`ntrue`nimports`n[7]" }
)

$pass = 0
$fail = 0
$skip = 0

foreach ($t in $tests) {
    $ir  = Join-Path $root "examples\$($t.name).vcbir"
    $obj = Join-Path $outDir "$($t.name).obj"
    $exe = Join-Path $outDir "$($t.name).exe"

    if (-not (Test-Path $ir)) {
        Write-Host ("SKIP  {0}  (missing {1})" -f $t.name, $ir) -ForegroundColor Yellow
        $skip++
        continue
    }

    & $vcb emit-obj $ir -o $obj --target pe *> $null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $obj)) {
        Write-Host ("FAIL  {0}  emit-obj exit={1}" -f $t.name, $LASTEXITCODE) -ForegroundColor Red
        $fail++
        continue
    }

    & $vcb link $obj -o $exe --target pe --entry vayu_entry --lib $klib *> $null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $exe)) {
        Write-Host ("FAIL  {0}  link exit={1}" -f $t.name, $LASTEXITCODE) -ForegroundColor Red
        $fail++
        continue
    }

    if ($NoRun) {
        Write-Host ("PASS  {0}  (emit-obj + link only)" -f $t.name) -ForegroundColor Green
        $pass++
        continue
    }

    $actual = & $exe 2>&1 | Out-String
    $actual = $actual -replace "`r`n", "`n"
    $actual = $actual.TrimEnd("`n")
    $want = $t.out

    if ($actual -eq $want) {
        Write-Host ("PASS  {0}" -f $t.name) -ForegroundColor Green
        $pass++
    }
    else {
        Write-Host ("FAIL  {0}" -f $t.name) -ForegroundColor Red
        Write-Host ("  expected: {0}" -f ($want   -replace "`n", "\n"))
        Write-Host ("  actual  : {0}" -f ($actual -replace "`n", "\n"))
        $fail++
    }
}

Write-Host ""
Write-Host ("total: {0} pass, {1} fail, {2} skip" -f $pass, $fail, $skip)
exit ($(if ($fail -gt 0) { 1 } else { 0 }))