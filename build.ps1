# Builds SRB2 Persona (SDL/OpenGL target) with MSVC and stages a runnable
# copy of the game into a deploy subfolder.
#
# Build path notes:
#   * We build src\sdl\Srb2SDL-vc10.vcxproj directly rather than srb2-vc10.sln.
#     The solution also pulls in the standalone r_opengl / s_openal DLL projects
#     and Srb2win (the non-SDL Win32 target), none of which are needed: the SDL
#     project compiles r_opengl.c itself.
#   * $(SolutionDir) must point at the repo root (trailing backslash). The
#     comptime.bat prebuild event is invoked as "$(SolutionDir)comptime.bat" and
#     comptime.bat lives at the repo root -- when MSBuild is handed a .vcxproj
#     instead of a .sln it defaults SolutionDir to the *project* directory, so
#     without this the prebuild event fails with exit code 9009.
#   * The vcxproj pins PlatformToolset v140/v141 and Windows SDK 10.0.16299.0,
#     neither of which ships with modern VS. We retarget on the command line so
#     the checked-in project file stays untouched.
#   * Win32 only. The x64 configuration in the vcxproj has never been maintained
#     upstream and does not compile (src\sdl\i_system.c guards mouse2filehandle
#     behind 32-bit-only ifdefs).
#   * The x86 build assembles tmap*.nas with NASM (SRB2_common.props wires up a
#     CustomBuild step that shells out to `nasm`). If NASM isn't on PATH we
#     fetch a portable copy into tools\nasm, mirroring what setup-nasm does in
#     CI.
#
# Usage:
#   .\build.ps1                        # Release, build + deploy
#   .\build.ps1 -Configuration Debug   # Debug
#   .\build.ps1 -Rebuild               # clean + build
#   .\build.ps1 -NoDeploy              # skip staging the deploy folder
#
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [switch]$Rebuild,
    [switch]$NoDeploy,
    [string]$DeployDir
)

$ErrorActionPreference = 'Stop'

$repoRoot = $PSScriptRoot
$platform = 'Win32'
$project  = Join-Path $repoRoot 'src\sdl\Srb2SDL-vc10.vcxproj'
$buildOut = Join-Path $repoRoot "bin\VC10\$platform\$Configuration"
if (-not $DeployDir) { $DeployDir = Join-Path $repoRoot "deploy\$Configuration" }

$nasmVersion = '2.16.03'

function Resolve-MSBuild {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found at $vswhere - is Visual Studio installed?"
    }
    $found = & $vswhere -latest -requires Microsoft.Component.MSBuild `
        -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    if (-not $found) { throw "MSBuild.exe not found via vswhere." }
    return $found
}

function Resolve-Nasm {
    # Already on PATH (CI installs it via ilammy/setup-nasm)? Use that.
    $onPath = Get-Command nasm -ErrorAction SilentlyContinue
    if ($onPath) { return Split-Path $onPath.Source -Parent }

    $localDir = Join-Path $repoRoot 'tools\nasm'
    $localExe = Join-Path $localDir 'nasm.exe'
    if (Test-Path $localExe) { return $localDir }

    Write-Host "NASM not found - fetching $nasmVersion into tools\nasm ..."
    New-Item -ItemType Directory -Force $localDir | Out-Null
    $url  = "https://www.nasm.us/pub/nasm/releasebuilds/$nasmVersion/win64/nasm-$nasmVersion-win64.zip"
    $zip  = Join-Path $env:TEMP "nasm-$nasmVersion.zip"
    $tmp  = Join-Path $env:TEMP "nasm-$nasmVersion-x"
    Invoke-WebRequest -Uri $url -OutFile $zip
    Expand-Archive $zip -DestinationPath $tmp -Force
    $exe = Get-ChildItem $tmp -Recurse -Filter nasm.exe | Select-Object -First 1
    if (-not $exe) { throw "nasm.exe not found inside $url" }
    Copy-Item $exe.FullName $localExe -Force
    Remove-Item $zip, $tmp -Recurse -Force -ErrorAction SilentlyContinue
    return $localDir
}

$msbuild = Resolve-MSBuild
$nasmDir = Resolve-Nasm
$env:Path = "$nasmDir;$env:Path"

$targets = if ($Rebuild) { 'Rebuild' } else { 'Build' }

Write-Host "MSBuild : $msbuild"
Write-Host "NASM    : $(Join-Path $nasmDir 'nasm.exe')"
Write-Host "Building $Configuration|$platform ($targets)..."

& $msbuild $project `
    /t:$targets `
    /m `
    /nodeReuse:false `
    /nologo `
    /verbosity:minimal `
    /p:Configuration=$Configuration `
    /p:Platform=$platform `
    /p:PlatformToolset=v143 `
    /p:WindowsTargetPlatformVersion=10.0 `
    /p:BrowseInformation=false `
    /p:SolutionDir="$($repoRoot.TrimEnd('\'))\"

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build FAILED (exit $LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}

$exePath = Join-Path $buildOut 'Srb2Win.exe'
if (-not (Test-Path $exePath)) { throw "Build reported success but $exePath is missing." }
Write-Host "Build succeeded -> $exePath" -ForegroundColor Green

if ($NoDeploy) { exit 0 }

# ---------------------------------------------------------------------------
# Deploy: exe + runtime DLLs + game data, i.e. a directly runnable game folder.
# ---------------------------------------------------------------------------
Write-Host "Deploying to $DeployDir ..."
New-Item -ItemType Directory -Force $DeployDir | Out-Null

Copy-Item $exePath (Join-Path $DeployDir 'SRB2Persona.exe') -Force

# Runtime DLLs. zlib and libpng are linked statically (built from libs\ as
# static .lib), so only the audio/SDL stack needs shipping.
$dllSources = @(
    'libs\SDL2\lib\x86\SDL2.dll'
    'libs\SDL2_mixer\lib\x86\*.dll'
    'libs\libopenmpt\bin\x86\libopenmpt.dll'
    'libs\libopenmpt\bin\x86\openmpt-*.dll'
    'libs\dll-binaries\i686\libgme.dll'
)
foreach ($pattern in $dllSources) {
    $full = Join-Path $repoRoot $pattern
    $hits = @(Get-ChildItem $full -ErrorAction SilentlyContinue)
    if ($hits.Count -eq 0) { Write-Warning "No DLL matched: $pattern" }
    foreach ($dll in $hits) { Copy-Item $dll.FullName $DeployDir -Force }
}

# Visual C++ runtime. Needed by our MSVC-built exe *and* by the prebuilt
# libopenmpt.dll (which imports MSVCP140/VCRUNTIME140), so the zip is only
# self-contained if we ship them. The Universal CRT (api-ms-win-crt-*) is part
# of Windows 10+ and doesn't need bundling.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath  = & $vswhere -latest -property installationPath
$crtDir  = Get-ChildItem "$vsPath\VC\Redist\MSVC\*\x86\Microsoft.VC*.CRT" -Directory -ErrorAction SilentlyContinue |
           Sort-Object { [version]($_.FullName -replace '.*\\MSVC\\([0-9.]+)\\.*', '$1') } |
           Select-Object -Last 1
if ($crtDir) {
    foreach ($name in @('vcruntime140.dll', 'msvcp140.dll')) {
        $src = Join-Path $crtDir.FullName $name
        if (Test-Path $src) { Copy-Item $src $DeployDir -Force }
        else { Write-Warning "VC redist missing $name in $($crtDir.FullName)" }
    }
} else {
    Write-Warning "Visual C++ redistributable DLLs not found; the deployed build will require the VC++ 2015-2022 x86 runtime to be installed."
}

# Game data. assets\installer is the CMake SRB2_ASSET_DIRECTORY convention and
# is where the v1.3.6 data files are tracked.
$assetDir = Join-Path $repoRoot 'assets\installer'
$assets = @(Get-ChildItem $assetDir -Include '*.pk3', '*.wad', '*.txt' -File -ErrorAction SilentlyContinue)
if ($assets.Count -eq 0) {
    $assets = @(Get-ChildItem (Join-Path $assetDir '*') -Include '*.pk3', '*.wad', '*.txt' -File -ErrorAction SilentlyContinue)
}
if ($assets.Count -eq 0) {
    Write-Warning "No game data found in $assetDir - the deployed build will not launch."
} else {
    foreach ($a in $assets) { Copy-Item $a.FullName $DeployDir -Force }
}

$count = (Get-ChildItem $DeployDir -File).Count
Write-Host "Deployed $count files -> $DeployDir" -ForegroundColor Green
Write-Host "Run it with: `"$DeployDir\SRB2Persona.exe`""
