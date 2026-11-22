# S0.1C real-engine acceptance: one ra2lobbyd, four simultaneous host/guest
# game pairs. Requires Windows PowerShell 7, Go and legally installed RA2 data.
# Runs on loopback only. No public ports, VPS deployment or private data.
#
# pwsh scripts/play/mp-online-multi-loopback.ps1 -Exe build/ra2engine.exe
[CmdletBinding()]
param(
    [string]$Exe = "build/ra2engine.exe",
    [string]$Out = "build/play-out/mp-online-multi",
    [ValidateRange(2, 4)][int]$MatchCount = 4
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Push-Location $repo
$serverProc = $null
$processes = @()
try {
    if (-not (Test-Path $Exe)) { throw "Build the game executable first: $Exe" }
    if (-not $env:RA2_INSTALL_DIR) { throw "RA2_INSTALL_DIR must point to your legally installed game data" }
    New-Item -ItemType Directory -Force $Out | Out-Null
    $outDir = (Resolve-Path $Out).Path
    $serverExe = Join-Path $outDir "ra2lobbyd.exe"
    Push-Location "server"
    try {
        & go build -o $serverExe ./cmd/ra2lobbyd
        if ($LASTEXITCODE -ne 0) { throw "Go server build failed" }
    } finally { Pop-Location }
    $serverProc = Start-Process -FilePath $serverExe -ArgumentList @(
        "--listen", "127.0.0.1:50000", "--v3", "127.0.0.1:50001",
        "--allow-loopback", "--relay-ports", "50100-50149",
        "--max-rooms-per-ip", "$($MatchCount + 1)", "--max-conns-per-ip", "$($MatchCount * 2 + 3)",
        "--admin", "127.0.0.1:18080", "--server-name", '"Loopback Test"'
    ) -WorkingDirectory $repo -RedirectStandardOutput (Join-Path $outDir "server.stdout.txt") -RedirectStandardError (Join-Path $outDir "server.stderr.txt") -PassThru

    Start-Sleep -Seconds 1
    if ($serverProc.HasExited) { throw "Lobby server failed to start; check server.stderr.txt" }
    $hostScript = Join-Path $repo "scripts/play/mp-online-host.play"
    $joinTemplate = Get-Content (Join-Path $repo "scripts/play/mp-online-join.play") -Raw
    if (-not $joinTemplate.Contains("click room:0")) { throw "Join script anchor changed; update this harness" }

    $hosts = @()
    for ($i = 0; $i -lt $MatchCount; $i++) {
        $dir = Join-Path $outDir "match-$i-host"
        New-Item -ItemType Directory -Force $dir | Out-Null
        $p = Start-Process -FilePath (Resolve-Path $Exe).Path -ArgumentList @(
            "--play", ('"' + $hostScript + '"'), "--out", ('"' + $dir + '"')
        ) -WorkingDirectory $repo -RedirectStandardOutput (Join-Path $dir "stdout.txt") -RedirectStandardError (Join-Path $dir "stderr.txt") -PassThru
        $hosts += [pscustomobject]@{ Process = $p; Dir = $dir; Code = "" }
        $processes += $p
    }

    $deadline = (Get-Date).AddMinutes(3)
    while ((Get-Date) -lt $deadline) {
        foreach ($h in $hosts) {
            if ($h.Code) { continue }
            $logFile = Join-Path $h.Dir "stderr.txt"
            if (Test-Path $logFile) {
                $m = Select-String -Path $logFile -Pattern "room created code=([A-Z2-9]{6})" | Select-Object -First 1
                if ($m) { $h.Code = $m.Matches[0].Groups[1].Value }
            }
            if (-not $h.Code -and $h.Process.HasExited) { throw "Host exited before creating a room: $($h.Dir)" }
        }
        if (@($hosts | Where-Object { -not $_.Code }).Count -eq 0) { break }
        Start-Sleep -Milliseconds 400
    }
    if (@($hosts | Where-Object { -not $_.Code }).Count -gt 0) { throw "Timed out waiting for all $MatchCount room codes" }
    $codes = @($hosts | ForEach-Object { $_.Code })
    if (@($codes | Select-Object -Unique).Count -ne $MatchCount) { throw "Two hosts received the same room code" }

    $before = (Invoke-WebRequest -UseBasicParsing http://127.0.0.1:18080/metrics -TimeoutSec 5).Content
    if ($before -notmatch "(?m)^ra2lobbyd_rooms $MatchCount$") { throw "Server does not report $MatchCount concurrent rooms" }
    Write-Host "Simultaneous waiting rooms: $MatchCount ($($codes -join ', '))"

    $guests = @()
    for ($i = 0; $i -lt $MatchCount; $i++) {
        $dir = Join-Path $outDir "match-$i-guest"
        New-Item -ItemType Directory -Force $dir | Out-Null
        # Query the exact room code before clicking row zero: unaffected by
        # the presence of other rooms or browser sorting.
        $code = $codes[$i]
        $searchActions = "click edit:search`n" + "type `"$code`"`n" +
                         "until 2000000 visible_rooms >= 1`nclick room:0"
        $script = $joinTemplate.Replace("click room:0", $searchActions)
        $scriptFile = Join-Path $dir "join.play"
        Set-Content -Path $scriptFile -Value $script -Encoding UTF8
        $p = Start-Process -FilePath (Resolve-Path $Exe).Path -ArgumentList @(
            "--play", ('"' + $scriptFile + '"'), "--out", ('"' + $dir + '"')
        ) -WorkingDirectory $repo -RedirectStandardOutput (Join-Path $dir "stdout.txt") -RedirectStandardError (Join-Path $dir "stderr.txt") -PassThru
        $guests += [pscustomobject]@{ Process = $p; Dir = $dir }
        $processes += $p
    }

    $deadline = (Get-Date).AddMinutes(5)
    while ((Get-Date) -lt $deadline) {
        if (@($processes | Where-Object { -not $_.HasExited }).Count -eq 0) { break }
        Start-Sleep -Milliseconds 500
    }
    if (@($processes | Where-Object { -not $_.HasExited }).Count -gt 0) {
        throw "Timed out: not all game clients finished"
    }

    for ($i = 0; $i -lt $MatchCount; $i++) {
        $h = $hosts[$i]; $g = $guests[$i]
        if ($h.Process.ExitCode -ne 0 -or $g.Process.ExitCode -ne 0) {
            throw "Match $i client exited with failure: host=$($h.Process.ExitCode) guest=$($g.Process.ExitCode)"
        }
        $hr = Get-Content (Join-Path $h.Dir "result.txt") -ErrorAction Stop -TotalCount 1
        $gr = Get-Content (Join-Path $g.Dir "result.txt") -ErrorAction Stop -TotalCount 1
        if (-not $hr -or -not $gr) {
            throw "Match $i missing client result evidence: host=$hr guest=$gr"
        }
        $hs = Select-String -Path (Join-Path $h.Dir "stderr.txt") -Pattern "\[mp\] session seed=(\d+)" | Select-Object -First 1
        $gs = Select-String -Path (Join-Path $g.Dir "stderr.txt") -Pattern "\[mp\] session seed=(\d+)" | Select-Object -First 1
        $ho = Select-String -Path (Join-Path $h.Dir "stderr.txt") -Pattern "\[mp\]   options: (.*)" | Select-Object -First 1
        $go = Select-String -Path (Join-Path $g.Dir "stderr.txt") -Pattern "\[mp\]   options: (.*)" | Select-Object -First 1
        if (-not $hs -or -not $gs -or -not $ho -or -not $go) { throw "Match $i missing session evidence" }
        if ($hs.Matches[0].Groups[1].Value -ne $gs.Matches[0].Groups[1].Value -or
            $ho.Matches[0].Groups[1].Value -ne $go.Matches[0].Groups[1].Value) {
            throw "Match $i host and guest disagree on seed/options"
        }
        Write-Host "Match $i PASS: $($codes[$i]) seed/options agree, host+guest exited successfully"
    }
    $after = (Invoke-WebRequest -UseBasicParsing http://127.0.0.1:18080/metrics -TimeoutSec 5).Content
    if ($after -notmatch "ra2lobbyd_v3_forwarded_total (\d+)" -or [int]$Matches[1] -lt $MatchCount) {
        throw "Insufficient relay traffic reported"
    }
    Write-Host "All $MatchCount real engine pairs passed on one server."
} finally {
    foreach ($p in $processes) {
        if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    }
    if ($serverProc -and -not $serverProc.HasExited) {
        Stop-Process -Id $serverProc.Id -Force -ErrorAction SilentlyContinue
    }
    Pop-Location
}
