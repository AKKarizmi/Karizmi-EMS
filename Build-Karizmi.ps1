# Run in x64 Developer PowerShell for Visual Studio.
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    foreach ($tool in @('msbuild', 'cl', 'git', 'nasm', 'vcpkg')) {
        if (!(Get-Command $tool -ErrorAction SilentlyContinue)) {
            throw "Missing $tool. Follow BUILD-KARIZMI.md before building."
        }
    }

    $serverRc = Get-Content winvnc/winvnc/winvnc.rc -Raw
    $webPaths = [regex]::Matches($serverRc, '"\.\./\.\./(3rdparty/noVNC/[^"]+)"') |
        ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
    if (!$webPaths) { throw 'No browser resource references found in the server project.' }
    if ($webPaths | Where-Object { !(Test-Path -LiteralPath $_ -PathType Leaf) }) {
        $dest = Join-Path $PSScriptRoot '3rdparty/noVNC'
        if (Get-ChildItem -LiteralPath $dest -Force -ErrorAction SilentlyContinue) {
            throw "Existing noVNC folder is incomplete. Move it aside and run again."
        }
        $rev = '7bd294a81f7bd64215629ef74c2dd1c74b2be10a'
        $url = "https://api.github.com/repos/ultravnc/UltraVNC/contents/3rdparty/noVNC?ref=$rev"
        $entry = Invoke-RestMethod $url -Headers @{'User-Agent' = 'Karizmi-Windows-Build'}
        if ($entry.sha -notmatch '^[0-9a-f]{40}$') { throw 'Could not resolve the noVNC revision.' }
        $tmp = Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString())
        try {
            git init --quiet $tmp
            if ($LASTEXITCODE) { throw 'Cannot create temporary noVNC checkout.' }
            git -C $tmp fetch --quiet --depth 1 https://github.com/novnc/noVNC.git $entry.sha
            if ($LASTEXITCODE) { throw 'Could not download the pinned noVNC revision.' }
            git -C $tmp checkout --quiet --detach FETCH_HEAD
            if ($LASTEXITCODE) { throw 'Could not check out noVNC.' }
            New-Item -ItemType Directory -Path $dest -Force | Out-Null
            Get-ChildItem -Force $tmp | Where-Object Name -ne '.git' | Copy-Item -Destination $dest -Recurse
        } finally {
            if (Test-Path $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }
        }
    }
    foreach ($path in $webPaths) {
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing resource: $path" }
    }
    # The server's legacy pre-link command writes the timestamp object here.
    New-Item -ItemType Directory -Path 'winvnc/x64/Release' -Force | Out-Null
    foreach ($project in @('vncviewer/vncviewer.vcxproj', 'winvnc/vnchooks/vnchooks.vcxproj', 'winvnc/winvnc/winvnc.vcxproj')) {
        msbuild $project /m /t:Build /p:Configuration=Release /p:Platform=x64 /p:VcpkgTriplet=x64-windows-static /p:PostBuildEventUseInBuild=false
        if ($LASTEXITCODE) { throw "Build failed for $project." }
    }
    foreach ($binary in @('vncviewer.exe','winvnc.exe','vnchooks.dll')) {
        $product = Get-ChildItem -Path vncviewer,winvnc -Filter $binary -File -Recurse |
            Where-Object FullName -match '[\\/]x64[\\/]Release[\\/]' | Select-Object -First 1
        if (!$product) { throw "Cannot locate x64 release output: $binary" }
        Write-Host $product.FullName
    }
} finally {
    Pop-Location
}
