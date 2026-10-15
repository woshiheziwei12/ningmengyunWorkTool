param([ValidateSet('Debug','Release')][string]$BuildConfiguration='Release', [switch]$BridgeTests)
$Smoke=$false;$Tests=$false
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$mode = if ($BridgeTests) { "Tests-$BuildConfiguration" } else { $BuildConfiguration }
$work = Join-Path $PSScriptRoot $mode
New-Item -ItemType Directory -Force -Path $work | Out-Null
$priorLogs = @(Get-ChildItem -LiteralPath $work -File | Where-Object { $_.Extension -in @('.json','.log') })
if ($priorLogs.Count -gt 0) {
    $audit = Join-Path $work ('audit-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Path $audit | Out-Null
    foreach ($file in $priorLogs) { Copy-Item -LiteralPath $file.FullName -Destination $audit }
}
function Read-BuildProcesses {
    @(Get-CimInstance Win32_Process -Filter "Name='MSBuild.exe' OR Name='mspdbsrv.exe' OR Name='cl.exe' OR Name='link.exe' OR Name='Tracker.exe'" |
        Select-Object ProcessId,ParentProcessId,CreationDate,ExecutablePath,CommandLine,Name)
}
function Identity($process) { "$($process.ProcessId)|$($process.CreationDate.ToUniversalTime().Ticks)|$($process.ExecutablePath)" }
function Read-Artifacts {
    foreach ($path in $script:artifactPaths) {
        $exists = Test-Path -LiteralPath $path
        [pscustomobject]@{ Path=$path; Exists=$exists; SHA256=if ($exists) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash } else { '' } }
    }
}
$artifactPaths = @(
    foreach ($dir in @('Release','Debug','QtWidgetsApplication1/Release','QtWidgetsApplication1/Debug','dist/local')) {
        foreach ($extension in @('exe','pdb')) { Join-Path $root "$dir/QtWidgetsApplication1.$extension" }
    }
    Get-ChildItem -LiteralPath $root -Recurse -Filter QtWidgetsApplication1.exe -File |
        Where-Object { $_.FullName -notlike "$root\BuildCheck\*" } | ForEach-Object { $_.FullName; [IO.Path]::ChangeExtension($_.FullName,'pdb') }
) | Sort-Object -Unique
$artifactPaths += @(Get-ChildItem -LiteralPath (Join-Path $root 'Release') -Filter 'QtWidgetsApplication1-BrowserSync*.exe' -File | ForEach-Object { $_.FullName; [IO.Path]::ChangeExtension($_.FullName,'pdb') })
$artifactPaths = @($artifactPaths | Sort-Object -Unique)
$beforeArtifacts = @(Read-Artifacts)
$beforeArtifacts | ConvertTo-Json -Depth 5 | Set-Content "$work/artifacts-before.json"
$baseline = @(Read-BuildProcesses)
$baseline | ConvertTo-Json -Depth 5 | Set-Content "$work/processes-before.json"
$baselineKeys = @{}
foreach ($entry in $baseline) { $baselineKeys[(Identity $entry)] = $true }
$seen = @{}
$owned = @{}
$buildExit = 1
$buildProcess = $null
try {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = if ($Tests) { $env:ComSpec } else { 'D:\app\visualStdio\base\MSBuild\Current\Bin\MSBuild.exe' }
    $start.WorkingDirectory = if ($Tests) { $work } else { $root }
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $environment = [Environment]::GetEnvironmentVariables()
    $start.Environment.Clear()
    foreach ($entry in $environment.GetEnumerator()) { $start.Environment[$entry.Key.ToUpperInvariant()] = [string]$entry.Value }
    $props = @{
        Configuration=$BuildConfiguration; Platform='Win32'; PlatformToolset='v143'; QtMsBuild='C:\Users\10604\AppData\Local\QtMsBuild';
        WindowsTargetPlatformVersion='10.0.22000.0'; WindowsSdkDir='D:\app\winsdk\';
        OutDir="$work\bin\"; IntDir="$work\obj\"; QtIntDir="$work\obj\qt\";
        TLogLocation="$work\obj\tlog\"; MSBuildProjectExtensionsPath="$work\obj\";
        ForceImportBeforeCppTargets="$PSScriptRoot\isolation.targets";
        BridgeTests=if ($BridgeTests) { 'true' } else { 'false' };
        TargetName=if ($BridgeTests) { 'BrowserLoginBridgeTests' } else { 'QtWidgetsApplication1' }
    }
    if (!$Tests) { foreach ($argument in @("$root\QtWidgetsApplication1\QtWidgetsApplication1.vcxproj",'/t:Build','/nr:false','/m:1','/nologo','/v:minimal')) { $start.ArgumentList.Add($argument) }
    foreach ($key in $props.Keys) { $start.ArgumentList.Add("/p:$key=$($props[$key])") }
    } else {
        foreach ($argument in @('/d','/c',"$PSScriptRoot\build-tests.cmd")) { $start.ArgumentList.Add($argument) }
    }
    $props | ConvertTo-Json | Set-Content "$work/build-paths.json"
    $buildProcess = [Diagnostics.Process]::Start($start)
    $owned[$buildProcess.Id] = $true
    $stdout = $buildProcess.StandardOutput.ReadToEndAsync()
    $stderr = $buildProcess.StandardError.ReadToEndAsync()
    do {
        $observed = @(Read-BuildProcesses)
        foreach ($entry in $observed) {
            $key = Identity $entry
            if (!$baselineKeys.ContainsKey($key)) {
                $seen[$key] = $entry
                if ($owned.ContainsKey([int]$entry.ParentProcessId)) { $owned[[int]$entry.ProcessId] = $true }
            }
        }
        do {
            $priorCount = $owned.Count
            foreach ($entry in $seen.Values) {
                if ($owned.ContainsKey([int]$entry.ParentProcessId)) { $owned[[int]$entry.ProcessId] = $true }
            }
        } while ($owned.Count -gt $priorCount)
        Start-Sleep -Milliseconds 200
    } while (!$buildProcess.HasExited)
    $buildProcess.WaitForExit()
    $buildExit = $buildProcess.ExitCode
    $log = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
    [IO.File]::WriteAllText("$work/build.log",$log)
    $log -split "`n" | Select-Object -Last 24
} finally {
    @($seen.Values) | ConvertTo-Json -Depth 5 | Set-Content "$work/processes-observed.json"
    $remaining = @(Read-BuildProcesses)
    $remaining | ConvertTo-Json -Depth 5 | Set-Content "$work/processes-after.json"
    $active = @($remaining | Where-Object { $_.Name -in @('cl.exe','link.exe') })
    foreach ($entry in $remaining) {
        $key = Identity $entry
        if ($baselineKeys.ContainsKey($key)) { continue }
        if ($entry.Name -ne 'mspdbsrv.exe') {
            Write-Output "RETAIN PID=$($entry.ProcessId) NAME=$($entry.Name): non-PDB process, no automatic termination"
            continue
        }
        if (!$seen.ContainsKey($key) -or !$owned.ContainsKey([int]$entry.ProcessId) -or $active.Count -gt 0 -or !$entry.ExecutablePath) {
            Write-Output "RETAIN PDB PID=$($entry.ProcessId): ownership unproven, active compiler/linker, or path unavailable"
            continue
        }
        $live = Get-CimInstance Win32_Process -Filter "ProcessId=$($entry.ProcessId)"
        if ($live -and (Identity $live) -eq $key) {
            try {
                Stop-Process -Id $entry.ProcessId -ErrorAction Stop
                Wait-Process -Id $entry.ProcessId -Timeout 10 -ErrorAction SilentlyContinue
                $stillLive = Get-CimInstance Win32_Process -Filter "ProcessId=$($entry.ProcessId)"
                if ($stillLive -and (Identity $stillLive) -eq $key) { Write-Output "RETAIN PDB PID=$($entry.ProcessId): did not exit" }
                else { Write-Output "CLEANED PDB PID=$($entry.ProcessId) PATH=$($entry.ExecutablePath)" }
            } catch { Write-Output "RETAIN PDB PID=$($entry.ProcessId): $($_.Exception.Message)" }
        }
    }
    @(Read-BuildProcesses) | ConvertTo-Json -Depth 5 | Set-Content "$work/processes-final.json"
    $afterArtifacts = @(Read-Artifacts)
    $afterArtifacts | ConvertTo-Json -Depth 5 | Set-Content "$work/artifacts-after.json"
    if (($beforeArtifacts | ConvertTo-Json -Compress) -ne ($afterArtifacts | ConvertTo-Json -Compress)) { throw 'Original EXE/PDB changed during isolated build.' }
    Write-Output "ARTIFACTS_UNCHANGED=$($afterArtifacts.Count) BUILD_EXIT=$buildExit"
}
exit $buildExit
