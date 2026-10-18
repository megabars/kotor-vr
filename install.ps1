param([Parameter(Mandatory=$true)][string]$GameDirectory, [switch]$SkipVideoSettings, [switch]$SkipShortcut)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scripts\Ini.ps1"
$GameDirectory = (Resolve-Path -LiteralPath $GameDirectory).Path
$exe = Join-Path $GameDirectory 'swkotor.exe'
$expected = '9c10e0450a6eeca417e036e3cde7474fed1f0a92aab018446d156944dea91435'
if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ine $expected) { throw 'Unsupported swkotor.exe. Requires the exact GOG 1.03 executable documented in README.' }
Assert-KvrGameStopped $GameDirectory
$binaries = @('kotor_vr.dll','kotor-vr-launcher.exe','kotor-vr-probe.exe','kotor-vr-math-test.exe')
foreach ($name in $binaries) { if (!(Test-Path -LiteralPath "$PSScriptRoot\bin\$name")) { throw "Missing bin\$name. Extract the release ZIP completely or build the source first." } }
$ini = Join-Path $GameDirectory 'swkotor.ini'
if (!$SkipVideoSettings -and !(Test-Path -LiteralPath $ini)) { throw 'swkotor.ini not found. Run the original game once, or use -SkipVideoSettings and configure it manually.' }
$destination = Join-Path $GameDirectory 'kotor-vr'
$sameDirectory = [string]::Equals([IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\'), [IO.Path]::GetFullPath($destination).TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase)
if (!$sameDirectory) {
    New-Item -ItemType Directory -Path "$destination\bin" -Force | Out-Null
    foreach ($name in $binaries) { Copy-Item -LiteralPath "$PSScriptRoot\bin\$name" -Destination "$destination\bin\$name" -Force }
    foreach ($name in @('README.md','README.ru.md','VERSION','LICENSE','THIRD_PARTY_NOTICES.md','install.ps1','restore-settings.ps1','docs','scripts','licenses')) {
        Copy-Item -LiteralPath "$PSScriptRoot\$name" -Destination $destination -Recurse -Force
    }
}
if (!$SkipVideoSettings) {
    New-Item -ItemType Directory -Path "$destination\backups" -Force | Out-Null
    $backup = "$destination\backups\swkotor.ini.original"
    if (!(Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $ini -Destination $backup }
    $text = Read-KvrIni $ini
    foreach ($entry in @(@('FullScreen','0'),@('AllowWindowedMode','1'),@('V-Sync','0'))) { $text = Set-KvrIniValue $text 'Graphics Options' $entry[0] $entry[1] }
    Write-KvrIni $ini $text
}
if (!$SkipShortcut) {
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut((Join-Path $GameDirectory 'KotOR VR.lnk'))
    $shortcut.TargetPath = "$destination\bin\kotor-vr-launcher.exe"
    $shortcut.WorkingDirectory = $GameDirectory
    $shortcut.Description = 'KotOR VR 0.0.1 - third-person OpenXR'
    $shortcut.Save()
}
Write-Host "Installed KotOR VR in $destination. Connect Virtual Desktop, then launch KotOR VR.lnk."
