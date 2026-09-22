# scripts/ci/win_build_stall.ps1 - the Windows half of
# build_with_stall_watchdog.sh: dump, or kill, the build's process tree
# once the build has stalled. Called from the MSYS2 shell with Windows-native
# paths and PIDs. See the .sh header for the why (#309).
#
#   -RootPid     the shell running the build: the tree root. It is dumped
#                for context and never killed.
#   -ExcludePid  the watchdog's own subshell: its subtree (this PowerShell
#                among it) is left out of the dump and the kill.
#   -Action Dump writes everything a post-mortem of a hung build needs to
#                stdout: the process tree with CPU/working-set/handle
#                counts, each thread's state and wait reason, native
#                stacks when the Windows SDK debugger is on the image,
#                recent crash/hang events, Defender state, memory, disk.
#                Every section is best-effort: a failing probe prints its
#                error and the next one still runs.
#   -Action Kill terminates the build's processes, leaves first.
#
# ASCII only: Windows PowerShell 5.1 reads a file without a BOM as ANSI, and
# one UTF-8 dash in a string was enough to fail the whole parse on the runner
# (the first real capture lost its dump and its kill to exactly that).
param(
    [Parameter(Mandatory = $true)][ValidateSet('Dump', 'Kill')][string]$Action,
    [Parameter(Mandatory = $true)][int]$RootPid,
    [int]$ExcludePid = 0,
    [string]$BuildDir = ''
)

$ErrorActionPreference = 'Continue'

function Get-AllProcesses {
    Get-CimInstance Win32_Process | ForEach-Object {
        [pscustomobject]@{
            Pid          = $_.ProcessId
            Ppid         = $_.ParentProcessId
            Name         = $_.Name
            CpuSeconds   = [math]::Round(($_.KernelModeTime + $_.UserModeTime) / 1e7, 2)
            WorkingSetMB = [math]::Round($_.WorkingSetSize / 1MB, 1)
            Threads      = $_.ThreadCount
            Handles      = $_.HandleCount
            Created      = $_.CreationDate
            CommandLine  = $_.CommandLine
        }
    }
}

# Breadth-first descendants of $root (root included, Depth 0), skipping the
# subtree rooted at $exclude.
function Get-Descendants([object[]]$all, [int]$root, [int]$exclude) {
    $byParent = @{}
    foreach ($p in $all) {
        if (-not $byParent.ContainsKey($p.Ppid)) { $byParent[$p.Ppid] = @() }
        $byParent[$p.Ppid] += $p
    }
    $out = @()
    $queue = New-Object System.Collections.Queue
    $rootProc = $all | Where-Object { $_.Pid -eq $root } | Select-Object -First 1
    if ($null -eq $rootProc) { return $out }
    $queue.Enqueue(@($rootProc, 0))
    while ($queue.Count -gt 0) {
        $entry = $queue.Dequeue()
        $p = $entry[0]; $depth = $entry[1]
        if ($exclude -ne 0 -and $p.Pid -eq $exclude) { continue }
        $p | Add-Member -NotePropertyName Depth -NotePropertyValue $depth -Force
        $out += $p
        foreach ($c in ($byParent[$p.Pid] | Where-Object { $_ -ne $null })) {
            $queue.Enqueue(@($c, $depth + 1))
        }
    }
    return $out
}

function Section([string]$title, [scriptblock]$body) {
    Write-Output ""
    Write-Output "=== $title ==="
    try { & $body } catch { Write-Output "($title failed: $($_.Exception.Message))" }
}

$all = @(Get-AllProcesses)
$tree = @(Get-Descendants $all $RootPid $ExcludePid)
$build = @($tree | Where-Object { $_.Pid -ne $RootPid })

if ($Action -eq 'Kill') {
    # Leaves first so no child is orphaned into a new parent mid-walk. The
    # root (the shell) stays alive: it is the one waiting to retry.
    foreach ($p in ($build | Sort-Object Depth -Descending)) {
        try {
            Stop-Process -Id $p.Pid -Force -ErrorAction Stop
            Write-Output "killed $($p.Pid) $($p.Name)"
        } catch {
            Write-Output "could not kill $($p.Pid) $($p.Name): $($_.Exception.Message)"
        }
    }
    exit 0
}

$now = Get-Date
Write-Output "stall dump at $($now.ToString('o')): shell pid $RootPid, watchdog pid $ExcludePid excluded"

Section "process tree under the shell (CPU is total seconds so far)" {
    if ($tree.Count -eq 0) { Write-Output "(pid $RootPid has no process - the shell already exited)" }
    if ($build.Count -eq 0) { Write-Output "(no build processes under the shell - everything already exited)" }
    foreach ($p in $tree) {
        $indent = '  ' * $p.Depth
        $age = if ($p.Created) { [math]::Round(($now - $p.Created).TotalSeconds) } else { '?' }
        $cmd = if ($p.CommandLine) { $p.CommandLine } else { '' }
        if ($cmd.Length -gt 300) { $cmd = $cmd.Substring(0, 300) + '...' }
        Write-Output ("{0}{1} {2}  cpu={3}s age={4}s ws={5}MB threads={6} handles={7}" -f `
            $indent, $p.Pid, $p.Name, $p.CpuSeconds, $age, $p.WorkingSetMB, $p.Threads, $p.Handles)
        if ($cmd) { Write-Output ("{0}    {1}" -f $indent, $cmd) }
    }
}

Section "threads of the build processes (state / wait reason)" {
    # Win32_Thread: ThreadState 5 = waiting; ThreadWaitReason 7 = user
    # request (blocked in a wait or on I/O), 15 = Executive, 5 = suspended.
    $pids = @{}
    foreach ($p in $build) { $pids[[uint32]$p.Pid] = $p.Name }
    $threads = Get-CimInstance Win32_Thread | Where-Object { $pids.ContainsKey([uint32]$_.ProcessHandle) }
    foreach ($t in ($threads | Sort-Object ProcessHandle, Handle)) {
        Write-Output ("pid {0} ({1}) tid {2}: state={3} waitReason={4} userTime={5} kernelTime={6}" -f `
            $t.ProcessHandle, $pids[[uint32]$t.ProcessHandle], $t.Handle, $t.ThreadState, `
            $t.ThreadWaitReason, $t.UserModeTime, $t.KernelModeTime)
    }
}

Section "native stacks (cdb -pv, non-invasive)" {
    $cdb = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64\cdb.exe",
        "$env:ProgramFiles\Windows Kits\10\Debuggers\x64\cdb.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $cdb) { Write-Output "(no cdb.exe on this image; skipping stacks)"; return }
    # No symbol server: a download per module would blow the dump's time
    # budget, and DLL exports already name the wait a thread is parked in.
    $env:_NT_SYMBOL_PATH = ''
    $interesting = '^(ninja|cmake|cmd|g\+\+|gcc|cc1plus|cc1|as|ar|ranlib|ld|collect2|python|python3|sh|bash)\.exe$'
    # Eight processes at most, forty seconds each: the whole dump has to fit
    # well inside the job's cap with the retry still to run.
    $targets = @($build | Where-Object { $_.Name -match $interesting } | Select-Object -First 8)
    foreach ($p in $targets) {
        Write-Output "--- pid $($p.Pid) $($p.Name)"
        $outFile = [System.IO.Path]::GetTempFileName()
        try {
            $proc = Start-Process -FilePath $cdb -ArgumentList @('-pv', '-p', $p.Pid, '-lines', '-c', '"~*k 25;qd"') `
                -NoNewWindow -PassThru -RedirectStandardOutput $outFile
            if (-not $proc.WaitForExit(40000)) {
                $proc.Kill()
                Write-Output "(cdb timed out on $($p.Pid))"
            }
            Get-Content $outFile | Where-Object { $_ -notmatch '^(Microsoft \(R\)|Copyright|\s*$)' } | Select-Object -First 200
        } finally {
            Remove-Item $outFile -ErrorAction SilentlyContinue
        }
    }
}

Section "every process on the machine (name, pid, ppid, cpu s, ws MB)" {
    foreach ($p in ($all | Sort-Object Name, Pid)) {
        Write-Output ("{0,-28} {1,6} {2,6} {3,9} {4,8}" -f $p.Name, $p.Pid, $p.Ppid, $p.CpuSeconds, $p.WorkingSetMB)
    }
}

Section "application error / hang / WER events in the last hour" {
    $events = Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = 1000, 1001, 1002; StartTime = $now.AddHours(-1) } -MaxEvents 20 -ErrorAction SilentlyContinue
    if (-not $events) { Write-Output "(none)"; return }
    foreach ($e in $events) {
        Write-Output ("{0} id={1} {2}" -f $e.TimeCreated.ToString('o'), $e.Id, ($e.Message -replace '\s+', ' ').Substring(0, [math]::Min(400, $e.Message.Length)))
    }
}

Section "WER report queue entries written in the last hour" {
    $dirs = @("$env:ProgramData\Microsoft\Windows\WER\ReportQueue", "$env:ProgramData\Microsoft\Windows\WER\ReportArchive")
    $recent = Get-ChildItem $dirs -Directory -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt $now.AddHours(-1) }
    if (-not $recent) { Write-Output "(none)"; return }
    $recent | ForEach-Object { Write-Output ("{0} {1}" -f $_.LastWriteTime.ToString('o'), $_.Name) }
}

Section "Windows Defender state" {
    $status = Get-MpComputerStatus -ErrorAction Stop
    Write-Output ("realtime={0} antivirus={1} behaviorMonitor={2} onAccess={3} scanInProgress={4} lastQuickScan={5}" -f `
        $status.RealTimeProtectionEnabled, $status.AntivirusEnabled, $status.BehaviorMonitorEnabled, `
        $status.OnAccessProtectionEnabled, $status.QuickScanInProgress, $status.QuickScanEndTime)
    $pref = Get-MpPreference -ErrorAction Stop
    Write-Output ("disableRealtime={0} exclusionPaths={1}" -f $pref.DisableRealtimeMonitoring, ($pref.ExclusionPath -join ';'))
}

Section "memory and disk" {
    $os = Get-CimInstance Win32_OperatingSystem
    Write-Output ("free physical {0} MB of {1} MB; free virtual {2} MB" -f `
        [math]::Round($os.FreePhysicalMemory / 1024), [math]::Round($os.TotalVisibleMemorySize / 1024), [math]::Round($os.FreeVirtualMemory / 1024))
    Get-PSDrive -PSProvider FileSystem | ForEach-Object {
        Write-Output ("{0}: free {1} GB" -f $_.Name, [math]::Round($_.Free / 1GB, 1))
    }
}

if ($BuildDir -and (Test-Path $BuildDir)) {
    Section "build outputs written in the last 10 minutes" {
        Get-ChildItem $BuildDir -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTime -gt $now.AddMinutes(-10) } |
            Sort-Object LastWriteTime -Descending | Select-Object -First 40 |
            ForEach-Object { Write-Output ("{0} {1,10} {2}" -f $_.LastWriteTime.ToString('HH:mm:ss'), $_.Length, $_.FullName.Substring($BuildDir.Length)) }
    }
}
