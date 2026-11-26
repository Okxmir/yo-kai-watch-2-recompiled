#!/usr/bin/env python3
"""Yo-kai Watch 2 native Linux builder and launcher."""
import os
import subprocess
import sys
import threading
import time
from pathlib import Path

from model import ROOT, Session, check, read_ini, write_ini, version

try:
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
except ImportError:
    print('Install python3-tk for the graphical launcher, or run bash build.sh --cartridge YOUR_ROM.3ds.',file=sys.stderr)
    raise SystemExit(1)

LANGS={'English':('en',1),'French':('fr',2),'German':('de',3),'Italian':('it',4),'Spanish':('es',5),'Dutch':('nl',8),'Russian':('ru',10)}
BG='#181b21';CARD='#242932';FG='#edf1f8';MUTED='#abb6ce';GREEN='#75bd9e'

class Launcher:
    def __init__(self,root):
        self.win=root;self.session=Session();self.busy=False;self.game=None;self.last_check=0;self.checking=False
        self.win.title('Yo-kai Watch 2 Recompiled');self.win.geometry('960x625');self.win.minsize(720,540);self.win.configure(bg=BG)
        self.win.protocol('WM_DELETE_WINDOW',self.close)
        self.style=ttk.Style();self.style.theme_use('clam');self.style.configure('TCombobox',fieldbackground=CARD,background=CARD,foreground=FG,arrowcolor=FG)
        configured=read_ini(self.session.ini);self.rom=self.session.prefs.get('rom','');manifest=self.session.manifest()
        if manifest:self.rom=manifest['cartridge']
        self.language=tk.StringVar(value=next((n for n,(_,v) in LANGS.items() if str(v)==configured.get('language','1')),'English'))
        self.fps=tk.StringVar(value='60 FPS (interpolation)' if configured.get('frame_interpolation')=='1' else '30 FPS' if configured.get('frame_limit')=='30' else 'Original timing')
        self.scale=tk.StringVar(value=configured.get('resolution','1'));self.aspect=tk.StringVar(value=configured.get('aspect_mode','0'))
        self.layout=tk.StringVar(value='Top only' if configured.get('single_screen')=='1' else 'Side by side' if configured.get('dual_layout')=='1' else 'Stacked')
        self.vsync=tk.BooleanVar(value=configured.get('vsync','1')=='1');self.fullscreen=tk.BooleanVar(value=configured.get('fullscreen','0')=='1')
        self.volume=tk.IntVar(value=int(configured.get('volume','100')));self.status=tk.StringVar();self.page=tk.Frame(root,bg=BG);self.page.pack(expand=True,fill='both')
        self.icon=None
        try:
            image=tk.PhotoImage(file=str(ROOT/'launcher/assets/icon.png'));self.win.iconphoto(True,image);self.icon=image.subsample(max(1,image.width()//40));self.big_icon=image.subsample(max(1,image.width()//100))
        except tk.TclError:self.big_icon=None
        self.home();self.win.after(100,self.start_update_check)

    def button(self,parent,text,command,primary=False):
        return tk.Button(parent,text=text,command=command,bg=GREEN if primary else CARD,fg='#14241e' if primary else FG,activebackground='#91cfb4' if primary else '#343d4a',activeforeground=FG,relief='flat',padx=18,pady=10,font=('Sans',12,'bold'),cursor='hand2')
    def label(self,parent,text,large=False,muted=False,**kw):
        return tk.Label(parent,text=text,bg=BG,fg=MUTED if muted else FG,font=('Sans',26 if large else 11,'bold' if large else 'normal'),**kw)
    def clear(self):
        for w in self.page.winfo_children():w.destroy()
        header=tk.Frame(self.page,bg=BG);header.pack(fill='x',padx=24,pady=20)
        if self.icon:tk.Label(header,image=self.icon,bg=BG).pack(side='left',padx=(0,10))
        self.label(header,'Yo-kai Watch 2 Recompiled').pack(side='left');self.label(header,'V'+self.session.version,muted=True).pack(side='left',padx=10)
    def home(self):
        self.clear();body=tk.Frame(self.page,bg=BG);body.pack(expand=True,fill='both',padx=60,pady=35)
        if self.session.update_required or self.checking:
            self.label(body,'Launcher update required' if self.session.update_required else 'Checking for updates',large=True).pack(pady=(70,12));self.label(body,'',muted=True,wraplength=650,textvariable=self.status).pack();self.button(body,'Retry',self.start_update_check,True).pack(pady=24);return
        manifest=self.session.manifest();ready=manifest is not None and not self.session.rebuild_required
        if self.big_icon:tk.Label(body,image=self.big_icon,bg=BG).pack(pady=(20,8))
        self.label(body,'Yo-kai Watch 2',large=True).pack();self.label(body,'Psychic Specters · Linux x64',muted=True).pack(pady=(0,20))
        if ready:self.label(body,'Ready to play').pack()
        elif self.session.rebuild_required:self.label(body,'Rebuild required — your saves will be kept.').pack()
        else:self.label(body,'Build your own decrypted European copy.').pack()
        row=tk.Frame(body,bg=BG);row.pack(pady=18)
        if ready:self.button(row,'▶  Play',self.play,True).pack(side='left',padx=5)
        self.button(row,'Rebuild game' if self.session.rebuild_required else 'Build game' if not ready else 'New build',self.choose_rom,not ready).pack(side='left',padx=5)
        self.button(row,'Settings',self.settings).pack(side='left',padx=5)
        if manifest:self.button(body,'Open save files',lambda:self.open_folder(self.session.dist/'data/games/00040000001b2900')).pack(pady=3)
        self.label(body,self.status.get(),muted=True,wraplength=660).pack(pady=10)
    def close(self):
        if self.busy:messagebox.showinfo('Build in progress','Wait for the build to finish.');return
        self.win.destroy()
    def start_update_check(self,play_after=False):
        if self.busy or self.checking or self.game is not None and self.game.poll() is None:return
        self.checking=True;self.status.set('Checking GitHub for the latest release…');self.home()
        def worker():
            result=None;error='';updated=False
            try:
                result=check()
                if result and version(result['version'])>version(self.session.version):
                    self.session.require(result['version']);self.win.after(0,lambda:self.status.set('Downloading V'+result['version']+'…'))
                    self.session.update(result,lambda s:self.win.after(0,lambda:self.status.set(s)));updated=True
            except Exception as e:error=str(e)
            self.win.after(0,lambda:self.done_update(updated,error,play_after))
        threading.Thread(target=worker,daemon=True).start()
    def done_update(self,updated,error,play_after):
        self.checking=False;self.last_check=time.monotonic()
        if updated:os.execv(sys.executable,[sys.executable,str(ROOT/'launcher/linux/launcher.py')])
        self.status.set(('Update unavailable: '+error) if error and self.session.update_required else '');self.home()
        if play_after and not self.session.update_required:self.play()
    def choose_rom(self):
        if self.busy or self.session.update_required:return
        selected=filedialog.askopenfilename(title='Choose decrypted European Psychic Specters ROM',initialdir=str(Path(self.rom).parent) if self.rom else str(ROOT),filetypes=[('3DS images','*.3ds *.cci *.cxi *.cia'),('All files','*')])
        if selected:self.rom=selected;self.session.prefs['rom']=selected;self.session.save();self.build_page()
    def save_settings(self):
        values=read_ini(self.session.ini);rate='60' if self.fps.get().startswith('60') else '30' if self.fps.get().startswith('30') else '0'
        values.update(language=str(LANGS[self.language.get()][1]),frame_limit=rate,frame_interpolation=str(int(rate=='60')),resolution=self.scale.get(),aspect_mode=self.aspect.get(),dual_layout='1' if self.layout.get()=='Side by side' else '0',single_screen=str(int(self.layout.get()=='Top only')),vsync=str(int(self.vsync.get())),fullscreen=str(int(self.fullscreen.get())),window_mode=str(int(self.fullscreen.get())),volume=str(self.volume.get()),show_launcher='0',hd_textures='0',check_updates='0');write_ini(self.session.ini,values)
    def settings(self):
        self.clear();body=tk.Frame(self.page,bg=BG);body.pack(fill='both',expand=True,padx=65,pady=18);self.label(body,'Settings',large=True).pack(anchor='w',pady=(0,15))
        for label,var,options in [('Game language',self.language,list(LANGS)),('Frame rate',self.fps,['Original timing','30 FPS','60 FPS (interpolation)']),('Upscaling',self.scale,[str(i) for i in range(1,11)]),('Aspect (0=original, 1=16:9, 2=16:10)',self.aspect,['0','1','2','3','4']),('Screen layout',self.layout,['Stacked','Side by side','Top only'])]:
            row=tk.Frame(body,bg=BG);row.pack(fill='x',pady=7);self.label(row,label).pack(side='left');ttk.Combobox(row,textvariable=var,values=options,state='readonly',width=26).pack(side='right')
        for label,var in [('VSync',self.vsync),('Fullscreen',self.fullscreen)]:tk.Checkbutton(body,text=label,variable=var,bg=BG,fg=FG,selectcolor=CARD,activebackground=BG,activeforeground=FG).pack(anchor='w')
        self.button(body,'Done',lambda:(self.save_settings(),self.home()),True).pack(pady=16)
    def build_page(self):
        self.clear();body=tk.Frame(self.page,bg=BG);body.pack(expand=True,fill='both',padx=55,pady=20);self.label(body,'Build your game',large=True).pack(anchor='w');self.label(body,Path(self.rom).name,muted=True).pack(anchor='w',pady=8)
        ttk.Combobox(body,textvariable=self.language,values=list(LANGS),state='readonly',width=26).pack(anchor='w',pady=8)
        self.button(body,'Build',self.start_build,True).pack(anchor='w',pady=12);self.button(body,'Back',self.home).pack(anchor='w');self.label(body,'First build downloads portable Rust and SDL3. System build prerequisites are listed in BUILD_INSTRUCTIONS.md.',muted=True,wraplength=680).pack(anchor='w',pady=14)
    def start_build(self):
        if self.busy or self.session.update_required:return
        self.busy=True;self.save_settings();language=LANGS[self.language.get()][0];cartridge=self.rom;self.clear();body=tk.Frame(self.page,bg=BG);body.pack(expand=True,fill='both',padx=55,pady=25);self.label(body,'Building…',large=True).pack(anchor='w');output=tk.Text(body,bg=CARD,fg=FG,wrap='word',height=18);output.pack(expand=True,fill='both',pady=16)
        def worker():
            error='';code=1
            try:
                log=ROOT/'logs/launcher-linux-build.txt';log.parent.mkdir(exist_ok=True)
                with log.open('w',encoding='utf-8') as f,subprocess.Popen(['bash',str(ROOT/'build.sh'),'--cartridge',cartridge,'--language',language],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,bufsize=1) as p:
                    for line in p.stdout:
                        f.write(line);f.flush();self.win.after(0,lambda s=line:(output.insert('end',s),output.see('end')))
                    code=p.wait()
            except Exception as e:error=str(e)
            self.win.after(0,lambda:self.done_build(code,error))
        threading.Thread(target=worker,daemon=True).start()
    def done_build(self,code,error):
        self.busy=False
        if code or self.session.manifest() is None:messagebox.showerror('Build failed',error or 'See logs/launcher-linux-build.txt and BUILD_INSTRUCTIONS.md.')
        else:self.status.set('Build complete. Ready to play.');self.save_settings()
        self.home()
    def play(self):
        if self.busy or self.checking or self.session.update_required or self.session.rebuild_required:return
        if time.monotonic()-self.last_check>120:self.start_update_check(play_after=True);return
        manifest=self.session.manifest()
        if not manifest or manifest['version']!=self.session.version:return
        self.save_settings();env=os.environ.copy();env.update(YW2_DATA_DIR=str(self.session.dist/'data'),YW2_STATE_DIR=str(self.session.dist/'data'))
        try:
            self.game=subprocess.Popen([str(self.session.executable),'--play',manifest['cartridge']],cwd=self.session.dist,env=env);self.win.iconify();self.win.after(300,self.poll_game)
        except OSError as e:messagebox.showerror('Could not start game',str(e))
    def poll_game(self):
        if self.game.poll() is None:self.win.after(300,self.poll_game);return
        self.win.deiconify();self.game=None;self.home()
    def open_folder(self,path):
        path.mkdir(parents=True,exist_ok=True);subprocess.Popen(['xdg-open',str(path)])

if __name__=='__main__':
    root=tk.Tk();app=Launcher(root);root.mainloop()
