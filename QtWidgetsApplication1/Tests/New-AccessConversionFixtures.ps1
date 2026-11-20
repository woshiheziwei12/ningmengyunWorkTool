param([Parameter(Mandatory=$true)][string]$OutputDirectory)
# Run with 32-bit Windows PowerShell: DAO 3.6 is a 32-bit COM server.
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$engine = New-Object -ComObject DAO.DBEngine.36
try {
    foreach ($entry in @(
        @{ Name='plain-jet3.mdb'; Locale=';LANGID=0x0409;CP=1252;COUNTRY=0'; Format=32 },
        @{ Name='protected-jet3.mdb'; Locale=';LANGID=0x0409;CP=1252;COUNTRY=0;pwd=Fixture123'; Format=32 },
        @{ Name='compatible-jet4.mdb'; Locale=';LANGID=0x0409;CP=1252;COUNTRY=0'; Format=64 }
    )) {
        $path = Join-Path $OutputDirectory $entry.Name
        if (Test-Path -LiteralPath $path) { throw "Fixture already exists: $path" }
        $database = $engine.CreateDatabase($path, $entry.Locale, $entry.Format)
        try {
            $database.Execute('CREATE TABLE RegressionData (ID INTEGER, Label TEXT(32))')
            $database.Execute("INSERT INTO RegressionData VALUES (1, 'conversion-fixture')")
        } finally {
            $database.Close()
            [Runtime.InteropServices.Marshal]::FinalReleaseComObject($database) | Out-Null
        }
        Write-Output "CREATED=$path"
    }
} finally {
    [Runtime.InteropServices.Marshal]::FinalReleaseComObject($engine) | Out-Null
}
