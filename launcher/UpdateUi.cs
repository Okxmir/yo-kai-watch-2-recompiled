using System;
using System.IO;
using System.Threading.Tasks;
using System.Windows.Controls;

internal sealed partial class Launcher
{
    private bool updateChecking;
    private Version requiredVersion;
    private DateTime lastUpdateCheck=DateTime.MinValue;
    private string updateMessage="Checking GitHub for updates…";
    private bool LauncherRequired {get{return requiredVersion!=null&&requiredVersion>LauncherUpdater.Current;}}
    private bool RebuildRequired {get{return exe!=null&&!core.BuiltWithCurrent(Path.GetDirectoryName(exe));}}
    private bool UpdateBlocked {get{return updateChecking||LauncherRequired;}}

    private void InitializeUpdates(){
        requiredVersion=LauncherUpdater.Required(core);
        Text("HeaderVersion","V"+LauncherCore.Version);
        Find<Button>("UpdateRetryButton").Click+=async(s,e)=>await CheckForUpdates();
        if(!demo&&!selfTest){bool started=false;window.ContentRendered+=async(s,e)=>{if(started)return;started=true;await CheckForUpdates();};}
    }
    private void RefreshUpdateUi(){
        bool blocked=UpdateBlocked;
        Visible("WelcomePane",exe==null&&!blocked);Visible("ReadyPane",exe!=null&&!RebuildRequired&&!blocked);Visible("RebuildPane",RebuildRequired&&!blocked);
        Find<Button>("PlayButton").IsEnabled=!blocked&&!RebuildRequired;
        if(blocked){Visible("HomePage",false);Visible("WizardShell",false);Visible("SettingsShell",false);}
        Visible("UpdateShell",blocked);
        Visible("UpdateProgress",updateChecking);Visible("UpdateRetryButton",LauncherRequired&&!updateChecking);
        Text("UpdateHeading",LauncherRequired?"Launcher update required":"Checking for updates");Text("UpdateMessage",updateMessage);
        if(RebuildRequired)Text("RebuildDetail","Rebuild from your original ROM to apply version V"+LauncherCore.Version+". Your saves and settings will be kept.");
    }
    private async Task CheckForUpdates(){
        if(updateChecking||busy||game!=null&&!game.HasExited||demo||selfTest)return;
        updateChecking=true;updateMessage="Checking GitHub for the latest release…";RefreshUpdateUi();
        try{
            var latest=await Task.Run(()=>LauncherUpdater.Check());lastUpdateCheck=DateTime.UtcNow;
            if(latest!=null&&latest.Version>LauncherUpdater.Current&&(requiredVersion==null||latest.Version>=requiredVersion)){
                LauncherUpdater.Require(core,latest.Version);requiredVersion=latest.Version;
                updateMessage="Downloading and verifying V"+LauncherUpdater.Display(latest.Version)+"…";RefreshUpdateUi();
                await Task.Run(()=>LauncherUpdater.StageAndRestart(core,latest));
                updateChecking=false;window.Close();return;
            }
            if(LauncherRequired)updateMessage="The required release is not available yet. Try again when connected to GitHub.";
        }catch(Exception e){lastUpdateCheck=DateTime.UtcNow;updateMessage="Could not install the update: "+e.Message;}
        finally{updateChecking=false;}
        ShowPage(Home);
    }
}
