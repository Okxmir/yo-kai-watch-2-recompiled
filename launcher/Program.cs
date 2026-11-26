using System;
using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

[assembly: System.Reflection.AssemblyTitle("Yo-kai Watch 2 Recompiled Launcher")]
internal static class Program
{
    [STAThread]
    internal static int Main(string[] args)
    {
        if(args.Length>0 && args[0]=="--finish-update") {
            try { LauncherUpdater.Finish(args);return 0; }
            catch(Exception error) { MessageBox.Show(error.Message,"Launcher update failed",MessageBoxButton.OK,MessageBoxImage.Error);return 1; }
        }
        bool preview=args.Length>=2 && args[0]=="--preview";
        bool verifyBuild=args.Length==4 && args[0]=="--verify-build" && args[2]=="--root";
        bool verifyControls=args.Length==4 && args[0]=="--verify-controls" && args[2]=="--root";
        bool verifySettings=args.Length==4 && args[0]=="--verify-settings" && args[2]=="--root";
        bool testing=verifyBuild||verifyControls||verifySettings;
        string report=null;
        try {
            string own=AppDomain.CurrentDomain.BaseDirectory;
            string root=File.Exists(Path.Combine(own,"build.ps1"))?own:Path.GetFullPath(Path.Combine(own,".."));
            if(args.Length>=4 && args[2]=="--root")root=Path.GetFullPath(args[3]);
            var app=new Application {ShutdownMode=ShutdownMode.OnMainWindowClose};
            Window w;
            using(Stream markup=Assembly.GetExecutingAssembly().GetManifestResourceStream("Launcher.xaml")) w=(Window)XamlReader.Load(markup);
            var launcher=new Launcher(w,new LauncherCore(root),preview,testing);
            if(testing){
                report=Path.Combine(root,"logs",verifyBuild?"launcher-build-verification.txt":verifySettings?"launcher-settings-verification.txt":"launcher-controls-verification.txt");
                Directory.CreateDirectory(Path.GetDirectoryName(report));
                bool started=false;
                w.ContentRendered+=async(s,e)=>{
                    if(started)return;started=true;
                    try{
                        if(verifyBuild)await launcher.VerifyBuildFlow(args[1]);else if(verifySettings)launcher.VerifySettingsFlow();else launcher.VerifyControlsFlow();
                        File.WriteAllText(report,"PASS\n");app.Shutdown(0);
                    }catch(Exception error){File.WriteAllText(report,error.ToString());app.Shutdown(1);}
                };
            }
            if(preview) {
                if(args.Length>=5)launcher.PreviewPage(args[4]);
                w.ContentRendered+=(s,e)=>w.Dispatcher.BeginInvoke(DispatcherPriority.ApplicationIdle,new Action(()=>{
                    try {
                        w.UpdateLayout();
                        var bitmap=new RenderTargetBitmap((int)Math.Ceiling(w.ActualWidth),(int)Math.Ceiling(w.ActualHeight),96,96,PixelFormats.Pbgra32);
                        bitmap.Render(w);var png=new PngBitmapEncoder();png.Frames.Add(BitmapFrame.Create(bitmap));
                        using(var file=File.Create(args[1]))png.Save(file);
                        app.Shutdown(0);
                    } catch(Exception error) {File.WriteAllText(args[1]+".error.txt",error.ToString());app.Shutdown(1);}
                }));
            }
            return app.Run(w);
        } catch(Exception error) {
            if(testing&&report!=null)File.WriteAllText(report,error.ToString());
            else if(preview)File.WriteAllText(args[1]+".error.txt",error.ToString());
            else MessageBox.Show(error.ToString(),"Yo-kai Watch 2 Launcher",MessageBoxButton.OK,MessageBoxImage.Error);
            return 1;
        }
    }
}
