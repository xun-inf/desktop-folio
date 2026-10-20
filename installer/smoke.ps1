param([switch]$Install)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$version = (Get-Content "$root/frontend/package.json" -Raw | ConvertFrom-Json).version
$setup = "$root/build/installer/folio-$version-windows-x64-setup.exe"
$evidence = "$root/build/installer-ui-smoke"
New-Item -ItemType Directory -Force $evidence | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class SetupUi {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string name, string title);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hwnd, int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, System.Text.StringBuilder text, int count);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendText(IntPtr hwnd, uint message, IntPtr wParam, string text);
}
'@
function Wait-For([scriptblock]$Condition, [int]$Seconds = 30) {
  $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
  do {
    if (& $Condition) { return }
    Start-Sleep -Milliseconds 200
  } while ([DateTime]::UtcNow -lt $deadline)
  throw "Timed out after $Seconds seconds"
}
function Button-Text([IntPtr]$Window) {
  $text = New-Object Text.StringBuilder 128
  [SetupUi]::GetWindowText([SetupUi]::GetDlgItem($Window, 100), $text, 128) | Out-Null
  return $text.ToString()
}
if ([SetupUi]::FindWindow('FolioSetupWindow', 'Folio 安装程序') -ne [IntPtr]::Zero) {
  throw 'Close the existing installer before running the UI check.'
}
foreach ($state in @('welcome', 'progress', 'complete', 'error')) {
  $capture = [IO.Path]::GetFullPath("$evidence/$state.png")
  $process = Start-Process -FilePath $setup -ArgumentList "--preview=$state", "`"--capture=$capture`"" -WindowStyle Hidden -PassThru
  try {
    if (-not $process.WaitForExit(10000)) { throw 'Preview did not close' }
    if ($process.ExitCode -ne 0) { throw 'Preview export failed' }
    $bitmap = [Drawing.Bitmap]::FromFile($capture)
    try {
      if ($bitmap.GetPixel(5, 5).R -ne 34) { throw 'Preview background was not rendered' }
    } finally { $bitmap.Dispose() }
  } finally { if (-not $process.HasExited) { $process.Kill() } }
}
Write-Output "PASS: four installer UI states captured in $evidence"
if (-not $Install) { return }

$guidLine = Get-Content "$root/build/installer-ui/CMakeCache.txt" | Where-Object { $_ -match '^SETUP_GUID:' }
$guid = ($guidLine -split '=', 2)[1]
$registry = "HKCU:\Software\$guid"
if ((Test-Path $registry) -or (Test-Path "HKLM:\Software\$guid")) {
  throw 'An existing installation is registered. Refusing to change it during the test.'
}
$target = [IO.Path]::GetFullPath((Join-Path $evidence ('install space ' + [guid]::NewGuid().ToString('N'))))
$process = Start-Process -FilePath $setup -WindowStyle Hidden -PassThru
try {
  Wait-For { [SetupUi]::FindWindow('FolioSetupWindow', 'Folio 安装程序') -ne [IntPtr]::Zero }
  $window = [SetupUi]::FindWindow('FolioSetupWindow', 'Folio 安装程序')
  [SetupUi]::SendText([SetupUi]::GetDlgItem($window, 104), 0xC, [IntPtr]::Zero, $target) | Out-Null
  [SetupUi]::SendMessage($window, 0x111, [IntPtr]100, [IntPtr]::Zero) | Out-Null
  Start-Sleep -Milliseconds 300
  Wait-For { (Button-Text $window) -in @('立即体验', '返回重试') } 180
  if ((Button-Text $window) -ne '立即体验') { throw 'Installer reported failure' }
  foreach ($file in @('folio.exe', 'media-service.exe', 'node-streampreview.node', 'resources/app.asar')) {
    if (-not (Test-Path (Join-Path $target $file))) { throw "Missing installed file: $file" }
  }
  if ((Get-ItemProperty $registry).InstallLocation -ne $target) { throw 'Install registry path mismatch' }
  [SetupUi]::SendMessage($window, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
  if (-not $process.WaitForExit(10000)) { throw 'Installer did not close' }
  if ($process.ExitCode -ne 0) { throw "Installer exit code: $($process.ExitCode)" }
  Write-Output "PASS: GUI installation, path with spaces, embedded payload, installed files and registry"
  $upgrade = Start-Process -FilePath $setup -ArgumentList '/S', "`"/D=$target`"" -WindowStyle Hidden -PassThru
  if (-not $upgrade.WaitForExit(120000) -or $upgrade.ExitCode -ne 0) { throw 'Silent reinstall failed' }
  if ((Get-ItemProperty $registry).InstallLocation -ne $target) { throw 'Reinstall changed the selected directory' }
  Write-Output 'PASS: silent reinstall preserves the selected path'
  $uninstaller = Get-ChildItem -LiteralPath $target -Filter '*Uninstall*.exe' | Select-Object -First 1
  if (-not $uninstaller) { throw 'Uninstaller missing' }
  $uninstall = Start-Process -FilePath $uninstaller.FullName -ArgumentList '/S' -WindowStyle Hidden -PassThru
  if (-not $uninstall.WaitForExit(60000)) { throw 'Uninstaller did not finish' }
  Wait-For { -not (Test-Path "$target/folio.exe") -and -not (Test-Path $registry) } 60
  Write-Output 'PASS: uninstall removed the application and installation registration'
} finally {
  if (-not $process.HasExited) { $process.Kill() }
}
