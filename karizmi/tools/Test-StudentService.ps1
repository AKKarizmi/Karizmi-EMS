#Requires -RunAsAdministrator
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$package = Join-Path $root 'karizmi/out/Student'
$log = Join-Path $root 'karizmi/build/service-test.log'
Start-Transcript -LiteralPath $log -Force
$installed = $false
try {
    if (Get-Service -Name KarizmiStudent -ErrorAction SilentlyContinue) { throw 'A service already exists; test will not modify it.' }
    if (Get-Process -Name KarizmiStudent -ErrorAction SilentlyContinue) { throw 'Close existing Student processes before this test.' }
    & (Join-Path $package 'Install-StudentService.ps1')
    $installed = $true
    $configuration = Get-CimInstance Win32_Service -Filter "Name='KarizmiStudent'"
    if ($configuration.StartMode -ne 'Auto' -or $configuration.StartName -ne 'LocalSystem' -or $configuration.DisplayName -ne 'Karizmi Student') { throw 'Incorrect service configuration.' }
    $deadline = [DateTime]::UtcNow.AddSeconds(40)
    $child = $null
    do {
        $child = Get-CimInstance Win32_Process -Filter "Name='KarizmiStudent.exe'" | Where-Object ParentProcessId -eq $configuration.ProcessId | Select-Object -First 1
        if (!$child) { Start-Sleep -Milliseconds 500 }
    } until ($child -or [DateTime]::UtcNow -gt $deadline)
    if (!$child -or $child.SessionId -eq 0) { throw 'Service did not start Student in the interactive session.' }
    $owner = Invoke-CimMethod -InputObject $child -MethodName GetOwner
    if ($owner.User -eq 'SYSTEM') { throw 'Interactive Student must run as the signed-in user.' }
    Write-Host "PASS: automatic LocalSystem service started Student in user session $($child.SessionId)."
    Stop-Service -Name KarizmiStudent
    (Get-Service KarizmiStudent).WaitForStatus('Stopped',[TimeSpan]::FromSeconds(15))
    if (Get-Process -Id $child.ProcessId -ErrorAction SilentlyContinue) { throw 'Service stop left its Student process running.' }
    Write-Host 'PASS: stopping service closes its child, including a pending monitoring notice.'
    Start-Service -Name KarizmiStudent
    (Get-Service KarizmiStudent).WaitForStatus('Running',[TimeSpan]::FromSeconds(15))
    Write-Host 'PASS: service restart.'
} catch {
    Write-Host "FAIL: $_"
    throw
} finally {
    if ($installed) { & (Join-Path $package 'Uninstall-StudentService.ps1') }
    Stop-Transcript
}
