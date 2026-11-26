[CmdletBinding()]
param([string]$Version='', [string]$LauncherPath='')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'scripts/hash.ps1')
$sourceVersion=(Get-Content -LiteralPath (Join-Path $root 'VERSION') -Raw).Trim()
if(!$Version){$Version=$sourceVersion}
if($Version -ne $sourceVersion -or $Version -notmatch '^\d+\.\d+\.\d+$'){throw 'Set VERSION before packaging.'}
$rar=Join-Path $env:ProgramFiles 'WinRAR/Rar.exe'
if(!(Test-Path -LiteralPath $rar)){throw 'WinRAR command-line Rar.exe is required to make the Windows RAR.'}
function Invoke-Rar([string]$Arguments,[string]$Directory){
    # Do not pass any password switch, including -p- (some RAR versions
    # interpret it as a literal password). Ignore inherited defaults.
    $start=[Diagnostics.ProcessStartInfo]::new($rar,$Arguments)
    $start.WorkingDirectory=$Directory;$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardInput=$true
    $start.EnvironmentVariables.Remove('RAR');$start.EnvironmentVariables.Remove('RARINISWITCHES')
    $child=[Diagnostics.Process]::Start($start)
    try{$child.StandardInput.Close();if(!$child.WaitForExit(120000)){$child.Kill();throw 'RAR operation timed out'};if($child.ExitCode -ne 0){throw "RAR operation failed: $($child.ExitCode)"}}finally{$child.Dispose()}
}
if(!$LauncherPath){$LauncherPath=Join-Path $root 'dist/Launcher.exe'}
$launcher=(Resolve-Path -LiteralPath $LauncherPath).Path
if([Reflection.AssemblyName]::GetAssemblyName($launcher).Version.ToString(3) -ne $Version){throw 'Compile the current launcher before packaging.'}
$out=Join-Path $root 'packages';New-Item -ItemType Directory $out -Force|Out-Null
$stage=Join-Path $out ('release-v'+$Version+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'));New-Item -ItemType Directory $stage -ErrorAction Stop|Out-Null
$sourceFiles=@('VERSION','README.md','build.cmd','build.ps1','build.sh','YoKaiWatch2Builder','.gitignore','LICENSE','BUILD_INSTRUCTIONS.md','PROGRESS.md','RECOMPILATION_STATUS.md','KNOWN_ISSUES.md','PERFORMANCE.md','ORIGINAL_TIMING_ANALYSIS.md','COMPATIBILITY.md','CHANGELOG.md')
foreach($platform in @('Windows','Linux')){
    $name="Yo-kai-Watch-2-$platform-x64-v$Version";$payload=Join-Path $stage $name;New-Item -ItemType Directory $payload|Out-Null
    foreach($file in $sourceFiles){Copy-Item -LiteralPath (Join-Path $root $file) -Destination $payload}
    foreach($dir in @('source','scripts','launcher')){
        Copy-Item -LiteralPath (Join-Path $root $dir) -Destination $payload -Recurse -Exclude '__pycache__','*.pyc'
    }
    # The Windows build uses this source-only test helper; other local test
    # scripts, captures and fixtures are not required by a released builder.
    New-Item -ItemType Directory (Join-Path $payload 'tests')|Out-Null
    Copy-Item -LiteralPath (Join-Path $root 'tests/instrument-stream-grid.ps1') -Destination (Join-Path $payload 'tests')
    if($platform -eq 'Windows'){Copy-Item -LiteralPath $launcher -Destination (Join-Path $payload 'Launcher.exe')}
    New-Item -ItemType Directory (Join-Path $payload 'licenses')|Out-Null
    Copy-Item -LiteralPath (Join-Path $root 'source/clockwork/LICENSE') -Destination (Join-Path $payload 'licenses/CLOCKWORK-MIT.txt')
    Copy-Item -LiteralPath (Join-Path $root 'launcher/LICENSE-REFERENCE.txt') -Destination (Join-Path $payload 'licenses/LAUNCHER-REFERENCE-MIT.txt')
    Copy-Item -LiteralPath (Join-Path $root 'source/clockwork/runtime/third_party/imgui/LICENSE.txt') -Destination (Join-Path $payload 'licenses/IMGUI-MIT.txt')
    $files=@(Get-ChildItem -LiteralPath $payload -Recurse -File -Force)
    $blocked=$files|Where-Object {$_.Extension -in '.3ds','.cci','.cxi','.cia','.sav','.yw','.nes','.rom','.pyc' -or $_.Name -in 'YoKaiWatch2.exe','YoKaiWatch2','code.bin','build-manifest.json' -or ($_.Extension -eq '.exe' -and $_.FullName -ne (Join-Path $payload 'Launcher.exe')) -or $_.FullName -match '[\\/](\.git|__pycache__|runs|generated|extracted|dist|launcher-data)[\\/]'}
    if($blocked){throw "Unexpected local data: $($blocked.FullName -join ', ')"}
    $archive=Join-Path $stage "$name.rar"
    Invoke-Rar ('a -cfg- -ma5 -r -idq -- "'+$archive+'" "'+$name+'"') $stage
    Invoke-Rar ('t -cfg- -idq -- "'+$archive+'"') $stage
    $entries=@(& tar.exe -tf $archive);if($LASTEXITCODE -or !$entries -or !($entries -match '/VERSION$')){throw 'RAR contents verification failed.'}
    $plainVersion=(& tar.exe -xOf $archive "$name/VERSION") -join "`n"
    if($LASTEXITCODE -or $plainVersion.Trim() -ne $Version){throw 'Archive is not readable without a password.'}
    if($platform -eq 'Linux'){
        # Linux users should use tar.gz to retain executable permission bits.
        $tgz=Join-Path $stage "$name.tar.gz"
        $python=Join-Path $root 'build/p/python/python.exe';if(!(Test-Path -LiteralPath $python)){$python='python'}
        & $python (Join-Path $root 'scripts/package-linux-tar.py') $payload $tgz
        if($LASTEXITCODE){throw 'Linux tar.gz creation failed.'}
        $entries=@(& tar.exe -tf $tgz);if($LASTEXITCODE -or !($entries -match '/YoKaiWatch2Builder$')){throw 'Linux tar.gz verification failed.'}
        [IO.File]::WriteAllText("$tgz.sha256",((Get-Yw2Sha256 $tgz)+'  '+[IO.Path]::GetFileName($tgz)+"`n"))
        Write-Host "Linux (recommended): $tgz"
    }
    [IO.File]::WriteAllText("$archive.sha256",((Get-Yw2Sha256 $archive)+'  '+[IO.Path]::GetFileName($archive)+"`n"))
    Write-Host "$platform RAR: $archive"
}
Write-Host "Completed release archives: $stage"
