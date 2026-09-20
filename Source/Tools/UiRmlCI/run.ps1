#
# Round-trip CI runner for the RmlUi layout editor's text model.
#
# Compiles RmlTextModel.cpp + main.cpp with cl.exe (no engine, no CMake target) and runs the
# resulting checker over every sample .rml/.rcss it can find. Exits non-zero on any failure,
# so it drops straight into a CI job next to the other LuaBindingCI scripts.
#
# Usage:  run.ps1 [extraRoot1 extraRoot2 ...]
# With no args it auto-discovers bin\Data\UI style folders under the repo.
#
$ErrorActionPreference = "Stop"

# This script lives in <repo>\Source\Tools\UiRmlCI; the model lives in <repo>\Source\Editor\Tabs\UIViewTab.
$ciDir   = $PSScriptRoot
$repo    = (Resolve-Path (Join-Path $ciDir "..\..\..")).Path
$modelDir = Join-Path $repo "Source\Editor\Tabs\UIViewTab"
$buildDir = Join-Path $ciDir "build"

$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $install = & $vswhere -latest -property installationPath
        $vcvars = Join-Path $install "VC\Auxiliary\Build\vcvars64.bat"
    }
}
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found; open a Developer Command Prompt or fix `$vcvars." }

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
$exe = Join-Path $buildDir "rml_roundtrip_ci.exe"

Push-Location $buildDir
try {
    $cl = "call `"$vcvars`" && cl /nologo /EHsc /std:c++17 /I `"$modelDir`" " +
          "`"$modelDir\RmlTextModel.cpp`" `"$ciDir\main.cpp`" /Fe:`"$exe`" /Fo:`"$buildDir\\`""
    cmd /c $cl
    if ($LASTEXITCODE -ne 0) { throw "compile failed ($LASTEXITCODE)" }
}
finally { Pop-Location }

# Default sample roots (dedup, only ones that exist).
$roots = $args
if (-not $roots -or $roots.Count -eq 0) {
    $roots = @(
        (Join-Path $repo "bin\Data\UI"),
        (Join-Path $repo "msvc\bin\Data\UI"),
        (Join-Path $repo "web\bin\Data\UI")
    ) | Where-Object { Test-Path $_ }
}

& $exe @roots
exit $LASTEXITCODE
