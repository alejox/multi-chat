#pragma once

// PowerShell script that installs the plugin on Windows (run elevated by install-win.cpp).

namespace updater {

// Copia el plugin nuevo a la carpeta de OBS 33 y quita las copias de antes. El .dll cargado no
// se puede borrar, pero sí renombrar: queda como .old (lo borra la actualización siguiente) y el
// nuevo se usa al reiniciar OBS. Si la copia falla, vuelve el .dll de antes.
inline constexpr const char kInstallScript[] = R"PS(param([string]$Dll, [string]$Data, [string]$Target, [string]$OldDll = '', [string]$OldData = '', [string]$OldBin = '')
$ErrorActionPreference = 'Stop'
$newDll = Join-Path $Target 'obs-multichat.dll'
$backup = ''
$copying = $false
try {
  New-Item -ItemType Directory -Force -Path $Target | Out-Null
  Get-ChildItem -LiteralPath $Target -Filter 'obs-multichat.dll.*.old' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
  if (Test-Path -LiteralPath $newDll) {
    $name = "obs-multichat.dll.$([DateTimeOffset]::Now.ToUnixTimeSeconds()).old"
    Rename-Item -LiteralPath $newDll -NewName $name
    $backup = Join-Path $Target $name
  }
  $copying = $true
  Copy-Item -LiteralPath $Dll -Destination $newDll -Force
  robocopy $Data (Join-Path $Target 'data') /E /NFL /NDL /NJH /NJS /NP | Out-Null
  if ($LASTEXITCODE -ge 8) { throw "robocopy $LASTEXITCODE" }
} catch {
  if ($copying) { Remove-Item -LiteralPath $newDll -Force -ErrorAction SilentlyContinue }
  if ($backup) { Rename-Item -LiteralPath $backup -NewName 'obs-multichat.dll' -ErrorAction SilentlyContinue }
  exit 1
}
if ($OldDll -and (Test-Path -LiteralPath $OldDll)) {
  Get-ChildItem -LiteralPath (Split-Path $OldDll) -Filter 'obs-multichat.dll.*.old' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
  Rename-Item -LiteralPath $OldDll -NewName "obs-multichat.dll.$([DateTimeOffset]::Now.ToUnixTimeSeconds()).old" -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath ([IO.Path]::ChangeExtension($OldDll, '.pdb')) -Force -ErrorAction SilentlyContinue
}
if ($OldData -and (Test-Path -LiteralPath $OldData)) { Remove-Item -LiteralPath $OldData -Recurse -Force -ErrorAction SilentlyContinue }
if ($OldBin -and (Test-Path -LiteralPath $OldBin)) { Remove-Item -LiteralPath $OldBin -Recurse -Force -ErrorAction SilentlyContinue }
exit 0
)PS";

} // namespace updater
