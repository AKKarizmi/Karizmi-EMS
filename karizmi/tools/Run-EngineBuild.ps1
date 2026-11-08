[CmdletBinding()]
param([Parameter(Mandatory)][string]$Project, [string]$PlatformToolset = 'v145')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'No Visual Studio C++ toolchain found.' }
$devcmd = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
$environment = & cmd.exe /d /c "call `"$devcmd`" -no_logo -arch=x64 `& set"
if ($LASTEXITCODE) { throw 'Cannot initialize Visual Studio environment.' }
foreach ($line in $environment) { if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') } }
Push-Location $root
try {
    $props = Join-Path $root 'karizmi/Karizmi.Engine.props'
    & msbuild $Project /m /t:Build /p:Configuration=Release /p:Platform=x64 "/p:PlatformToolset=$PlatformToolset" "/p:ForceImportBeforeCppTargets=$props" /p:PostBuildEventUseInBuild=false /p:PreLinkEventUseInBuild=false /v:minimal /nologo
    if ($LASTEXITCODE) { throw "Build failed: $Project" }
} finally { Pop-Location }
