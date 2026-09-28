param(
    [string]$WatcomRoot = $env:WATCOM,
    [ValidateSet('Hello', 'PortTest', 'Machine')]
    [string]$Target = 'Hello'
)

$ErrorActionPreference = 'Stop'
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
            $includeDirs = @('src/sysdepend/dos', 'src/osdepend/dos', 'src', 'src/pc88', 'src/screen', 'src/screen/func', 'src/screen/func/macro', 'src/tk', 'src/tk/q8tk', 'src/tk/q8tk/q8tk', 'src/ui', 'src/ui/menu', 'src/mon', 'src/snddrv', 'src/sysdepend', 'src/osdepend')
            $includes = $includeDirs | ForEach-Object { '-i=' + (Join-Path $repoRoot $_) }
            $linkLines = @('system causeway', 'name ../QUASI88.EXE', 'option map=QUASI88.MAP', 'option stack=131072')
            $index = 0
            foreach ($source in (Get-Content (Join-Path $PSScriptRoot 'sources.txt'))) {
                $object = 'Q{0:D3}.OBJ' -f $index++
                $warningOptions = @()
                if ($source -like 'src/*depend/dos/*') { $warningOptions = @('-we') }
                & $compilerPath '-y' '-c' '-bt=dos' '-3r' '-j' '-w4' @warningOptions @includes ('-fo=' + $object) (Join-Path $repoRoot $source)
                if ($LASTEXITCODE -ne 0) { throw "DOS compile failed: $source" }
                $linkLines += "file $object"
            }
            $linkLines | Set-Content 'MACHINE.LNK' -Encoding ASCII
            & (Join-Path $hostBin 'wlink.exe') '@MACHINE.LNK'
            if ($LASTEXITCODE -ne 0) { throw 'DOS machine link failed' }
            Write-Host "Built $outputDir\QUASI88.EXE"
        } finally { Pop-Location }
    } else {
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
    }
} finally {
    Pop-Location
    $env:WATCOM = $oldWatcom
    $env:INCLUDE = $oldInclude
    $env:PATH = $oldPath
}
