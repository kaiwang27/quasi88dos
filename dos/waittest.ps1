param(
    [string]$DosBoxX,
    [ValidateRange(100, 1000000)] [int]$Cycles = 3000
)
$ErrorActionPreference = 'Stop'
if (-not $DosBoxX) { $DosBoxX = (Get-Command dosbox-x.exe -ErrorAction Stop).Source }
$DosBoxX = (Resolve-Path -LiteralPath $DosBoxX).Path
$buildDir = Join-Path (Split-Path $PSScriptRoot -Parent) 'build-dos'
$binary = Join-Path $buildDir 'WAITTEST.EXE'
if (-not (Test-Path -LiteralPath $binary)) { throw 'Build -Target WaitTest first.' }
$testDir = Join-Path $buildDir ('wait-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDir | Out-Null
Copy-Item -LiteralPath $binary -Destination $testDir
@'
@echo off
WAITTEST > WAIT.OUT
exit
'@ | Set-Content -LiteralPath (Join-Path $testDir 'RUN.BAT') -Encoding ASCII
@"
[dosbox]
working directory option=noprompt
[cpu]
core=normal
cputype=386
cycles=fixed $Cycles
[autoexec]
mount c .
c:
call RUN.BAT
"@ | Set-Content -LiteralPath (Join-Path $testDir 'TEST.CONF') -Encoding ASCII
$testConfig = Join-Path $testDir 'TEST.CONF'
$process = Start-Process -FilePath $DosBoxX -ArgumentList "-conf `"$testConfig`"" -WorkingDirectory $testDir -WindowStyle Hidden -PassThru
try {
    if (-not $process.WaitForExit(20000)) { throw 'DOSBox-X PIT pacing test timed out.' }
    $output = Get-Content -LiteralPath (Join-Path $testDir 'WAIT.OUT') -Raw
    Write-Host $output
    if ($process.ExitCode -ne 0 -or $output -notmatch 'PIT pacing 60 x 18050us = [0-9]+ms; PASS') {
        throw 'PIT pacing validation failed; inspect WAIT.OUT.'
    }
    Write-Host "PASS: BIOS/PIT frame pacing at $Cycles DOSBox-X cycles"
} finally {
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose()
}
