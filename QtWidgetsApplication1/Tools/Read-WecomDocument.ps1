param(
    [switch]$Status,
    [string]$FromDate,
    [ValidateRange(1, 600)][int]$TimeoutSeconds = 310
)
$ErrorActionPreference = 'Stop'
$pipe = $null
$reader = $null
$writer = $null
$exitCode = 0
try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $pipeName = 'LemonWorkTool.Wecom.v1.' + $identity.User.Value
    $identity.Dispose()
    $pipe = [IO.Pipes.NamedPipeClientStream]::new('.', $pipeName, [IO.Pipes.PipeDirection]::InOut, [IO.Pipes.PipeOptions]::Asynchronous)
    try { $pipe.Connect(2000) }
    catch [UnauthorizedAccessException] { throw 'access_denied' }
    catch [TimeoutException] { throw 'tool_not_running' }
    $utf8 = [Text.UTF8Encoding]::new($false, $true)
    $writer = [IO.StreamWriter]::new($pipe, $utf8, 4096, $true)
    $reader = [IO.StreamReader]::new($pipe, $utf8, $false, 4096, $true)
    $request = @{ protocolVersion = 1; action = $(if ($Status) { 'status' } else { 'read' }) }
    if ($FromDate) { $request.fromDate = $FromDate }
    $writer.WriteLine(($request | ConvertTo-Json -Compress))
    $writer.Flush()
    # A bounded asynchronous read keeps a stalled/closed tool from hanging the caller.
    $line = [Text.StringBuilder]::new()
    $buffer = [char[]]::new(4096)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $complete = $false
    while (!$complete) {
        $remaining = [int][Math]::Ceiling(($deadline - [DateTime]::UtcNow).TotalMilliseconds)
        if ($remaining -le 0) { throw 'read_timeout' }
        $pending = $reader.ReadAsync($buffer, 0, $buffer.Length)
        if (!$pending.Wait($remaining)) { throw 'read_timeout' }
        $count = $pending.GetAwaiter().GetResult()
        if ($count -eq 0) { throw 'tool_disconnected' }
        $chunk = [string]::new($buffer, 0, $count)
        $end = $chunk.IndexOf("`n")
        if ($end -ge 0) { [void]$line.Append($chunk.Substring(0, $end)); $complete = $true }
        else { [void]$line.Append($chunk) }
        if ($line.Length -gt 67108864) { throw 'response_too_large' }
    }
    $json = $line.ToString()
    $response = $json | ConvertFrom-Json
    if ($response.protocolVersion -ne 1 -or $response.ok -isnot [bool]) { throw 'invalid_response' }
    [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
    [Console]::WriteLine($json)
    if (!$response.ok) { $exitCode = 2 }
} catch {
    $code = $_.Exception.Message
    if ($code -notin @('tool_not_running','access_denied','read_timeout','tool_disconnected','response_too_large','invalid_response')) { $code = 'connection_failed' }
    $message = switch ($code) {
        'tool_not_running' { 'Open the updated desktop tool and enter its workspace, then retry.' }
        'access_denied' { 'Run the caller in the same Windows user context as the desktop tool; a restricted sandbox may need permission to connect.' }
        'read_timeout' { 'Timed out. Check the document progress in the desktop tool before retrying.' }
        'tool_disconnected' { 'The desktop tool disconnected before returning data.' }
        default { 'Unable to read the desktop tool response.' }
    }
    [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
    [Console]::WriteLine((@{ protocolVersion=1; ok=$false; error=@{ code=$code; message=$message } } | ConvertTo-Json -Compress))
    $exitCode = 2
} finally {
    # Close the pipe first to cancel a pending asynchronous read after timeout.
    if ($pipe) { $pipe.Dispose() }
    if ($reader) { try { $reader.Dispose() } catch {} }
    if ($writer) { try { $writer.Dispose() } catch {} }
}
exit $exitCode
