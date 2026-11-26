#pragma once
#include "settings.h"
#include <SDL3/SDL.h>
#include <cwchar>

#if defined(_WIN32)
#include <windows.h>
#include <commctrl.h>
#endif

namespace frontend {
// Native Windows menu. SDL/game state is changed only when the frontend
// consumes the queued event, never from inside a Win32 window procedure.
class GameMenu {
public:
    enum : int { Scale=0x6200, Aspect=0x6220, Layout=0x6240,
                 Swap=0x6250, Fullscreen=0x6251, Stats=0x6252,
                 Hd=0x6260, Fps=0x6270 };
    static Uint32 event_type() { static Uint32 id=SDL_RegisterEvents(1);return id; }
    static bool dispatch(int command) {
        const Uint32 type=event_type();if(type==Uint32(-1))return false;
        SDL_Event e{};e.type=type;e.user.code=command;return SDL_PushEvent(&e);
    }
    explicit GameMenu(SDL_Window *window) {
#if defined(_WIN32)
        hwnd_=static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));
        if(!hwnd_)return;
        bar_=CreateMenu();
        if(!bar_)return;
        scale_=add_menu(L"Upscaling");aspect_=add_menu(L"Aspect ratio");layout_=add_menu(L"Display");
        fps_=add_menu(L"FPS");
        if(!scale_||!aspect_||!layout_||!fps_)return;
        for(int i=1;i<=10;++i){wchar_t label[24];std::swprintf(label,24,L"%d\u00d7%ls",i,i==1?L" (Native)":L"");AppendMenuW(scale_,MF_STRING,Scale+i,label);}
        const wchar_t *aspects[]={L"Original (5:3)",L"16:9",L"16:10",L"21:9",L"32:9 (Experimental)",L"Custom"};
        for(int i=0;i<6;++i)AppendMenuW(aspect_,MF_STRING,Aspect+i,aspects[i]);
        const wchar_t *layouts[]={L"Stacked",L"Side by side (top left)",L"Side by side (top right)",L"Top priority (left)",L"Top priority (right)",L"Custom two screens",L"Separate windows",L"Adaptive one screen",L"Top only",L"Picture-in-picture"};
        for(int i=0;i<10;++i)AppendMenuW(layout_,MF_STRING,Layout+i,layouts[i]);
        AppendMenuW(layout_,MF_SEPARATOR,0,nullptr);
        AppendMenuW(layout_,MF_STRING,Swap,L"Swap screens\tTab");
        AppendMenuW(layout_,MF_STRING,Fullscreen,L"Fullscreen\tF11");
        AppendMenuW(layout_,MF_STRING,Stats,L"Statistics\tF4");
        AppendMenuW(fps_,MF_STRING,Fps,L"Original timing");
        AppendMenuW(fps_,MF_STRING,Fps+1,L"30 FPS");
        AppendMenuW(fps_,MF_STRING,Fps+2,L"60 FPS (Interpolation)");
        hooked_=SetWindowSubclass(hwnd_,window_proc,0x594d,reinterpret_cast<DWORD_PTR>(this))!=FALSE;
#else
        (void)window;
#endif
    }
    ~GameMenu() {
#if defined(_WIN32)
        if(hwnd_&&IsWindow(hwnd_)&&hooked_){RemoveWindowSubclass(hwnd_,window_proc,0x594d);SetMenu(hwnd_,nullptr);}
        if(bar_)DestroyMenu(bar_);
#endif
    }
    void sync(const Settings &s,bool gpu,bool separate) {
#if defined(_WIN32)
        if(!hooked_)return;
        for(int i=1;i<=10;++i)EnableMenuItem(scale_,Scale+i,MF_BYCOMMAND|((gpu||i<=4)?MF_ENABLED:MF_GRAYED));
        CheckMenuRadioItem(scale_,Scale+1,Scale+10,Scale+s.resolution,MF_BYCOMMAND);
        CheckMenuRadioItem(aspect_,Aspect,Aspect+5,Aspect+(s.aspect_mode<0?0:s.aspect_mode),MF_BYCOMMAND);
        const int l=s.detached_screen?6:!s.single_screen?s.dual_layout:s.smart_layout?7:s.picture_in_picture?9:8;
        CheckMenuRadioItem(layout_,Layout,Layout+9,Layout+l,MF_BYCOMMAND);
        EnableMenuItem(layout_,Layout+6,MF_BYCOMMAND|(separate?MF_ENABLED:MF_GRAYED));
        CheckMenuItem(layout_,Swap,MF_BYCOMMAND|(s.swap_screens?MF_CHECKED:MF_UNCHECKED));
        CheckMenuItem(layout_,Fullscreen,MF_BYCOMMAND|(s.fullscreen?MF_CHECKED:MF_UNCHECKED));
        CheckMenuItem(layout_,Stats,MF_BYCOMMAND|(s.show_stats?MF_CHECKED:MF_UNCHECKED));
        EnableMenuItem(fps_,Fps+2,MF_BYCOMMAND|(gpu?MF_ENABLED:MF_GRAYED));
        CheckMenuRadioItem(fps_,Fps,Fps+2,Fps+(s.frame_interpolation&&gpu?2:s.frame_limit==30?1:0),MF_BYCOMMAND);
        HMENU wanted=s.fullscreen?nullptr:bar_;
        if(GetMenu(hwnd_)!=wanted)SetMenu(hwnd_,wanted);
        DrawMenuBar(hwnd_);
#else
        (void)s;(void)gpu;(void)separate;
#endif
    }
private:
#if defined(_WIN32)
    HWND hwnd_=nullptr;HMENU bar_=nullptr,scale_=nullptr,aspect_=nullptr,layout_=nullptr,fps_=nullptr;
    bool hooked_=false;
    HMENU add_menu(const wchar_t *name) {
        HMENU menu=CreatePopupMenu();
        if(!menu)return nullptr;
        if(!AppendMenuW(bar_,MF_POPUP,reinterpret_cast<UINT_PTR>(menu),name)){DestroyMenu(menu);return nullptr;}
        return menu;
    }
    static LRESULT CALLBACK window_proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
        if(msg==WM_COMMAND&&HIWORD(w)==0&&l==0) {
            int command=LOWORD(w);
            if(command>=Scale&&command<=Fps+2){dispatch(command);return 0;}
        }
        return DefSubclassProc(hwnd,msg,w,l);
    }
#endif
};
}
