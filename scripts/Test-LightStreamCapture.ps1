# Offline checks of Capture-LightStreams.ps1 evidence; no game access.
#requires -Version 7.4
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Directory)
$ErrorActionPreference='Stop'
function Read-Trace([string]$name) {
    $path=Join-Path $Directory $name
    if ((Get-Item -LiteralPath $path).Length -gt 128MB) { throw 'Trace exceeds capture bound.' }
    @([IO.File]::ReadLines((Resolve-Path -LiteralPath $path).Path) | ForEach-Object { $_ | ConvertFrom-Json })
}
$raw=@(Read-Trace 'raw.jsonl'); $smoothed=@(Read-Trace 'smoothed.jsonl')
$before=Get-Content -LiteralPath (Join-Path $Directory 'before.json') -Raw | ConvertFrom-Json
$after=Get-Content -LiteralPath (Join-Path $Directory 'after.json') -Raw | ConvertFrom-Json
$rawBySequence=@{}
foreach($message in $raw) { if($message.lights.rendered.status -eq 'available') { $rawBySequence[[string]$message.lights.rendered.captureSequence]=$message.lights.rendered } }
$unique=@($smoothed | Where-Object status -eq available | Group-Object sourceCaptureSequence | ForEach-Object { $_.Group[0] } | Sort-Object sourceCaptureSequence)
if ($unique.Count -lt 2 -or $after.snapshot.sequence -le $before.snapshot.sequence) { throw 'No progressing control.' }
$matched=0; $memberChecks=0; $emaChecks=0; $repeatChecks=0
$violations=[Collections.Generic.List[string]]::new()
function Check([bool]$ok,[string]$message) { if(-not $ok -and $violations.Count -lt 30) { $violations.Add($message) } }
function Close-Number([double]$a,[double]$b) { [Math]::Abs($a-$b) -le 0.000003*[Math]::Max(1,[Math]::Max([Math]::Abs($a),[Math]::Abs($b))) }
function Distance-Squared($a,$b) { [Math]::Pow($a.x-$b.x,2)+[Math]::Pow($a.y-$b.y,2)+[Math]::Pow($a.z-$b.z,2) }
$previous=$null
foreach($sample in $smoothed) {
    if($sample.status -ne 'available') { $previous=$null; continue }
    Check ($sample.ageMilliseconds -ge 0 -and $sample.ageMilliseconds -le 500) 'Invalid available age'
    if($previous -and $previous.sourceCaptureSequence -eq $sample.sourceCaptureSequence) {
        Check (($previous.sources | ConvertTo-Json -Depth 9 -Compress) -ceq ($sample.sources | ConvertTo-Json -Depth 9 -Compress)) 'Repeated capture changed groups/values'
        $repeatChecks++
    }
    $previous=$sample
}
$previous=$null
foreach($sample in $unique) {
    $source=$rawBySequence[[string]$sample.sourceCaptureSequence]
    if(-not $source) { $previous=$sample; continue }
    $matched++
    $members=@($sample.sources | ForEach-Object { $_.contributions })
    Check ($source.sources.Count -eq $members.Count) 'Raw/group contribution count mismatch'
    Check (@($members.sampleIndex | Sort-Object -Unique).Count -eq $members.Count) 'Double-counted member'
    $sourceMap=@{}; foreach($entry in $source.sources) { $sourceMap[[int]$entry.sampleIndex]=$entry }
    foreach($member in $members) {
        $memberChecks++
        Check (($member | ConvertTo-Json -Depth 6 -Compress) -ceq ($sourceMap[[int]$member.sampleIndex] | ConvertTo-Json -Depth 6 -Compress)) 'Raw member changed or absent'
    }
    $old=@{}; if($previous) { foreach($group in $previous.sources) { $old[$group.trackingId]=$group } }
    foreach($group in $sample.sources) {
        foreach($axis in @('x','y','z')) {
            $sum=($group.contributions | ForEach-Object {$_.colorLinear.$axis} | Measure-Object -Sum).Sum
            Check (Close-Number $sum $group.rawSumColorLinear.$axis) 'Linear sum mismatch'
        }
        $luma=$group.colorLinear.x*.212671+$group.colorLinear.y*.71516+$group.colorLinear.z*.07216
        Check (Close-Number $luma $group.luminanceLinear) 'Luminance mismatch'
        if($previous -and $sample.sourceCaptureSequence -eq $previous.sourceCaptureSequence+1 -and $old.ContainsKey($group.trackingId)) {
            $dt=([DateTimeOffset]$sample.capturedAt-[DateTimeOffset]$previous.capturedAt).TotalMilliseconds
            $alpha=if($sample.settings.timeConstantMilliseconds -eq 0){1}else{1-[Math]::Exp(-$dt/$sample.settings.timeConstantMilliseconds)}
            foreach($axis in @('x','y','z')) {
                $expected=$old[$group.trackingId].colorLinear.$axis+$alpha*($group.rawSumColorLinear.$axis-$old[$group.trackingId].colorLinear.$axis)
                Check (Close-Number $expected $group.colorLinear.$axis) 'EMA mismatch'
                $emaChecks++
            }
        }
    }
    $previous=$sample
}
# Select the two nearest initial groups, then independently follow position
# (not trackingId) to expose unexpected identity churn of static fixtures.
$targets=@($unique[0].sources | Sort-Object { Distance-Squared $_.position $before.snapshot.player.position } | Select-Object -First 2)
$targetStats=foreach($target in $targets) {
    $observations=@(foreach($sample in $unique) {
        $group=$sample.sources | Where-Object { (Distance-Squared $_.position $target.position) -lt .04 } | Sort-Object { Distance-Squared $_.position $target.position } | Select-Object -First 1
        if($group) { [pscustomobject]@{id=$group.trackingId; contributions=$group.contributions.Count; raw=($group.rawSumColorLinear.x*.212671+$group.rawSumColorLinear.y*.71516+$group.rawSumColorLinear.z*.07216); smooth=$group.luminanceLinear; indices=($group.contributions.sampleIndex -join ',')} }
    })
    $rawVariation=0.0; $smoothVariation=0.0
    for($i=1;$i -lt $observations.Count;$i++) { $rawVariation += [Math]::Abs($observations[$i].raw-$observations[$i-1].raw); $smoothVariation += [Math]::Abs($observations[$i].smooth-$observations[$i-1].smooth) }
    [pscustomobject]@{position=$target.position; observedCaptures=$observations.Count; trackingIds=@($observations.id | Sort-Object -Unique).Count; contributionCounts=@($observations.contributions | Sort-Object -Unique); differentIndexSets=@($observations.indices | Sort-Object -Unique).Count; rawLuminance=($observations.raw | Measure-Object -Minimum -Maximum -Average | Select-Object Minimum,Maximum,Average); smoothedLuminance=($observations.smooth | Measure-Object -Minimum -Maximum -Average | Select-Object Minimum,Maximum,Average); rawTotalVariation=$rawVariation; smoothedTotalVariation=$smoothVariation; variationReductionPercent=if($rawVariation -gt 0){100*(1-$smoothVariation/$rawVariation)}else{$null}}
}
$report=[ordered]@{rawMessages=$raw.Count;smoothedMessages=$smoothed.Count;unavailableMessages=@($smoothed | Where-Object status -ne available).Count;uniqueCaptures=$unique.Count;matchedRawCaptures=$matched;memberChecks=$memberChecks;emaLaneChecks=$emaChecks;repeatedCaptureChecks=$repeatChecks;age=($smoothed.ageMilliseconds | Measure-Object -Minimum -Maximum -Average | Select-Object Minimum,Maximum,Average);groupCounts=($unique | ForEach-Object {$_.sources.Count} | Measure-Object -Minimum -Maximum | Select-Object Minimum,Maximum);playerDisplacementSquared=(Distance-Squared $before.snapshot.player.position $after.snapshot.player.position);targets=@($targetStats);violations=@($violations)}
$text=$report | ConvertTo-Json -Depth 9
$file=[IO.File]::Open((Join-Path $Directory 'validation.json'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
try { $bytes=[Text.Encoding]::UTF8.GetBytes($text); $file.Write($bytes) } finally { $file.Dispose() }
$text
if($violations.Count) { throw 'Live stream invariants failed; inspect preserved validation.json.' }
