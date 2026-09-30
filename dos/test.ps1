# Copyright (c) 2026, Kai Wang. Part of the QUASI88 MS-DOS port.
# SPDX-License-Identifier: BSD-3-Clause (see LICENSE).
param(
    [string]$DosBoxX,
    [ValidateRange(100, 1000000)]
    [int]$Cycles = 3000
)

$ErrorActionPreference = 'Stop'
if (-not $DosBoxX) {
    $DosBoxX = (Get-Command dosbox-x.exe -ErrorAction Stop).Source
}
$DosBoxX = (Resolve-Path -LiteralPath $DosBoxX).Path
$repoRoot = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $repoRoot 'build-dos'
$binary = Join-Path $buildDir 'Q88TEST.EXE'
if (-not (Test-Path -LiteralPath $binary)) { throw 'Build -Target PortTest first.' }
# Fresh isolated directory prevents stale results and avoids all user media.
$testDir = Join-Path $buildDir ('test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDir | Out-Null
Copy-Item -LiteralPath $binary -Destination $testDir
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination (Join-Path $testDir 'LICENSE.TXT')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'porttest.bat') -Destination (Join-Path $testDir 'PORTTEST.BAT')
$configPath = Join-Path $testDir 'TEST.CONF'
@"
[dosbox]
working directory option=noprompt
memsize=16
[cpu]
core=normal
cputype=386
cycles=fixed $Cycles
[autoexec]
mount c .
c:
call PORTTEST.BAT
"@ | Set-Content -LiteralPath $configPath -Encoding ASCII

Write-Host "DOSBox-X test directory: $testDir"
$testProcess = Start-Process -FilePath $DosBoxX -ArgumentList "-conf `"$configPath`"" -WorkingDirectory $testDir -WindowStyle Hidden -PassThru
try {
    if (-not $testProcess.WaitForExit(20000)) { throw 'DOSBox-X test timed out after 20 seconds.' }
    if ($testProcess.ExitCode -ne 0) { throw "DOSBox-X exited with code $($testProcess.ExitCode)." }
    $output = Get-Content -LiteralPath (Join-Path $testDir 'PORT.OUT') -Raw
    $result = (Get-Content -LiteralPath (Join-Path $testDir 'RESULT.TXT') -Raw).Trim()
    Write-Host $output
    if ($result -ne 'PASS' -or $output -notmatch '(?m)^PASS: DOS bring-up\r?$' -or $output -match 'FAIL:') {
        throw 'DOS bring-up test failed; inspect PORT.OUT and RESULT.TXT.'
    }
    if (Test-Path -LiteralPath (Join-Path $testDir 'IOCHK.TMP')) { throw 'Test left its scratch file behind.' }
    Write-Host "PASS: DOSBox-X, 386, $Cycles cycles, 16 MB emulated RAM"
} finally {
    if (-not $testProcess.HasExited) { $testProcess.Kill(); $testProcess.WaitForExit() }
    $testProcess.Dispose()
}
