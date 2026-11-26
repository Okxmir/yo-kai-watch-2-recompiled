using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Web.Script.Serialization;

internal static class LauncherMaintenance
{
    private static string Stamp(){return DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff")+"-"+Guid.NewGuid().ToString("N").Substring(0,8);}
    internal static string ArchiveCaches(string profile)
    {
        // Keep cache clearing recoverable, and never touch saves or mods.
        profile=Path.GetFullPath(profile);
        string[] known={"cache","upscaled"};
        var present=known.Where(n=>Directory.Exists(Path.Combine(profile,n))).ToArray();
        if(present.Length==0)return null;
        foreach(string name in present)
            if((File.GetAttributes(Path.Combine(profile,name))&FileAttributes.ReparsePoint)!=0)
                throw new IOException("Refusing to move a redirected cache folder.");
        string backup=Path.Combine(profile,"cache-backups","before-reset-"+Stamp());
        Directory.CreateDirectory(backup);
        var moved=new List<string>();
        try {
            foreach(string name in present){Directory.Move(Path.Combine(profile,name),Path.Combine(backup,name));moved.Add(name);}
        } catch {
            foreach(string name in moved)Directory.Move(Path.Combine(backup,name),Path.Combine(profile,name));
            throw;
        }
        return backup;
    }
    internal static string ExportLogs(string root,string destination)
    {
        string logs=Path.Combine(root,"logs");
        if(!Directory.Exists(logs))throw new IOException("No log files yet.");
        string[] files=Directory.GetFiles(logs).Where(f=>new[]{".txt",".log",".csv"}.Contains(Path.GetExtension(f),StringComparer.OrdinalIgnoreCase)&&(File.GetAttributes(f)&FileAttributes.ReparsePoint)==0).ToArray();
        if(files.Length==0)throw new IOException("No log files yet.");
        string output=Path.Combine(destination,"YW2-logs-"+Stamp());
        Directory.CreateDirectory(output);
        foreach(string file in files)File.Copy(file,Path.Combine(output,Path.GetFileName(file)),false);
        return output;
    }
    internal static string CreateDiagnostic(string root,string profile,string exe)
    {
        var report=new StringBuilder();
        report.AppendLine("Yo-kai Watch 2 Recompiled diagnostic");
        report.AppendLine("Launcher: "+LauncherCore.Version);
        report.AppendLine("OS: "+Environment.OSVersion.VersionString);
        report.AppendLine("64-bit process: "+Environment.Is64BitProcess);
        report.AppendLine("Logical CPU cores: "+Environment.ProcessorCount);
        if(!String.IsNullOrEmpty(exe)&&File.Exists(exe)){
            report.AppendLine("Native executable SHA256: "+LauncherUpdater.HashFile(exe));
            string manifest=Path.Combine(Path.GetDirectoryName(exe),"build-manifest.json");
            if(File.Exists(manifest))try{
                var m=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(File.ReadAllText(manifest));
                foreach(string name in new[]{"version","configuration","sanitizer","title_id","code_sha256","rom_sha256"})
                    if(m.ContainsKey(name))report.AppendLine(name+": "+Convert.ToString(m[name]));
            }catch(Exception){report.AppendLine("Build manifest could not be parsed.");}
        }else report.AppendLine("No verified native installation.");
        report.AppendLine();report.AppendLine("[Graphics and gameplay settings]");
        var state=LauncherCore.ReadSettings(Path.Combine(profile,"settings.ini"));
        foreach(string key in new[]{"language","gpu_renderer","prefer_integrated_gpu","frame_limit","frame_interpolation","resolution","aspect_mode","aspect_num","aspect_den","window_width","window_height","window_mode","filter","vsync","single_screen","dual_layout","detached_screen","picture_in_picture","performance_profile","texture_cache_mib","prewarm_pipelines","texture_sampling","hd_textures","audio_latency","stick_deadzone","stick_sensitivity"}){
            string value;if(state.TryGetValue(key,out value))report.AppendLine(key+"="+value);
        }
        string recent=Path.Combine(root,"logs","game-latest.txt");
        if(File.Exists(recent)){
            report.AppendLine();report.AppendLine("[Recent renderer measurements]");
            // Only known measurement lines: no cartridge paths, save contents
            // or arbitrary guest logs in a shareable diagnostic summary.
            var lines=File.ReadLines(recent).Where(x=>x.StartsWith("gpu: renderer active (",StringComparison.Ordinal)||
                x.StartsWith("gpu: prewarmed ",StringComparison.Ordinal)||x.StartsWith("display: selected aspect ",StringComparison.Ordinal)||
                x.StartsWith("fps: ",StringComparison.Ordinal)||x.StartsWith("interpolation: ",StringComparison.Ordinal)).ToList();
            foreach(string line in lines.Skip(Math.Max(0,lines.Count-24)))report.AppendLine(line);
        }
        return report.ToString();
    }
}
