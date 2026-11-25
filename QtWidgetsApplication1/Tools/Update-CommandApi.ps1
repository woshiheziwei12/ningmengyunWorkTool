# Requires PowerShell 7. Regenerates the command reference from the built CLI.
param(
    [Parameter(Mandatory)][string]$CliPath,
    [switch]$Check
)
$ErrorActionPreference = 'Stop'
$cli = (Resolve-Path -LiteralPath $CliPath).Path
$helpResponse = & $cli help | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or !$helpResponse.ok -or !$helpResponse.data.commandDetails) {
    throw 'CLI did not return the command definitions.'
}

$lines = [Collections.Generic.List[string]]::new()
foreach ($command in $helpResponse.data.commandDetails) {
    $lines.Add('### `' + $command.name + '`')
    $lines.Add('')
    $lines.Add($command.summary)
    $lines.Add('')
    $lines.Add('`' + $command.usage + '`')
    $lines.Add('')
    foreach ($option in $command.options.PSObject.Properties) {
        $lines.Add('- `' + $option.Name + '` ' + $option.Value)
    }
    $lines.Add('')
    $lines.Add('```powershell')
    foreach ($example in $command.examples) { $lines.Add($example) }
    $lines.Add('```')
    $lines.Add('')
}

$manual = Join-Path $PSScriptRoot '../docs/command-api.md'
$text = [IO.File]::ReadAllText($manual).Replace("`r`n", "`n")
$start = '<!-- BEGIN CLI REFERENCE -->'
$end = '<!-- END CLI REFERENCE -->'
$pattern = [regex]::Escape($start) + '(?s:.*?)' + [regex]::Escape($end)
if ([regex]::Matches($text, $pattern).Count -ne 1) { throw 'Expected one command reference block.' }
$reference = $start + "`n`n" + ($lines -join "`n") + "`n" + $end
$updated = [regex]::Replace($text, $pattern, { param($match) $reference })
if ($Check -and $updated -cne $text) { throw 'Command API manual is out of date. Run this script without -Check.' }
if (!$Check -and $updated -cne $text) { [IO.File]::WriteAllText($manual, $updated) }
