# Native EUR Psychic Specters: increase the game's original heap budgets, not
# its map geometry, collision rules, asset data or game logic. The caller has
# already verified the original code SHA-256. Keep the original 3DS budgets
# of every other heap by adding the same 27.25 MiB to the map parent, linear root,
# startup linear allocation and APPMEMALLOC (see loader.cpp/kernel.cpp).
param([Parameter(Mandatory=$true)][string]$GeneratedDirectory)
$ErrorActionPreference='Stop'
$file=Join-Path $GeneratedDirectory 'recomp_0010.c'
$source=[IO.File]::ReadAllText($file)
$original=$source
$patches=@(
    @{Pc='00104894';Old='029c0000';Trial='03dc0000';New='04500000';Count=1}, # 41.75 -> 69 MiB
    @{Pc='00105984';Old='02400000';Trial='03800000';New='03f40000';Count=2}, # 36 -> 63.25 MiB
    @{Pc='001059a4';Old='00840000';Trial='01c40000';New='02380000';Count=2}  # Map: 8.25 -> 35.5 MiB
)
foreach($p in $patches){
    $pattern="(?m)(^    /\* $($p.Pc): mov r[013], #[0-9]+ \*/\r?\n    \{\r?\n        uint32_t res = 0x)($($p.Old)|$($p.Trial)|$($p.New))(u;)"
    $matches=[regex]::Matches($source,$pattern)
    if($matches.Count -ne $p.Count){throw "Unexpected translation at $($p.Pc): found $($matches.Count), expected $($p.Count)."}
    $source=[regex]::Replace($source,$pattern,{param($m) $m.Groups[1].Value+$p.New+$m.Groups[3].Value})
}
if($source -cne $original){[IO.File]::WriteAllText($file,$source,[Text.UTF8Encoding]::new($false))}
