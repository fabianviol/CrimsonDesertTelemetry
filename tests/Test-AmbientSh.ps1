# Synthetic/offline only. Never starts, modifies or instruments the game.
#requires -Version 7.4
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot '../scripts/AmbientSh.psm1') -Force
$script:checks=0
function Near([double]$actual,[double]$expected,[string]$name) {
    if (-not [double]::IsFinite($actual) -or
        [Math]::Abs($actual-$expected) -gt 2e-6*[Math]::Max(1.0,[Math]::Abs($expected))) {
        throw "$name : actual=$actual expected=$expected"
    }
    $script:checks++
}
function Reject([scriptblock]$body,[string]$name) {
    $rejected=$false
    try { & $body | Out-Null } catch { $rejected=$true }
    if (-not $rejected) { throw "Accepted invalid input: $name" }
    $script:checks++
}
function New-Rows {
    return ,@(for($r=0;$r -lt 64;$r++) {
        [pscustomobject]@{row=$r;values=@(0.0,0.0,0.0,0.0)}
    })
}
# Independently project 256 samples using the shader's hemisphere and basis,
# then compare decoder results with direct (non-SH) sample sums.
$m=@(@(0.61312,0.33951,0.04737),@(0.07020,0.91636,0.01345),@(0.02062,0.10958,0.86980))
foreach($mode in @('black','uniform','directional','hdr')) {
    $coeff=[double[,]]::new(3,9)
    $sum=[double[]]::new(3);$sumY=[double[]]::new(3);$preSum=[double[]]::new(3)
    for($i=0;$i -lt 256;$i++) {
        $reversed=0
        for($b=0;$b -lt 8;$b++) { $reversed=($reversed -shl 1) -bor (($i -shr $b) -band 1) }
        $y=1.0-$i/256.0; $radial=[Math]::Sqrt(1-$y*$y)
        $x=$radial*[Math]::Cos(2*[Math]::PI*$reversed/256.0)
        $z=$radial*[Math]::Sin(2*[Math]::PI*$reversed/256.0)
        $pre=switch($mode) {
            'black' { @(0.0,0.0,0.0) }
            'uniform' { @(0.2,1.0,3.0) }
            'directional' { @((0.2+2*$y), (0.3+($x+1)*0.4), (0.1+($z+1)*0.2)) }
            'hdr' { @(100.0,20.0,1.0) }
        }
        $basis=@(0.282095,(-0.488603*$y),(0.488603*$z),(-0.488603*$x),
            (1.092548*$x*$y),(-1.092548*$y*$z),(0.9461759328842163*$z*$z-0.31539198756217957),
            (-1.092548*$x*$z),(0.546274*($x*$x-$y*$y)))
        for($channel=0;$channel -lt 3;$channel++) {
            $color=0.0
            for($k=0;$k -lt 3;$k++) { $color += [double][single]$m[$channel][$k]*$pre[$k] }
            $sum[$channel]+=$color; $sumY[$channel]+=$color*$y; $preSum[$channel]+=$pre[$channel]
            for($k=0;$k -lt 9;$k++) { $coeff[$channel,$k]+=$color*$basis[$k]/128.0 }
        }
    }
    $rows=New-Rows
    for($channel=0;$channel -lt 3;$channel++) {
        for($k=0;$k -lt 8;$k++) {
            $row=2*$channel+[int][Math]::Floor($k/4.0)
            $rows[$row].values[$k%4]=[single]$coeff[$channel,$k]
        }
        $rows[6].values[$channel]=[single]$coeff[$channel,8]
    }
    $result=ConvertFrom-AmbientShRows $rows
    for($channel=0;$channel -lt 3;$channel++) {
        Near $result.upperHemisphereMeanWorking[$channel] ($sum[$channel]/256.0) "$mode mean channel $channel"
        Near $result.upwardIrradianceOverPiWorking[$channel] ($sumY[$channel]/128.0) "$mode cosine channel $channel"
        Near $result.inverseMatrixMean[$channel] ($preSum[$channel]/256.0) "$mode matrix inverse channel $channel"
        for($k=0;$k -lt 9;$k++) {
            Near $result.coefficientsWorking[$channel][$k] ([single]$coeff[$channel,$k]) "$mode coefficient $channel/$k"
        }
    }
    # Unknown/non-SH rows and W must never get mixed into the color/brightness.
    $rows[6].values[3]=12345.0
    $rows[7].values=@(99999.0,1.0,2.0,3.0)
    $rows[56].values=@(9.0,8.0,7.0,6.0)
    $second=ConvertFrom-AmbientShRows $rows
    for($c=0;$c -lt 3;$c++) {
        Near $second.upperHemisphereMeanWorking[$c] $result.upperHemisphereMeanWorking[$c] 'non-SH isolation'
    }
}
$bad=New-Rows
Reject { ConvertFrom-AmbientShRows $bad[0..62] } 'short rows'
$bad[1].row=7; Reject { ConvertFrom-AmbientShRows $bad } 'misordered rows'; $bad[1].row=1
$bad[0].values=@(1.0,2.0); Reject { ConvertFrom-AmbientShRows $bad } 'short row'; $bad=New-Rows
foreach($invalid in @([double]::NaN,[double]::PositiveInfinity,'1',$null,$true)) {
    $bad[1].values[2]=$invalid; Reject { ConvertFrom-AmbientShRows $bad } 'invalid numeric lane'
}
$bad=New-Rows;$bad[0].values[0]=-1.0;Reject { ConvertFrom-AmbientShRows $bad } 'negative DC'
$bad=New-Rows;$bad[0].values[1]=1.0;Reject { ConvertFrom-AmbientShRows $bad } 'wrong hemisphere sign'

# Binary integration validates provenance first and creates outputs exclusively.
$fixtureDir=Join-Path $PSScriptRoot ('../artifacts/tests/ambient-sh-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixtureDir | Out-Null
$binary=[byte[]]::new(3904)
function Put-U32([int]$offset,[uint32]$value) { [BitConverter]::GetBytes($value).CopyTo($binary,$offset) }
Put-U32 0 0x41445443;Put-U32 4 1;Put-U32 8 3904;Put-U32 12 99
Put-U32 16 1;Put-U32 20 0x3849BB7;Put-U32 24 7;Put-U32 28 1
foreach($offset in @(32,40,48,56)) { [BitConverter]::GetBytes([uint64]0x123450).CopyTo($binary,$offset) }
Put-U32 (64+0x20) 1
[BitConverter]::GetBytes([single]6360000).CopyTo($binary,64+0xAC0)
$fixture=Join-Path $fixtureDir 'synthetic.bin'
[IO.File]::WriteAllBytes($fixture,$binary)
$reader=Join-Path $PSScriptRoot '../scripts/Read-AmbientProbe.ps1'
$readerDefault=& $reader -Path $fixture
Near $readerDefault.Samples 1 'legacy reader summary'
Near $readerDefault.FirstRawRows.Count 8 'legacy reader row preview'
$readerFull=& $reader -Path $fixture -PassThru
Near $readerFull.sampleCount 1 'full reader sample count'
if ($readerFull.capturedPrefixSha256 -ne (Get-FileHash -LiteralPath $fixture).Hash) {
    throw 'Reader source digest mismatch.'
}
$script:checks++
$decoder=Join-Path $PSScriptRoot '../scripts/Decode-AmbientProbe.ps1'
Reject { & $decoder -Path $fixture } 'missing explicit assumption'
$output=Join-Path $fixtureDir 'synthetic-decoded.json'
$decoded=& $decoder -Path $fixture -AssumeCsPrecomputeAmbientLayout -OutFile $output
Near $decoded.samples 1 'binary sample count'
Near $decoded.first.rec709MeanLuminanceEstimate 0 'black luminance'
$saved=Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
if ($saved.isPublicApi -ne $false -or $saved.runtimeShaderIdentityVerified -ne $false) {
    throw 'Candidate output overstates provenance.'
}
$script:checks++
$savedHash=(Get-FileHash -LiteralPath $output).Hash
Reject { & $decoder -Path $fixture -AssumeCsPrecomputeAmbientLayout -OutFile $output } 'overwrite'
if ((Get-FileHash -LiteralPath $output).Hash -ne $savedHash) { throw 'Existing export changed.' }
$script:checks++
Put-U32 20 0x384CBA3
$other=Join-Path $fixtureDir 'synthetic-other-producer.bin';[IO.File]::WriteAllBytes($other,$binary)
Reject { & $decoder -Path $other -AssumeCsPrecomputeAmbientLayout } 'other producer'
Put-U32 20 0x3849BB7;Put-U32 24 3
$torn=Join-Path $fixtureDir 'synthetic-invalid-flags.bin';[IO.File]::WriteAllBytes($torn,$binary)
Reject { & $decoder -Path $torn -AssumeCsPrecomputeAmbientLayout } 'unfenced provenance'
Write-Output "PASS $script:checks synthetic/offline checks. Fixtures: $fixtureDir"
