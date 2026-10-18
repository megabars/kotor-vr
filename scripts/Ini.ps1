# Latin-1 preserves all original bytes outside the edited ASCII settings.
function Read-KvrIni([string]$Path) {
    [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($Path))
}
function Get-KvrIniValue([string]$Text, [string]$Section, [string]$Key) {
    $active = $false
    foreach ($line in ($Text -split '\r?\n')) {
        if ($line -match '^\s*\[([^\]]+)\]\s*$') { $active = $Matches[1] -ieq $Section; continue }
        if ($active -and $line -match ('^\s*' + [regex]::Escape($Key) + '\s*=(.*)$')) { return $Matches[1].Trim() }
    }
    return $null
}
function Set-KvrIniValue([string]$Text, [string]$Section, [string]$Key, $Value) {
    $newline = if ($Text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $lines = [Collections.Generic.List[string]]::new()
    $lines.AddRange([string[]]($Text -split '\r?\n'))
    $start = -1; $end = $lines.Count
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^\s*\[([^\]]+)\]\s*$') {
            if ($start -ge 0) { $end = $i; break }
            if ($Matches[1] -ieq $Section) { $start = $i }
        }
    }
    if ($start -lt 0) {
        if ($null -eq $Value) { return $Text }
        $lines.Add("[$Section]"); $lines.Add("$Key=$Value")
    } else {
        $found = $false
        for ($i = $end - 1; $i -gt $start; $i--) {
            if ($lines[$i] -match ('^\s*' + [regex]::Escape($Key) + '\s*=')) {
                if ($null -eq $Value -or $found) { $lines.RemoveAt($i) } else { $lines[$i] = "$Key=$Value" }
                $found = $true
            }
        }
        if (!$found -and $null -ne $Value) { $lines.Insert($start + 1, "$Key=$Value") }
    }
    return [string]::Join($newline, $lines)
}
function Write-KvrIni([string]$Path, [string]$Text) {
    [IO.File]::WriteAllBytes($Path, [Text.Encoding]::GetEncoding(28591).GetBytes($Text))
}
function Assert-KvrGameStopped([string]$GameDirectory) {
    $target = [IO.Path]::GetFullPath((Join-Path $GameDirectory 'swkotor.exe'))
    foreach ($process in (Get-Process swkotor -ErrorAction SilentlyContinue)) {
        try { $runningPath = $process.Path } catch { throw 'Cannot inspect running KotOR. Close KotOR before installation/restoration.' }
        if (!$runningPath -or [string]::Equals($runningPath, $target, [StringComparison]::OrdinalIgnoreCase)) { throw 'Close this KotOR installation before changing its files.' }
    }
}
