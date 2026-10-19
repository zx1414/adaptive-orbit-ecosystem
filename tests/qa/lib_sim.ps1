# =============================================================================
# lib_sim.ps1 - shared harness for v2.2.0 red-team acceptance tests.
#
# Only tests/** may be written by red-* members; nothing here touches product code.
# Design notes:
#  * raw TcpClient HTTP (no proxy, no Invoke-WebRequest quirks, binary safe);
#  * every server is started from an isolated stage dir and tracked by PID so we
#    only ever kill simulators that THIS run started (never other agents' procs);
#  * ports are 191xx; if a port is busy we fail fast instead of stealing it.
#
# ASCII-only source on purpose: Windows PowerShell 5.1 parses .ps1 without BOM as
# ANSI, so any non-ASCII literal would get mojibake. Log parsing uses ASCII
# anchors only ([boom], PASS/FAIL, ...).
# =============================================================================

if ($null -eq $global:QaChecks) { $global:QaChecks = New-Object System.Collections.ArrayList }
if ($null -eq $global:QaServers) { $global:QaServers = New-Object System.Collections.ArrayList }
if ($null -eq $global:QaStageRoot) { $global:QaStageRoot = $null }

$ErrorActionPreference = 'Stop'

function Get-RepoRoot {
    return (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent)
}

function Get-QaSimExe {
    return (Join-Path (Get-RepoRoot) '_agent\integration\build\simulator.exe')
}

function Write-QaLog {
    param([string]$Message)
    $line = ('[{0}] {1}' -f (Get-Date -Format 'HH:mm:ss'), $Message)
    Write-Host $line
    if ($global:QaLogFile) { Add-Content -Path $global:QaLogFile -Value $line -Encoding UTF8 }
}

function Add-Check {
    param(
        [string]$Suite,
        [string]$Name,
        [bool]$Pass,
        [string]$Detail = ''
    )
    $tag = if ($Pass) { 'PASS' } else { 'FAIL' }
    [void]$global:QaChecks.Add([pscustomobject]@{
        suite = $Suite; name = $Name; pass = $Pass; detail = $Detail
    })
    $msg = ('[{0}] {1}.{2}{3}' -f $tag, $Suite, $Name, $(if ($Detail) { ' :: ' + $Detail } else { '' }))
    Write-Host $msg
    if ($global:QaLogFile) { Add-Content -Path $global:QaLogFile -Value $msg -Encoding UTF8 }
}

function Get-QaChecksFor {
    param([string]$Suite)
    return @($global:QaChecks | Where-Object { $_.suite -eq $Suite })
}

function Export-QaResults {
    param([string]$Path)
    $dir = Split-Path $Path -Parent
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $json = $global:QaChecks | ConvertTo-Json -Depth 6
    if ($null -eq $json) { $json = '[]' }
    Set-Content -Path $Path -Value $json -Encoding UTF8
}

function Get-Sha256 {
    param([string]$Path)
    return (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Test-BytesEqual {
    param([byte[]]$A, [byte[]]$B)
    if ($A.Length -ne $B.Length) { return $false }
    for ($i = 0; $i -lt $A.Length; $i++) { if ($A[$i] -ne $B[$i]) { return $false } }
    return $true
}

# ---------------------------------------------------------------------------
# Stage
# ---------------------------------------------------------------------------
function New-QaStage {
    param(
        [string]$StageDir,
        [string[]]$Mods = @(),
        [string]$EnvConfigPath = ''
    )
    $repo = Get-RepoRoot
    $resolved = [IO.Path]::GetFullPath($StageDir)
    if ($resolved -notmatch '[\\/]red_play[\\/]') {
        throw "refusing to wipe stage dir outside _agent/red_play: $resolved"
    }
    if (Test-Path $resolved) { Remove-Item -Path $resolved -Recurse -Force }
    New-Item -ItemType Directory -Path $resolved -Force | Out-Null

    $exe = Get-QaSimExe
    if (-not (Test-Path $exe)) { throw "simulator.exe missing: $exe" }
    Copy-Item -Path $exe -Destination (Join-Path $resolved 'simulator.exe') -Force
    Copy-Item -Path (Join-Path $repo 'web') -Destination (Join-Path $resolved 'web') -Recurse -Force
    Copy-Item -Path (Join-Path $repo 'resourcepacks') -Destination (Join-Path $resolved 'resourcepacks') -Recurse -Force
    Copy-Item -Path (Join-Path $repo 'packs.list') -Destination (Join-Path $resolved 'packs.list') -Force
    if ($EnvConfigPath -and (Test-Path $EnvConfigPath)) {
        Copy-Item -Path $EnvConfigPath -Destination (Join-Path $resolved 'env_config.txt') -Force
    } else {
        Copy-Item -Path (Join-Path $repo 'env_config.txt') -Destination (Join-Path $resolved 'env_config.txt') -Force
    }
    $modsFile = Join-Path $resolved 'mods.list'
    if (Test-Path $modsFile) { Remove-Item -Path $modsFile -Force }
    if ($Mods.Count -gt 0) {
        Set-Content -Path $modsFile -Value ($Mods -join "`r`n") -Encoding ASCII
    }
    return $resolved
}

function Copy-QaStage {
    param([string]$From, [string]$To)
    $resolved = [IO.Path]::GetFullPath($To)
    if ($resolved -notmatch '[\\/]red_play[\\/]') { throw "refusing to wipe stage dir outside _agent/red_play: $resolved" }
    if (Test-Path $resolved) { Remove-Item -Path $resolved -Recurse -Force }
    Copy-Item -Path $From -Destination $resolved -Recurse -Force
    return $resolved
}

# ---------------------------------------------------------------------------
# Raw HTTP (TcpClient)
# ---------------------------------------------------------------------------
function Invoke-SimHttp {
    param(
        [int]$Port,
        [string]$Method = 'GET',
        [string]$Path = '/',
        [string]$Body = '',
        [int]$TimeoutMs = 15000
    )
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $client.ReceiveTimeout = $TimeoutMs
        $client.SendTimeout = $TimeoutMs
        $client.Connect('127.0.0.1', $Port)
        $stream = $client.GetStream()
        $bodyBytes = [Text.Encoding]::UTF8.GetBytes([string]$Body)
        $head = ('{0} {1} HTTP/1.1' + "`r`n" +
                 'Host: 127.0.0.1:{2}' + "`r`n" +
                 'Content-Type: text/plain; charset=utf-8' + "`r`n" +
                 'Content-Length: {3}' + "`r`n" +
                 'Connection: close' + "`r`n`r`n") -f $Method, $Path, $Port, $bodyBytes.Length
        $headBytes = [Text.Encoding]::ASCII.GetBytes($head)
        $stream.Write($headBytes, 0, $headBytes.Length)
        if ($bodyBytes.Length -gt 0) { $stream.Write($bodyBytes, 0, $bodyBytes.Length) }
        $stream.Flush()

        $ms = New-Object System.IO.MemoryStream
        $buf = New-Object byte[] 65536
        while ($true) {
            $n = $stream.Read($buf, 0, $buf.Length)
            if ($n -le 0) { break }
            $ms.Write($buf, 0, $n)
        }
        $all = $ms.ToArray()
    } finally {
        $client.Close()
    }
    # split headers / body
    $sep = -1
    for ($i = 0; $i -le $all.Length - 4; $i++) {
        if ($all[$i] -eq 13 -and $all[$i + 1] -eq 10 -and $all[$i + 2] -eq 13 -and $all[$i + 3] -eq 10) {
            $sep = $i; break
        }
    }
    $headText = ''
    $bodyOut = New-Object byte[] 0
    if ($sep -ge 0) {
        $headText = [Text.Encoding]::ASCII.GetString($all, 0, $sep)
        $boLen = $all.Length - ($sep + 4)
        $bodyOut = New-Object byte[] $boLen
        if ($boLen -gt 0) { [Array]::Copy($all, $sep + 4, $bodyOut, 0, $boLen) }
    }
    $status = 0
    if ($headText -match '^HTTP/\d\.\d\s+(\d{3})') { $status = [int]$Matches[1] }
    $text = [Text.Encoding]::UTF8.GetString($bodyOut)
    $json = $null
    if ($text.Length -gt 0 -and ($text.TrimStart()[0] -eq '{' -or $text.TrimStart()[0] -eq '[')) {
        try { $json = $text | ConvertFrom-Json } catch { $json = $null }
    }
    return [pscustomobject]@{
        Status = $status; Head = $headText; Bytes = $bodyOut; Text = $text; Json = $json
    }
}

function Get-SimStatus {
    param([int]$Port)
    $r = Invoke-SimHttp -Port $Port -Path '/status'
    return $r.Text.Trim()
}

function Get-EnvJson {
    param([int]$Port)
    $r = Invoke-SimHttp -Port $Port -Path '/env'
    if ($null -eq $r.Json) { throw "GET /env did not return JSON: $($r.Text)" }
    return $r.Json
}

function Send-EnvCmd {
    param([int]$Port, [string[]]$Lines)
    $body = ($Lines -join "`n")
    $r = Invoke-SimHttp -Port $Port -Method 'POST' -Path '/env' -Body $body
    if ($null -eq $r.Json) { throw "POST /env did not return JSON: status=$($r.Status) text=$($r.Text)" }
    return $r.Json
}

function Send-Control {
    param([int]$Port, [string]$Cmd)
    $body = '{"cmd":"' + $Cmd + '"}'
    return (Invoke-SimHttp -Port $Port -Method 'POST' -Path '/control' -Body $body)
}

function Send-Save {
    param([int]$Port, [string]$Name)
    $r = Invoke-SimHttp -Port $Port -Method 'POST' -Path '/save' -Body ("name=" + $Name)
    return $r
}

function Send-Load {
    param([int]$Port, [string]$Name)
    return (Invoke-SimHttp -Port $Port -Method 'POST' -Path '/load' -Body ("name=" + $Name))
}

function Get-SchemaJson {
    param([int]$Port)
    $r = Invoke-SimHttp -Port $Port -Path '/schema'
    if ($null -eq $r.Json) { throw "GET /schema did not return JSON" }
    return $r.Json
}

function Get-ModsJson {
    param([int]$Port)
    $r = Invoke-SimHttp -Port $Port -Path '/mods'
    if ($null -eq $r.Json) { throw "GET /mods did not return JSON" }
    return $r.Json
}

# ---------------------------------------------------------------------------
# /state binary parser
#   header 80B: magic0 ver1 finished2 frame@4 w@8 h@12 balls@16 alive@20
#               typeCounts@24/28/32 finishReason@36.. cyanoNodes@64 cyanoLinks@68
#               cyanoAvgEnergy@72 sun@76
#   records: ball 6B, nucleus 32B (alive only), cyano node 24B, link 8B
# ---------------------------------------------------------------------------
function Read-StateBinary {
    param([byte[]]$Bytes)
    if ($null -eq $Bytes -or $Bytes.Length -lt 80) { return $null }
    $u32 = { param($o) [BitConverter]::ToUInt32($Bytes, $o) }
    $f32 = { param($o) [BitConverter]::ToSingle($Bytes, $o) }
    $balls = & $u32 16
    $alive = & $u32 20
    $cn = & $u32 64
    $cl = & $u32 68
    $nucOff = 80 + $balls * 6
    $nuclei = New-Object System.Collections.ArrayList
    for ($i = 0; $i -lt $alive; $i++) {
        $o = $nucOff + $i * 32
        if ($o + 32 -gt $Bytes.Length) { break }
        [void]$nuclei.Add([pscustomobject]@{
            Energy = [double](& $f32 ($o + 8))
            Id = [int](& $u32 ($o + 24))
            X = [double](& $f32 $o)
            Y = [double](& $f32 ($o + 4))
        })
    }
    $cyOff = $nucOff + $alive * 32
    $nodes = New-Object System.Collections.ArrayList
    for ($i = 0; $i -lt $cn; $i++) {
        $o = $cyOff + $i * 24
        if ($o + 24 -gt $Bytes.Length) { break }
        [void]$nodes.Add([pscustomobject]@{
            X = [double](& $f32 $o)
            Y = [double](& $f32 ($o + 4))
            Energy = [double](& $f32 ($o + 8))
            Id = [long](& $u32 ($o + 12))
            LinkCount = [int][BitConverter]::ToUInt16($Bytes, $o + 16)
            ColonyId = [int][BitConverter]::ToUInt16($Bytes, $o + 18)
            Age = [int][BitConverter]::ToUInt16($Bytes, $o + 20)
            Flags = [int][BitConverter]::ToUInt16($Bytes, $o + 22)
        })
    }
    $linkOff = $cyOff + $cn * 24
    $links = New-Object System.Collections.ArrayList
    for ($i = 0; $i -lt $cl; $i++) {
        $o = $linkOff + $i * 8
        if ($o + 8 -gt $Bytes.Length) { break }
        [void]$links.Add([pscustomobject]@{ A = [long](& $u32 $o); B = [long](& $u32 ($o + 4)) })
    }
    return [pscustomobject]@{
        Magic = if ($Bytes.Length -ge 1) { $Bytes[0] } else { 0 }
        Version = if ($Bytes.Length -ge 2) { $Bytes[1] } else { 0 }
        Finished = if ($Bytes.Length -ge 3) { $Bytes[2] } else { 0 }
        Frame = [int](& $u32 4)
        Width = [double](& $f32 8)
        Height = [double](& $f32 12)
        Balls = [int]$balls
        Alive = [int]$alive
        TypeCountA = [int](& $u32 24)
        TypeCountB = [int](& $u32 28)
        TypeCountC = [int](& $u32 32)
        CyanoNodes = [int]$cn
        CyanoLinks = [int]$cl
        CyanoAvgEnergy = [double](& $f32 72)
        SunLevel = [double](& $f32 76)
        Nuclei = @($nuclei)
        Nodes = @($nodes)
        Links = @($links)
        Raw = $Bytes
    }
}

function Get-SimState {
    param([int]$Port)
    $r = Invoke-SimHttp -Port $Port -Path '/state'
    return (Read-StateBinary -Bytes $r.Bytes)
}

# ---------------------------------------------------------------------------
# save.bin parser (v1/v2)
# ---------------------------------------------------------------------------
function Read-SaveBin {
    param([string]$Path)
    $b = [IO.File]::ReadAllBytes($Path)
    $ms = New-Object System.IO.MemoryStream
    $ms.Write($b, 0, $b.Length)
    $ms.Position = 0
    $br = New-Object System.IO.BinaryReader($ms)
    $magic = [Text.Encoding]::ASCII.GetString($br.ReadBytes(4))
    $version = $br.ReadUInt32()
    $cfgCount = $br.ReadUInt32()
    $cfgLen = $br.ReadUInt32()
    $cfgStr = [Text.Encoding]::UTF8.GetString($br.ReadBytes([int]$cfgLen))
    $cfg = @{}
    foreach ($line in ($cfgStr -split "`n")) {
        $eq = $line.IndexOf('=')
        if ($eq -lt 0) { continue }
        $k = $line.Substring(0, $eq).Trim()
        $v = $line.Substring($eq + 1).Trim()
        if ($k) { $cfg[$k] = $v }
    }
    $frame = [long]$br.ReadUInt64()
    $nextId = [long]$br.ReadUInt64()
    $rngLen = $br.ReadUInt32()
    $rngStr = [Text.Encoding]::ASCII.GetString($br.ReadBytes([int]$rngLen))
    $ballCount = [long]$br.ReadUInt64()
    for ($i = 0; $i -lt $ballCount; $i++) {
        [void]$br.ReadBytes(32)
        [void]$br.ReadByte()
        [void]$br.ReadBytes(4)
    }
    [void]$br.ReadBytes([int]($ballCount * 4))
    $nCount = [long]$br.ReadUInt64()
    $nuclei = New-Object System.Collections.ArrayList
    for ($i = 0; $i -lt $nCount; $i++) {
        $x = $br.ReadDouble(); $y = $br.ReadDouble(); $vx = $br.ReadDouble(); $vy = $br.ReadDouble()
        $energy = $br.ReadDouble()
        $alive = $br.ReadByte() -ne 0
        $id = $br.ReadInt32()
        $age = $br.ReadInt32()
        $wander = $br.ReadDouble()
        for ($t = 0; $t -lt 3; $t++) { for ($q = 0; $q -lt 4; $q++) { [void]$br.ReadDouble() } }
        for ($q = 0; $q -lt 7; $q++) { [void]$br.ReadDouble() }
        [void]$br.ReadDouble(); [void]$br.ReadDouble(); [void]$br.ReadDouble()
        [void]$nuclei.Add([pscustomobject]@{
            Id = $id; Energy = $energy; Alive = $alive; Age = $age; X = $x; Y = $y; Index = $i
        })
    }
    $v2Offset = [long]$ms.Position
    $cyanoNodes = New-Object System.Collections.ArrayList
    $cyanoLinks = New-Object System.Collections.ArrayList
    $envState = $null
    $cyanoEnabled = $false
    $cyanoNextId = 0
    if ($version -ge 2) {
        $hueLen = $br.ReadUInt32(); [void]$br.ReadBytes([int]$hueLen)
        $cyCount = $br.ReadUInt32()
        for ($i = 0; $i -lt $cyCount; $i++) {
            $fx = $br.ReadSingle(); $fy = $br.ReadSingle(); $fe = $br.ReadSingle()
            $cid = $br.ReadUInt32()
            $colony = $br.ReadUInt16(); $cage = $br.ReadUInt16(); $cflags = $br.ReadUInt16()
            $lcount = $br.ReadUInt16(); $ctype = $br.ReadByte()
            [void]$cyanoNodes.Add([pscustomobject]@{
                Id = [long]$cid; X = [double]$fx; Y = [double]$fy; Energy = [double]$fe
                ColonyId = [int]$colony; Age = [int]$cage; Flags = [int]$cflags
                LinkCount = [int]$lcount; Type = [int]$ctype
            })
        }
        $cyLinks = $br.ReadUInt32()
        for ($i = 0; $i -lt $cyLinks; $i++) {
            $la = $br.ReadUInt32(); $lb = $br.ReadUInt32(); $flow = $br.ReadDouble()
            [void]$cyanoLinks.Add([pscustomobject]@{ A = [long]$la; B = [long]$lb; Flow = $flow })
        }
        $cyanoNextId = $br.ReadUInt32()
        $cyanoEnabled = ($br.ReadByte() -ne 0)
        $influence = $br.ReadDouble()
        $cd = @(); for ($i = 0; $i -lt 4; $i++) { $cd += $br.ReadInt32() }
        $interventions = $br.ReadInt32()
        $peakAlive = $br.ReadInt32()
        $envState = [pscustomobject]@{
            Influence = $influence; Cooldown = $cd
            Interventions = $interventions; PeakAlive = $peakAlive
        }
    }
    $modCount = -1
    $modSegment = New-Object System.Collections.ArrayList
    try {
        $modCount = $br.ReadUInt32()
        for ($i = 0; $i -lt [Math]::Min($modCount, 100); $i++) {
            $idLen = $br.ReadUInt32()
            $mid = [Text.Encoding]::ASCII.GetString($br.ReadBytes([int]$idLen))
            [void]$modSegment.Add($mid)
            # unknown state length: cannot skip safely; stop after first id (tests use stateless mods only)
            break
        }
    } catch { $modCount = -1 }
    $br.Close(); $ms.Close()
    return [pscustomobject]@{
        Raw = $b; Magic = $magic; Version = $version; CfgCount = $cfgCount
        Config = $cfg; Frame = $frame; NextNucleusId = $nextId; RngLen = $rngLen
        BallCount = $ballCount; Nuclei = @($nuclei); V2Offset = $v2Offset
        CyanoNodes = @($cyanoNodes); CyanoLinks = @($cyanoLinks)
        CyanoEnabled = $cyanoEnabled; CyanoNextId = $cyanoNextId
        Env = $envState; ModCount = $modCount; ModIds = @($modSegment)
    }
}

# ---------------------------------------------------------------------------
# Server lifecycle
# ---------------------------------------------------------------------------
function Test-TcpPortOpen {
    param([int]$Port)
    $c = New-Object System.Net.Sockets.TcpClient
    try {
        $iar = $c.BeginConnect('127.0.0.1', $Port, $null, $null)
        if (-not $iar.AsyncWaitHandle.WaitOne(250)) { return $false }
        $c.EndConnect($iar) | Out-Null
        return $true
    } catch { return $false } finally { $c.Close() }
}

function Wait-SimReady {
    param([int]$Port, $Process, [int]$TimeoutSec = 25, [string]$OutLog)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if ($Process -and $Process.HasExited) {
            $tail = ''
            if ($OutLog -and (Test-Path $OutLog)) { $tail = (Get-Content $OutLog -Tail 20 -Encoding UTF8) -join ' | ' }
            throw "simulator exited during startup (code $($Process.ExitCode)): $tail"
        }
        try {
            $r = Invoke-SimHttp -Port $Port -Path '/status' -TimeoutMs 1000
            if ($r.Status -eq 200) { return $r.Text.Trim() }
        } catch { }
        Start-Sleep -Milliseconds 100
    }
    throw "simulator not ready on port $Port within $TimeoutSec s"
}

function Start-QaServer {
    param(
        [string]$StageDir,
        [int]$Port,
        [string]$LogDir,
        [string]$Tag = 'srv'
    )
    if (Test-TcpPortOpen -Port $Port) { throw "port $Port already in use; refusing to steal it" }
    if (-not (Test-Path $LogDir)) { New-Item -ItemType Directory -Path $LogDir -Force | Out-Null }
    $exe = Join-Path $StageDir 'simulator.exe'
    $stamp = Get-Date -Format 'HHmmssfff'
    $out = Join-Path $LogDir ("{0}_{1}_{2}.out.log" -f $Tag, $Port, $stamp)
    $err = Join-Path $LogDir ("{0}_{1}_{2}.err.log" -f $Tag, $Port, $stamp)
    $p = Start-Process -FilePath $exe -ArgumentList '--serve', '--no-browser', '--port', "$Port" `
        -WorkingDirectory $StageDir -RedirectStandardOutput $out -RedirectStandardError $err `
        -PassThru -WindowStyle Hidden
    $actualPort = $Port
    # The server increments the port if the requested one got taken between our
    # check and bind; the startup banner is the source of truth.
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) {
        if ($p.HasExited) { throw "simulator exited during startup (code $($p.ExitCode))" }
        $line = ''
        if (Test-Path $out) { $line = (Get-Content $out -Encoding UTF8 -ErrorAction SilentlyContinue) -join "`n" }
        if ($line -match 'http://127\.0\.0\.1:(\d+)/') {
            $actualPort = [int]$Matches[1]
            break
        }
        Start-Sleep -Milliseconds 100
    }
    if ($actualPort -ne $Port) {
        Write-QaLog "port $Port was busy inside server; started on actual port $actualPort"
    }
    Wait-SimReady -Port $actualPort -Process $p -OutLog $out | Out-Null
    # Automation: remove the launch fps cap (env_config.txt default max_fps=30).
    Send-Control -Port $actualPort -Cmd 'maxfps:0' | Out-Null
    $srv = [pscustomobject]@{
        Proc = $p; Port = $actualPort; StageDir = $StageDir; LogOut = $out; LogErr = $err; Tag = $Tag
    }
    [void]$global:QaServers.Add($srv)
    Write-QaLog ("server {0} up on port {1} (pid {2})" -f $Tag, $actualPort, $p.Id)
    return $srv
}

function Stop-QaServer {
    param($Server)
    if ($null -eq $Server) { return }
    try { Send-Control -Port $Server.Port -Cmd 'exit' | Out-Null } catch { }
    $exited = $false
    try { $exited = $Server.Proc.WaitForExit(5000) } catch { }
    if (-not $exited) {
        try { Stop-Process -Id $Server.Proc.Id -Force -ErrorAction SilentlyContinue } catch { }
        if (-not $exited) { Write-QaLog ("forced kill pid {0}" -f $Server.Proc.Id) }
    }
    for ($i = $global:QaServers.Count - 1; $i -ge 0; $i--) {
        if ($global:QaServers[$i].Port -eq $Server.Port -and $global:QaServers[$i].Proc.Id -eq $Server.Proc.Id) {
            $global:QaServers.RemoveAt($i)
        }
    }
}

function Stop-AllQaServers {
    $snapshot = @($global:QaServers)
    foreach ($s in $snapshot) { Stop-QaServer -Server $s }
}

# ---------------------------------------------------------------------------
# Frame-accurate control
# ---------------------------------------------------------------------------
function Wait-Status {
    param([int]$Port, [string[]]$Patterns, [int]$TimeoutSec = 30)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $st = Get-SimStatus -Port $Port
        foreach ($p in $Patterns) { if ($st -like $p) { return $st } }
        Start-Sleep -Milliseconds 50
    }
    throw ("status did not match [{0}] in {1}s (last: {2})" -f ($Patterns -join ','), $TimeoutSec, (Get-SimStatus -Port $Port))
}

# Bring the world to an idle paused state and return the current authoritative frame.
# render_interval must be 1 for the /state frame to be exact.
function Pause-Sim {
    param([int]$Port, [int]$TimeoutSec = 30)
    Send-Control -Port $Port -Cmd 'pause' | Out-Null
    Wait-Status -Port $Port -Patterns @('paused') -TimeoutSec $TimeoutSec | Out-Null
    Start-Sleep -Milliseconds 150
    # wait until two consecutive /state frames agree => main loop idle
    $prev = -1
    for ($i = 0; $i -lt 60; $i++) {
        $s = Get-SimState -Port $Port
        if ($null -eq $s) { Start-Sleep -Milliseconds 50; continue }
        if ($s.Frame -eq $prev) { return $s.Frame }
        $prev = $s.Frame
        Start-Sleep -Milliseconds 50
    }
    throw "paused world did not settle on port $Port"
}

function Step-Sim {
    param([int]$Port, [int]$Count, [int]$TargetFrame = -1, [int]$TimeoutSec = 120)
    if ($Count -le 0) { return }
    for ($i = 0; $i -lt $Count; $i++) { Send-Control -Port $Port -Cmd 'step' | Out-Null }
    if ($TargetFrame -ge 0) { Wait-Frame -Port $Port -TargetFrame $TargetFrame -TimeoutSec $TimeoutSec | Out-Null }
}

function Wait-Frame {
    param([int]$Port, [int]$TargetFrame, [int]$TimeoutSec = 120)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $s = Get-SimState -Port $Port
        if ($null -ne $s -and $s.Frame -ge $TargetFrame) { return $s.Frame }
        Start-Sleep -Milliseconds 30
    }
    throw "frame did not reach $TargetFrame within $TimeoutSec s"
}

function Get-FrameNow {
    param([int]$Port)
    $s = Get-SimState -Port $Port
    if ($null -eq $s) { throw "no /state snapshot on port $Port" }
    return $s.Frame
}

# Start a world and pause it at a known idle point. Returns @{ Server; Frame }.
function Start-PausedWorld {
    param(
        [string]$StageDir,
        [int]$Port,
        [string]$LogDir,
        [string]$RunBody,
        [string]$Tag = 'world'
    )
    $srv = Start-QaServer -StageDir $StageDir -Port $Port -LogDir $LogDir -Tag $Tag
    $rr = Invoke-SimHttp -Port $Port -Method 'POST' -Path '/run' -Body $RunBody
    if ($rr.Status -ne 200) { throw "POST /run failed: status=$($rr.Status)" }
    Wait-Status -Port $Port -Patterns @('running', 'paused') -TimeoutSec 20 | Out-Null
    $frame = Pause-Sim -Port $Port
    return [pscustomobject]@{ Server = $srv; Frame = $frame }
}

# ---------------------------------------------------------------------------
# Console (headless) runner
# ---------------------------------------------------------------------------
function Invoke-ConsoleSim {
    param(
        [string]$StageDir,
        [string]$WorkDir,
        [string[]]$SimArgs,
        [int]$TimeoutSec = 180,
        [string]$Tag = 'console'
    )
    if (-not (Test-Path $WorkDir)) { New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null }
    $exe = Join-Path $StageDir 'simulator.exe'
    $out = Join-Path $WorkDir ("{0}.out.log" -f $Tag)
    $err = Join-Path $WorkDir ("{0}.err.log" -f $Tag)
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $exe -ArgumentList $SimArgs -WorkingDirectory $WorkDir `
        -RedirectStandardOutput $out -RedirectStandardError $err -PassThru -WindowStyle Hidden
    $exited = $false
    try { $exited = $p.WaitForExit($TimeoutSec * 1000) } catch { }
    $timedOut = -not $exited
    if ($timedOut) { try { Stop-Process -Id $p.Id -Force } catch { } }
    $sw.Stop()
    return [pscustomobject]@{
        ExitCode = $(if ($exited) { $p.ExitCode } else { -1 })
        TimedOut = $timedOut
        Seconds = [Math]::Round($sw.Elapsed.TotalSeconds, 3)
        StdoutPath = $out; StderrPath = $err
        Out = $(if (Test-Path $out) { (Get-Content $out -Encoding UTF8) -join "`n" } else { '' })
        Err = $(if (Test-Path $err) { (Get-Content $err -Encoding UTF8) -join "`n" } else { '' })
    }
}

# ---------------------------------------------------------------------------
# misc helpers
# ---------------------------------------------------------------------------
function Get-NucleusEnergyMap {
    param($Save)
    $map = @{}
    foreach ($n in $Save.Nuclei) { $map[[int]$n.Id] = $n }
    return $map
}

function Compare-NucleusSaves {
    param($SaveA, $SaveB)
    $ma = Get-NucleusEnergyMap -Save $SaveA
    $mb = Get-NucleusEnergyMap -Save $SaveB
    $ids = @($ma.Keys + $mb.Keys | Sort-Object -Unique)
    $rows = New-Object System.Collections.ArrayList
    foreach ($id in $ids) {
        $a = $ma[$id]; $b = $mb[$id]
        [void]$rows.Add([pscustomobject]@{
            Id = $id
            EnergyA = $(if ($a) { [double]$a.Energy } else { $null })
            EnergyB = $(if ($b) { [double]$b.Energy } else { $null })
            AliveA = $(if ($a) { [bool]$a.Alive } else { $null })
            AliveB = $(if ($b) { [bool]$b.Alive } else { $null })
            Delta = $(if ($a -and $b) { [double]($b.Energy - $a.Energy) } else { $null })
        })
    }
    return @($rows)
}

function Get-UniqueDeltasRounded {
    param($Rows, [int]$Digits = 4)
    return @($Rows | Where-Object { $null -ne $_.Delta } |
        ForEach-Object { [Math]::Round($_.Delta, $Digits) } | Sort-Object -Unique)
}

function Format-List2 {
    param($Items, [string]$Sep = '; ')
    return (@($Items) | ForEach-Object { "$_" }) -join $Sep
}
