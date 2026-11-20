param(
    [Parameter(Mandatory=$true)][string[]]$SourcePaths,
    [ValidateSet('success','failure','password-rejected','recovered','copied','timeout')][string]$Expectation='success',
    [int]$Repeat=1,
    [int]$TimeoutSeconds=45
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/..").Path
$work = Join-Path $root ('BuildCheck/AccessConversionRuns/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -Force | Out-Null
$copies = @()
$originals = @()
foreach ($source in $SourcePaths) {
    $path = (Resolve-Path -LiteralPath $source).Path
    $originals += @{ Path=$path; Hash=(Get-FileHash -LiteralPath $path).Hash }
    $copy = Join-Path $work ("input-$($copies.Count)" + [IO.Path]::GetExtension($path))
    Copy-Item -LiteralPath $path -Destination $copy
    $copies += $copy
}
$process = $null
try {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $root 'BuildCheck/Repository/AccessConversion-Release/bin/Access2003ConversionTests.exe'
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.Encoding]::UTF8
    $start.Environment['PATH'] = "D:\app\qt5_12\5.12.0\msvc2017\bin;" + $env:PATH
    $start.ArgumentList.Add($Expectation)
    $outputDirectory = Join-Path $work 'out'
    $start.ArgumentList.Add($outputDirectory)
    for ($n=0; $n -lt $Repeat; $n++) {
        foreach ($copy in $copies) { $start.ArgumentList.Add($copy) }
    }
    $process = [Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
        throw 'FAIL: conversion exceeded timeout (check Access dialogs left by the failed test).'
    }
    $stdout.GetAwaiter().GetResult()
    $stderr.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 0) { throw "FAIL: test exit=$($process.ExitCode)" }
    $outputs = @(Get-ChildItem -LiteralPath $outputDirectory -File -ErrorAction SilentlyContinue | Where-Object Extension -ne '.json')
    if ($Expectation -in @('success','recovered','copied')) {
        if ($outputs.Count -ne $copies.Count * $Repeat) { throw 'FAIL: wrong output count' }
        foreach ($output in $outputs) {
            $stream = [IO.File]::OpenRead($output.FullName)
            $bytes = [byte[]]::new(24)
            try { $read = $stream.Read($bytes,0,24) } finally { $stream.Dispose() }
            if ($read -lt 24 -or $bytes[20] -ne 1 -or
                [Text.Encoding]::ASCII.GetString($bytes,4,15) -cne 'Standard Jet DB') {
                throw 'FAIL: output is not Jet 4.0'
            }
        }
    } elseif ($outputs.Count -ne 0) { throw 'FAIL: failed conversion left output files' }
    Write-Output "PASS: bounded conversion; output validation. Artifacts=$work"
} finally {
    if ($process -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    foreach ($original in $originals) {
        if ((Get-FileHash -LiteralPath $original.Path).Hash -ne $original.Hash) { throw 'Original source changed' }
    }
    Write-Output 'ORIGINALS_UNCHANGED=true'
}
