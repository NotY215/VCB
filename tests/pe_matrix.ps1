# tests\pe_matrix.ps1
# PE test matrix for VCB.  Builds each .vcbir under examples\, runs
# the produced .exe, compares stdout against expected.
#
# Usage:  powershell -NoProfile -File tests\pe_matrix.ps1 [-NoRun]
#
# -NoRun: build only, skip execution.  Use on machines where the
#         WDAC inbox policy blocks unsigned binaries.

param(
    [switch]$NoRun
)

$ErrorActionPreference = "Stop"
$root   = Split-Path -Parent $PSScriptRoot
$vcb    = Join-Path $root "build\x64-Release\bin\vcb.exe"
$outDir = Join-Path $root "_out\matrix"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

if (-not (Test-Path $vcb)) {
    Write-Host "missing $vcb -- build VCB first" -ForegroundColor Red
    exit 2
}

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
    $exe = Join-Path $outDir "$($t.name).exe"

    if (-not (Test-Path $ir)) {
        Write-Host ("SKIP  {0}  (missing {1})" -f $t.name, $ir) -ForegroundColor Yellow
        $skip++
        continue
    }

    # Build
    & $vcb build $ir -o $exe --target pe *> $null
    if ($LASTEXITCODE -ne 0) {
        Write-Host ("FAIL  {0}  build exit={1}" -f $t.name, $LASTEXITCODE) -ForegroundColor Red
        $fail++
        continue
    }

    # Validate the header
    $hdr = & $vcb headers $exe 2>&1 | Out-String
    if ($hdr -notmatch "0 error\(s\)") {
        Write-Host ("FAIL  {0}  validator" -f $t.name) -ForegroundColor Red
        Write-Host $hdr
        $fail++
        continue
    }

    if ($NoRun) {
        Write-Host ("PASS  {0}  (build+validate only)" -f $t.name) -ForegroundColor Green
        $pass++
        continue
    }

    # Run
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
        Write-Host ("  expected: {0}" -f ($want -replace "`n", "\n"))
        Write-Host ("  actual  : {0}" -f ($actual -replace "`n", "\n"))
        $fail++
    }
}

Write-Host ""
Write-Host ("total: {0} pass, {1} fail, {2} skip" -f $pass, $fail, $skip)
exit ($(if ($fail -gt 0) { 1 } else { 0 }))