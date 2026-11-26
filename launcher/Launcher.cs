using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media.Imaging;
using Microsoft.Win32;

internal sealed partial class Launcher
{
    private const int Home=0,Rom=1,Options=2,Compile=3,Graphics=4,Controls=5,Audio=6,Mods=7,Maintenance=8,Journal=9,Display=10,Performance=11;
    private readonly Window window;
    private readonly LauncherCore core;
    private readonly bool demo,selfTest;
    private Dictionary<string,string> prefs;
    private string rom,exe;
    private int page=Home,settingsReturn=Home;
    private bool busy,initializing;
    private Process game;
    private readonly object logLock=new object();
    private readonly string[] languages={"English","French","German","Italian","Spanish","Dutch","Russian"};
    private readonly string[] languageIds={"1","2","3","4","5","8","10"};
    private readonly int[] scalePresets={1,2,3,4,5,6,8,10};
    private readonly int[] windowWidths={0,1280,1920,2560,3840,2560,3440};
    private readonly int[] windowHeights={0,720,1080,1440,2160,1080,1440};

    internal Launcher(Window w,LauncherCore c,bool preview,bool testing=false)
    {
        window=w;core=c;demo=preview;selfTest=testing;initializing=true;
        prefs=LauncherCore.ReadSettings(core.Preferences);
        rom=Pref("rom",core.FindRom());
        using(Stream icon=Assembly.GetExecutingAssembly().GetManifestResourceStream("LauncherIcon.png")) {
            var bitmap=BitmapFrame.Create(icon,BitmapCreateOptions.None,BitmapCacheOption.OnLoad);bitmap.Freeze();window.Icon=bitmap;
            foreach(string name in new[]{"TitleIcon","WelcomeIcon","RebuildIcon","UpdateIcon"})Find<Image>(name).Source=bitmap;
        }
        window.SourceInitialized+=(s,e)=>System.Windows.Shell.WindowChrome.SetWindowChrome(window,new System.Windows.Shell.WindowChrome {CaptionHeight=0,ResizeBorderThickness=new Thickness(5),CornerRadius=new CornerRadius(13),GlassFrameThickness=new Thickness(0),UseAeroCaptionButtons=false});
        Find<Border>("TitleBar").MouseLeftButtonDown+=(s,e)=>{if(e.ChangedButton==MouseButton.Left && window.WindowState==WindowState.Normal)window.DragMove();};
        Click("MinButton",()=>window.WindowState=WindowState.Minimized);Click("CloseButton",window.Close);
        Click("SetupButton",()=>ShowPage(Rom));Click("NewBuildButton",()=>ShowPage(Rom));Click("RebuildButton",()=>ShowPage(Rom));
        Click("LinkGameButton",LocateBuild);Click("BrowseButton",PickRom);
        Click("WizardNext",()=>{if(page==Rom && File.Exists(rom))ShowPage(Options);else if(page==Options)ShowPage(Compile);});
        Click("WizardBack",()=>ShowPage(page==Rom?Home:page-1));
        Find<Button>("BuildButton").Click+=async(s,e)=>await Build(false);
        Click("SettingsButton",()=>OpenSettings(Graphics));Click("PlaySettingsButton",()=>OpenSettings(Graphics));
        Click("SettingsBack",()=>ShowPage(settingsReturn));
        foreach(var pair in new[]{new KeyValuePair<string,int>("GraphicsNav",Graphics),new KeyValuePair<string,int>("DisplayNav",Display),new KeyValuePair<string,int>("PerformanceNav",Performance),new KeyValuePair<string,int>("ControlsNav",Controls),new KeyValuePair<string,int>("AudioNav",Audio),new KeyValuePair<string,int>("ModsNav",Mods),new KeyValuePair<string,int>("MaintenanceNav",Maintenance),new KeyValuePair<string,int>("LogNav",Journal)}){
            int target=pair.Value;Click(pair.Key,()=>OpenSettings(target));
        }
        Click("CompileLogButton",()=>OpenSettings(Journal));Click("PlayButton",Play);
        Click("SavesButton",()=>OpenFolder(Path.Combine(CurrentProfile(),"games","00040000001b2900")));
        Click("InstallFolderButton",()=>OpenFolder(exe==null?core.Dist:Path.GetDirectoryName(exe)));
        Click("OpenFolderButton",()=>OpenFolder(core.Root));Click("CopyLogButton",()=>Clipboard.SetText(Find<TextBox>("JournalText").Text));
        Click("OpenModsButton",()=>OpenFolder(ModsFolder()));Click("ToggleModButton",ToggleMod);
        Click("ModUp",()=>MoveMod(-1));Click("ModDown",()=>MoveMod(1));Click("ModConflict",CheckModConflicts);Click("ModOriginal",RestoreOriginalMods);
        Click("ModProfileSave",SaveModProfile);Click("ModProfileLoad",LoadModProfile);
        Find<CheckBox>("RegionalModCheck").Click+=(s,e)=>{if(!CanEditMods())return;try{ModManager.SetRegional(RegionalModsFolder(),Find<CheckBox>("RegionalModCheck").IsChecked==true);RefreshMods();}catch(Exception error){ShowError(error.Message);}};
        Find<Button>("VerifyButton").Click+=async(s,e)=>await Build(true);
        Click("RepairButton",()=>ShowPage(Rom));Click("BackupButton",BackupSave);Click("ExportSaveButton",ExportSave);Click("ImportSaveButton",ImportSave);Click("ResetButton",ResetSettings);
        Click("CacheResetButton",ResetGraphicsCaches);Click("ExportLogsButton",ExportLogs);Click("DiagnosticButton",ExportDiagnostic);
        Click("SaveControlProfile",SaveNamedControls);Click("LoadControlProfile",LoadNamedControls);
        Fill("LanguageBox",languages);Fill("SettingsLanguage",languages);Fill("BackendBox",new[]{"Vulkan GPU","Software fallback"});
        Fill("GpuBox",new[]{"Automatic (high power)","Prefer integrated GPU"});
        Fill("FpsBox",new[]{"Original timing","30 FPS","60 FPS (interpolation)"});
        Fill("ScaleBox",new[]{"1× · Native","2×","3×","4×","5×","6×","8×","10×","Custom multiplier"});Fill("AspectBox",new[]{"Original (5:3)","16:9","16:10","21:9","32:9 (experimental)","Custom aspect"});
        Fill("WindowResolution",new[]{"Automatic","1280 × 720","1920 × 1080","2560 × 1440","3840 × 2160","2560 × 1080 (Ultrawide)","3440 × 1440 (Ultrawide)","Custom dimensions"});
        Fill("WindowModeBox",new[]{"Windowed","Borderless fullscreen","Exclusive fullscreen"});
        Fill("LayoutBox",new[]{"Stacked (both screens)","Side by side · top left","Side by side · top right","Top priority · left","Top priority · right","Custom two-screen layout","Separate windows / monitors","Adaptive one-screen","Top only","Picture-in-picture"});
        string[] monitors=Enumerable.Range(1,Math.Max(1,System.Windows.Forms.Screen.AllScreens.Length)).Select(i=>"Display "+i).ToArray();Fill("MainDisplayBox",monitors);Fill("DetachedDisplayBox",monitors);
        Fill("TextureBox",new[]{"Sharp","Smooth","Nearest"});Fill("LatencyBox",new[]{"Low","Normal","High"});
        Fill("SamplingBox",new[]{"Original","Nearest","Linear","Anisotropic ×2","Anisotropic ×4","Anisotropic ×8","Anisotropic ×16"});Fill("ProfileBox",new[]{"Economy","Balanced","Performance","Maximum"});
        foreach(string n in new[]{"LanguageBox","SettingsLanguage","BackendBox","GpuBox","FpsBox","ScaleBox","AspectBox","LayoutBox","WindowResolution","WindowModeBox","MainDisplayBox","DetachedDisplayBox","TextureBox","LatencyBox","SamplingBox","ProfileBox"})Find<ComboBox>(n).SelectionChanged+=(s,e)=>SettingsChanged(n);
        foreach(string n in new[]{"VsyncCheck","MonoCheck","MuteCheck","DetachedFullCheck","PrewarmCheck","StatsCheck","FxaaCheck"})Find<CheckBox>(n).Click+=(s,e)=>SettingsChanged(n);
        foreach(string n in new[]{"HudSlider","ZoomSlider","VolumeSlider","DeadzoneSlider","SensitivitySlider","TopSize","BottomSize","ScreenGap","PipSize","PipX","PipY","PipOpacity","SharpnessSlider"})Find<Slider>(n).ValueChanged+=(s,e)=>SettingsChanged(n);
        Find<ComboBox>("AudioDevice").SelectionChanged+=(s,e)=>SettingsChanged("AudioDevice");
        foreach(string n in new[]{"CustomScale","AspectNumerator","AspectDenominator","WindowWidth","WindowHeight","CustomTopX","CustomTopY","CustomBottomX","CustomBottomY","BackgroundRgb"})Find<TextBox>(n).TextChanged+=(s,e)=>SettingsChanged(n);
        window.DragOver+=(s,e)=>{e.Effects=page==Rom&&!busy&&Dropped(e.Data)!=null?DragDropEffects.Copy:DragDropEffects.None;e.Handled=true;};
        window.Drop+=(s,e)=>{string p=Dropped(e.Data);if(page==Rom&&!busy&&p!=null){rom=p;ShowPage(Rom);}e.Handled=true;};
        window.Closing+=(s,e)=>{if(busy){e.Cancel=true;MessageBox.Show(window,"Wait for the build to finish.");}else if(!demo)SavePreferences();};
        ResolveGame();LoadOptions();InitializeControls();InitializeUpdates();initializing=false;ShowPage(Home);
    }
    private T Find<T>(string name) where T:class {return (T)window.FindName(name);}
    private void Click(string name,Action action){Find<Button>(name).Click+=(s,e)=>{try{action();}catch(Exception err){ShowError(err.Message);}};}
    private void Text(string name,string value){Find<TextBlock>(name).Text=value;}
    private string Pref(string n,string fallback){return LauncherCore.Get(prefs,n,fallback);}
    private void Fill(string n,string[] values){foreach(string v in values)Find<ComboBox>(n).Items.Add(v);}
    private int Selected(string n){return Math.Max(0,Find<ComboBox>(n).SelectedIndex);}
    private void Set(string n,int i){Find<ComboBox>(n).SelectedIndex=i;}
    private void Check(string n,bool v){Find<CheckBox>(n).IsChecked=v;}
    private string Bool(string n){return Find<CheckBox>(n).IsChecked==true?"1":"0";}
    private int Num(string s,int fallback){int n;return Int32.TryParse(s,out n)?n:fallback;}
    private string CurrentProfile(){return exe==null?Path.Combine(core.Dist,"data"):core.Profile(exe);}
    private void ShowError(string s){if(selfTest)throw new InvalidOperationException(s);MessageBox.Show(window,s,window.Title,MessageBoxButton.OK,MessageBoxImage.Warning);}
    private void ResolveGame(){string why,foundRom;string folder=Pref("linked_dist",core.Dist);if(core.TryInstall(folder,out exe,out foundRom,out why))rom=foundRom;else exe=null;}
    private void LoadOptions()
    {
        initializing=true;
        var state=LauncherCore.ReadSettings(Path.Combine(CurrentProfile(),"settings.ini"));
        // The game can change display settings with its hotkeys. Treat its
        // current settings as authoritative so reopening the launcher does not
        // silently undo them.
        Func<string,string,string> val=(k,d)=>LauncherCore.Get(state,k,Pref(k,d));
        int lang=Array.IndexOf(languageIds,val("language","1"));if(lang<0)lang=0;Set("LanguageBox",lang);Set("SettingsLanguage",lang);
        Set("BackendBox",val("gpu_renderer","1")=="1"?0:1);Set("GpuBox",val("prefer_integrated_gpu","0")=="1"?1:0);
        Set("FpsBox",val("frame_interpolation","0")=="1"?2:val("frame_limit","0")=="30"?1:0);
        int scale=Math.Min(10,Math.Max(1,Num(val("resolution","1"),1)));int scaleIndex=Array.IndexOf(scalePresets,scale);Set("ScaleBox",scaleIndex>=0?scaleIndex:8);Find<TextBox>("CustomScale").Text=scale.ToString();
        Set("AspectBox",Math.Min(5,Math.Max(0,Num(val("aspect_mode",val("widescreen","0")),0))));Find<TextBox>("AspectNumerator").Text=val("aspect_num","16");Find<TextBox>("AspectDenominator").Text=val("aspect_den","9");Set("TextureBox",Math.Min(2,Math.Max(0,Num(val("filter","0"),0))));
        int ww=Num(val("window_width","0"),0),wh=Num(val("window_height","0"),0),winIndex=7;for(int i=0;i<windowWidths.Length;i++)if(ww==windowWidths[i]&&wh==windowHeights[i]){winIndex=i;break;}Set("WindowResolution",winIndex);Find<TextBox>("WindowWidth").Text=(ww>=320?ww:1920).ToString();Find<TextBox>("WindowHeight").Text=(wh>=240?wh:1080).ToString();Set("WindowModeBox",Math.Min(2,Math.Max(0,Num(val("window_mode",val("fullscreen","0")),0))));
        Set("LayoutBox",val("detached_screen","0")=="1"?6:val("single_screen","0")!="1"?Math.Min(5,Math.Max(0,Num(val("dual_layout","0"),0))):val("smart_layout","1")=="1"?7:val("picture_in_picture","1")=="1"?9:8);
        Set("MainDisplayBox",Math.Min(Find<ComboBox>("MainDisplayBox").Items.Count-1,Math.Max(0,Num(val("main_display","0"),0))));Set("DetachedDisplayBox",Math.Min(Find<ComboBox>("DetachedDisplayBox").Items.Count-1,Math.Max(0,Num(val("detached_display","1"),1))));Check("DetachedFullCheck",val("detached_fullscreen","0")=="1");
        Find<Slider>("TopSize").Value=Num(val("top_size","100"),100);Find<Slider>("BottomSize").Value=Num(val("bottom_size","100"),100);Find<Slider>("ScreenGap").Value=Num(val("screen_gap","0"),0);
        Find<Slider>("PipSize").Value=Num(val("pip_size","35"),35);Find<Slider>("PipX").Value=Num(val("pip_x","100"),100);Find<Slider>("PipY").Value=Num(val("pip_y","100"),100);Find<Slider>("PipOpacity").Value=Num(val("pip_opacity","100"),100);
        Find<TextBox>("CustomTopX").Text=val("custom_top_x","0");Find<TextBox>("CustomTopY").Text=val("custom_top_y","0");Find<TextBox>("CustomBottomX").Text=val("custom_bottom_x","404");Find<TextBox>("CustomBottomY").Text=val("custom_bottom_y","0");Find<TextBox>("BackgroundRgb").Text=val("background_rgb","000000");
        Check("VsyncCheck",val("vsync","1")=="1");
        Set("SamplingBox",Math.Min(6,Math.Max(0,Num(val("texture_sampling","0"),0))));
        Check("FxaaCheck",val("fxaa","0")=="1");Find<Slider>("SharpnessSlider").Value=Num(val("sharpening","0"),0);
        Set("ProfileBox",Math.Min(3,Math.Max(0,Num(val("performance_profile","1"),1))));Check("PrewarmCheck",val("prewarm_pipelines","1")=="1");Check("StatsCheck",val("show_stats","0")=="1");
        Find<Slider>("HudSlider").Value=Num(val("hud_size","80"),80);Find<Slider>("ZoomSlider").Value=Num(val("camera_zoom","90"),90);
        Find<Slider>("VolumeSlider").Value=Num(val("volume","100"),100);Set("LatencyBox",Math.Min(2,Math.Max(0,Num(val("audio_latency","1"),1))));
        SetAudioDeviceList(val("audio_device",""),new string[0]);Check("MonoCheck",val("mono","0")=="1");Check("MuteCheck",val("mute_in_background","0")=="1");
        Find<Slider>("DeadzoneSlider").Value=Num(val("stick_deadzone","18"),18);Find<Slider>("SensitivitySlider").Value=Num(val("stick_sensitivity","100"),100);
        UpdateDisplayRows();
        initializing=false;
    }
    private void SettingsChanged(string name)
    {
        if(initializing)return;initializing=true;
        if(name=="LanguageBox")Set("SettingsLanguage",Selected("LanguageBox"));else if(name=="SettingsLanguage")Set("LanguageBox",Selected("SettingsLanguage"));
        if(Selected("BackendBox")==1 && Selected("FpsBox")==2)Set("FpsBox",0);
        if(Selected("BackendBox")==1 && Selected("ScaleBox")>3)Set("ScaleBox",0);
        UpdateDisplayRows();
        initializing=false;SavePreferences();
    }
    private void UpdateDisplayRows(){int layout=Selected("LayoutBox");Visible("CustomScaleRow",Selected("ScaleBox")==8);Visible("CustomAspectRow",Selected("AspectBox")==5);Visible("CustomWindowRow",Selected("WindowResolution")==7);Visible("CustomLayoutOptions",layout==5);Visible("DetachedOptions",layout==6);Visible("PipOptions",layout==9);foreach(string name in new[]{"TopSize","BottomSize","ScreenGap"})Find<Slider>(name).IsEnabled=layout<=5;}
    private void SavePreferences()
    {
        if(demo||initializing)return;
        prefs["rom"]=rom??"";prefs["language"]=languageIds[Selected("SettingsLanguage")];
        prefs["gpu_renderer"]=Selected("BackendBox")==0?"1":"0";prefs["prefer_integrated_gpu"]=Selected("GpuBox")==1?"1":"0";
        prefs["frame_limit"]=new[]{"0","30","60"}[Selected("FpsBox")];prefs["frame_interpolation"]=Selected("FpsBox")==2?"1":"0";
        int scale=Selected("ScaleBox")<scalePresets.Length?scalePresets[Selected("ScaleBox")]:Math.Min(10,Math.Max(1,Num(Find<TextBox>("CustomScale").Text,1)));prefs["resolution"]=scale.ToString();
        prefs["aspect_mode"]=Selected("AspectBox").ToString();prefs["widescreen"]=Selected("AspectBox")==0?"0":"1";prefs["aspect_num"]=Math.Min(1000,Math.Max(1,Num(Find<TextBox>("AspectNumerator").Text,16))).ToString();prefs["aspect_den"]=Math.Min(1000,Math.Max(1,Num(Find<TextBox>("AspectDenominator").Text,9))).ToString();prefs["filter"]=Selected("TextureBox").ToString();
        int wr=Selected("WindowResolution");prefs["window_width"]=(wr<windowWidths.Length?windowWidths[wr]:Math.Min(8192,Math.Max(320,Num(Find<TextBox>("WindowWidth").Text,1920)))).ToString();prefs["window_height"]=(wr<windowHeights.Length?windowHeights[wr]:Math.Min(4320,Math.Max(240,Num(Find<TextBox>("WindowHeight").Text,1080)))).ToString();prefs["window_mode"]=Selected("WindowModeBox").ToString();
        int layout=Selected("LayoutBox");prefs["single_screen"]=layout>=7?"1":"0";prefs["dual_layout"]=layout<=5?layout.ToString():"0";prefs["detached_screen"]=layout==6?"1":"0";prefs["smart_layout"]=layout==7?"1":"0";prefs["picture_in_picture"]=layout==7||layout==9?"1":"0";
        prefs["main_display"]=Selected("MainDisplayBox").ToString();prefs["detached_display"]=Selected("DetachedDisplayBox").ToString();prefs["detached_fullscreen"]=Bool("DetachedFullCheck");
        foreach(var entry in new[]{new KeyValuePair<string,string>("top_size","TopSize"),new KeyValuePair<string,string>("bottom_size","BottomSize"),new KeyValuePair<string,string>("screen_gap","ScreenGap"),new KeyValuePair<string,string>("pip_size","PipSize"),new KeyValuePair<string,string>("pip_x","PipX"),new KeyValuePair<string,string>("pip_y","PipY"),new KeyValuePair<string,string>("pip_opacity","PipOpacity")})prefs[entry.Key]=((int)Find<Slider>(entry.Value).Value).ToString();
        foreach(var entry in new[]{new KeyValuePair<string,string>("custom_top_x","CustomTopX"),new KeyValuePair<string,string>("custom_top_y","CustomTopY"),new KeyValuePair<string,string>("custom_bottom_x","CustomBottomX"),new KeyValuePair<string,string>("custom_bottom_y","CustomBottomY")})prefs[entry.Key]=Math.Min(2000,Math.Max(0,Num(Find<TextBox>(entry.Value).Text,0))).ToString();
        uint color; if(UInt32.TryParse(Find<TextBox>("BackgroundRgb").Text.TrimStart('#'),System.Globalization.NumberStyles.HexNumber,System.Globalization.CultureInfo.InvariantCulture,out color))prefs["background_rgb"]=(color&0xffffffu).ToString("X6");else if(!prefs.ContainsKey("background_rgb"))prefs["background_rgb"]="000000";
        prefs["fullscreen"]=Selected("WindowModeBox")==0?"0":"1";prefs["vsync"]=Bool("VsyncCheck");prefs["hud_size"]=((int)Find<Slider>("HudSlider").Value).ToString();prefs["camera_zoom"]=((int)Find<Slider>("ZoomSlider").Value).ToString();
        prefs["texture_sampling"]=Selected("SamplingBox").ToString();prefs["hd_textures"]="0";prefs["performance_profile"]=Selected("ProfileBox").ToString();prefs["texture_cache_mib"]=Pref("texture_cache_mib","128");prefs["prewarm_pipelines"]=Bool("PrewarmCheck");prefs["show_stats"]=Bool("StatsCheck");
        prefs["fxaa"]=Bool("FxaaCheck");prefs["sharpening"]=((int)Find<Slider>("SharpnessSlider").Value).ToString();
        prefs["volume"]=((int)Find<Slider>("VolumeSlider").Value).ToString();prefs["audio_latency"]=Selected("LatencyBox").ToString();prefs["audio_device"]=AudioDeviceValue();
        prefs["mono"]=Bool("MonoCheck");prefs["mute_in_background"]=Bool("MuteCheck");
        prefs["stick_deadzone"]=((int)Find<Slider>("DeadzoneSlider").Value).ToString();prefs["stick_sensitivity"]=((int)Find<Slider>("SensitivitySlider").Value).ToString();
        LauncherCore.WriteSettings(core.Preferences,prefs);
        if(exe==null||game!=null&&!game.HasExited)return;
        string path=Path.Combine(CurrentProfile(),"settings.ini");var settings=LauncherCore.ReadSettings(path);
        foreach(string key in new[]{"language","gpu_renderer","prefer_integrated_gpu","frame_limit","frame_interpolation","resolution","widescreen","aspect_mode","aspect_num","aspect_den","window_width","window_height","window_mode","filter","single_screen","dual_layout","detached_screen","main_display","detached_display","detached_fullscreen","top_size","bottom_size","screen_gap","pip_size","pip_x","pip_y","pip_opacity","custom_top_x","custom_top_y","custom_bottom_x","custom_bottom_y","background_rgb","smart_layout","picture_in_picture","fullscreen","vsync","hud_size","camera_zoom","volume","audio_latency","audio_device","mono","mute_in_background","stick_deadzone","stick_sensitivity","texture_sampling","hd_textures","performance_profile","texture_cache_mib","prewarm_pipelines","show_stats"})settings[key]=prefs[key];
        settings["fxaa"]=prefs["fxaa"];settings["sharpening"]=prefs["sharpening"];
        settings["show_launcher"]="0";settings["check_updates"]="0";settings["game_path"]=rom;settings["dump_folder"]=Path.GetDirectoryName(rom);
        ApplyControlPreferences(settings);LauncherCore.WriteSettings(path,settings);
    }
    private void Visible(string name,bool state){Find<FrameworkElement>(name).Visibility=state?Visibility.Visible:Visibility.Collapsed;}
    private void ShowPage(int next)
    {
        if(controlsRoot!=null && page==Controls&&next!=Controls)CancelCapture(null);
        page=next;Visible("HomePage",next==Home);Visible("WizardShell",next>=Rom&&next<=Compile);Visible("SettingsShell",next>=Graphics);
        Visible("Steps",next>=Rom&&next<=Compile);Visible("RomPage",next==Rom);Visible("OptionsPage",next==Options);Visible("CompilePage",next==Compile);
        Visible("GraphicsPage",next==Graphics);Visible("DisplayPage",next==Display);Visible("PerformancePage",next==Performance);Visible("ControlsPage",next==Controls);Visible("AudioPage",next==Audio);Visible("ModsPage",next==Mods);Visible("MaintenancePage",next==Maintenance);Visible("LogPage",next==Journal);
        Visible("WizardNext",next==Rom||next==Options);Visible("BuildButton",next==Compile);Find<Border>("RomStep").Tag=next==Rom?"active":null;Find<Border>("OptionsStep").Tag=next==Options?"active":null;Find<Border>("CompileStep").Tag=next==Compile?"active":null;
        Visible("WelcomePane",exe==null);Visible("ReadyPane",exe!=null);Visible("RebuildPane",false);Visible("UpdateShell",false);
        Find<Button>("WizardNext").IsEnabled=next!=Rom||File.Exists(rom);Text("RomPathText",String.IsNullOrEmpty(rom)?"No ROM selected":rom);
        Text("RomHelp",File.Exists(rom)?"Selected. The build checks the cartridge ID and code fingerprint.":"Drop your decrypted European ROM or browse.");
        Text("WizardNote",next==Compile?"This generates native code locally; your ROM stays unchanged.":next==Options?"The language can be changed later.":"Choose a compatible file to continue.");
        Text("BuildSelection",Path.GetFileName(rom)+" · "+languages[Selected("LanguageBox")]);
        Text("InstallDetail","Local installation · Windows x64 · "+LauncherCore.Version);
        Text("UpdateCheckNote","60 FPS uses frame interpolation. Original gameplay timing is unchanged; development build.");
        if(next==Controls)OpenControls();if(next==Mods)RefreshMods();if(next==Journal)RefreshLog();if(next==Audio)RefreshAudioDevices();RefreshUpdateUi();
    }
    private string AudioDeviceValue(){var box=Find<ComboBox>("AudioDevice");return box.SelectedIndex>0?box.SelectedItem as string??"":"";}
    private void SetAudioDeviceList(string selected,IEnumerable<string> devices){var box=Find<ComboBox>("AudioDevice");var items=new List<string>{"System default"};items.AddRange(devices.Where(n=>!String.IsNullOrEmpty(n)).Distinct());if(!String.IsNullOrEmpty(selected)&&!items.Contains(selected))items.Add(selected);box.ItemsSource=items;box.SelectedIndex=String.IsNullOrEmpty(selected)?0:Math.Max(0,items.IndexOf(selected));}
    private void RefreshAudioDevices(){string chosen=AudioDeviceValue();bool previous=initializing;initializing=true;try{using(var sdl=new SdlInput(core.Root)){var devices=sdl.PlaybackDevices();SetAudioDeviceList(chosen,devices);Text("AudioDeviceNote",!String.IsNullOrEmpty(chosen)&&!devices.Contains(chosen)?"Selected device is unavailable; the game will use the system default if necessary.":"Playback device selection applies at the next launch.");}}catch{Text("AudioDeviceNote","Could not list playback devices. The system default remains available.");}finally{initializing=previous;}}
    private void OpenSettings(int target){if(page<Graphics)settingsReturn=page;ShowPage(target);}
    private void PickRom(){var d=new OpenFileDialog {Title="Choose your decrypted European Psychic Specters cartridge",Filter="Nintendo 3DS dumps|*.3ds;*.cci;*.cxi;*.cia|All files|*.*"};if(d.ShowDialog(window)==true){rom=d.FileName;ShowPage(Rom);SavePreferences();}}
    private static string Dropped(IDataObject d){if(!d.GetDataPresent(DataFormats.FileDrop))return null;var files=d.GetData(DataFormats.FileDrop) as string[];if(files==null||files.Length!=1||!File.Exists(files[0]))return null;string ext=Path.GetExtension(files[0]).ToLowerInvariant();return new[]{".3ds",".cci",".cxi",".cia"}.Contains(ext)?files[0]:null;}
    private void LocateBuild(){var d=new OpenFileDialog {Title="Select a compatible build manifest",Filter="Build manifest|build-manifest.json"};if(d.ShowDialog(window)!=true)return;string e,r,why;string folder=Path.GetDirectoryName(d.FileName);if(!core.TryInstall(folder,out e,out r,out why)){ShowError(why);return;}prefs["linked_dist"]=folder;exe=e;rom=r;LoadOptions();SavePreferences();ShowPage(Home);}
    private void OpenFolder(string dir){Directory.CreateDirectory(dir);Process.Start("explorer.exe","\""+dir+"\"");}
    private void RefreshLog(){if(!File.Exists(core.Log)){Find<TextBox>("JournalText").Text="No build log yet.";return;}using(var file=new FileStream(core.Log,FileMode.Open,FileAccess.Read,FileShare.ReadWrite))using(var reader=new StreamReader(file))Find<TextBox>("JournalText").Text=reader.ReadToEnd();}
    private async Task Build(bool validateOnly)
    {
        if(UpdateBlocked||busy||game!=null&&!game.HasExited)return;
        if(!File.Exists(rom)){ShowPage(Rom);return;}
        if(!File.Exists(Path.Combine(core.Root,"build.ps1"))){ShowError("The builder source files are missing. Extract the complete builder folder.");return;}
        if(!File.Exists(Path.Combine(core.Root,"VERSION"))||File.ReadAllText(Path.Combine(core.Root,"VERSION")).Trim()!=LauncherCore.Version){ShowError("The source version does not match this launcher. Install the complete latest builder.");return;}
        if(!validateOnly&&exe!=null&&!String.Equals(Path.GetDirectoryName(exe),core.Dist,StringComparison.OrdinalIgnoreCase)){
            string from=CurrentProfile(),to=Path.Combine(core.Dist,"data");
            if(Directory.Exists(from)&&!Directory.Exists(to))LauncherCore.CopyTree(from,to);
            else if(Directory.Exists(Path.Combine(from,"games"))&&Directory.Exists(to)){ShowError("The linked installation has its own saves. Export its Diary before rebuilding, then import it in the new local installation.");return;}
        }
        busy=true;SavePreferences();if(!validateOnly)ShowPage(Compile);
        Find<Button>("BuildButton").IsEnabled=false;Find<Button>("WizardBack").IsEnabled=false;Find<ProgressBar>("Progress").IsIndeterminate=true;Text("BuildStatus",validateOnly?"Verifying cartridge…":"Preparing the native build…");
        try {
            Directory.CreateDirectory(Path.GetDirectoryName(core.Log));File.WriteAllText(core.Log,"",new UTF8Encoding(false));
            string shellDirectory=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"WindowsPowerShell","v1.0");
            var psi=new ProcessStartInfo(Path.Combine(shellDirectory,"powershell.exe"),"-NoProfile -ExecutionPolicy Bypass -File \""+Path.Combine(core.Root,"build.ps1")+"\" -Cartridge \""+rom+"\" -SkipLauncher"+(validateOnly?" -ValidateOnly":"")) {WorkingDirectory=core.Root,UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
            // PowerShell 7 can hand its module search path to the WPF process.
            // Use the built-in Windows PowerShell modules for this build.
            psi.EnvironmentVariables["PSModulePath"]=Path.Combine(shellDirectory,"Modules");
            int exit=await Task.Run(()=>{using(var p=new Process{StartInfo=psi}){DataReceivedEventHandler receive=(s,e)=>{if(e.Data==null)return;lock(logLock)File.AppendAllText(core.Log,e.Data+Environment.NewLine);if(e.Data.StartsWith("Build stage:"))window.Dispatcher.BeginInvoke(new Action(()=>Text("BuildStatus",e.Data.Substring(13).Trim())));};p.OutputDataReceived+=receive;p.ErrorDataReceived+=receive;p.Start();p.BeginOutputReadLine();p.BeginErrorReadLine();p.WaitForExit();return p.ExitCode;}});
            if(exit!=0)throw new Exception("Build failed. See the build log for details (exit "+exit+").");
            if(validateOnly){ShowError("Cartridge identity and code SHA-256 verified successfully.");}
            else {prefs["linked_dist"]=core.Dist;ResolveGame();if(exe==null)throw new Exception("The build finished without a verified installation.");SavePreferences();ShowPage(Home);}
        } catch(Exception error){Text("BuildStatus",error.Message);ShowError(error.Message);OpenSettings(Journal);}
        finally {busy=false;Find<Button>("BuildButton").IsEnabled=true;Find<Button>("WizardBack").IsEnabled=true;Find<ProgressBar>("Progress").IsIndeterminate=false;}
    }
    private async void Play()
    {
        if(UpdateBlocked||busy||game!=null&&!game.HasExited)return;
        if(!demo&&!selfTest&&DateTime.UtcNow-lastUpdateCheck>TimeSpan.FromMinutes(2))await CheckForUpdates();
        if(UpdateBlocked)return;ResolveGame();if(exe==null){ShowPage(Home);ShowError("Build a compatible game first.");return;}
        if(RebuildRequired){ShowPage(Home);return;}
        SavePreferences();Directory.CreateDirectory(Path.Combine(core.Root,"logs"));string log=Path.Combine(core.Root,"logs","game-latest.txt");File.WriteAllText(log,"");
        var psi=new ProcessStartInfo(exe,"--play \""+rom+"\""){WorkingDirectory=Path.GetDirectoryName(exe),UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
        psi.EnvironmentVariables["YW2_DATA_DIR"]=CurrentProfile();psi.EnvironmentVariables["YW2_STATE_DIR"]=CurrentProfile();
        game=new Process{StartInfo=psi,EnableRaisingEvents=true};DataReceivedEventHandler receive=(s,e)=>{if(e.Data!=null)lock(logLock)File.AppendAllText(log,e.Data+Environment.NewLine);};game.OutputDataReceived+=receive;game.ErrorDataReceived+=receive;
        game.Exited+=(s,e)=>window.Dispatcher.BeginInvoke(new Action(()=>{int code=game.ExitCode;game.Dispose();game=null;LoadOptions();window.WindowState=WindowState.Normal;window.Activate();if(code!=0)ShowError("The game closed with exit code "+code+". See logs/game-latest.txt.");}));
        game.Start();game.BeginOutputReadLine();game.BeginErrorReadLine();window.WindowState=WindowState.Minimized;
    }
    private string ModsFolder(){return Path.Combine(CurrentProfile(),"mods","yw2");}
    private string RegionalModsFolder(){return Path.Combine(CurrentProfile(),"mods","00040000001b2900");}
    private string ModProfilesDirectory(){return Path.Combine(core.Root,"launcher-data","mod-profiles");}
    private bool CanEditMods(){if(game!=null&&!game.HasExited){ShowError("Close the game before editing mods.");return false;}return !demo;}
    private string SelectedMod(){var item=Find<ListBox>("ModList").SelectedItem as ListBoxItem;return item==null?null:(string)item.Tag;}
    private void RefreshMods(string selected=null){
        var list=Find<ListBox>("ModList");string current=selected??SelectedMod();list.Items.Clear();
        foreach(string n in ModManager.Ordered(ModsFolder())){var item=new ListBoxItem{Content=(ModManager.Enabled(Path.Combine(ModsFolder(),n))?"Enabled · ":"Disabled · ")+n,Tag=n};list.Items.Add(item);if(n==current)list.SelectedItem=item;}
        if(list.SelectedIndex<0&&list.Items.Count>0)list.SelectedIndex=0;
        var regional=Find<CheckBox>("RegionalModCheck");regional.IsEnabled=Directory.Exists(RegionalModsFolder());regional.IsChecked=ModManager.Enabled(RegionalModsFolder());
        var box=Find<ComboBox>("ModProfileBox");string prior=box.SelectedItem as string;box.ItemsSource=ModManager.Profiles(ModProfilesDirectory());box.SelectedItem=prior;if(box.SelectedIndex<0&&box.Items.Count>0)box.SelectedIndex=0;Find<Button>("ModProfileLoad").IsEnabled=box.Items.Count>0;
    }
    private void ToggleMod(){if(!CanEditMods())return;string name=SelectedMod();if(name==null)return;try{ModManager.Toggle(ModsFolder(),name);RefreshMods(name);}catch(Exception e){ShowError(e.Message);}}
    private void MoveMod(int offset){if(!CanEditMods())return;string name=SelectedMod();if(name==null)return;try{ModManager.Move(ModsFolder(),name,offset);RefreshMods(name);}catch(Exception e){ShowError(e.Message);}}
    private void CheckModConflicts(){try{var warnings=ModManager.Conflicts(ModsFolder(),RegionalModsFolder());MessageBox.Show(window,warnings.Length==0?"No direct file overlaps detected. This does not guarantee mod compatibility.":"Potential conflicts (later mods win):\n\n"+String.Join("\n",warnings.Take(30))+(warnings.Length>30?"\n…":"") ,window.Title);}catch(Exception e){ShowError(e.Message);}}
    private void RestoreOriginalMods(){if(!CanEditMods())return;if(MessageBox.Show(window,"Disable all local mods and EUR-specific replacements? No mod files will be removed.",window.Title,MessageBoxButton.YesNo)!=MessageBoxResult.Yes)return;try{ModManager.RestoreOriginal(ModsFolder(),RegionalModsFolder());RefreshMods();}catch(Exception e){ShowError(e.Message);}}
    private void SaveModProfile(){if(!CanEditMods())return;string name=Find<TextBox>("ModProfileName").Text.Trim();if(!ControlProfileManager.ValidName(name)){ShowError("Enter a valid profile name.");return;}try{bool exists=ModManager.ProfileExists(ModProfilesDirectory(),name);if(exists&&MessageBox.Show(window,"Replace the profile "+name+"?",window.Title,MessageBoxButton.YesNo)!=MessageBoxResult.Yes)return;ModManager.SaveProfile(ModsFolder(),RegionalModsFolder(),ModProfilesDirectory(),name,exists);RefreshMods();Find<ComboBox>("ModProfileBox").SelectedItem=name;}catch(Exception e){ShowError(e.Message);}}
    private void LoadModProfile(){if(!CanEditMods())return;string name=Find<ComboBox>("ModProfileBox").SelectedItem as string;if(name==null)return;if(MessageBox.Show(window,"Apply profile "+name+"? Mods not included in the profile will be disabled.",window.Title,MessageBoxButton.YesNo)!=MessageBoxResult.Yes)return;try{ModManager.LoadProfile(ModsFolder(),RegionalModsFolder(),ModProfilesDirectory(),name);RefreshMods();}catch(Exception e){ShowError(e.Message);}}
    private void BackupSave(){if(game!=null&&!game.HasExited){ShowError("Close the game before backing up saves.");return;}string dir=Path.Combine(CurrentProfile(),"games","00040000001b2900");string source=Path.Combine(dir,"save");if(!Directory.Exists(source)){ShowError("No save files yet.");return;}string dest=Path.Combine(dir,"manual-backups",DateTime.Now.ToString("yyyyMMdd-HHmmss-fff"));LauncherCore.CopyTree(source,Path.Combine(dest,"save"));foreach(string f in Directory.GetFiles(dir))File.Copy(f,Path.Combine(dest,Path.GetFileName(f)));ShowError("Save backup created.");}
    private string SelectFolder(string description){using(var folder=new System.Windows.Forms.FolderBrowserDialog{Description=description,ShowNewFolderButton=true})return folder.ShowDialog()==System.Windows.Forms.DialogResult.OK?folder.SelectedPath:null;}
    private string SaveRoot(){return Path.Combine(CurrentProfile(),"games","00040000001b2900");}
    private void ExportSave(){if(game!=null&&!game.HasExited){ShowError("Close the game before exporting saves.");return;}string path=SelectFolder("Choose where to export your Diary");if(path==null)return;try{string dest=SaveManager.Export(SaveRoot(),path);MessageBox.Show(window,"Diary exported to:\n"+dest,window.Title);}catch(Exception e){ShowError(e.Message);}}
    private void ImportSave(){if(game!=null&&!game.HasExited){ShowError("Close the game before importing saves.");return;}if(exe==null){ShowError("Build or link the game before importing a Diary.");return;}string path=SelectFolder("Choose a decrypted Psychic Specters Europe save folder or exported Diary");if(path==null)return;if(MessageBox.Show(window,"Import this Diary? Current save files will be moved to a recovery backup. The original imported files will not be modified.",window.Title,MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes)return;try{string backup=SaveManager.Import(SaveRoot(),path);MessageBox.Show(window,"Diary imported. Previous files are preserved in:\n"+backup,window.Title);}catch(Exception e){ShowError(e.Message);}}
    private void ResetGraphicsCaches(){if(game!=null&&!game.HasExited){ShowError("Close the game before resetting caches.");return;}if(MessageBox.Show(window,"Reset the graphics caches? They will be recreated on the next launch. Saves and mods will be kept, and the old caches will be archived.",window.Title,MessageBoxButton.YesNo)!=MessageBoxResult.Yes)return;try{string path=LauncherMaintenance.ArchiveCaches(CurrentProfile());MessageBox.Show(window,path==null?"No graphics caches yet.":"Graphics caches reset. Previous caches archived in:\n"+path,window.Title);}catch(Exception e){ShowError(e.Message);}}
    private void ExportLogs(){if(game!=null&&!game.HasExited){ShowError("Close the game before exporting logs.");return;}string path=SelectFolder("Choose where to export logs (these can contain local file paths)");if(path==null)return;try{MessageBox.Show(window,"Logs exported to:\n"+LauncherMaintenance.ExportLogs(core.Root,path),window.Title);}catch(Exception e){ShowError(e.Message);}}
    private void ExportDiagnostic(){var dialog=new SaveFileDialog{Filter="Diagnostic report|*.txt",FileName="YW2-diagnostic.txt",Title="Export diagnostic report"};if(dialog.ShowDialog(window)!=true)return;try{File.WriteAllText(dialog.FileName,LauncherMaintenance.CreateDiagnostic(core.Root,CurrentProfile(),exe),new UTF8Encoding(false));MessageBox.Show(window,"Diagnostic exported.",window.Title);}catch(Exception e){ShowError(e.Message);}}
    private string ControlProfilesDirectory(){return Path.Combine(core.Root,"launcher-data","control-profiles");}
    private void RefreshControlProfiles(string selected=null){var box=Find<ComboBox>("ControlProfileBox");string current=selected??box.SelectedItem as string;box.ItemsSource=ControlProfileManager.List(ControlProfilesDirectory());box.SelectedItem=current;if(box.SelectedIndex<0&&box.Items.Count>0)box.SelectedIndex=0;Find<Button>("LoadControlProfile").IsEnabled=box.Items.Count>0;}
    private void SaveNamedControls(){if(demo)return;string name=Find<TextBox>("ControlProfileName").Text.Trim();if(!ControlProfileManager.ValidName(name)){ShowError("Enter a profile name (1–48 letters, numbers, spaces, _ or -).");return;}try{bool exists=ControlProfileManager.Exists(ControlProfilesDirectory(),name);if(exists&&MessageBox.Show(window,"Replace the control profile "+name+"?",window.Title,MessageBoxButton.YesNo)!=MessageBoxResult.Yes)return;var values=new Dictionary<string,string>();foreach(var binding in ControlBindings.All)foreach(bool pad in new[]{false,true})values[PreferenceKey(binding.Id,pad)]=BindingValue(binding,pad);values["stick_deadzone"]=((int)Find<Slider>("DeadzoneSlider").Value).ToString();values["stick_sensitivity"]=((int)Find<Slider>("SensitivitySlider").Value).ToString();ControlProfileManager.Save(ControlProfilesDirectory(),name,values,exists);RefreshControlProfiles(name);}catch(Exception e){ShowError(e.Message);}}
    private void LoadNamedControls(){string name=Find<ComboBox>("ControlProfileBox").SelectedItem as string;if(name==null)return;try{var values=ControlProfileManager.Load(ControlProfilesDirectory(),name);initializing=true;foreach(var entry in values)prefs[entry.Key]=entry.Value;Find<Slider>("DeadzoneSlider").Value=Num(LauncherCore.Get(values,"stick_deadzone","18"),18);Find<Slider>("SensitivitySlider").Value=Num(LauncherCore.Get(values,"stick_sensitivity","100"),100);initializing=false;SavePreferences();CancelCapture("Loaded control profile: "+name+".");}catch(Exception e){initializing=false;ShowError(e.Message);}}
    private void ResetSettings(){if(game!=null&&!game.HasExited){ShowError("Close the game before resetting settings.");return;}if(MessageBox.Show(window,"Reset graphics, audio and control settings? Saves and mods will be kept.",window.Title,MessageBoxButton.YesNo)!=MessageBoxResult.Yes)return;string linked=Pref("linked_dist",core.Dist);prefs.Clear();prefs["linked_dist"]=linked;string path=Path.Combine(CurrentProfile(),"settings.ini");if(File.Exists(path))File.Move(path,path+".backup-"+DateTime.Now.ToString("yyyyMMddHHmmssfff"));LoadOptions();SavePreferences();OpenControls();}
    internal void PreviewPage(string name){if(name=="settings")ShowPage(Graphics);else if(name=="display")ShowPage(Display);else if(name=="performance")ShowPage(Performance);else if(name=="controls")ShowPage(Controls);else if(name=="rom")ShowPage(Rom);else if(name=="mods")ShowPage(Mods);else if(name=="audio")ShowPage(Audio);else if(name=="maintenance")ShowPage(Maintenance);else ShowPage(Home);}
    internal async Task VerifyBuildFlow(string cartridge)
    {
        if(!selfTest)throw new InvalidOperationException("Not a test run.");
        rom=Path.GetFullPath(cartridge);if(!File.Exists(rom))throw new FileNotFoundException("Test cartridge missing.");
        ShowPage(Rom);ShowPage(Options);ShowPage(Compile);await Build(false);
        if(page!=Home||exe==null)throw new InvalidOperationException("The build flow did not return to Ready to play.");
        string found,source,why;if(!core.TryInstall(core.Dist,out found,out source,out why))throw new InvalidOperationException(why);
    }
    internal void VerifyControlsFlow(){if(!selfTest)throw new InvalidOperationException("Not a test run.");ShowPage(Controls);VerifyControlsEditor();}
    internal void VerifySettingsFlow(){
        if(!selfTest||exe==null)throw new InvalidOperationException("A separate compatible test installation must be provided.");
        ShowPage(Graphics);initializing=true;
        Set("BackendBox",0);Set("FpsBox",2);Set("ScaleBox",8);Find<TextBox>("CustomScale").Text="7";Check("FxaaCheck",true);Find<Slider>("SharpnessSlider").Value=65;
        Set("AspectBox",5);Find<TextBox>("AspectNumerator").Text="16";Find<TextBox>("AspectDenominator").Text="10";Set("WindowResolution",7);Find<TextBox>("WindowWidth").Text="1536";Find<TextBox>("WindowHeight").Text="864";Set("LayoutBox",9);Find<Slider>("PipSize").Value=45;Find<Slider>("PipOpacity").Value=80;initializing=false;SavePreferences();
        ShowPage(Audio);string selected="";var box=Find<ComboBox>("AudioDevice");if(box.Items.Count>1){box.SelectedIndex=1;selected=AudioDeviceValue();}Find<Slider>("VolumeSlider").Value=75;SavePreferences();
        var expected=new Dictionary<string,string>{{"frame_limit","60"},{"frame_interpolation","1"},{"resolution","7"},{"aspect_num","16"},{"aspect_den","10"},{"window_width","1536"},{"window_height","864"},{"single_screen","1"},{"picture_in_picture","1"},{"pip_size","45"},{"pip_opacity","80"},{"fxaa","1"},{"sharpening","65"},{"hd_textures","0"},{"volume","75"},{"audio_device",selected}};
        foreach(string file in new[]{core.Preferences,Path.Combine(CurrentProfile(),"settings.ini")}){var state=LauncherCore.ReadSettings(file);foreach(var pair in expected)if(LauncherCore.Get(state,pair.Key,"missing")!=pair.Value)throw new InvalidOperationException("Incorrect saved setting: "+pair.Key);}
        LoadOptions();if(Selected("ScaleBox")!=8||Selected("LayoutBox")!=9||!Find<CheckBox>("FxaaCheck").IsChecked.GetValueOrDefault()||Find<Slider>("SharpnessSlider").Value!=65||AudioDeviceValue()!=selected)throw new InvalidOperationException("Reloaded settings did not match the selection.");
    }
}
