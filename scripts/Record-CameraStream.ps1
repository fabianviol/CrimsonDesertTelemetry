param(
    [ValidateRange(2,60)][int]$Seconds = 20,
    [ValidateRange(1024,65535)][int]$Port = 27311,
    [Parameter(Mandatory=$true)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$destination = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $destination) { throw 'Refusing to overwrite an existing recording.' }
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
$socket = [Net.WebSockets.ClientWebSocket]::new()
$cancellation = [Threading.CancellationTokenSource]::new([TimeSpan]::FromSeconds($Seconds + 5))
$rows = [Collections.Generic.List[object]]::new()
$writer = $null
try {
    $null = $socket.ConnectAsync([Uri]"ws://127.0.0.1:$Port/v1/stream", $cancellation.Token).GetAwaiter().GetResult()
    $writer = [IO.StreamWriter]::new([IO.FileStream]::new($destination, [IO.FileMode]::CreateNew), [Text.UTF8Encoding]::new($false))
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $buffer = [byte[]]::new(65536)
    Write-Output "RECORDING for $Seconds seconds: camera stream only; no extra memory scan."
    while ($clock.Elapsed.TotalSeconds -lt $Seconds) {
        $offset = 0
        do {
            $segment = [ArraySegment[byte]]::new($buffer, $offset, $buffer.Length - $offset)
            $received = $socket.ReceiveAsync($segment, $cancellation.Token).GetAwaiter().GetResult()
            if ($received.MessageType -ne [Net.WebSockets.WebSocketMessageType]::Text) { throw 'Expected text telemetry.' }
            $offset += $received.Count
            if ($offset -ge $buffer.Length -and -not $received.EndOfMessage) { throw 'Message too large.' }
        } while (-not $received.EndOfMessage)
        $text = [Text.Encoding]::UTF8.GetString($buffer, 0, $offset)
        $now = [DateTimeOffset]::UtcNow
        # Preserve the server's original timestamp/JSON verbatim inside a bounded wrapper.
        $writer.WriteLine('{"receivedAt":"' + $now.ToString('o') + '","elapsedMs":' +
            $clock.Elapsed.TotalMilliseconds.ToString('F3', [Globalization.CultureInfo]::InvariantCulture) + ',"snapshot":' + $text + '}')
        $sample = $text | ConvertFrom-Json
        $yaw = if ($null -ne $sample.camera) { [Math]::Atan2($sample.camera.forward.x, $sample.camera.forward.z) * 180 / [Math]::PI } else { $null }
        $rows.Add([pscustomobject]@{ Seq=[long]$sample.sequence; Ms=$clock.Elapsed.TotalMilliseconds; Yaw=$yaw;
            Age=($now - [DateTimeOffset]$sample.capturedAt).TotalMilliseconds;
            State=$sample.game.state; Consensus=$sample.quality.consensusCopies; Valid=$sample.quality.validCopies;
            Distinct=$sample.quality.distinctStates; CaptureUs=$sample.quality.captureDurationMicroseconds })
    }
} finally {
    if ($writer) { $writer.Dispose() }
    $socket.Abort(); $socket.Dispose(); $cancellation.Dispose()
}
$steps = for ($i=1; $i -lt $rows.Count; $i++) {
    $a=$rows[$i-1]; $b=$rows[$i]
    if ($null -eq $a.Yaw -or $null -eq $b.Yaw) { continue }
    $delta = (($b.Yaw - $a.Yaw + 540) % 360) - 180
    [pscustomobject]@{ Seq=$b.Seq; Ms=[Math]::Round($b.Ms,1); Delta=[Math]::Round($delta,4);
        Age=[Math]::Round($b.Age,1); Consensus=$b.Consensus; Distinct=$b.Distinct }
}
[pscustomobject]@{
    Samples=$rows.Count; Output=$destination;
    PositiveSteps=@($steps | Where-Object Delta -gt 0.05).Count;
    NegativeSteps=@($steps | Where-Object Delta -lt -0.05).Count;
    RepeatedSteps=@($steps | Where-Object { [Math]::Abs($_.Delta) -lt 0.0001 }).Count;
    AgeMilliseconds=($rows | Measure-Object Age -Average -Minimum -Maximum);
    LargestJumps=@($steps | Sort-Object { [Math]::Abs($_.Delta) } -Descending | Select-Object -First 12)
} | ConvertTo-Json -Depth 5
