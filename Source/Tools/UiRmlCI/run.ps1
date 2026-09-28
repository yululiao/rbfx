#
# Round-trip CI runner for the RmlUi layout editor's model layer.
#
# Compiles the DOM-free units (RmlTextModel + UIViewParagraphText +
# UIViewDocumentModelText) + main.cpp with cl.exe. The model's UiNode derives
# from the engine's RefCounted, so the checker links the prebuilt EASTL.lib
# and the Urho3D.dll import library and finds Urho3D.dll at runtime through
# PATH - no engine build, no CMake target. Runs the resulting checker over
# every sample .rml/.rcss it can find and exits non-zero on any failure,
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

# Prebuilt engine artifacts (Release): the EASTL static lib and the Urho3D.dll
# import lib to link against, plus the runtime directory holding Urho3D.dll and
# the DLLs it depends on (found via PATH below).
$engineLibDir = Join-Path $repo "msvc\lib\Release"
$engineBinDir = Join-Path $repo "msvc\bin\Release"

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
    # The macro set mirrors the Release consumers of the prebuilt libs
    # (EditorLibrary/EASTL.vcxproj). Mismatched EASTL_DEBUG or size_t flags
    # would compile different inline container code against the same lib.
    $defs = "/DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS " +
            "/DURHO3D_DEBUG=0 /DURHO3D_64BIT=1 " +
            "/DEASTL_OPENSOURCE=1 /DEASTL_RTTI_ENABLED=0 /DEASTL_SIZE_T_32BIT=1 " +
            "/DEASTDC_GLOBALPTR_SUPPORT_ENABLED=0 /DEASTDC_THREADING_SUPPORTED=0 " +
            "/DEASTL_STD_ITERATOR_CATEGORY_ENABLED=1 /DEASTL_DEBUG=0 " +
            "/D_CHAR16T=1 /DEA_DLL=1"
    $cl = "call `"$vcvars`" && cl /nologo /EHsc /std:c++17 /MD $defs " +
          "/I `"$modelDir`" /I `"$repo\Source`" " +
          "/I `"$repo\Source\ThirdParty\EASTL\include`" " +
          "/I `"$repo\Source\ThirdParty\EASTL\test\packages\EABase\include\Common`" " +
          "`"$modelDir\RmlTextModel.cpp`" " +
          "`"$modelDir\UIViewParagraphText.cpp`" " +
          "`"$modelDir\UIViewDocumentModelText.cpp`" `"$ciDir\main.cpp`" " +
          "/Fe:`"$exe`" /Fo:`"$buildDir\\`" " +
          "/link /LIBPATH:`"$engineLibDir`" EASTL.lib Urho3D.lib"
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

# The model's UiNode derives from RefCounted, which lives inside Urho3D.dll:
# make the engine's runtime directory visible to the loader (it also carries
# the DLLs Urho3D.dll itself depends on).
$env:PATH = "$engineBinDir;$env:PATH"

& $exe @roots
exit $LASTEXITCODE
