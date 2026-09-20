<#
.SYNOPSIS
  Headless smoke-run of every Lua sample and pass/fail summary.

.DESCRIPTION
  Iterates each sample directory under Source/LuaSamples, launches
  LuaSamplesRunner.exe detached for a bounded time (the runner is a GUI
  subsystem app, so it is started with its own log file and killed after the
  window rather than waiting on a stdout pipe), then inspects that log.

  A sample PASSES when its log contains:
      Lua sample '<name>' started
  which is emitted only after Framework.lua and <name>/main.lua both execute
  without a Lua error, AND the log shows NO [error]-level line at all (that is
  the whole point of the gate: a post-start Lua callback failure shows up as
  "event handler error", a bad SetVariant/SetVar payload as "LuaToVariant:
  cannot convert", dead-object access as "Lua ObjectRef: ... destroyed" -- none
  of which used to count as FAIL), AND the Lua-heap probe that Framework.lua
  runs once per second (forced full GC, then a resident-KB reading) never
  exceeds its baseline by more than -MemBudgetKB.
  The [error] criterion is not whitelisted: healthy samples log ZERO [error]
  lines (verified across audio/network/RmlUI/console samples), so any single
  [error] line is a real defect by definition.

  Exit code 0 = all selected samples passed; 1 = at least one failed.

.PARAMETER Config
  Debug or Release build configuration (default Release).

.PARAMETER Only
  Substring filter to run a subset of samples (e.g. -Only Physics).

.PARAMETER Headless
  Run with --headless. Off by default: the samples are graphics/UI/console apps
  and their Lua indexes subsystems (graphics, console) that headless mode leaves
  nil, so a headless run reports false failures. Keep windowed (the default) for
  a faithful pass/fail; --headless is only for a fast non-GPU subset.

.PARAMETER Seconds
  Seconds the sample runs before the ENGINE exits itself via --timeout (default 8;
  the probe's leak detection needs ~5+ consecutive readings, so raise this for a
  more sensitive memory gate). A forced kill only happens as a safety net.

.PARAMETER MemBudgetKB
  Allowed Lua-heap growth over the probe's first reading (default 256). Passed
  to the runner through URHO3D_LUA_MEM_BUDGET_KB; exceeding it fails the sample.

.EXAMPLE
  powershell -File Source/Tools/LuaBindingCI/run_samples.ps1 -Only 01_
#>
[CmdletBinding()]
param(
    [string]$Config = 'Release',
    [string]$Only,
    [int]$Seconds = 8,
    [int]$MemBudgetKB = 256,
    [switch]$Headless,
    [string]$RepoRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not $RepoRoot) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
}

$binDir = Join-Path $RepoRoot "msvc/bin/$Config"
$runner = Join-Path $binDir 'LuaSamplesRunner.exe'
if (-not (Test-Path $runner)) { throw "runner not found: $runner (build the solution first)" }

$samplesRoot = Join-Path $RepoRoot 'Source/LuaSamples'
$workLogDir = Join-Path $env:TEMP ("rbfx_samples_" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $workLogDir | Out-Null

$samples = Get-ChildItem -Path $samplesRoot -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName 'main.lua') } |
    Select-Object -ExpandProperty Name

if ($Only) { $samples = $samples | Where-Object { $_ -like "*$Only*" } }

# Read by the Framework.lua memory probe; inherited by the runner process.
$env:URHO3D_LUA_MEM_BUDGET_KB = "$MemBudgetKB"

# Failed samples leave their log behind for post-mortem (the temp work dir is
# wiped on exit); re-running the suite overwrites stale entries.
$failLogDir = Join-Path $RepoRoot 'msvc/sample_fail_logs'

$results = @()
foreach ($s in $samples) {
    $log = Join-Path $workLogDir "$s.log"
    # --timeout makes the ENGINE exit by itself at $Seconds, which flushes the
    # whole log; a forced kill loses the buffered tail (and with it the probe's
    # budget-exceeded lines). The kill below is only the safety net.
    $argList = @($s, '--nosound', '--log-file', $log, '--timeout', "$Seconds")
    if ($Headless) { $argList += '--headless' } else { $argList += '--windowed' }
    $proc = Start-Process -FilePath $runner -ArgumentList $argList -PassThru
    $waited = 0
    $limit = $Seconds + 6
    while (-not $proc.HasExited -and $waited -lt $limit) {
        Start-Sleep -Milliseconds 500; $waited += 0.5
    }
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }

    $text = if (Test-Path $log) { Get-Content -Raw -LiteralPath $log } else { '' }
    $started = $text -match [regex]::Escape("Lua sample '$s' started")
    $loadErr = $text -match 'Failed to (load|execute)'
    $tb = ([regex]::Matches($text, 'stack traceback|LuaException|error:')).Count
    # ANY [error]-level engine log line fails the sample. This is the strict
    # criterion that closes the "started, then errors, still PASS" hole: C++
    # binding-side errors (LuaToVariant, dead ObjectRef, LuaBases audit, the
    # probe's budget lines) carry the [error] tag but not necessarily any of
    # the "stack traceback|error:" text markers above. Healthy samples log
    # zero [error] lines, so there is no whitelist to maintain.
    $errLines = ([regex]::Matches($text, '\[error\]')).Count
    $memPeakKB = 0
    foreach ($m in [regex]::Matches($text, 'Lua mem probe: (\d+) KB')) {
        $kb = [int]$m.Groups[1].Value
        if ($kb -gt $memPeakKB) { $memPeakKB = $kb }
    }
    $memOver = $text -match 'Lua mem budget exceeded'
    $status = if ($started -and -not $loadErr -and $errLines -eq 0 -and $tb -eq 0 -and -not $memOver) { 'PASS' } else { 'FAIL' }
    # Hashtable (not [pscustomobject]): ConstrainedLanguage PowerShell rejects
    # the accelerator cast, and this gate must run in any language mode.
    $results += @{ Sample = $s; Status = $status; Errors = $errLines; Tracebacks = $tb; MemPeakKB = $memPeakKB }
    if ($status -eq 'FAIL' -and (Test-Path $log)) {
        New-Item -ItemType Directory -Force -Path $failLogDir | Out-Null
        Copy-Item -Force $log (Join-Path $failLogDir "$s.log")
    }
    $color = if ($status -eq 'PASS') { 'Green' } else { 'Red' }
    Write-Host ("{0,-32} {1}  errors={2} memPeak={3}KB" -f $s, $status, $errLines, $memPeakKB) -ForegroundColor $color
}

Write-Host ''
$pass = @($results | Where-Object { $_['Status'] -eq 'PASS' }).Count
$fail = @($results | Where-Object { $_['Status'] -eq 'FAIL' }).Count
Write-Host "TOTAL=$($results.Count) PASS=$pass FAIL=$fail (mem budget ${MemBudgetKB}KB)" -ForegroundColor Cyan
if ($fail -gt 0) {
    Write-Host 'Failed samples:' -ForegroundColor Red
    $results | Where-Object { $_['Status'] -eq 'FAIL' } | ForEach-Object { Write-Host "  $($_['Sample'])" -ForegroundColor Red }
}

Remove-Item -Recurse -Force $workLogDir -ErrorAction SilentlyContinue
exit $(if ($fail -gt 0) { 1 } else { 0 })
