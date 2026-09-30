param(
    [string]$WatcomRoot = $env:WATCOM,
    [ValidateSet('Hello', 'PortTest', 'WaitTest', 'WssTest', 'Machine')]
    [string]$Target = 'Hello'
)

$ErrorActionPreference = 'Continue'
# Keep PowerShell cmdlets fail-fast while allowing warning text emitted on
# stderr by native Watcom tools to pass through as ordinary diagnostics.
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'
# Open Watcom reports warnings on stderr with a zero exit code. PowerShell 7
# otherwise turns those diagnostic records into terminating errors here.
$PSNativeCommandUseErrorActionPreference = $false
if (-not $WatcomRoot) {
    $compiler = Get-Command wcl386.exe -ErrorAction Stop
    $WatcomRoot = Split-Path (Split-Path $compiler.Source -Parent) -Parent
}
$WatcomRoot = (Resolve-Path -LiteralPath $WatcomRoot).Path
$hostBin = Join-Path $WatcomRoot 'binnt64'
if (-not (Test-Path (Join-Path $hostBin 'wcl386.exe'))) {
    $hostBin = Join-Path $WatcomRoot 'binnt'
}
$compilerPath = Join-Path $hostBin 'wcl386.exe'
if (-not (Test-Path $compilerPath)) {
    throw "Cannot find the Windows Watcom compiler under $WatcomRoot"
}
$outputDir = Join-Path (Split-Path $PSScriptRoot -Parent) 'build-dos'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$oldWatcom = $env:WATCOM
$oldInclude = $env:INCLUDE
$oldPath = $env:PATH
Push-Location $outputDir
try {
    $env:WATCOM = $WatcomRoot
    $env:INCLUDE = Join-Path $WatcomRoot 'h'
    $env:PATH = "$hostBin;$(Join-Path $WatcomRoot 'binw');$oldPath"
    $assemblerPath = Join-Path $hostBin 'wasm.exe'
    if ($Target -eq 'Hello') {
        & $compilerPath '-y' '-bt=dos' '-l=causeway' '-3r' '-w4' '-we' '-fe=HELLO.EXE' '-fo=HELLO.OBJ' '-fm=HELLO.MAP' (Join-Path $PSScriptRoot 'hello.c')
        if ($LASTEXITCODE -ne 0) { throw "DOS build failed with exit code $LASTEXITCODE" }
        Write-Host "Built $outputDir\HELLO.EXE"
    } elseif ($Target -eq 'Machine') {
        $repoRoot = Split-Path $PSScriptRoot -Parent
        $machineDir = Join-Path $outputDir 'machine'
        New-Item -ItemType Directory -Force -Path $machineDir | Out-Null
        Push-Location $machineDir
        try {
            $includeDirs = @('src/sysdepend/dos', 'src/osdepend/dos', 'src', 'src/pc88', 'src/screen', 'src/screen/func', 'src/screen/func/macro', 'src/tk', 'src/tk/q8tk', 'src/tk/q8tk/q8tk', 'src/ui', 'src/ui/menu', 'src/mon', 'src/snddrv', 'src/sysdepend', 'src/osdepend', 'src/snddrv/xmame', 'src/snddrv/xmame/quasi88', 'src/snddrv/xmame/src', 'src/snddrv/xmame/src/sound')
            $includes = $includeDirs | ForEach-Object { '-i=' + (Join-Path $repoRoot $_) }
            $linkLines = @('system causeway', 'name ../QUASI88.EXE', 'option map=QUASI88.MAP', 'option stack=131072')
            $index = 0
            foreach ($source in (Get-Content (Join-Path $PSScriptRoot 'sources.txt'))) {
                $object = 'Q{0:D3}.OBJ' -f $index++
                $warningOptions = @()
                if ($source.EndsWith('.asm')) {
                & $assemblerPath '-bt=dos' '-3p' '-mf' ('-fo=' + $object) (Join-Path $repoRoot $source) 2>&1
                } else {
                    if ($source -like 'src/*depend/dos/*' -and $source -ne 'src/sysdepend/dos/audio.c') { $warningOptions = @('-we') }
                    & $compilerPath '-y' '-c' '-bt=dos' '-3r' '-mf' '-j' '-w4' '-dUSE_SOUND' '-dM_PI=3.14159265358979323846' '-dPI=M_PI' @warningOptions @includes ('-fo=' + $object) (Join-Path $repoRoot $source) 2>&1
                }
                if ($LASTEXITCODE -ne 0) { throw "DOS compile failed: $source" }
                $linkLines += "file $object"
            }
            $linkLines | Set-Content 'MACHINE.LNK' -Encoding ASCII
            & (Join-Path $hostBin 'wlink.exe') '@MACHINE.LNK' 2>&1
            if ($LASTEXITCODE -ne 0) { throw 'DOS machine link failed' }
            Write-Host "Built $outputDir\QUASI88.EXE"
        } finally { Pop-Location }
    } elseif ($Target -eq 'PortTest') {
        $repoRoot = Split-Path $PSScriptRoot -Parent
        $includes = @('src/sysdepend/dos', 'src/osdepend/dos', 'src', 'src/pc88', 'src/osdepend', 'src/tk') |
            ForEach-Object { '-i=' + (Join-Path $repoRoot $_) }
        $sources = @(
            @('dos/porttest.c', 'PORTTEST.OBJ'),
            @('src/osdepend/dos/file-op.c', 'FILEOP.OBJ'),
            @('src/pc88/z80.c', 'Z80.OBJ')
        )
        foreach ($source in $sources) {
            # Keep upstream warnings visible; require warning-free new DOS code.
            $warningOptions = @()
            if ($source[0] -ne 'src/pc88/z80.c') { $warningOptions = @('-we') }
            & $compilerPath '-y' '-c' '-bt=dos' '-3r' '-w4' @warningOptions @includes ('-fo=' + $source[1]) (Join-Path $repoRoot $source[0])
            if ($LASTEXITCODE -ne 0) { throw "DOS compile failed: $($source[0])" }
        }
        & $compilerPath '-y' '-bt=dos' '-l=causeway' '-fe=Q88TEST.EXE' '-fm=Q88TEST.MAP' 'PORTTEST.OBJ' 'FILEOP.OBJ' 'Z80.OBJ'
        if ($LASTEXITCODE -ne 0) { throw 'DOS link failed' }
        Write-Host "Built $outputDir\Q88TEST.EXE"
    } elseif ($Target -eq 'WssTest') {
        # Stand-alone WSS codec probe and tone test; shares only the PIT timer.
        $repoRoot = Split-Path $PSScriptRoot -Parent
        $wssIncludeDirs = @('src/sysdepend/dos', 'src/osdepend/dos', 'src', 'src/pc88', 'src/screen', 'src/screen/func', 'src/screen/func/macro', 'src/tk', 'src/tk/q8tk', 'src/tk/q8tk/q8tk', 'src/ui', 'src/ui/menu', 'src/mon', 'src/snddrv', 'src/sysdepend', 'src/osdepend')
        $includes = $wssIncludeDirs |
            ForEach-Object { '-i=' + (Join-Path $repoRoot $_) }
        & $compilerPath '-y' '-c' '-bt=dos' '-3r' '-mf' '-w4' '-we' @includes '-fo=WSSTEST.OBJ' (Join-Path $PSScriptRoot 'wsstest.c')
        if ($LASTEXITCODE -ne 0) { throw 'DOS WSS test compile failed' }
        & $compilerPath '-y' '-c' '-bt=dos' '-3r' '-mf' '-w4' '-we' @includes '-fo=WSSWAIT.OBJ' (Join-Path $repoRoot 'src/sysdepend/dos/wait.c')
        if ($LASTEXITCODE -ne 0) { throw 'DOS wait backend compile failed' }
        & $compilerPath '-y' '-bt=dos' '-l=causeway' '-fe=WSSTEST.EXE' '-fm=WSSTEST.MAP' 'WSSTEST.OBJ' 'WSSWAIT.OBJ'
        if ($LASTEXITCODE -ne 0) { throw 'DOS WSS test link failed' }
        Write-Host "Built $outputDir\WSSTEST.EXE"
    } else {
        $repoRoot = Split-Path $PSScriptRoot -Parent
        $waitIncludeDirs = @('src/sysdepend/dos', 'src/osdepend/dos', 'src', 'src/pc88', 'src/screen', 'src/screen/func', 'src/screen/func/macro', 'src/tk', 'src/tk/q8tk', 'src/tk/q8tk/q8tk', 'src/ui', 'src/ui/menu', 'src/mon', 'src/snddrv', 'src/sysdepend', 'src/osdepend')
        $includes = $waitIncludeDirs |
            ForEach-Object { '-i=' + (Join-Path $repoRoot $_) }
        & $compilerPath '-y' '-c' '-bt=dos' '-3r' '-mf' '-w4' '-we' @includes '-fo=WAITTEST.OBJ' (Join-Path $PSScriptRoot 'waittest.c')
        if ($LASTEXITCODE -ne 0) { throw 'DOS wait test compile failed' }
        & $compilerPath '-y' '-c' '-bt=dos' '-3r' '-mf' '-w4' '-we' @includes '-fo=WAITDOS.OBJ' (Join-Path $repoRoot 'src/sysdepend/dos/wait.c')
        if ($LASTEXITCODE -ne 0) { throw 'DOS wait backend compile failed' }
        & $compilerPath '-y' '-bt=dos' '-l=causeway' '-fe=WAITTEST.EXE' '-fm=WAITTEST.MAP' 'WAITTEST.OBJ' 'WAITDOS.OBJ'
        if ($LASTEXITCODE -ne 0) { throw 'DOS wait test link failed' }
        Write-Host "Built $outputDir\WAITTEST.EXE"
    }
} finally {
    Pop-Location
    $env:WATCOM = $oldWatcom
    $env:INCLUDE = $oldInclude
    $env:PATH = $oldPath
}
