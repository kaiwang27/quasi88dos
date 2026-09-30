param(
    [string]$WatcomRoot = $env:WATCOM,
    [string]$OutputDirectory,
    [switch]$SkipBuild,
    [switch]$NoZip
)
# Build the DOS programs and assemble a release folder with DOS 8.3 names:
#   build-dos\dist\Q88DOS\   QUASI88.EXE AZTSB.EXE WSSTEST.EXE README.TXT
#                            LICENSE.TXT MAME.TXT ROM\ROMS.TXT
#   build-dos\dist\Q88DOS.ZIP
# No ROM or disk images are included.
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $repoRoot 'build-dos'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $buildDir 'dist\Q88DOS' }

if (-not $SkipBuild) {
    foreach ($target in 'Machine', 'AztSb', 'WssTest') {
        $buildArgs = @{ Target = $target }
        if ($WatcomRoot) { $buildArgs.WatcomRoot = $WatcomRoot }
        & (Join-Path $PSScriptRoot 'build.ps1') @buildArgs | Out-Null
        Write-Host "Built target $target"
    }
}

if (Test-Path -LiteralPath $OutputDirectory) { Remove-Item -LiteralPath $OutputDirectory -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $OutputDirectory 'ROM') -Force | Out-Null

foreach ($name in 'QUASI88.EXE', 'AZTSB.EXE', 'WSSTEST.EXE') {
    $source = Join-Path $buildDir $name
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing $source; run without -SkipBuild." }
    Copy-Item -LiteralPath $source -Destination (Join-Path $OutputDirectory $name)
}

# Text files are written as ASCII with CRLF line endings for DOS.
function Copy-DosText([string]$Source, [string]$Destination) {
    $text = [IO.File]::ReadAllText($Source)
    if ($text -match '[^\x00-\x7f]') { throw "$Source contains non-ASCII characters." }
    $text = ($text -replace "`r`n", "`n") -replace "`n", "`r`n"
    [IO.File]::WriteAllText($Destination, $text, [Text.Encoding]::ASCII)
}
Copy-DosText (Join-Path $PSScriptRoot 'dist\README.TXT') (Join-Path $OutputDirectory 'README.TXT')
Copy-DosText (Join-Path $PSScriptRoot 'dist\ROMS.TXT') (Join-Path $OutputDirectory 'ROM\ROMS.TXT')
Copy-DosText (Join-Path $repoRoot 'LICENSE') (Join-Path $OutputDirectory 'LICENSE.TXT')
Copy-DosText (Join-Path $repoRoot 'src\snddrv\xmame\license.txt') (Join-Path $OutputDirectory 'MAME.TXT')

# Every distributed name must be a DOS 8.3 name.
Get-ChildItem -LiteralPath $OutputDirectory -Recurse | ForEach-Object {
    if ($_.Name -notmatch '^[A-Z0-9_]{1,8}(\.[A-Z0-9]{1,3})?$') { throw "Not a DOS 8.3 name: $($_.FullName)" }
}

if (-not $NoZip) {
    $zip = "$OutputDirectory.ZIP"
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    Compress-Archive -Path $OutputDirectory -DestinationPath $zip
    Write-Host "Created $zip"
}
Write-Host "Release folder: $OutputDirectory"
Get-ChildItem -LiteralPath $OutputDirectory -Recurse -File | ForEach-Object {
    '{0,10}  {1}' -f $_.Length, $_.FullName.Substring($OutputDirectory.Length + 1)
}
