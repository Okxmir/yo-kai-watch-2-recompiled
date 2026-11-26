param([string]$OutputDirectory=(Join-Path $PSScriptRoot '..\dist'))
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'scripts/hash.ps1')
$framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$wpf=Join-Path $framework 'WPF'
$csc=Join-Path $framework 'csc.exe'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$out=(Resolve-Path -LiteralPath $OutputDirectory).Path
$png=Join-Path $PSScriptRoot 'assets/icon.png'
$version=(Get-Content -LiteralPath (Join-Path $root 'VERSION') -Raw).Trim()
if($version -notmatch '^\d+\.\d+\.\d+$'){throw 'Invalid VERSION file.'}
$versionFile=Join-Path $root 'build/launcher-version.cs'
New-Item -ItemType Directory -Path (Split-Path $versionFile -Parent) -Force|Out-Null
[IO.File]::WriteAllText($versionFile,('[assembly: System.Reflection.AssemblyVersion("'+$version+'.0")]' + "`n" + '[assembly: System.Reflection.AssemblyFileVersion("'+$version+'.0")]'))
$ico=Join-Path $out 'LauncherIcon.ico'
& "$PSScriptRoot/New-LauncherIcon.ps1" -Source $png -Destination $ico
$sdl=Join-Path $root 'third_party/SDL3-3.4.0/lib/x64/SDL3.dll'
if ((Get-Yw2Sha256 $sdl) -ne '2632E21625861A0DC106F2B1ABB649E610D8BE88534BA74C76A763ABE5AEFAA3'){throw 'SDL3 controller library hash mismatch.'}
$refs=@('PresentationFramework.dll','PresentationCore.dll','WindowsBase.dll')|ForEach-Object{"/reference:$wpf\$_"}
$refs+=@('System.Xaml.dll','System.Web.Extensions.dll','System.Windows.Forms.dll')|ForEach-Object{"/reference:$framework\$_"}
$files=@('Program.cs','Launcher.cs','LauncherCore.cs','LauncherUpdater.cs','UpdateUi.cs','SaveManager.cs','LauncherMaintenance.cs','ControlProfileManager.cs','ModManager.cs','ControlBindings.cs','ControlsEditor.cs','SdlInput.cs')|ForEach-Object{Join-Path $PSScriptRoot $_}
$files+=@($versionFile)
& $csc /nologo /target:winexe /platform:x64 /optimize+ /codepage:65001 "/out:$out/Launcher.exe" "/win32icon:$ico" "/resource:$PSScriptRoot/Launcher.xaml,Launcher.xaml" "/resource:$png,LauncherIcon.png" "/resource:$PSScriptRoot/ControlsWorkbench.xaml,ControlsWorkbench.xaml" "/resource:$sdl,Controls.SDL3.dll" @refs @files
if($LASTEXITCODE -ne 0){throw 'Launcher compilation failed.'}
Write-Host "$out/Launcher.exe"
