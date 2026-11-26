param([Parameter(Mandatory=$true)][string]$Archive,[Parameter(Mandatory=$true)][string]$Stage)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$Archive=(Resolve-Path -LiteralPath $Archive).Path
$Stage=[IO.Path]::GetFullPath($Stage)
New-Item -ItemType Directory $Stage -ErrorAction Stop|Out-Null
$assembly=[Reflection.Assembly]::LoadFrom((Join-Path $root 'dist/Launcher.exe'))
$flags=[Reflection.BindingFlags]'Static,NonPublic,Public'
$updater=$assembly.GetType('LauncherUpdater')
function Release($version,[bool]$draft=$false){$name="Yo-kai-Watch-2-Windows-x64-v$version.rar";@{tag_name="v$version";draft=$draft;prerelease=$false;assets=@(@{name=$name;state='uploaded';digest=('sha256:'+('1'*64));size=123;browser_download_url="https://github.com/Okxmir/yo-kai-watch-2-recompiled/releases/download/v$version/$name"})}}
$json=ConvertTo-Json @((Release '1.0.1'),(Release '1.0.3'),(Release '9.0.0' $true)) -Depth 8 -Compress
$best=$updater.GetMethod('ParseReleases',$flags).Invoke($null,[object[]]@([string]$json))
$selected=$best.GetType().GetField('Version',[Reflection.BindingFlags]'Instance,NonPublic').GetValue($best)
if($selected.ToString(3) -ne '1.0.3'){throw 'Did not select the newest stable version.'}
$version=[Version]'1.0.0.0';$hash=(Get-FileHash -LiteralPath $Archive).Hash.ToLowerInvariant();$size=(Get-Item -LiteralPath $Archive).Length
$payload=$updater.GetMethod('ExtractVerified',$flags).Invoke($null,[object[]]@([string]$Archive,[string]$Stage,$version,[string]$hash,[long]$size))
$old=Join-Path $Stage 'old builder with spaces';New-Item -ItemType Directory "$old/source","$old/dist/data/games/save" -Force|Out-Null
[IO.File]::WriteAllText("$old/VERSION",'0.9.0');[IO.File]::WriteAllText("$old/source/old.txt",'old source');[IO.File]::WriteAllText("$old/dist/data/games/save/head.yw",'test save');[IO.File]::WriteAllText("$old/test cartridge.3ds",'test rom')
Copy-Item -LiteralPath (Join-Path $root 'dist/Launcher.exe') -Destination "$old/Launcher.exe"
$updater.GetMethod('ApplyPackage',$flags).Invoke($null,[object[]]@([string]$old,[string]$payload,[string](Join-Path $Stage 'backup'),[string](Join-Path $old 'Launcher.exe'),$version))|Out-Null
if((Get-Content "$old/VERSION" -Raw).Trim() -ne '1.0.0' -or (Get-Content "$old/dist/data/games/save/head.yw" -Raw) -ne 'test save' -or (Get-Content "$old/test cartridge.3ds" -Raw) -ne 'test rom' -or !(Test-Path "$Stage/backup/source/old.txt")){throw 'Full update did not preserve the local data.'}
Write-Host 'PASS: stable latest release, verified RAR extraction, updated source and launcher, preserved ROM and saves.'
