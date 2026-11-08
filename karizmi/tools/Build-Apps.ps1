[CmdletBinding()]
param([string]$PlatformToolset = 'v145', [switch]$SkipCore, [switch]$SkipDependencies,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$PackageFolder = 'out')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ Build Tools and the Windows SDK. See BUILD-KARIZMI.md.' }
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'Install Visual Studio C++ build tools.' }
$devcmd = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
$environment = & cmd.exe /d /c "call `"$devcmd`" -no_logo -arch=x64 `& set"
if ($LASTEXITCODE) { throw 'Cannot initialize Visual Studio x64 environment.' }
foreach ($line in $environment) { if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') } }
foreach ($tool in @('cmake', 'ninja', 'msbuild', 'cl', 'python', 'nmake')) {
    if (!(Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing $tool. See BUILD-KARIZMI.md." }
}
if (!$SkipCore) { & (Join-Path $PSScriptRoot 'Build-Core.ps1'); if (!$?) { throw 'Shared component build failed.' } }
Push-Location $root
try {
    if ($SkipCore) {
        & cmake -S karizmi -B karizmi/build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DKARIZMI_SECURITY=ON
        if ($LASTEXITCODE) { throw 'Shared component configuration failed.' }
        & cmake --build karizmi/build/release
        if ($LASTEXITCODE) { throw 'Shared component compilation failed.' }
        & ctest --test-dir karizmi/build/release --output-on-failure
        if ($LASTEXITCODE) { throw 'Shared component tests failed.' }
    }
    $dependencyRoot = Join-Path $root 'karizmi/vendor/vcpkg_installed/x64-windows-static'
    foreach ($required in @('include/zlib.h', 'lib/zlib.lib', 'lib/zstd.lib', 'lib/jpeg.lib', 'lib/turbojpeg.lib', 'lib/lzma.lib', 'lib/libsodium.lib', 'lib/minizip-ng.lib', 'lib/bz2.lib')) {
        if (!(Test-Path -LiteralPath (Join-Path $dependencyRoot $required))) { throw "Missing engine dependency $required. Run without -SkipDependencies." }
    }
    $notices = @(Get-ChildItem -LiteralPath (Join-Path $dependencyRoot 'share') -Filter copyright -Recurse -File)
    if (!$notices.Count) { throw 'Missing vcpkg copyright notices. Restore dependencies before packaging.' }
    if (!$SkipDependencies) {
        $vcpkg = Join-Path $installation 'VC/vcpkg/vcpkg.exe'
        if (!(Test-Path -LiteralPath $vcpkg)) { $vcpkg = (Get-Command vcpkg).Source }
        & $vcpkg install --triplet x64-windows-static --x-install-root=karizmi/vendor/vcpkg_installed --x-buildtrees-root=karizmi/vendor/vcpkg-buildtrees --downloads-root=karizmi/vendor/downloads --x-packages-root=karizmi/vendor/vcpkg-packages
        if ($LASTEXITCODE) { throw 'UltraVNC dependency installation failed.' }
    }
    $props = Join-Path $root 'karizmi/Karizmi.Engine.props'
    foreach ($project in @('vncviewer/vncviewer.vcxproj','winvnc/vnchooks/vnchooks.vcxproj','winvnc/winvnc/winvnc.vcxproj')) {
        & msbuild $project /m /t:Build /p:Configuration=Release /p:Platform=x64 "/p:PlatformToolset=$PlatformToolset" "/p:ForceImportBeforeCppTargets=$props" /p:PostBuildEventUseInBuild=false /p:PreLinkEventUseInBuild=false /p:CustomBuildAfterTargets= /p:CustomBuildBeforeTargets= /p:BuildProjectReferences=true /v:minimal /nologo
        if ($LASTEXITCODE) { throw "Build failed: $project" }
    }
    $teacher = Join-Path $root "karizmi/$PackageFolder/Teacher"
    $student = Join-Path $root "karizmi/$PackageFolder/Student"
    New-Item -ItemType Directory -Force -Path $teacher,$student | Out-Null
    Copy-Item -LiteralPath karizmi/build/engine/vncviewer/KarizmiTeacher.exe -Destination $teacher
    Copy-Item -LiteralPath karizmi/build/engine/winvnc/KarizmiStudent.exe -Destination $student
    Copy-Item -LiteralPath karizmi/build/engine/vnchooks/vnchooks.dll -Destination $student
    Copy-Item -LiteralPath karizmi/build/release/KarizmiStudentService.exe -Destination $student
    Copy-Item -LiteralPath karizmi/tools/Install-StudentService.ps1,karizmi/tools/Uninstall-StudentService.ps1,karizmi/SERVICE.md -Destination $student
    foreach ($destination in @($teacher,$student)) {
        Copy-Item -LiteralPath LICENSE -Destination $destination
        Copy-Item -LiteralPath karizmi/vendor/Botan-3.13.0/license.txt -Destination (Join-Path $destination 'Botan-LICENSE.txt')
        Copy-Item -LiteralPath karizmi/PACKAGE-README.md -Destination (Join-Path $destination 'README.md')
        Copy-Item -LiteralPath karizmi/TECHNICAL-SPECIFICATION.md,karizmi/THIRD-PARTY-NOTICES.md -Destination $destination
        $noticeDir = Join-Path $destination 'third-party-licenses'
        New-Item -ItemType Directory -Force -Path $noticeDir | Out-Null
        foreach ($notice in $notices) {
            $relative = $notice.FullName.Substring((Join-Path $dependencyRoot 'share').Length + 1)
            $target = Join-Path $noticeDir $relative
            New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
            Copy-Item -LiteralPath $notice.FullName -Destination $target
        }
        Copy-Item -LiteralPath karizmi/SERVICE.md,karizmi/UI-VALIDATION.md,karizmi/CLASSROOM.md,karizmi/RECOVERY-VALIDATION.md -Destination $destination
    }
    Write-Host "Built packages: $teacher and $student"
} finally { Pop-Location }
