using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.RegularExpressions;

// Save archives are game-authored files. We do not patch their contents.
internal static class SaveManager
{
    private static readonly Regex DiaryName=new Regex(@"^(head|game[1-3])\.yw$",RegexOptions.IgnoreCase|RegexOptions.CultureInvariant);
    private static readonly Regex SecureName=new Regex(@"^save_secure_value_[0-9]{1,10}\.bin$",RegexOptions.IgnoreCase|RegexOptions.CultureInvariant);
    private static string Stamp(){return DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff")+"-"+Guid.NewGuid().ToString("N").Substring(0,8);}
    private static bool Regular(string file){return File.Exists(file)&&(File.GetAttributes(file)&FileAttributes.ReparsePoint)==0;}
    private static bool OrdinaryDirectory(string path){return Directory.Exists(path)&&(File.GetAttributes(path)&FileAttributes.ReparsePoint)==0;}
    private static string[] SecureFiles(string dir){return Directory.Exists(dir)?Directory.GetFiles(dir).Where(f=>SecureName.IsMatch(Path.GetFileName(f))&&Regular(f)).ToArray():new string[0];}
    private static void VerifiedCopy(string src,string dest)
    {
        if(!Regular(src))throw new IOException("Not a regular save file: "+Path.GetFileName(src));
        File.Copy(src,dest,false);
        using(var sha=SHA256.Create())using(var a=File.OpenRead(src))using(var b=File.OpenRead(dest))
            if(!sha.ComputeHash(a).SequenceEqual(sha.ComputeHash(b)))throw new IOException("The copy could not be verified.");
    }
    private static string[] DiaryFiles(string save)
    {
        if(!OrdinaryDirectory(save))throw new IOException("Choose a folder containing the decrypted Diary files.");
        string[] files=Directory.GetFiles(save).Where(f=>DiaryName.IsMatch(Path.GetFileName(f))).ToArray();
        if(!files.Any(f=>String.Equals(Path.GetFileName(f),"head.yw",StringComparison.OrdinalIgnoreCase))||
           !files.Any(f=>!String.Equals(Path.GetFileName(f),"head.yw",StringComparison.OrdinalIgnoreCase)))
            throw new IOException("Expected head.yw and at least one game1.yw, game2.yw, or game3.yw file.");
        foreach(string file in files)
            if(!Regular(file)||new FileInfo(file).Length<=0||new FileInfo(file).Length>16L*1024*1024)
                throw new IOException("Invalid Diary file: "+Path.GetFileName(file));
        return files;
    }
    // Export to a new timestamped folder; no existing export is replaced.
    internal static string Export(string gameRoot,string destination)
    {
        string save=Path.Combine(gameRoot,"save");
        string[] files=DiaryFiles(save);
        string result=Path.Combine(destination,"YW2-save-"+Stamp());
        Directory.CreateDirectory(Path.Combine(result,"save"));
        foreach(string f in files)VerifiedCopy(f,Path.Combine(result,"save",Path.GetFileName(f)));
        foreach(string f in SecureFiles(gameRoot))VerifiedCopy(f,Path.Combine(result,Path.GetFileName(f)));
        return result;
    }
    // Accept the original decrypted files or one of our exports. Stage and
    // verify every imported file before touching the current Diary. Any old
    // files are moved into a recovery directory, never permanently deleted.
    internal static string Import(string gameRoot,string source)
    {
        source=Path.GetFullPath(source);
        string from=Directory.Exists(Path.Combine(source,"save"))?Path.Combine(source,"save"):source;
        string[] files=DiaryFiles(from);
        string[] secure=SecureFiles(source);
        foreach(string f in secure)if(new FileInfo(f).Length!=8)throw new IOException("Invalid secure save value: "+Path.GetFileName(f));
        Directory.CreateDirectory(gameRoot);
        string id=Stamp(), live=Path.Combine(gameRoot,"save");
        string stage=Path.Combine(gameRoot,"import-pending-"+id),backup=Path.Combine(gameRoot,"manual-backups","before-import-"+id);
        Directory.CreateDirectory(Path.Combine(stage,"save"));
        foreach(string f in files)VerifiedCopy(f,Path.Combine(stage,"save",Path.GetFileName(f)));
        foreach(string f in secure)VerifiedCopy(f,Path.Combine(stage,Path.GetFileName(f)));
        Directory.CreateDirectory(backup);
        var oldSecure=new List<string>();var newSecure=new List<string>();
        bool movedOld=false,movedNew=false;
        try {
            if(Directory.Exists(live)){if(!OrdinaryDirectory(live))throw new IOException("Current save directory is not an ordinary folder.");Directory.Move(live,Path.Combine(backup,"save"));movedOld=true;}
            foreach(string f in SecureFiles(gameRoot)){string n=Path.GetFileName(f);File.Move(f,Path.Combine(backup,n));oldSecure.Add(n);}
            Directory.Move(Path.Combine(stage,"save"),live);movedNew=true;
            foreach(string f in SecureFiles(stage)){string n=Path.GetFileName(f);File.Move(f,Path.Combine(gameRoot,n));newSecure.Add(n);}
            Directory.Delete(stage,false);
            return backup;
        } catch(Exception error) {
            try {
                foreach(string n in newSecure)File.Move(Path.Combine(gameRoot,n),Path.Combine(stage,n));
                if(movedNew)Directory.Move(live,Path.Combine(stage,"save"));
                if(movedOld)Directory.Move(Path.Combine(backup,"save"),live);
                foreach(string n in oldSecure)File.Move(Path.Combine(backup,n),Path.Combine(gameRoot,n));
            } catch(Exception rollback) {throw new IOException("Import interrupted; recover your previous Diary from "+backup+" or staged files from "+stage+". "+rollback.Message,error);}
            throw new IOException("Import failed; the previous Diary was restored. "+error.Message,error);
        }
    }
}
