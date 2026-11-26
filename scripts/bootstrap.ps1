# Pinned portable tools, based on the Super Mario 3D Land builder. No VS,
# system Python, Git account, firmware, cartridge keys or global installation.
function Initialize-Yw2Tools([string]$Root) {
    $portable = Join-Path $Root 'build/p'
    $downloads = Join-Path $portable 'downloads'
    New-Item -ItemType Directory -Path $downloads -Force | Out-Null
    function Get-Pinned($name, $url, $sha) {
        $path = Join-Path $downloads $name
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
            $partial = "$path.partial"
            Write-Host "Downloading $name"
            & curl.exe --fail --location --silent --show-error --retry 2 --connect-timeout 30 --output $partial $url
            if ($LASTEXITCODE -ne 0) { throw "Download failed: $name" }
            if ((Get-Yw2Sha256 $partial) -ne $sha) { throw "Invalid checksum: $name" }
            Move-Item -LiteralPath $partial -Destination $path -Force
        }
        if ((Get-Yw2Sha256 $path) -ne $sha) { throw "Invalid checksum: $name" }
        return $path
    }
    function Unpack-Tool($name, $file, $url, $sha, $marker, [int]$strip=0) {
        $dest = Join-Path $portable $name
        if (Test-Path -LiteralPath (Join-Path $dest $marker) -PathType Leaf) { return }
        $archive = Get-Pinned $file $url $sha
        New-Item -ItemType Directory -Path $dest -Force | Out-Null
        & tar.exe -xf $archive -C $dest "--strip-components=$strip"
        if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath (Join-Path $dest $marker))) { throw "Extraction failed: $file" }
    }
    Unpack-Tool llvm 'llvm-mingw-20260616-ucrt-x86_64.zip' `
        'https://github.com/mstorsjo/llvm-mingw/releases/download/20260616/llvm-mingw-20260616-ucrt-x86_64.zip' `
        'B9B68A4D276E16FA25802AABA458E4638F64B3884C290AACCDC2D87083B6CA35' 'bin/x86_64-w64-mingw32-clang++.exe' 1
    Unpack-Tool cmake 'cmake-3.31.12-windows-x86_64.zip' `
        'https://github.com/Kitware/CMake/releases/download/v3.31.12/cmake-3.31.12-windows-x86_64.zip' `
        '0C4BAA40F28B3F8225EB3FDF6946C987B4FE901403B4EAF2FBBD9378100AAA0C' 'bin/cmake.exe' 1
    Unpack-Tool ninja 'ninja-win.zip' 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip' `
        '07FC8261B42B20E71D1720B39068C2E14FFCEE6396B76FB7A795FB460B78DC65' 'ninja.exe'
    Unpack-Tool python 'python-3.13.12-embed-amd64.zip' `
        'https://www.python.org/ftp/python/3.13.12/python-3.13.12-embed-amd64.zip' `
        '76F238F606250C87C6BEAC75DCCD35EE99070A13490555936ABB6CB64ECCE3D0' 'python.exe'
    $target='x86_64-pc-windows-gnullvm'
    $rustVersion="1.100.0-$target"
    $rustRoot=Join-Path $portable 'rust'
    if (!(Test-Path -LiteralPath "$rustRoot/bin/rustc.exe") -or !(Test-Path -LiteralPath "$rustRoot/lib/rustlib/$target/lib")) {
        $base='https://static.rust-lang.org/dist/2026-11-12'
        $compiler=Get-Pinned "rustc-$rustVersion.tar.xz" "$base/rustc-$rustVersion.tar.xz" '1D4F93B7264C098BF90C86B48B7953A9009F7DC01E4C2BDC8BA30D9407ED47D9'
        $stdlib=Get-Pinned "rust-std-$rustVersion.tar.xz" "$base/rust-std-$rustVersion.tar.xz" '5E63B95C73BD783864B4350792C27292D67AAB8E3033BF49381D0E55EF4384CF'
        New-Item -ItemType Directory -Path $rustRoot -Force | Out-Null
        $extract=@'
import sys,tarfile
archive,dest,component=sys.argv[1:]
prefix=component+'/'
def member_filter(member, dest):
    if not member.name.startswith(prefix) or member.name==prefix:
        return None
    relative=member.replace(name=member.name[len(prefix):],deep=False)
    return tarfile.data_filter(relative,dest)
with tarfile.open(archive,'r|xz',stream=True) as package:
    package.extractall(dest,filter=member_filter)
'@
        & "$portable/python/python.exe" -c $extract $compiler $rustRoot "rustc-$rustVersion/rustc"
        if ($LASTEXITCODE -ne 0) { throw 'Rust extraction failed.' }
        & "$portable/python/python.exe" -c $extract $stdlib $rustRoot "rust-std-$rustVersion/rust-std-$target"
        if ($LASTEXITCODE -ne 0) { throw 'Rust standard library extraction failed.' }
    }
    $third=Join-Path $Root 'third_party'
    New-Item -ItemType Directory -Path $third -Force | Out-Null
    if (!(Test-Path -LiteralPath "$third/SDL3-3.4.0/cmake/SDL3Config.cmake")) {
        $sdl=Get-Pinned 'SDL3-devel-3.4.0-VC.zip' 'https://github.com/libsdl-org/SDL/releases/download/release-3.4.0/SDL3-devel-3.4.0-VC.zip' '4489BA0070B8CE047B8717E4B71FA7E35AC71362E7A6BB2A90A7A27F624B3D5D'
        & tar.exe -xf $sdl -C $third
        if ($LASTEXITCODE -ne 0) { throw 'SDL3 extraction failed.' }
    }
    if (!(Test-Path -LiteralPath "$third/memmap2-0.9.11/src/lib.rs")) {
        $memmap=Get-Pinned 'memmap2-0.9.11.crate' 'https://static.crates.io/crates/memmap2/memmap2-0.9.11.crate' 'D1219ED1B7F229EE7104D281DD01D6802FE28BB6E95D292942C4DAACDEB798C0'
        & tar.exe -xf $memmap -C $third
        if ($LASTEXITCODE -ne 0) { throw 'memmap2 extraction failed.' }
    }
    foreach ($name in @('INCLUDE','LIB','LIBPATH','CC','CXX','CFLAGS','CXXFLAGS','LDFLAGS','CMAKE_TOOLCHAIN_FILE')) {
        Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
    }
    $env:Path="$portable/llvm/bin;$portable/ninja;$portable/cmake/bin;$env:Path"
}
