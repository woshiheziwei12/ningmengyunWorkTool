param([Parameter(Mandatory=$true)][string]$JsonPath,[Parameter(Mandatory=$true)][string]$OutputPath,[switch]$VerifyOnly)
$ErrorActionPreference='Stop'
if (!$VerifyOnly -and (Test-Path -LiteralPath $OutputPath)) { throw 'Output already exists; refusing overwrite' }
$data=Get-Content -LiteralPath $JsonPath -Raw -Encoding UTF8 | ConvertFrom-Json
Add-Type -ReferencedAssemblies System.Data,System.Web.Extensions -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Data;
using System.Data.OleDb;
using System.Globalization;
using System.Collections.Generic;
using System.Web.Script.Serialization;
using System.Security.Cryptography;
public static class JetRecoveryData {
 static CultureInfo Inv=CultureInfo.InvariantCulture;
 static string Q(string name) { return "["+name.Replace("]","]]")+"]"; }
 static Dictionary<string,object> Obj(object o) { return (Dictionary<string,object>)o; }
 static object[] Arr(object o) { return o is object[] ? (object[])o : ((System.Collections.ArrayList)o).ToArray(); }
 static Dictionary<string,object> Load(string path) {
  var serializer=new JavaScriptSerializer(); serializer.MaxJsonLength=128*1024*1024;
  return serializer.Deserialize<Dictionary<string,object>>(File.ReadAllText(path,Encoding.UTF8));
 }
 static object Value(object raw,int type) {
  if(raw==null) return DBNull.Value;
  switch(type) {
   case 1:return Convert.ToBoolean(raw,Inv); case 2:return Convert.ToByte(raw,Inv);
   case 3:return Convert.ToInt16(raw,Inv); case 4:return Convert.ToInt32(raw,Inv);
   case 5:return Decimal.Parse((string)raw,Inv); case 6:return Single.Parse((string)raw,Inv);
   case 7:case 8:return Double.Parse((string)raw,Inv);
   case 9:case 11:return Convert.FromBase64String((string)raw);
   case 10:case 12:return (string)raw;
   default:throw new Exception("Unsupported column type "+type);
  }
 }
 static OleDbType DbType(int type) {
  switch(type) {
   case 1:return OleDbType.Boolean;case 2:return OleDbType.UnsignedTinyInt;
   case 3:return OleDbType.SmallInt;case 4:return OleDbType.Integer;
   case 5:return OleDbType.Currency;case 6:return OleDbType.Single;
   case 7:case 8:return OleDbType.Double;
   case 9:return OleDbType.VarBinary;case 11:return OleDbType.LongVarBinary;
   case 10:return OleDbType.VarWChar;case 12:return OleDbType.LongVarWChar;
   default:throw new Exception("Unsupported parameter type");
  }
 }
 static OleDbConnection Open(string path) {
  var builder=new OleDbConnectionStringBuilder();builder.Provider="Microsoft.Jet.OLEDB.4.0";
  builder.DataSource=path;var connection=new OleDbConnection(builder.ConnectionString);connection.Open();return connection;
 }
 public static int Import(string path,string json) {
  int total=0;
  using(var db=Open(path)) using(var tx=db.BeginTransaction()) {
   foreach(var rawTable in Arr(Load(json)["tables"])) {
    var table=Obj(rawTable);var columns=Arr(table["columns"]);var names=new List<string>();var marks=new List<string>();
    using(var cmd=db.CreateCommand()) {
     cmd.Transaction=tx;
     for(int c=0;c<columns.Length;c++) {
      var col=Obj(columns[c]);int type=Convert.ToInt32(col["type"]);names.Add(Q((string)col["name"]));marks.Add("?");
      var p=cmd.Parameters.Add("p"+c,DbType(type));
      if(type==10 || type==9) p.Size=Math.Max(1,Convert.ToInt32(col["size"]));
     }
     cmd.CommandText="INSERT INTO "+Q((string)table["name"])+" ("+String.Join(",",names)+") VALUES ("+String.Join(",",marks)+")";
     int row=0;
     foreach(var rawRow in Arr(table["records"])) {
      var record=Arr(rawRow);row++;
      for(int c=0;c<columns.Length;c++) {
       int type=Convert.ToInt32(Obj(columns[c])["type"]);
       var value=Value(record[c],type);cmd.Parameters[c].Value=value;
       if(type==11) cmd.Parameters[c].Size=value==DBNull.Value?1:Math.Max(1,((byte[])value).Length);
       if(type==12) cmd.Parameters[c].Size=value==DBNull.Value?1:Math.Max(1,((string)value).Length);
      }
      try { if(cmd.ExecuteNonQuery()!=1) throw new Exception("Unexpected affected-row count"); }
      catch(Exception ex) { throw new Exception("Insert failed table="+table["name"]+" row="+row+": "+ex.Message,ex); }
      total++;
     }
    }
   }
   tx.Commit();
  }
  return total;
 }
 static string RowHash(object[] values,object[] columns) {
  using(var stream=new MemoryStream()) using(var writer=new BinaryWriter(stream,Encoding.UTF8)) {
   for(int i=0;i<values.Length;i++) {
    var value=values[i];bool empty=value==null || value==DBNull.Value;writer.Write(empty);
    if(empty) continue;
    int type=Convert.ToInt32(Obj(columns[i])["type"]);
    switch(type) {
     case 1:writer.Write(Convert.ToBoolean(value,Inv));break;
     case 2:case 3:case 4:writer.Write(Convert.ToInt32(value,Inv));break;
     case 5:writer.Write(Convert.ToDecimal(value,Inv).ToString("G29",Inv));break;
     case 6:writer.Write(Convert.ToSingle(value,Inv));break;
     case 7:case 8:writer.Write(Convert.ToDouble(value,Inv));break;
     case 9:case 11:var bytes=(byte[])value;writer.Write(bytes.Length);writer.Write(bytes);break;
     case 10:case 12:writer.Write((string)value);break;
    }
   }
   writer.Flush();using(var sha=SHA256.Create()) return Convert.ToBase64String(sha.ComputeHash(stream.ToArray()));
  }
 }
 public static int Verify(string path,string json) {
  int total=0;
  using(var db=Open(path)) {
   foreach(var rawTable in Arr(Load(json)["tables"])) {
    var table=Obj(rawTable);var columns=Arr(table["columns"]);var names=new List<string>();var counts=new Dictionary<string,int>();
    foreach(var rawRow in Arr(table["records"])) {
     var row=Arr(rawRow);var values=new object[row.Length];
     for(int c=0;c<row.Length;c++) values[c]=Value(row[c],Convert.ToInt32(Obj(columns[c])["type"]));
     string hash=RowHash(values,columns);counts[hash]=counts.ContainsKey(hash)?counts[hash]+1:1;
    }
    var positions=new int[columns.Length];var nullPositions=new int[columns.Length];
    for(int c=0;c<columns.Length;c++) {
     var col=Obj(columns[c]);var name=Q((string)col["name"]);
     // CDbl preserves the stored OLE date's full precision, avoiding DateTime rounding.
     positions[c]=names.Count;nullPositions[c]=-1;
     if(Convert.ToInt32(col["type"])==8) {
      names.Add("CDbl(IIF(IsNull("+name+"),0,"+name+")) AS [value"+c+"]");
      nullPositions[c]=names.Count;names.Add("IsNull("+name+") AS [null"+c+"]");
     } else names.Add(name);
    }
    using(var cmd=db.CreateCommand()) {
     cmd.CommandText="SELECT "+String.Join(",",names)+" FROM "+Q((string)table["name"]);
     using(var reader=cmd.ExecuteReader()) {
      while(reader.Read()) {
       var values=new object[columns.Length];
       for(int c=0;c<values.Length;c++) {
        try { values[c]=nullPositions[c]>=0 && Convert.ToBoolean(reader.GetValue(nullPositions[c]),Inv) ? DBNull.Value : reader.GetValue(positions[c]); }
        catch(Exception ex) { throw new Exception("Readback failed table="+table["name"]+" column="+Obj(columns[c])["name"]+": "+ex.Message,ex); }
       }
       string hash=RowHash(values,columns);
       if(!counts.ContainsKey(hash) || counts[hash]==0) throw new Exception("Readback mismatch in table "+table["name"]);
       counts[hash]--;total++;
      }
     }
    }
    foreach(var count in counts.Values) if(count!=0) throw new Exception("Missing recovered records in "+table["name"]);
   }
  }
  return total;
 }
}
'@
if($VerifyOnly) { $count=[JetRecoveryData]::Verify($OutputPath,$JsonPath); "VERIFIED_ROWS=$count";return }
$engine=New-Object -ComObject DAO.DBEngine.36
$db=$null
$warnings=New-Object System.Collections.Generic.List[string]
try {
 $db=$engine.CreateDatabase($OutputPath,';LANGID=0x0804;CP=936;COUNTRY=0',64)
 foreach($table in $data.tables) {
  $definition=$db.CreateTableDef([string]$table.name)
  foreach($column in $table.columns) {
   $field=$definition.CreateField([string]$column.name,[int]$column.type,[int]$column.size)
   if($column.auto) { $field.Attributes=$field.Attributes -bor 16 }
   if($column.type -in @(10,12)) { $field.AllowZeroLength=$true }
   $definition.Fields.Append($field)
   [Runtime.InteropServices.Marshal]::FinalReleaseComObject($field) | Out-Null
  }
  $db.TableDefs.Append($definition)
  [Runtime.InteropServices.Marshal]::FinalReleaseComObject($definition) | Out-Null
 }
 $db.Close();[Runtime.InteropServices.Marshal]::FinalReleaseComObject($db) | Out-Null;$db=$null
 $rows=[JetRecoveryData]::Import($OutputPath,$JsonPath)
 $verified=[JetRecoveryData]::Verify($OutputPath,$JsonPath)
 $db=$engine.OpenDatabase($OutputPath,$false,$false)
 $indexCount=0
 foreach($table in $data.tables) {
  $definition=$db.TableDefs.Item([string]$table.name)
  foreach($index in $table.indexes) {
   if($index.type -eq 2) { continue }
   if($index.keys.Count -eq 0) { $warnings.Add("Unresolved source index: $($table.name)/$($index.name)");continue }
   $destinationIndex=$definition.CreateIndex([string]$index.name)
   $destinationIndex.Primary=($index.type -eq 1)
   $destinationIndex.Unique=([bool]($index.flags -band 1))
   $destinationIndex.IgnoreNulls=([bool]($index.flags -band 2))
   $destinationIndex.Required=([bool]($index.flags -band 8))
   foreach($key in $index.keys) {
    $field=$destinationIndex.CreateField([string]$table.columns[[int]$key.column-1].name)
    if($key.order -eq 1) { $field.Attributes=1 }
    $destinationIndex.Fields.Append($field)
    [Runtime.InteropServices.Marshal]::FinalReleaseComObject($field) | Out-Null
   }
   try { $definition.Indexes.Append($destinationIndex);$indexCount++ }
   catch { $warnings.Add("Index restore failed: $($table.name)/$($index.name): $($_.Exception.Message)") }
   [Runtime.InteropServices.Marshal]::FinalReleaseComObject($destinationIndex) | Out-Null
  }
  [Runtime.InteropServices.Marshal]::FinalReleaseComObject($definition) | Out-Null
 }
 $db.Close();[Runtime.InteropServices.Marshal]::FinalReleaseComObject($db) | Out-Null;$db=$null
 $verified=[JetRecoveryData]::Verify($OutputPath,$JsonPath)
 $report=@{ Output=$OutputPath;Tables=$data.tables.Count;Rows=$rows;VerifiedRows=$verified;Indexes=$indexCount;Warnings=@($warnings.ToArray());Scope='Data tables; excludes original workgroup ACL, saved queries/forms and relationship constraints' }
 $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($OutputPath+'.validation.json') -Encoding UTF8
 Write-Output "PASS tables=$($data.tables.Count) rows=$rows verified=$verified indexes=$indexCount warnings=$($warnings.Count)"
} finally {
 if($db) { $db.Close();[Runtime.InteropServices.Marshal]::FinalReleaseComObject($db) | Out-Null }
 [Runtime.InteropServices.Marshal]::FinalReleaseComObject($engine) | Out-Null
}
