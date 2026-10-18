# Integration test using a locally supplied original EXE. Never packages it.
param([Parameter(Mandatory=$true)][string]$SupportedExe, [string]$PackageRoot)
$ErrorActionPreference = 'Stop'
if (!$PackageRoot) { $PackageRoot = Split-Path $PSScriptRoot -Parent }
$workspace = Join-Path (Split-Path $PSScriptRoot -Parent) ('test-output\install-' + [guid]::NewGuid().ToString('N'))
$game = Join-Path $workspace 'game'
New-Item -ItemType Directory -Path $game -Force | Out-Null
Copy-Item -LiteralPath $SupportedExe -Destination "$game\swkotor.exe"
. "$PackageRoot\scripts\Ini.ps1"
$initial = "; untouched byte: $([char]0xe9)`r`n[Display Options]`r`nFullScreen=7`r`n[Graphics Options]`r`nFullScreen=1`r`nV-Sync=1`r`nWidth=1280`r`n[Other]`r`nValue=keep`r`n"
Write-KvrIni "$game\swkotor.ini" $initial
& "$PackageRoot\install.ps1" -GameDirectory $game -SkipShortcut
$backup = "$game\kotor-vr\backups\swkotor.ini.original"
if ((Read-KvrIni $backup) -cne $initial) { throw 'Backup did not preserve original INI bytes.' }
$installed = Read-KvrIni "$game\swkotor.ini"
foreach ($entry in @(@('FullScreen','0'),@('AllowWindowedMode','1'),@('V-Sync','0'))) {
    if ((Get-KvrIniValue $installed 'Graphics Options' $entry[0]) -ne $entry[1]) { throw "Setting failed: $($entry[0])" }
}
if ((Get-KvrIniValue $installed 'Display Options' 'FullScreen') -ne '7') { throw 'Another section changed.' }
foreach ($name in @('kotor_vr.dll','kotor-vr-launcher.exe','kotor-vr-probe.exe','kotor-vr-math-test.exe')) {
    if ((Get-FileHash "$PackageRoot\bin\$name").Hash -ne (Get-FileHash "$game\kotor-vr\bin\$name").Hash) { throw "Copied binary differs: $name" }
}
$installed = Set-KvrIniValue $installed 'Graphics Options' 'Width' '1600'
Write-KvrIni "$game\swkotor.ini" $installed
& "$PackageRoot\install.ps1" -GameDirectory $game -SkipShortcut
if ((Read-KvrIni $backup) -cne $initial) { throw 'Reinstall overwrote first backup.' }
& "$game\kotor-vr\restore-settings.ps1" -GameDirectory $game
$restored = Read-KvrIni "$game\swkotor.ini"
if ((Get-KvrIniValue $restored 'Graphics Options' 'FullScreen') -ne '1' -or (Get-KvrIniValue $restored 'Graphics Options' 'V-Sync') -ne '1') { throw 'Original values not restored.' }
if ($null -ne (Get-KvrIniValue $restored 'Graphics Options' 'AllowWindowedMode')) { throw 'Originally absent key was not removed.' }
if ((Get-KvrIniValue $restored 'Graphics Options' 'Width') -ne '1600') { throw 'Restoration overwrote unrelated user change.' }
if (!$restored.Contains("; untouched byte: $([char]0xe9)")) { throw 'Non-ASCII bytes changed.' }
$unsupported = Join-Path $workspace 'unsupported'
New-Item -ItemType Directory -Path $unsupported | Out-Null
[IO.File]::WriteAllText("$unsupported\swkotor.exe", 'not a supported executable')
$rejected = $false
try { & "$PackageRoot\install.ps1" -GameDirectory $unsupported -SkipShortcut -SkipVideoSettings } catch { $rejected = $_.Exception.Message -like 'Unsupported swkotor.exe*' }
if (!$rejected -or (Test-Path "$unsupported\kotor-vr")) { throw 'Unsupported EXE was not rejected before installation.' }
Write-Host 'PASS: install, binary copy, section scope, backup preservation, reinstall, selective restore, non-ASCII bytes and unsupported EXE rejection.'
Write-Host "Local test files retained in $workspace (ignored by Git)."
