#Requires -RunAsAdministrator
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$serviceName = 'KarizmiStudent'
$destination = Join-Path $env:ProgramFiles 'Karizmi Student'
$files = @('KarizmiStudent.exe','KarizmiStudentService.exe','vnchooks.dll','LICENSE','Botan-LICENSE.txt','README.md','SERVICE.md','CLASSROOM.md','RECOVERY-VALIDATION.md','UI-VALIDATION.md','Install-StudentService.ps1','Uninstall-StudentService.ps1')
if (Get-Service -Name $serviceName -ErrorAction SilentlyContinue) { throw 'Karizmi Student service already exists. Uninstall it before installing a replacement.' }
if (Test-Path -LiteralPath $destination) { throw "Installation directory already exists: $destination. Remove the old installation first; existing directories are never trusted for a SYSTEM service." }
foreach ($file in $files) {
    if (!(Test-Path -LiteralPath (Join-Path $PSScriptRoot $file) -PathType Leaf)) { throw "Missing package file: $file. Run this script from the built Student package." }
}
$created = $false
try {
    New-Item -ItemType Directory -Path $destination | Out-Null
    # Protect the executable and dependency directory before copying executable code.
    $acl = New-Object System.Security.AccessControl.DirectorySecurity
    $acl.SetAccessRuleProtection($true,$false)
    $admin = New-Object System.Security.Principal.SecurityIdentifier('S-1-5-32-544')
    $acl.SetOwner($admin)
    foreach ($entry in @(@('S-1-5-18','FullControl'),@('S-1-5-32-544','FullControl'),@('S-1-5-32-545','ReadAndExecute'))) {
        $sid = New-Object System.Security.Principal.SecurityIdentifier($entry[0])
        $rule = New-Object System.Security.AccessControl.FileSystemAccessRule($sid,$entry[1],'ContainerInherit,ObjectInherit','None','Allow')
        $acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $destination -AclObject $acl
    foreach ($file in $files) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination (Join-Path $destination $file) }
    $binary = '"' + (Join-Path $destination 'KarizmiStudentService.exe') + '"'
    New-Service -Name $serviceName -DisplayName 'Karizmi Student' -BinaryPathName $binary -StartupType Automatic -Description 'Starts the Karizmi Student tray application for the signed-in console user. Monitoring requires disclosure and secure pairing.' | Out-Null
    $created = $true
    Start-Service -Name $serviceName
    (Get-Service -Name $serviceName).WaitForStatus('Running',[TimeSpan]::FromSeconds(15))
    Write-Host "Karizmi Student service installed and running. Files: $destination"
} catch {
    if ($created) {
        Stop-Service -Name $serviceName -ErrorAction SilentlyContinue
        & sc.exe delete $serviceName | Out-Null
    }
    # Retain any copied files for diagnosis; never recursively delete a computed path.
    throw
}
