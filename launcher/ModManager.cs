using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

internal static class ModManager
{
    private static readonly StringComparer Names=StringComparer.OrdinalIgnoreCase;
    private static readonly string[] Kinds={"romfs","include","loose","textures"};
    private static bool OrdinaryDirectory(string dir){return Directory.Exists(dir)&&(File.GetAttributes(dir)&FileAttributes.ReparsePoint)==0;}
    private static string OrderFile(string root){return Path.Combine(root,".order");}
    internal static string[] Ordered(string root)
    {
        if(!OrdinaryDirectory(root))return new string[0];
        var installed=Directory.GetDirectories(root).Where(OrdinaryDirectory).Select(Path.GetFileName).OrderBy(n=>n,Names).ToArray();
        var result=new List<string>();
        var seen=new HashSet<string>(Names);
        string file=OrderFile(root);
        if(File.Exists(file)&&(File.GetAttributes(file)&FileAttributes.ReparsePoint)==0)
            foreach(string name in File.ReadAllLines(file)){
                string actual=installed.FirstOrDefault(n=>Names.Equals(n,name));
                if(actual!=null&&seen.Add(actual))result.Add(actual);
            }
        foreach(string name in installed)if(seen.Add(name))result.Add(name);
        return result.ToArray();
    }
    private static void WriteOrder(string root,IEnumerable<string> requested)
    {
        var installed=Ordered(root);
        var chosen=new List<string>();var seen=new HashSet<string>(Names);
        foreach(string name in requested){string n=installed.FirstOrDefault(x=>Names.Equals(x,name));if(n!=null&&seen.Add(n))chosen.Add(n);}
        foreach(string n in installed)if(seen.Add(n))chosen.Add(n);
        Directory.CreateDirectory(root);
        string dest=OrderFile(root),pending=dest+"."+Guid.NewGuid().ToString("N")+".tmp";
        if(File.Exists(dest)&&(File.GetAttributes(dest)&FileAttributes.ReparsePoint)!=0)throw new IOException("The mod order file is redirected.");
        File.WriteAllLines(pending,chosen,new UTF8Encoding(false));
        if(File.Exists(dest))File.Replace(pending,dest,null);else File.Move(pending,dest);
    }
    internal static bool Enabled(string directory){return OrdinaryDirectory(directory)&&!File.Exists(Path.Combine(directory,"disabled"))&&!Directory.Exists(Path.Combine(directory,"disabled"));}
    private static void SetEnabled(string directory,bool enabled)
    {
        if(!OrdinaryDirectory(directory))return;
        string marker=Path.Combine(directory,"disabled");
        if(Directory.Exists(marker))throw new IOException("Unexpected directory named disabled in "+Path.GetFileName(directory));
        if(File.Exists(marker)&&(File.GetAttributes(marker)&FileAttributes.ReparsePoint)!=0)throw new IOException("Redirected mod marker.");
        if(enabled){if(File.Exists(marker))File.Delete(marker);}
        else if(!File.Exists(marker))using(File.Create(marker)){}
    }
    internal static void Toggle(string root,string name)
    {
        string actual=Ordered(root).FirstOrDefault(n=>Names.Equals(n,name));if(actual==null)throw new IOException("Mod not found.");
        string path=Path.Combine(root,actual);SetEnabled(path,!Enabled(path));
    }
    internal static void SetRegional(string directory,bool enabled){SetEnabled(directory,enabled);}
    internal static void Move(string root,string name,int delta)
    {
        var order=Ordered(root).ToList();int i=order.FindIndex(n=>Names.Equals(n,name)),j=i+delta;
        if(i<0||j<0||j>=order.Count)return;
        string other=order[j];order[j]=order[i];order[i]=other;WriteOrder(root,order);
    }
    private static IEnumerable<string> Files(string root)
    {
        if(!OrdinaryDirectory(root))yield break;
        foreach(string file in Directory.GetFiles(root))if((File.GetAttributes(file)&FileAttributes.ReparsePoint)==0)yield return file;
        foreach(string dir in Directory.GetDirectories(root).Where(OrdinaryDirectory))foreach(string file in Files(dir))yield return file;
    }
    internal static string[] Conflicts(string root,string regional)
    {
        var owners=new Dictionary<string,string>(Names);var result=new List<string>();
        var ordered=Ordered(root).Where(n=>Enabled(Path.Combine(root,n))).Select(n=>new[]{n,Path.Combine(root,n)}).ToList();
        if(Enabled(regional))ordered.Add(new[]{"EUR-specific",regional});
        foreach(var mod in ordered){
            string name=mod[0],directory=mod[1];
            foreach(string kind in name=="EUR-specific"?new[]{"romfs"}:Kinds){
                string baseDir=Path.Combine(directory,kind);
                foreach(string file in Files(baseDir)){
                    string relative=file.Substring(baseDir.Length).TrimStart(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar).Replace('\\','/');
                    if(kind=="loose")relative=Path.GetFileName(file);
                    if(kind=="textures"){var hash=Regex.Match(Path.GetFileName(file),@"^[0-9a-fA-F]{16}");relative=hash.Success?hash.Value:Path.GetFileName(file);}
                    string key=kind+":"+relative,prior;
                    if(owners.TryGetValue(key,out prior)&&!Names.Equals(prior,name))result.Add(relative+" ("+kind+"): "+prior+" → "+name);
                    owners[key]=name;
                }
            }
            if(File.Exists(Path.Combine(directory,"code.bin"))||File.Exists(Path.Combine(directory,"code.ips"))||Directory.Exists(Path.Combine(directory,"exefs")))
                result.Add(name+": executable code patches are unsupported by the pinned native build.");
        }
        return result.ToArray();
    }
    internal static void RestoreOriginal(string root,string regional){foreach(string name in Ordered(root))SetEnabled(Path.Combine(root,name),false);SetEnabled(regional,false);}
    private static string ProfilePath(string directory,string name){if(!ControlProfileManager.ValidName(name))throw new IOException("Invalid profile name.");return Path.Combine(directory,name+".ini");}
    internal static string[] Profiles(string directory){return ControlProfileManager.List(directory);}
    internal static bool ProfileExists(string directory,string name){return File.Exists(ProfilePath(directory,name));}
    internal static void SaveProfile(string root,string regional,string directory,string name,bool overwrite)
    {
        string file=ProfilePath(directory,name);
        if(File.Exists(file)&&(!overwrite||(File.GetAttributes(file)&FileAttributes.ReparsePoint)!=0))throw new IOException("Profile already exists.");
        string[] ordered=Ordered(root);
        LauncherCore.WriteSettings(file,new Dictionary<string,string>{{"order",String.Join("|",ordered)},{"enabled",String.Join("|",ordered.Where(n=>Enabled(Path.Combine(root,n))))},{"regional",Enabled(regional)?"1":"0"}});
    }
    internal static void LoadProfile(string root,string regional,string directory,string name)
    {
        string file=ProfilePath(directory,name);if(!File.Exists(file)||(File.GetAttributes(file)&FileAttributes.ReparsePoint)!=0)throw new IOException("Missing mod profile.");
        var values=LauncherCore.ReadSettings(file);
        if(!values.ContainsKey("order")||!values.ContainsKey("enabled"))throw new IOException("Invalid mod profile.");
        var selected=new HashSet<string>(values["enabled"].Split('|'),Names);
        string[] installed=Ordered(root);
        foreach(string mod in installed)SetEnabled(Path.Combine(root,mod),selected.Contains(mod));
        SetRegional(regional,LauncherCore.Get(values,"regional","0")=="1");
        WriteOrder(root,values["order"].Split('|'));
    }
}
