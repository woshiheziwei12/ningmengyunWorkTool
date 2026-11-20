param([Parameter(Mandatory=$true)][string]$FixturePath,[string]$RecoveredPath)
# Run with 32-bit Windows PowerShell. Independently compare DAO-created data
# through OleDb, rather than using the native reader as the test oracle.
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Data
if (!$RecoveredPath) {
    if(Test-Path -LiteralPath $FixturePath) { throw 'Fixture already exists' }
    $engine=New-Object -ComObject DAO.DBEngine.36
    $db=$null
    try {
        $db=$engine.CreateDatabase($FixturePath,';LANGID=0x0804;CP=936;COUNTRY=0',32)
        $table=$db.CreateTableDef('Types')
        for($type=1;$type -le 12;$type++) {
            $size=if($type -eq 10) { 100 } elseif($type -eq 9) { 8 } else { 0 }
            $field=$table.CreateField("Field$type",$type,$size)
            if($type -eq 4) { $field.Attributes=16 }
            if($type -in @(10,12)) { $field.AllowZeroLength=$true }
            $table.Fields.Append($field)
            [Runtime.InteropServices.Marshal]::FinalReleaseComObject($field) | Out-Null
        }
        $db.TableDefs.Append($table)
        [Runtime.InteropServices.Marshal]::FinalReleaseComObject($table) | Out-Null
        $db.Execute('CREATE UNIQUE INDEX PrimaryId ON Types (Field4) WITH PRIMARY')
        $db.Close();[Runtime.InteropServices.Marshal]::FinalReleaseComObject($db) | Out-Null;$db=$null
    } finally {
        if($db) { $db.Close();[Runtime.InteropServices.Marshal]::FinalReleaseComObject($db) | Out-Null }
        [Runtime.InteropServices.Marshal]::FinalReleaseComObject($engine) | Out-Null
    }
    $db=New-Object System.Data.OleDb.OleDbConnection("Provider=Microsoft.Jet.OLEDB.4.0;Data Source=$FixturePath")
    try {
        $db.Open()
        $cmd=$db.CreateCommand()
        $cmd.CommandText='INSERT INTO Types VALUES (?,?,?,?,?,?,?,?,?,?,?,?)'
        $types=@('Boolean','UnsignedTinyInt','SmallInt','Integer','Currency','Single','Double','Date','Binary','VarWChar','LongVarBinary','LongVarWChar')
        for($i=0;$i -lt 12;$i++) { [void]$cmd.Parameters.Add("p$i",[System.Data.OleDb.OleDbType]::$($types[$i])) }
        $chinese=([string][char]0x4e2d)+([char]0x6587)
        $blob=[byte[]](0..255)*24
        $rows=@(
            @($true,[byte]255,[int16]-32768,42,[decimal]'-123456789.1234',[single]1.234567,[double]1.23456789012345,[datetime]'2025-02-03T04:05:06',[byte[]](0,1,2,3,255,0,9,7),$chinese,$blob,($chinese*3000)),
            @($false,[DBNull]::Value,[DBNull]::Value,43,[DBNull]::Value,[DBNull]::Value,[DBNull]::Value,[DBNull]::Value,[DBNull]::Value,[DBNull]::Value,[DBNull]::Value,[DBNull]::Value),
            @($false,[byte]0,[int16]0,44,[decimal]0,[single]-1.5,[double]-98765.4321,[datetime]'1899-12-29T12:00:00',[byte[]](0,0,0,0,0,0,0,0),'',[DBNull]::Value,'')
        )
        foreach($row in $rows) {
            for($i=0;$i -lt 12;$i++) { $cmd.Parameters[$i].Value=$row[$i] }
            [void]$cmd.ExecuteNonQuery()
        }
        $cmd.Dispose()
    } finally { $db.Dispose() }
    Write-Output "CREATED=$FixturePath"
    return
}
function Read-CanonicalRows([string]$path) {
    $db=New-Object System.Data.OleDb.OleDbConnection("Provider=Microsoft.Jet.OLEDB.4.0;Data Source=$path")
    try {
        try { $db.Open() } catch { throw "Cannot open $path : $($_.Exception.Message)" }
        $cmd=$db.CreateCommand();$cmd.CommandText='SELECT * FROM Types ORDER BY Field4'
        $reader=$cmd.ExecuteReader()
        try {
            while($reader.Read()) {
                $values=@(for($i=0;$i -lt $reader.FieldCount;$i++) {
                    $value=$reader.GetValue($i)
                    if($value -is [DBNull]) { '<NULL>' }
                    elseif($value -is [byte[]]) { [Convert]::ToBase64String($value) }
                    elseif($value -is [datetime]) { $value.ToString('o',[Globalization.CultureInfo]::InvariantCulture) }
                    elseif($value -is [IFormattable]) { $value.ToString($null,[Globalization.CultureInfo]::InvariantCulture) }
                    else { [string]$value }
                })
                ConvertTo-Json -InputObject $values -Compress
            }
        } finally { $reader.Dispose();$cmd.Dispose() }
    } finally { $db.Dispose() }
}
$source=@(Read-CanonicalRows $FixturePath)
$recovered=@(Read-CanonicalRows $RecoveredPath)
if($source.Count -ne 3 -or ($source -join "`n") -cne ($recovered -join "`n")) { throw 'Independent type/null/memo/binary readback mismatch' }
Write-Output 'PASS: all 12 Jet3 types, auto IDs, null/empty, Chinese text, chained memo/OLE, negative dates and exact currency'
