param([switch]$Undo)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$groups=@(@{file='recomp_003a.c';pcs=@('003a0990','003a0cd0');hook='yw2_trace_stream_grid'},@{file='recomp_0039.c';pcs=@('0039c164','0039c1a8','0039c1c4');hook='yw2_trace_stream_registration'},@{file='recomp_0040.c';pcs=@('00404998','00404704','0040494c','00404ad8');hook='yw2_trace_stream_resource'},@{file='recomp_005e.c';pcs=@('005e7c00','005e7c04','005e7ccc');hook='yw2_trace_archive_job'},@{file='recomp_0025.c';pcs=@('0025ed30','0025ed88');hook='yw2_trace_archive_stage'},@{file='recomp_0014.c';pcs=@('00144aa0','00144adc','00144b00','00144b24','00144bc4','00144bd8','00144c00','00144c70');hook='yw2_trace_archive_stage'},@{file='recomp_0023.c';pcs=@('00233d90','00233dd0','0023695c','002369e4','00236a14','00236a18','00236a1c');hook='yw2_trace_archive_finalize'},@{file='recomp_0023.c';pcs=@('00238b7c','00238bc8','00238c00','00238d78','00238e0c','00238e58','00238efc','00239494','002394bc','00239548','00239594','002395d8','00239838','00239860','00239884','00239898');hook='yw2_trace_archive_objects'})
$groups+=@{file='recomp_0021.c';pcs=@('00211bf4','00211c54','00211ca8','00211cbc','00211cf8','00211da0','00211ef8','00211f54','00212134','00218244','002188bc','0021794c','00217974','0021799c');hook='yw2_trace_prm'}
$groups+=@{file='recomp_001f.c';pcs=@('001fde2c','001fde54');hook='yw2_trace_prm'}
$groups+=@{file='recomp_0017.c';pcs=@('0017862c','00178630','0017864c','00172558');hook='yw2_trace_prm'}
foreach($group in $groups){
$file=Join-Path $root ('generated/yw2_eur/'+$group.file)
$code=[IO.File]::ReadAllText($file)
$original=$code
foreach($pc in $group.pcs) {
    $pattern="(?m)^    /\* ${pc}:.*\*/\r?\n"
    $hook="    { extern void $($group.hook)(const Ctx *, uint32_t); $($group.hook)(ctx, 0x${pc}u); }"
    if($Undo){$code=$code.Replace($hook+"`n",'').Replace($hook+"`r`n",'');continue}
    if($code.Contains($hook)){continue}
    if(![regex]::IsMatch($code,$pattern)){throw "Guest instruction $pc was not found."}
    $code=[regex]::Replace($code,$pattern,{param($m) $m.Value+$hook+"`n"})
}
if($code -cne $original){[IO.File]::WriteAllText($file,$code,[Text.UTF8Encoding]::new($false))}
}
# The optional path formatter probe uses a one-argument hook.
$file=Join-Path $root 'generated/yw2_eur/recomp_0010.c'
$code=[IO.File]::ReadAllText($file)
$original=$code
$hook='    { extern void yw2_trace_formatted_path(const Ctx *ctx); yw2_trace_formatted_path(ctx); }'
if($Undo){$code=$code.Replace($hook+"`n",'').Replace($hook+"`r`n",'')}
elseif(!$code.Contains($hook)){
    $pattern='(?m)^    /\* 0010011e: movs r0, r4 \*/\r?\n'
    if(![regex]::IsMatch($code,$pattern)){throw 'Guest path formatter instruction was not found.'}
    $code=[regex]::Replace($code,$pattern,{param($m) $m.Value+$hook+"`n"})
}
if($code -cne $original){[IO.File]::WriteAllText($file,$code,[Text.UTF8Encoding]::new($false))}
