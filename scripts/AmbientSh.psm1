# Private offline research math, NOT a public API or an automatic layout detector.
# Derivation and limitations: docs/AMBIENT_DECODE.md. No live game access.
#requires -Version 7.4
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'

$script:Y00 = [double][single]0.282095
$script:Y1 = [double][single]0.488603
# Exact float32 constants from the inspected csPrecomputeAmbient DXIL, in row order.
$script:ColorMatrix = @(
    @([double][single]0.61312, [double][single]0.33951, [double][single]0.04737),
    @([double][single]0.07020, [double][single]0.91636, [double][single]0.01345),
    @([double][single]0.02062, [double][single]0.10958, [double][single]0.86980)
)

function Undo-AmbientColorMatrix {
    param([double[]]$Color)
    # Solve M*x=Color with pivoted elimination. Use the observed matrix, not a
    # rounded inverse borrowed from another engine/color-space implementation.
    $a = [double[,]]::new(3,4)
    for ($r=0; $r -lt 3; $r++) {
        for ($c=0; $c -lt 3; $c++) { $a[$r,$c]=$script:ColorMatrix[$r][$c] }
        $a[$r,3]=$Color[$r]
    }
    for ($c=0; $c -lt 3; $c++) {
        $pivot=$c
        for ($r=$c+1; $r -lt 3; $r++) {
            if ([Math]::Abs($a[$r,$c]) -gt [Math]::Abs($a[$pivot,$c])) { $pivot=$r }
        }
        if ([Math]::Abs($a[$pivot,$c]) -lt 1e-12) { throw 'Singular color matrix.' }
        for ($k=0; $k -lt 4; $k++) {
            $tmp=$a[$c,$k]; $a[$c,$k]=$a[$pivot,$k]; $a[$pivot,$k]=$tmp
        }
        $divisor=$a[$c,$c]
        for ($k=$c; $k -lt 4; $k++) { $a[$c,$k]/=$divisor }
        for ($r=0; $r -lt 3; $r++) {
            if ($r -eq $c) { continue }
            $factor=$a[$r,$c]
            for ($k=$c; $k -lt 4; $k++) { $a[$r,$k]-=$factor*$a[$c,$k] }
        }
    }
    return ,@($a[0,3],$a[1,3],$a[2,3])
}

function ConvertFrom-AmbientShRows {
    [CmdletBinding()]
    param([Parameter(Mandatory)][object[]]$Rows)
    if ($Rows.Count -ne 64) { throw 'Expected exactly 64 float4 rows.' }
    for ($r=0; $r -lt 64; $r++) {
        if ($Rows[$r].row -ne $r -or $Rows[$r].values.Count -ne 4) {
            throw "Invalid row ordering/width at $r."
        }
        foreach ($v in $Rows[$r].values) {
            if ($null -eq $v -or $v -is [string] -or $v -is [bool] -or
                -not [double]::IsFinite([double]$v)) { throw "Invalid number in row $r." }
        }
    }
    $coefficients = @()
    $mean = [double[]]::new(3)
    $up = [double[]]::new(3)
    for ($channel=0; $channel -lt 3; $channel++) {
        $first=$Rows[2*$channel].values
        $second=$Rows[2*$channel+1].values
        $coefficients += ,@($first[0],$first[1],$first[2],$first[3],
            $second[0],$second[1],$second[2],$second[3],$Rows[6].values[$channel])
        # Positive radiance sampled only at y>0 implies DC>=0 and vertical C1<=0.
        # Failure means the assumption/record is incompatible, not negative light.
        if ([double]$first[0] -lt 0 -or [double]$first[1] -gt 0) {
            throw 'Coefficients contradict the assumed positive upper-hemisphere profile.'
        }
        # 256 samples; shared sum /128. Therefore C0=2*Y00*mean(L),
        # C1=-2*Y1*mean(L*y). No extra pi, gamma, exposure or clamping.
        $mean[$channel]=[double]$first[0]/(2*$script:Y00)
        $up[$channel]=-[double]$first[1]/$script:Y1
    }
    $preMean=Undo-AmbientColorMatrix $mean
    $preUp=Undo-AmbientColorMatrix $up
    foreach ($v in @($mean)+@($up)+@($preMean)+@($preUp)) {
        if (-not [double]::IsFinite($v)) { throw 'Nonfinite decoded result.' }
    }
    [pscustomobject]@{
        coefficientsWorking=$coefficients
        upperHemisphereMeanWorking=@($mean)
        upwardIrradianceOverPiWorking=@($up)
        inverseMatrixMean=@($preMean)
        inverseMatrixUpwardIrradianceOverPi=@($preUp)
        # An explicit estimate only: input primaries and absolute units are not
        # proven by recognizing the AP1-like matrix. Never treat as calibrated lux.
        rec709MeanLuminanceEstimate=($preMean[0]*.2126+$preMean[1]*.7152+$preMean[2]*.0722)
    }
}

Export-ModuleMember -Function ConvertFrom-AmbientShRows
