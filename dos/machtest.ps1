param(
    [string]$DosBoxX,
    [ValidateRange(100, 1000000)] [int]$Cycles = 3000,
    [string]$RomDirectory,
    [string]$Mk2srDirectory,
    [ValidateRange(3, 600)] [int]$Frames = 3,
    [switch]$VgaTest,
    [switch]$DiskTest
)
$ErrorActionPreference = 'Stop'
if ($Mk2srDirectory -and -not $RomDirectory) { throw '-Mk2srDirectory requires the base -RomDirectory.' }
if ($DiskTest -and -not $RomDirectory) { throw '-DiskTest requires real ROMs via -RomDirectory.' }
if (-not $DosBoxX) { $DosBoxX = (Get-Command dosbox-x.exe -ErrorAction Stop).Source }
$DosBoxX = (Resolve-Path -LiteralPath $DosBoxX).Path
$repoRoot = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $repoRoot 'build-dos'
$binary = Join-Path $buildDir 'QUASI88.EXE'
if (-not (Test-Path $binary)) { throw 'Build -Target Machine first.' }
$testDir = Join-Path $buildDir ('machine-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDir | Out-Null
Copy-Item -LiteralPath $binary -Destination $testDir
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination (Join-Path $testDir 'LICENSE.TXT')
foreach ($name in @('ROM', 'EMPTY', 'SHORT')) {
    New-Item -ItemType Directory -Path (Join-Path $testDir $name) | Out-Null
}
[IO.File]::WriteAllBytes((Join-Path $testDir 'SHORT\N88.ROM'), [byte[]]@(0))
if ($RomDirectory) {
    $RomDirectory = (Resolve-Path -LiteralPath $RomDirectory).Path
    # Copy only ROM files. Originals and all disk images remain outside the mount.
    Get-ChildItem -LiteralPath $RomDirectory -File -Filter '*.ROM' |
        Copy-Item -Destination (Join-Path $testDir 'ROM')
    if ($Mk2srDirectory) {
        $Mk2srDirectory = (Resolve-Path -LiteralPath $Mk2srDirectory).Path
        foreach ($name in @('N88_1.ROM', 'N88_2.ROM', 'N88_3.ROM', 'KANJI2.ROM')) {
            Copy-Item -LiteralPath (Join-Path $Mk2srDirectory $name) -Destination (Join-Path $testDir "ROM\$name")
        }
        # Model-specific input name exceeds DOS 8.3; only the isolated copy is renamed.
        Copy-Item -LiteralPath (Join-Path $Mk2srDirectory 'mk2sr_n88.rom') -Destination (Join-Path $testDir 'ROM\N88.ROM')
    }
    $checkOption = if ($VgaTest) { '-dosvga -dosvideochk -dosmouselog' } else { '' }
} else {
    # Tiny original programs: write a RAM marker, then loop at address 0005.
    $mainRom = New-Object byte[] 32768
    ([byte[]]@(0x3e,0x5a,0x32,0x00,0x90,0xc3,0x05,0x00)).CopyTo($mainRom, 0)
    [IO.File]::WriteAllBytes((Join-Path $testDir 'ROM\N88.ROM'), $mainRom)
    $subRom = New-Object byte[] 8192
    ([byte[]]@(0x3e,0xa5,0x32,0x00,0x40,0xc3,0x05,0x00)).CopyTo($subRom, 0)
    [IO.File]::WriteAllBytes((Join-Path $testDir 'ROM\N88SUB.ROM'), $subRom)
    # Default scheduling switches CPUs on PIO; these simple fixtures have no
    # PIO handshake, so use the existing interleaved CPU mode for this test.
    $checkOption = '-doscheck -cpu 2'
    if ($VgaTest) { $checkOption += ' -dosvga -dosvideochk -dosmouselog' }
}
if ($DiskTest) {
    # One-image D88 with a single 256-byte sector (0..255) for FDC tests.
    $fixture = New-Object byte[] 960
    [Text.Encoding]::ASCII.GetBytes('DOS D88 TEST').CopyTo($fixture, 0)
    $fixture[0x1c] = 0xc0
    $fixture[0x1d] = 0x03
    $fixture[0x20] = 0xb0
    $fixture[0x21] = 0x02
    $fixture[0x2b0] = 0
    $fixture[0x2b1] = 0
    $fixture[0x2b2] = 1
    $fixture[0x2b3] = 1
    $fixture[0x2b4] = 1
    $fixture[0x2bd] = 0
    $fixture[0x2be] = 0
    $fixture[0x2bf] = 1
    for ($i = 0; $i -lt 256; ++$i) { $fixture[0x2c0 + $i] = $i }
    [IO.File]::WriteAllBytes((Join-Path $testDir 'BASE.D88'), $fixture)
    Copy-Item (Join-Path $testDir 'BASE.D88') (Join-Path $testDir 'RW.D88')
    Copy-Item (Join-Path $testDir 'BASE.D88') (Join-Path $testDir 'RO.D88')
    [IO.File]::WriteAllBytes((Join-Path $testDir 'BAD.D88'), (New-Object byte[] 31))
    $diskTestCommands = @"
QUASI88 -noconfig -nosaveconfig -v2 -romdir ROM -verbose 1 -dosframes $Frames $checkOption -dosdiskchk RW.D88 > DISKRW.OUT
if errorlevel 1 goto fail
QUASI88 -noconfig -nosaveconfig -v2 -romdir ROM -verbose 1 -dosframes $Frames $checkOption -ro -dosdiskchk RO.D88 > DISKRO.OUT
if errorlevel 1 goto fail
QUASI88 -noconfig -nosaveconfig -v2 -romdir ROM -verbose 1 -dosframes $Frames $checkOption -dosdiskchk BAD.D88 > DISKBAD.OUT
if not errorlevel 1 goto fail
"@
} else {
    $diskTestCommands = ''
}
@"
@echo off
QUASI88 -noconfig -nosaveconfig -v2 -romdir EMPTY > MISSING.OUT
if not errorlevel 1 goto fail
QUASI88 -noconfig -nosaveconfig -v2 -romdir SHORT > SHORT.OUT
if not errorlevel 1 goto fail
QUASI88 -noconfig -nosaveconfig -v2 -romdir ROM -verbose 1 -dosframes $Frames $checkOption > MACHINE.OUT
if errorlevel 1 goto fail
$diskTestCommands
echo PASS > RESULT.TXT
goto end
:fail
echo FAIL > RESULT.TXT
:end
exit
"@ | Set-Content (Join-Path $testDir 'RUN.BAT') -Encoding ASCII
$config = Join-Path $testDir 'TEST.CONF'
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
call RUN.BAT
"@ | Set-Content $config -Encoding ASCII
Write-Host "Machine test directory: $testDir"
$stopwatch = [Diagnostics.Stopwatch]::StartNew()
$process = Start-Process -FilePath $DosBoxX -ArgumentList "-conf `"$config`"" -WorkingDirectory $testDir -WindowStyle Hidden -PassThru
try {
    if (-not $process.WaitForExit(45000)) { throw 'DOSBox-X machine test timed out.' }
    $stopwatch.Stop()
    if ($process.ExitCode -ne 0) { throw "DOSBox-X exited with code $($process.ExitCode)." }
    $output = Get-Content (Join-Path $testDir 'MACHINE.OUT') -Raw
    Write-Host $output
    $result = (Get-Content (Join-Path $testDir 'RESULT.TXT') -Raw).Trim()
    $missing = Get-Content (Join-Path $testDir 'MISSING.OUT') -Raw
    $short = Get-Content (Join-Path $testDir 'SHORT.OUT') -Raw
    if ($result -ne 'PASS' -or $output -notmatch "completed $Frames/$Frames frames; clean shutdown" -or
        $missing -notmatch 'missing required main ROM' -or $short -notmatch 'must be 32768 bytes') {
        throw 'Machine startup/negative tests failed; inspect the output files.'
    }
    if (-not $RomDirectory -and $output -notmatch 'synthetic CPU markers: PASS') {
        throw 'Synthetic CPU execution check failed.'
    }
    if ($VgaTest -and ($output -notmatch 'VGA plane readback: PASS' -or
                       $output -notmatch 'original video mode restored: PASS')) {
        throw 'VGA memory or mode restoration check failed.'
    }
    if ($VgaTest) {
        $mouseLog = Get-Content (Join-Path $testDir 'MOUSE.LOG') -Raw
        if ($mouseLog -notmatch 'driver=(installed|not-installed)') {
            throw 'DOS mouse driver detection did not produce MOUSE.LOG.'
        }
        Write-Host (($mouseLog -split "`r?`n" | Select-String '^driver=' | Select-Object -First 1).Line)
    }
    if ($DiskTest) {
        $rw = Get-Content (Join-Path $testDir 'DISKRW.OUT') -Raw
        $ro = Get-Content (Join-Path $testDir 'DISKRO.OUT') -Raw
        $bad = Get-Content (Join-Path $testDir 'DISKBAD.OUT') -Raw
        $blankSize = 960
        $appendedSize = 32 + (164 * 4) + (84 * 0x1600)
        if ($rw -notmatch 'FDC sector read/write: PASS' -or
            $rw -notmatch 'D88 append/write check: PASS' -or
            $ro -notmatch 'FDC read-only sector protection: PASS' -or
            $bad -notmatch 'Image not found' -or
            $bad -notmatch 'D88 mounted-image check: FAIL' -or
            (Get-Item (Join-Path $testDir 'RW.D88')).Length -ne ($blankSize + $appendedSize) -or
            (Get-FileHash (Join-Path $testDir 'RO.D88')).Hash -ne
                (Get-FileHash (Join-Path $testDir 'BASE.D88')).Hash) {
            throw 'D88 writable/read-only disk checks failed; inspect DISKRW.OUT and DISKRO.OUT.'
        }
        Write-Host 'PASS: D88 image mount, append/write, and read-only preservation'
    }
    Write-Host "PASS: missing/truncated ROM rejection and $Frames bounded frames at $Cycles cycles"
    Write-Host "DOSBox-X total runtime: $([math]::Round($stopwatch.Elapsed.TotalSeconds, 3)) seconds"
} finally {
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose()
}
