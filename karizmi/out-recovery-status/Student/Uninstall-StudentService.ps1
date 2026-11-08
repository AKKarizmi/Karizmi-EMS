#Requires -RunAsAdministrator
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$serviceName = 'KarizmiStudent'
$destination = [IO.Path]::GetFullPath((Join-Path $env:ProgramFiles 'Karizmi Student'))
$service = Get-CimInstance Win32_Service -Filter "Name='KarizmiStudent'"
if ($service) {
    $expected = '"' + (Join-Path $destination 'KarizmiStudentService.exe') + '"'
    if ($service.PathName -ne $expected) { throw 'The existing service does not belong to this package. No changes made.' }
    Stop-Service -Name $serviceName
    (Get-Service -Name $serviceName).WaitForStatus('Stopped',[TimeSpan]::FromSeconds(15))
    & sc.exe delete $serviceName
    if ($LASTEXITCODE) { throw 'Windows could not remove the service.' }
}
if (Test-Path -LiteralPath $destination) {
    $directory = Get-Item -LiteralPath $destination
    if ($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing cleanup through a reparse point.' }
    foreach ($file in @('KarizmiStudent.exe','KarizmiStudentService.exe','vnchooks.dll','LICENSE','Botan-LICENSE.txt','README.md','SERVICE.md','CLASSROOM.md','RECOVERY-VALIDATION.md','UI-VALIDATION.md','Install-StudentService.ps1','Uninstall-StudentService.ps1')) {
        $path = Join-Path $destination $file
        if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path -Force }
    }
    if (!(Get-ChildItem -LiteralPath $destination -Force)) { Remove-Item -LiteralPath $destination }
}
Write-Host 'Karizmi Student service removed. Teacher and other VNC installations were not changed.'
