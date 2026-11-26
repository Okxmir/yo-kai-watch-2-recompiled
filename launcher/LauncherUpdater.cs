using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

// Updates replace the builder sources AND launcher, not the player's ROM or
// locally generated game. The game manifest must then match the new version.
internal static class LauncherUpdater
{
    internal const string Repo="Okxmir/yo-kai-watch-2-recompiled";
    internal static readonly Version Current=typeof(LauncherUpdater).Assembly.GetName().Version;
    const long MaxArchive=100L*1024*1024,MaxExpanded=256L*1024*1024;
    static readonly Regex VersionName=new Regex(@"^Yo-kai-Watch-2-Windows-x64-v(\d{1,5}\.\d{1,5}\.\d{1,5})\.rar$",RegexOptions.CultureInvariant);
    static readonly Regex Hex=new Regex(@"^[a-fA-F0-9]{64}$");
    static readonly HashSet<string> Files=new HashSet<string>(new[]{"VERSION","README.md","BUILD_INSTRUCTIONS.md","CHANGELOG.md","COMPATIBILITY.md","KNOWN_ISSUES.md","ORIGINAL_TIMING_ANALYSIS.md","PERFORMANCE.md","PROGRESS.md","RECOMPILATION_STATUS.md","LICENSE","build.ps1","build.cmd","build.sh","YoKaiWatch2Builder",".gitignore","Launcher.exe"},StringComparer.OrdinalIgnoreCase);
    static readonly HashSet<string> Folders=new HashSet<string>(new[]{"source","scripts","launcher","tests","licenses"},StringComparer.OrdinalIgnoreCase);
    internal sealed class Candidate { internal Version Version;internal string Hash,Name;internal Uri Url;internal long Size; }
    public sealed class Release { public string tag_name{get;set;} public bool draft{get;set;} public bool prerelease{get;set;} public Asset[] assets{get;set;} }
    public sealed class Asset { public string name{get;set;} public string state{get;set;} public string digest{get;set;} public string browser_download_url{get;set;} public long size{get;set;} }
    internal static string HashFile(string path){using(var sha=SHA256.Create())using(var stream=File.OpenRead(path))return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-","").ToLowerInvariant();}
    internal static Version ParseVersion(string value){Version v;return Regex.IsMatch(value??"",@"^\d{1,5}\.\d{1,5}\.\d{1,5}$")&&Version.TryParse(value+".0",out v)?v:null;}
    internal static string Display(Version v){return v.ToString(3);}
    internal static string RequiredPath(LauncherCore core){return Path.Combine(core.Root,"launcher-data","required-update.txt");}
    internal static Version Required(LauncherCore core){try{return ParseVersion(File.ReadAllText(RequiredPath(core)).Trim());}catch{return null;}}
    internal static void Require(LauncherCore core,Version value){var old=Required(core);if(old!=null&&old>=value)return;Directory.CreateDirectory(Path.GetDirectoryName(RequiredPath(core)));File.WriteAllText(RequiredPath(core),Display(value),new UTF8Encoding(false));}
    internal static Candidate ParseReleases(string json){
        var releases=new JavaScriptSerializer{MaxJsonLength=2*1024*1024}.Deserialize<Release[]>(json);Candidate best=null;
        foreach(var r in releases??new Release[0]){
            if(r==null||r.draft||r.prerelease||r.tag_name==null||!r.tag_name.StartsWith("v",StringComparison.Ordinal))continue;
            var tag=ParseVersion(r.tag_name.Substring(1));if(tag==null)continue;
            foreach(var a in r.assets??new Asset[0]){
                if(a==null||a.state!="uploaded"||a.size<=0||a.size>MaxArchive)continue;
                var m=VersionName.Match(a.name??"");if(!m.Success||ParseVersion(m.Groups[1].Value)!=tag)continue;
                if(a.digest==null||!a.digest.StartsWith("sha256:",StringComparison.Ordinal)||!Hex.IsMatch(a.digest.Substring(7)))continue;
                string url="https://github.com/"+Repo+"/releases/download/"+r.tag_name+"/"+a.name;
                if(a.browser_download_url!=url)continue;
                if(best==null||tag>best.Version)best=new Candidate{Version=tag,Hash=a.digest.Substring(7),Name=a.name,Size=a.size,Url=new Uri(url)};
            }
        }
        return best;
    }
    static HttpWebRequest Request(Uri uri,string accept,int timeout){
        ServicePointManager.SecurityProtocol|=SecurityProtocolType.Tls12;
        var r=(HttpWebRequest)WebRequest.Create(uri);r.UserAgent="YoKaiWatch2-Launcher/"+Display(Current);r.Accept=accept;r.Timeout=timeout;r.ReadWriteTimeout=timeout;r.AllowAutoRedirect=false;return r;
    }
    static long CopyLimit(Stream input,Stream output,long max){byte[] buf=new byte[65536];long total=0;int n;while((n=input.Read(buf,0,buf.Length))!=0){total+=n;if(total>max)throw new InvalidDataException("Update exceeds the expected size.");output.Write(buf,0,n);}return total;}
    internal static Candidate Check(){var r=Request(new Uri("https://api.github.com/repos/"+Repo+"/releases?per_page=30"),"application/vnd.github+json",8000);r.Headers["X-GitHub-Api-Version"]="2022-11-28";using(var response=(HttpWebResponse)r.GetResponse())using(var data=new MemoryStream())using(var input=response.GetResponseStream()){CopyLimit(input,data,2*1024*1024);return ParseReleases(Encoding.UTF8.GetString(data.ToArray()));}}
    static bool Trusted(Uri u){return u.Scheme=="https"&&u.IsDefaultPort&&u.UserInfo.Length==0&&(u.Host=="github.com"||u.Host=="release-assets.githubusercontent.com"||u.Host=="objects.githubusercontent.com");}
    static void Download(Candidate asset,string path){Uri u=asset.Url;for(int i=0;i<5;i++){if(!Trusted(u))throw new InvalidDataException("Unexpected update download host.");using(var response=(HttpWebResponse)Request(u,"application/octet-stream",25000).GetResponse()){int status=(int)response.StatusCode;if(status>=300&&status<400){u=new Uri(u,response.Headers["Location"]);continue;}using(var output=new FileStream(path,FileMode.CreateNew))using(var input=response.GetResponseStream()){if(status!=200||CopyLimit(input,output,asset.Size)!=asset.Size)throw new IOException("Incomplete update download.");}return;}}throw new IOException("Too many update redirects.");}
    static string Quote(string p){if(p.IndexOf('"')>=0)throw new InvalidDataException("Invalid path.");return "\""+p+"\"";}
    static string Tar(string args,string cwd){string path=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"tar.exe");if(!File.Exists(path))throw new IOException("Windows tar.exe is required to install the update.");var start=new ProcessStartInfo(path,args){WorkingDirectory=cwd,UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};using(var p=Process.Start(start)){var a=p.StandardOutput.ReadToEndAsync();var b=p.StandardError.ReadToEndAsync();if(!p.WaitForExit(120000)){p.Kill();throw new IOException("Update archive operation timed out.");}string output=a.GetAwaiter().GetResult(),err=b.GetAwaiter().GetResult();if(p.ExitCode!=0||output.Length>4*1024*1024)throw new InvalidDataException("Cannot read update archive: "+err);return output;}}
    static bool SafeComponent(string s){return Regex.IsMatch(s,@"^[A-Za-z0-9._ -]+$")&&s!="."&&s!=".."&&!s.EndsWith(" ")&&!s.EndsWith(".")&&!Regex.IsMatch(s,@"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)",RegexOptions.IgnoreCase);}
    internal static string ExtractVerified(string archive,string stage,Version version,string digest,long size){
        if(new FileInfo(archive).Length!=size||size<=0||size>MaxArchive||!Hex.IsMatch(digest??"")||!String.Equals(HashFile(archive),digest,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Update SHA-256 or size mismatch.");
        string folder="Yo-kai-Watch-2-Windows-x64-v"+Display(version);
        string[] entries=Tar("-tf "+Quote(archive),stage).Split(new[]{'\r','\n'},StringSplitOptions.RemoveEmptyEntries);
        string[] details=Tar("-tvf "+Quote(archive),stage).Split(new[]{'\r','\n'},StringSplitOptions.RemoveEmptyEntries);
        if(entries.Length==0||entries.Length>5000||entries.Length!=details.Length)throw new InvalidDataException("Unexpected package entry count.");
        var seen=new HashSet<string>(StringComparer.OrdinalIgnoreCase);long total=0;
        for(int i=0;i<entries.Length;i++){
            string entry=entries[i].Replace('\\','/').TrimEnd('/');var parts=entry.Split('/');
            if(parts[0]!=folder||parts.Any(p=>!SafeComponent(p))||!seen.Add(entry))throw new InvalidDataException("Unsafe update archive path: "+entry);
            var m=Regex.Match(details[i],@"^[-d]\S*\s+\d+\s+\S+\s+\S+\s+(\d+)\s+");long n;if(!m.Success||!Int64.TryParse(m.Groups[1].Value,out n)||n<0||n>64L*1024*1024||(total+=n)>MaxExpanded)throw new InvalidDataException("Unexpected archive member type or size.");
            if(parts.Length==1){if(details[i][0]!='d')throw new InvalidDataException("Invalid package root.");continue;}
            if(parts.Length==2&&!Files.Contains(parts[1])&&!Folders.Contains(parts[1])||parts.Length>2&&!Folders.Contains(parts[1]))throw new InvalidDataException("Package contains an unmanaged path: "+entry);
            if(parts.Any(p=>String.Equals(p,"runs",StringComparison.OrdinalIgnoreCase)||String.Equals(p,".git",StringComparison.OrdinalIgnoreCase))||Regex.IsMatch(entry,@"\.(3ds|cci|cxi|cia|sav|yw)$",RegexOptions.IgnoreCase))throw new InvalidDataException("Package contains local data.");
        }
        string extracted=Path.Combine(stage,"extracted");Directory.CreateDirectory(extracted);Tar("-xf "+Quote(archive)+" -C "+Quote(extracted),stage);string payload=Path.Combine(extracted,folder);
        ValidatePayload(payload,version);return payload;
    }
    static void ValidatePayload(string payload,Version version){if(File.ReadAllText(Path.Combine(payload,"VERSION")).Trim()!=Display(version)||!File.Exists(Path.Combine(payload,"build.ps1"))||!Directory.Exists(Path.Combine(payload,"source"))||!Directory.Exists(Path.Combine(payload,"launcher")))throw new InvalidDataException("Incomplete builder update.");var assembly=AssemblyName.GetAssemblyName(Path.Combine(payload,"Launcher.exe"));if(assembly.Name!=typeof(LauncherUpdater).Assembly.GetName().Name||assembly.Version!=version)throw new InvalidDataException("Incorrect launcher version in archive.");}
    internal static void StageAndRestart(LauncherCore core,Candidate a){
        if(a.Version<=Current)throw new IOException("Cannot downgrade the launcher.");
        string stage=Path.Combine(core.Root,"launcher-data","updates",Guid.NewGuid().ToString("N"));Directory.CreateDirectory(stage);string archive=Path.Combine(stage,"package.rar");Download(a,archive);
        string payload=ExtractVerified(archive,stage,a.Version,a.Hash,a.Size);string target=Assembly.GetExecutingAssembly().Location;
        string kind=String.Equals(Path.GetDirectoryName(target),core.Root,StringComparison.OrdinalIgnoreCase)?"root":"dist";
        string expected=Path.Combine(kind=="root"?core.Root:core.Dist,"Launcher.exe");if(!String.Equals(expected,target,StringComparison.OrdinalIgnoreCase))throw new IOException("Open Launcher.exe from the builder folder.");
        string helper=Path.Combine(stage,"finish.exe");File.Copy(target,helper);string hash=HashFile(Path.Combine(payload,"Launcher.exe"));
        File.WriteAllText(Path.Combine(stage,"verified.txt"),Display(a.Version)+"\n"+hash+"\n"+a.Hash+"\n"+a.Size,new UTF8Encoding(false));
        using(var p=Process.Start(new ProcessStartInfo(helper,"--finish-update "+Process.GetCurrentProcess().Id+" "+kind){WorkingDirectory=stage,UseShellExecute=false}))if(p==null)throw new IOException("Could not start update helper.");
    }
    // The backup is intentionally kept, including any locally edited builder source.
    internal static void ApplyPackage(string root,string payload,string backup,string target,Version version){
        ValidatePayload(payload,version);Directory.CreateDirectory(backup);var moved=new List<string>();var installed=new List<string>();
        Action<string,string> install=(from,to)=>{string rel=to.Substring(root.Length).TrimStart(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar);string save=Path.Combine(backup,rel);if(Directory.Exists(to)||File.Exists(to)){Directory.CreateDirectory(Path.GetDirectoryName(save));if(Directory.Exists(to))Directory.Move(to,save);else File.Move(to,save);moved.Add(rel);}Directory.CreateDirectory(Path.GetDirectoryName(to));if(Directory.Exists(from))Directory.Move(from,to);else File.Copy(from,to);installed.Add(rel);};
        try{
            foreach(string name in new[]{"source","scripts","launcher","licenses"}){string p=Path.Combine(payload,name);if(Directory.Exists(p))install(p,Path.Combine(root,name));}
            foreach(string name in Files.Where(s=>s!="Launcher.exe")){string p=Path.Combine(payload,name);if(File.Exists(p))install(p,Path.Combine(root,name));}
            string tests=Path.Combine(payload,"tests");if(Directory.Exists(tests))foreach(string f in Directory.GetFiles(tests))install(f,Path.Combine(root,"tests",Path.GetFileName(f)));
            install(Path.Combine(payload,"Launcher.exe"),target);
            if(!String.Equals(target,Path.Combine(root,"Launcher.exe"),StringComparison.OrdinalIgnoreCase)&&File.Exists(Path.Combine(root,"Launcher.exe")))install(Path.Combine(payload,"Launcher.exe"),Path.Combine(root,"Launcher.exe"));
        }catch{
            foreach(string rel in Enumerable.Reverse(installed)){string p=Path.Combine(root,rel),failed=Path.Combine(backup,"failed-new",rel);Directory.CreateDirectory(Path.GetDirectoryName(failed));if(Directory.Exists(p))Directory.Move(p,failed);else if(File.Exists(p))File.Move(p,failed);}
            foreach(string rel in Enumerable.Reverse(moved)){string p=Path.Combine(root,rel),old=Path.Combine(backup,rel);if(Directory.Exists(old))Directory.Move(old,p);else File.Move(old,p);}throw;
        }
    }
    internal static void Finish(string[] args){
        string stage=Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);var dir=new DirectoryInfo(stage);int pid;
        if(args.Length!=3||!Int32.TryParse(args[1],out pid)||pid<=0||(args[2]!="root"&&args[2]!="dist")||dir.Parent==null||dir.Parent.Name!="updates"||dir.Parent.Parent==null||dir.Parent.Parent.Name!="launcher-data"||!Regex.IsMatch(dir.Name,@"^[0-9a-f]{32}$"))throw new InvalidDataException("Invalid update handoff.");
        string root=dir.Parent.Parent.Parent.FullName,target=Path.Combine(args[2]=="root"?root:Path.Combine(root,"dist"),"Launcher.exe");
        string[] info=File.ReadAllLines(Path.Combine(stage,"verified.txt"));var version=info.Length==4?ParseVersion(info[0]):null;long size;
        if(version==null||version<=Current||!Hex.IsMatch(info[1])||!Hex.IsMatch(info[2])||!Int64.TryParse(info[3],out size))throw new InvalidDataException("Invalid update verification record.");
        string archive=Path.Combine(stage,"package.rar"),payload=Path.Combine(stage,"extracted","Yo-kai-Watch-2-Windows-x64-v"+Display(version));
        if(new FileInfo(archive).Length!=size||HashFile(archive)!=info[2].ToLowerInvariant()||HashFile(Path.Combine(payload,"Launcher.exe"))!=info[1].ToLowerInvariant())throw new InvalidDataException("Staged update changed.");
        try{using(var old=Process.GetProcessById(pid)){if(!old.HasExited&&(!String.Equals(old.MainModule.FileName,target,StringComparison.OrdinalIgnoreCase)||!old.WaitForExit(30000)))throw new IOException("Close the old launcher and retry.");}}catch(ArgumentException){}
        if(AssemblyName.GetAssemblyName(target).Version>=version)throw new IOException("Launcher already updated.");
        ApplyPackage(root,payload,Path.Combine(stage,"backup"),target,version);
        using(var p=Process.Start(new ProcessStartInfo(target){UseShellExecute=false,WorkingDirectory=Path.GetDirectoryName(target)}))if(p==null)throw new IOException("Could not reopen the updated launcher.");
    }
}
