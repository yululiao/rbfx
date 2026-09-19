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
  without a Lua error. Any load error or runtime traceback is reported.

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
  Max wall time to let a sample run before killing it (default 8).

.EXAMPLE
  powershell -File Source/Tools/LuaBindingCI/run_samples.ps1 -Only 01_
#>
[CmdletBinding()]
param(
    [string]$Config = 'Release',
    [string]$Only,
    [int]$Seconds = 8,
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

$results = @()
foreach ($s in $samples) {
    $log = Join-Path $workLogDir "$s.log"
    $argList = @($s, '--nosound', '--log-file', $log)
    if ($Headless) { $argList += '--headless' } else { $argList += '--windowed' }
    $proc = Start-Process -FilePath $runner -ArgumentList $argList -PassThru
    $waited = 0
    while (-not $proc.HasExited -and $waited -lt $Seconds) {
        Start-Sleep -Milliseconds 500; $waited += 0.5
    }
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }

    $text = if (Test-Path $log) { Get-Content -Raw -LiteralPath $log } else { '' }
    $started = $text -match [regex]::Escape("Lua sample '$s' started")
    $loadErr = $text -match 'Failed to (load|execute)'
    $tb = ([regex]::Matches($text, 'stack traceback|LuaException|error:')).Count
    $status = if ($started -and -not $loadErr) { 'PASS' } else { 'FAIL' }
    $results += [pscustomobject]@{ Sample = $s; Status = $status; Tracebacks = $tb }
    $color = if ($status -eq 'PASS') { 'Green' } else { 'Red' }
    Write-Host ("{0,-32} {1}" -f $s, $status) -ForegroundColor $color
}

Write-Host ''
$pass = @($results | Where-Object Status -eq 'PASS').Count
$fail = @($results | Where-Object Status -eq 'FAIL').Count
Write-Host "TOTAL=$($results.Count) PASS=$pass FAIL=$fail" -ForegroundColor Cyan
if ($fail -gt 0) {
    Write-Host 'Failed samples:' -ForegroundColor Red
    $results | Where-Object Status -eq 'FAIL' | ForEach-Object { Write-Host "  $($_.Sample)" -ForegroundColor Red }
}

Remove-Item -Recurse -Force $workLogDir -ErrorAction SilentlyContinue
exit $(if ($fail -gt 0) { 1 } else { 0 })
