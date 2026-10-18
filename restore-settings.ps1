param([string]$GameDirectory)
$ErrorActionPreference = 'Stop'
if (!$GameDirectory) { $GameDirectory = Split-Path $PSScriptRoot -Parent }
. "$PSScriptRoot\scripts\Ini.ps1"
$GameDirectory = (Resolve-Path -LiteralPath $GameDirectory).Path
Assert-KvrGameStopped $GameDirectory
$backup = Join-Path $GameDirectory 'kotor-vr\backups\swkotor.ini.original'
if (!(Test-Path -LiteralPath $backup)) { throw "Original settings backup not found: $backup" }
$ini = Join-Path $GameDirectory 'swkotor.ini'
$original = Read-KvrIni $backup
$current = Read-KvrIni $ini
foreach ($key in @('FullScreen','AllowWindowedMode','V-Sync')) {
    $value = Get-KvrIniValue $original 'Graphics Options' $key
    $current = Set-KvrIniValue $current 'Graphics Options' $key $value
}
Write-KvrIni $ini $current
Write-Host 'Restored FullScreen, AllowWindowedMode and V-Sync. Other settings were preserved.'
