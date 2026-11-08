[CmdletBinding()]
param([switch]$SkipDownload, [switch]$CoreOnly)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ Build Tools and the Windows SDK.' }
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'No Visual Studio x64 C++ toolchain found.' }
$devcmd = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
# Import the developer environment; no repository paths are interpolated into cmd.
$environment = & cmd.exe /d /c "call `"$devcmd`" -no_logo -arch=x64 `& set"
if ($LASTEXITCODE) { throw 'Cannot initialize Visual Studio environment.' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
foreach ($tool in @('cmake', 'ninja', 'python', 'nmake', 'cl', 'ctest')) {
    if (!(Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing $tool. See BUILD-KARIZMI.md." }
}
& python -c 'import sys; sys.exit(0 if sys.version_info >= (3, 12) else 1)'
if ($LASTEXITCODE) { throw 'Python 3.12 or newer is required.' }
Push-Location $root
try {
    $lock = Get-Content karizmi/dependencies.json -Raw | ConvertFrom-Json
    $vendor = Join-Path $root 'karizmi/vendor'
    New-Item -ItemType Directory -Force -Path $vendor | Out-Null
    if (!$CoreOnly) {
        $archive = Join-Path $vendor "Botan-$($lock.botan.version).tar.xz"
        if (!(Test-Path -LiteralPath $archive)) {
            if ($SkipDownload) { throw "Missing dependency archive: $archive" }
            Invoke-WebRequest -Uri $lock.botan.url -OutFile $archive
        }
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $lock.botan.sha256) { throw 'Botan source checksum mismatch.' }
        $source = Join-Path $vendor "Botan-$($lock.botan.version)"
        if (!(Test-Path -LiteralPath $source)) {
            & python -c 'import sys,tarfile; tarfile.open(sys.argv[1]).extractall(sys.argv[2],filter="data")' $archive $vendor
            if ($LASTEXITCODE) { throw 'Botan extraction failed. Python 3.12+ is required.' }
        }
        & python (Join-Path $source 'configure.py') --cc=msvc --os=windows --cpu=x86_64 --build-targets=static --with-build-dir=karizmi/vendor/botan-build --minimized-build --enable-modules=srp6,tls13,auto_rng,system_rng,hkdf,sha2_32,aes,gcm,x25519,hex --without-documentation --msvc-runtime=MT --extra-cxxflags=/FIalgorithm
        if ($LASTEXITCODE) { throw 'Botan configuration failed.' }
        & nmake /nologo /f karizmi/vendor/botan-build/Makefile
        if ($LASTEXITCODE) { throw 'Botan compilation failed.' }
    }
    $security = if ($CoreOnly) { 'OFF' } else { 'ON' }
    & cmake -S karizmi -B karizmi/build/release -G Ninja -DCMAKE_BUILD_TYPE=Release "-DKARIZMI_SECURITY=$security"
    if ($LASTEXITCODE) { throw 'Karizmi configuration failed.' }
    & cmake --build karizmi/build/release
    if ($LASTEXITCODE) { throw 'Karizmi compilation failed.' }
    & ctest --test-dir karizmi/build/release --output-on-failure
    if ($LASTEXITCODE) { throw 'Karizmi tests failed.' }
} finally { Pop-Location }
