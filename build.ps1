[CmdletBinding()]
param([string]$Cartridge, [int]$Jobs=6, [switch]$ValidateOnly, [string]$OutputDirectory='dist', [switch]$SkipLauncher, [switch]$MapDiagnostics,
      [ValidateSet('Release','Debug','RelWithDebInfo')][string]$Configuration='Release',
      [ValidateSet('None','Undefined')][string]$Sanitizer='None')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
Set-Location -LiteralPath $root
. (Join-Path $root 'scripts/hash.ps1')
$version=(Get-Content -LiteralPath (Join-Path $root 'VERSION') -Raw).Trim()
if($version -notmatch '^\d+\.\d+\.\d+$'){throw 'Invalid VERSION file.'}
if (!$Cartridge) {
    $roms=@(Get-ChildItem -LiteralPath $root -File | Where-Object Extension -in '.3ds','.cci','.cxi','.cia')
    if ($roms.Count -ne 1) { throw 'Select one decrypted European Psychic Specters dump using -Cartridge.' }
    $Cartridge=$roms[0].FullName
}
$Cartridge=(Resolve-Path -LiteralPath $Cartridge).Path
if(!$PSBoundParameters.ContainsKey('OutputDirectory') -and ($Configuration -ne 'Release' -or $Sanitizer -ne 'None')){
    $OutputDirectory='dist-'+$Configuration.ToLowerInvariant()+$(if($Sanitizer -ne 'None'){'-ubsan'}else{''})
}
$out=if([IO.Path]::IsPathRooted($OutputDirectory)){[IO.Path]::GetFullPath($OutputDirectory)}else{[IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))}
if(!$out.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'The generated installation must remain inside this project.'}
if ($Jobs -lt 1 -or $Jobs -gt 32) { throw 'Jobs must be between 1 and 32.' }
New-Item -ItemType Directory -Force -Path 'build/tools','logs' | Out-Null
# Serialize local builds without ever modifying the input cartridge.
$lockPath=Join-Path $root 'build/build.lock'
$lock=[IO.File]::Open($lockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try {
    . (Join-Path $root 'scripts/bootstrap.ps1')
    Write-Host 'Build stage: Preparing portable dependencies'
    Initialize-Yw2Tools $root
    $rust=Join-Path $root 'build/p/rust/bin/rustc.exe'
    function Check-Exit($stage) { if ($LASTEXITCODE -ne 0) { throw "$stage failed (exit $LASTEXITCODE)." } }
    function Invoke-NativeLogged([string]$Program,[string[]]$Arguments,[string]$Log) {
        # Windows PowerShell 5 wraps native stderr in ErrorRecord. Compiler
        # warnings are not failed builds; the actual process exit code is.
        $oldPreference=$ErrorActionPreference
        try {
            $ErrorActionPreference='Continue'
            & $Program @Arguments 2>&1 | ForEach-Object { $_.ToString() } | Tee-Object -FilePath $Log
            $code=$LASTEXITCODE
        } finally { $ErrorActionPreference=$oldPreference }
        if ($code -ne 0) { throw "$Program failed (exit $code). See $Log." }
    }
    Write-Host 'Build stage: Compiling the extractor and native recompiler'
    & $rust --edition=2021 -C opt-level=2 --crate-type rlib --crate-name memmap2 'third_party/memmap2-0.9.11/src/lib.rs' -o build/tools/libmemmap2.rlib
    Check-Exit 'memmap2 compilation'
    & $rust --edition=2024 -C opt-level=2 -C debuginfo=0 --crate-name ctrx --extern memmap2=build/tools/libmemmap2.rlib source/clockwork/tools/ctrx/src/main.rs -o build/tools/ctrx.exe
    Check-Exit 'Extractor compilation'
    & $rust --edition=2024 -C opt-level=2 -C debuginfo=0 --crate-name recomp source/clockwork/tools/recomp/src/main.rs -o build/tools/recomp.exe
    Check-Exit 'Recompiler compilation'
    Write-Host 'Build stage: Validating and extracting the cartridge'
    Invoke-NativeLogged (Join-Path $root 'build/tools/ctrx.exe') @('--code-only',$Cartridge,'extracted/yw2_eur') 'logs/extraction.txt'
    $info=Get-Content -LiteralPath 'extracted/yw2_eur/info.txt' -Raw
    if ($info -notmatch 'program id:\s+00040000001B2900' -or $info -notmatch 'product code:\s+CTR-P-BYSP') { throw 'This is not the supported European Psychic Specters cartridge.' }
    $codeHash=Get-Yw2Sha256 'extracted/yw2_eur/code.bin'
    if ($codeHash -ne 'B536DFCE0A7138F2D5691532604D782B1EA3E20731120CC662BAEE118C370547') { throw 'Unsupported game revision. The decompressed code does not match the supported EUR revision.' }
    $romHash=Get-Yw2Sha256 $Cartridge
    Write-Host "Validated cartridge: $romHash"
    if ($ValidateOnly) { return }
    Write-Host 'Build stage: Static ARM to C recompilation'
    # Fingerprint the translator sources rather than a PE timestamp, making
    # unchanged repeat builds genuinely incremental.
    $sourceHashes=(Get-ChildItem -LiteralPath 'source/clockwork/tools/recomp/src' -Recurse -Filter '*.rs' -File | Sort-Object FullName | ForEach-Object {Get-Yw2Sha256 $_.FullName}) -join ' '
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $generatorHash=[BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::ASCII.GetBytes($sourceHashes))).Replace('-','') } finally {$sha.Dispose()}
    $signature="$codeHash $generatorHash"
    $stamp='generated/yw2_eur/.recompiled'
    if (!(Test-Path -LiteralPath $stamp) -or (Get-Content -LiteralPath $stamp -Raw).Trim() -ne $signature -or !(Test-Path -LiteralPath 'generated/yw2_eur/lookup.c')) {
        Invoke-NativeLogged (Join-Path $root 'build/tools/recomp.exe') @('emit','extracted/yw2_eur','generated/yw2_eur') 'logs/recompile.txt'
        [IO.File]::WriteAllText((Join-Path $root $stamp),$signature)
    }
    & "$root/scripts/apply-native-map-memory.ps1" -GeneratedDirectory (Join-Path $root 'generated/yw2_eur')
    # Normal builds never contain the per-instruction map debugging calls.
    & "$root/tests/instrument-stream-grid.ps1" -Undo:(!$MapDiagnostics)
    Write-Host 'Build stage: Compiling the native Windows x64 game'
    $p=$root.Replace('\','/')
    $cmake=Join-Path $root 'build/p/cmake/bin/cmake.exe'
    $buildName='windows-x64'
    if($Configuration -ne 'Release' -or $Sanitizer -ne 'None'){$buildName+='-'+$Configuration.ToLowerInvariant()+$(if($Sanitizer -ne 'None'){'-ubsan'}else{''})}
    $nativeBuild="$p/build/$buildName"
    $instrumentation=if($Sanitizer -eq 'Undefined'){'-fsanitize=undefined -fno-sanitize-recover=undefined'}else{''}
    Invoke-NativeLogged $cmake @('-S',"$p/source/clockwork",'-B',$nativeBuild,'-G','Ninja',"-DCMAKE_C_COMPILER=$p/build/p/llvm/bin/x86_64-w64-mingw32-clang.exe","-DCMAKE_CXX_COMPILER=$p/build/p/llvm/bin/x86_64-w64-mingw32-clang++.exe","-DCMAKE_MAKE_PROGRAM=$p/build/p/ninja/ninja.exe","-DCMAKE_PREFIX_PATH=$p/third_party/SDL3-3.4.0","-DYW2_EUR_GENERATED_DIR=$p/generated/yw2_eur","-DCMAKE_BUILD_TYPE=$Configuration",'-DCMAKE_C_FLAGS_RELEASE=-O1 -DNDEBUG','-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG','-DCMAKE_C_FLAGS_DEBUG=-O0 -g','-DCMAKE_CXX_FLAGS_DEBUG=-O0 -g','-DCMAKE_C_FLAGS_RELWITHDEBINFO=-O1 -g -DNDEBUG','-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O2 -g -DNDEBUG',"-DCMAKE_C_FLAGS=$instrumentation","-DCMAKE_CXX_FLAGS=$instrumentation","-DCMAKE_EXE_LINKER_FLAGS=$instrumentation",'-DYW2_LOCAL_ONLY=ON') "logs/configure-$buildName.txt"
    Invoke-NativeLogged $cmake @('--build',$nativeBuild,'--target','yw2_eur','--parallel',[string]$Jobs) "logs/build-native-$buildName.txt"
    Write-Host 'Build stage: Preparing the local game folder'
    New-Item -ItemType Directory -Force -Path $out,"$out/data","$out/data/mods/yw2","$out/licenses" | Out-Null
    Copy-Item -LiteralPath "$nativeBuild/YoKaiWatch2.exe",'third_party/SDL3-3.4.0/lib/x64/SDL3.dll' -Destination $out -Force
    foreach($dll in @('libc++.dll','libunwind.dll','libwinpthread-1.dll')) {
        $path="build/p/llvm/x86_64-w64-mingw32/bin/$dll"
        if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $out -Force }
    }
    Copy-Item -LiteralPath 'source/clockwork/LICENSE' -Destination "$out/licenses/CLOCKWORK-MIT.txt" -Force
    Copy-Item -LiteralPath 'third_party/SDL3-3.4.0/LICENSE.txt' -Destination "$out/licenses/SDL3.txt" -Force
    Copy-Item -LiteralPath 'source/clockwork/runtime/third_party/imgui/LICENSE.txt' -Destination "$out/licenses/IMGUI-MIT.txt" -Force
    Copy-Item -LiteralPath 'launcher/LICENSE-REFERENCE.txt' -Destination "$out/licenses/LAUNCHER-REFERENCE-MIT.txt" -Force
    # A CLI build creates the corresponding launcher from current source.
    # The graphical launcher cannot replace its own running executable while
    # a build is in progress, so that caller explicitly skips this step.
    if(!$SkipLauncher){Write-Host 'Build stage: Compiling the launcher'; & "$root/launcher/build.ps1" -OutputDirectory $out}
    $manifest=@{version=$version;configuration=$Configuration;sanitizer=$Sanitizer;cartridge=$Cartridge;rom_sha256=$romHash;code_sha256=$codeHash;title_id='00040000001B2900';game_exe='YoKaiWatch2.exe';game_exe_sha256=(Get-Yw2Sha256 "$out/YoKaiWatch2.exe")}
    $json=$manifest | ConvertTo-Json
    [IO.File]::WriteAllText((Join-Path $out 'build-manifest.json'),$json,(New-Object Text.UTF8Encoding($false)))
    Write-Host "Build complete: $out/YoKaiWatch2.exe"
} finally { $lock.Dispose() }
