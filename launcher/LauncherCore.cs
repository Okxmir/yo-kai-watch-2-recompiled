using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;

internal sealed class LauncherCore
{
    internal static readonly string Version = typeof(LauncherCore).Assembly.GetName().Version.ToString(3);
    internal const string CodeHash = "b536dfce0a7138f2d5691532604d782b1ea3e20731120cc662baee118c370547";
    internal readonly string Root, Preferences, Dist, Log;
    internal LauncherCore(string root)
    {
        string full = Path.GetFullPath(root);
        Root = full.Length > Path.GetPathRoot(full).Length ? full.TrimEnd(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar) : full;
        Dist = Path.Combine(Root, "dist");
        Preferences = Path.Combine(Root, "launcher-data", "launcher.ini");
        Log = Path.Combine(Root, "logs", "launcher-build.txt");
    }
    internal string Profile(string exe) { return Path.Combine(Path.GetDirectoryName(exe), "data"); }
    internal bool BuiltWithCurrent(string folder) {
        try {
            var m=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(File.ReadAllText(Path.Combine(folder,"build-manifest.json")));
            return Convert.ToString(m["version"])==Version;
        }catch{return false;}
    }
    internal static string Get(IDictionary<string,string> settings, string key, string fallback)
    { string value; return settings.TryGetValue(key, out value) ? value : fallback; }
    internal static Dictionary<string,string> ReadSettings(string path)
    {
        var result = new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
        if (!File.Exists(path)) return result;
        foreach (var line in File.ReadAllLines(path)) { int eq=line.IndexOf('='); if(eq>0) result[line.Substring(0,eq)]=line.Substring(eq+1); }
        return result;
    }
    internal static void WriteSettings(string path, IDictionary<string,string> settings)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path));
        string pending=path+"."+Guid.NewGuid().ToString("N")+".tmp";
        try {
            using (var writer=new StreamWriter(pending,false,new UTF8Encoding(false)))
                foreach(var p in settings) if(p.Key.IndexOfAny(new[]{'\r','\n','='})<0 && p.Value.IndexOfAny(new[]{'\r','\n'})<0)
                    writer.WriteLine(p.Key+"="+p.Value);
            if(File.Exists(path)) File.Replace(pending,path,null); else File.Move(pending,path);
        } finally { if(File.Exists(pending)) File.Delete(pending); }
    }
    internal bool TryInstall(string folder, out string exe, out string rom, out string why)
    {
        exe=null; rom=null; why="No compatible local build found.";
        try {
            var manifest=Path.Combine(folder,"build-manifest.json");
            if(!File.Exists(manifest))return false;
            var m=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(File.ReadAllText(manifest));
            if(!String.Equals(Convert.ToString(m["code_sha256"]),CodeHash,StringComparison.OrdinalIgnoreCase) || Convert.ToString(m["title_id"])!="00040000001B2900") { why="The build uses an unsupported cartridge revision.";return false; }
            string candidate=Path.Combine(folder,"YoKaiWatch2.exe");
            string source=Convert.ToString(m["cartridge"]);
            if(!File.Exists(candidate) || !File.Exists(source)) { why="The game or original cartridge is missing.";return false; }
            if(!String.Equals(LauncherUpdater.HashFile(candidate),Convert.ToString(m["game_exe_sha256"]),StringComparison.OrdinalIgnoreCase)) {why="The native executable failed its integrity check. Rebuild it.";return false;}
            exe=candidate;rom=source;why="";return true;
        } catch(Exception e) {why=e.Message;return false;}
    }
    internal string FindRom()
    {
        foreach(string path in Directory.GetFiles(Root)) {
            string ext=Path.GetExtension(path).ToLowerInvariant();
            if(ext==".3ds" || ext==".cci" || ext==".cxi" || ext==".cia") return path;
        }
        return "";
    }
    internal static void CopyTree(string from,string to)
    {
        Directory.CreateDirectory(to);
        foreach(string f in Directory.GetFiles(from))File.Copy(f,Path.Combine(to,Path.GetFileName(f)),false);
        foreach(string d in Directory.GetDirectories(from))if((File.GetAttributes(d)&FileAttributes.ReparsePoint)==0)CopyTree(d,Path.Combine(to,Path.GetFileName(d)));
    }
}
