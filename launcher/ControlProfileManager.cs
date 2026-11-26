using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

internal static class ControlProfileManager
{
    private static readonly Regex ProfileName=new Regex(@"^[A-Za-z0-9][A-Za-z0-9 _-]{0,47}$",RegexOptions.CultureInvariant);
    internal static bool ValidName(string name){return !String.IsNullOrEmpty(name)&&ProfileName.IsMatch(name)&&name==name.Trim();}
    private static string PathFor(string directory,string name){if(!ValidName(name))throw new IOException("Use 1–48 letters, numbers, spaces, underscores or hyphens.");return Path.Combine(directory,name+".ini");}
    internal static string[] List(string directory){return Directory.Exists(directory)?Directory.GetFiles(directory,"*.ini").Where(f=>(File.GetAttributes(f)&FileAttributes.ReparsePoint)==0).Select(Path.GetFileNameWithoutExtension).Where(ValidName).OrderBy(n=>n,StringComparer.OrdinalIgnoreCase).ToArray():new string[0];}
    internal static bool Exists(string directory,string name){return File.Exists(PathFor(directory,name));}
    private static IEnumerable<string> AllowedKeys(){foreach(var binding in ControlBindings.All){yield return "binding_key_"+binding.Id;yield return "binding_pad_"+binding.Id;}yield return "stick_deadzone";yield return "stick_sensitivity";}
    internal static void Save(string directory,string name,IDictionary<string,string> values,bool overwrite)
    {
        string file=PathFor(directory,name);
        if(File.Exists(file)&&(!overwrite||(File.GetAttributes(file)&FileAttributes.ReparsePoint)!=0))throw new IOException("A profile with that name already exists.");
        var filtered=new Dictionary<string,string>();foreach(string key in AllowedKeys()){string value;if(values.TryGetValue(key,out value))filtered[key]=value;}
        LauncherCore.WriteSettings(file,filtered);
    }
    internal static Dictionary<string,string> Load(string directory,string name)
    {
        string file=PathFor(directory,name);
        if(!File.Exists(file)||(File.GetAttributes(file)&FileAttributes.ReparsePoint)!=0)throw new IOException("The control profile is missing.");
        var raw=LauncherCore.ReadSettings(file);var result=new Dictionary<string,string>();
        foreach(string key in AllowedKeys()){string value;if(raw.TryGetValue(key,out value))result[key]=value;}
        if(result.Count==0)throw new IOException("No valid control settings in the profile.");
        return result;
    }
}
