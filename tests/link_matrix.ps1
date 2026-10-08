# tests\link_matrix.ps1 -- Phase 28.3 link matrix for ELF objects.
#
# Builds each .vcbir as an ELF object, links it with ld.lld.exe, and
# verifies the resulting ELF header.  Execution requires a Linux host.
#
# Usage:  powershell -NoProfile -File tests\link_matrix.ps1

$ErrorActionPreference = "Continue"
$root   = Split-Path -Parent $PSScriptRoot
$vcb    = Join-Path $root "build\x64-Release\bin\vcb.exe"
$outDir = Join-Path $root "_out\link_matrix"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

if (-not (Test-Path $vcb)) { Write-Host "missing $vcb -- build VCB first" -ForegroundColor Red; exit 2 }

$tests = @(
    "t01_empty", "t02_arith", "t03_print_int", "t04_print_str",
    "t05_print_bool", "t06_list_create", "t07_list_push",
    "t08_multiple_calls", "t09_str_const", "t10_large_text",
    "t11_multi_import"
)

$pass = 0
$fail = 0
$skip = 0

foreach ($name in $tests) {
    $ir  = Join-Path $root "examples\$name.vcbir"
    $obj = Join-Path $outDir "$name.o"
    $elf = Join-Path $outDir "$name.elf"

    if (-not (Test-Path $ir)) {
        Write-Host ("SKIP  {0}  (missing {1})" -f $name, $ir) -ForegroundColor Yellow
        $skip++
        continue
    }

    & $vcb emit-obj $ir -o $obj --target elf *> $null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $obj)) {
        Write-Host ("FAIL  {0}  emit-obj exit={1}" -f $name, $LASTEXITCODE) -ForegroundColor Red
        $fail++
        continue
    }

    & $vcb link $obj -o $elf --target elf --entry vayu_entry *> $null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $elf)) {
        Write-Host ("FAIL  {0}  link exit={1}" -f $name, $LASTEXITCODE) -ForegroundColor Red
        $fail++
        continue
    }

    $hdr = & $vcb elfheaders $elf 2>&1 | Out-String
    if ($hdr -notmatch "ELF64" -or $hdr -notmatch "x86-64") {
        Write-Host ("FAIL  {0}  elfheaders" -f $name) -ForegroundColor Red
        Write-Host $hdr
        $fail++
        continue
    }

    Write-Host ("PASS  {0}" -f $name) -ForegroundColor Green
    $pass++
}

Write-Host ""
Write-Host ("total: {0} pass, {1} fail, {2} skip" -f $pass, $fail, $skip)
exit ($(if ($fail -gt 0) { 1 } else { 0 }))