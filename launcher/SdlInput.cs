using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

internal sealed class SdlInput : IDisposable
{
    private const string AssetHash = "2632e21625861a0dc106f2b1abb649e610d8be88534ba74c76a763abe5aefaa3";
    private const uint Gamepad = 0x00002000;
    private const uint Audio = 0x00000010;
    [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr LoadLibraryEx(string name, IntPtr reserved, uint flags);
    [DllImport("kernel32", CharSet = CharSet.Ansi, ExactSpelling = true)]
    private static extern IntPtr GetProcAddress(IntPtr module, string name);
    [DllImport("kernel32")] private static extern bool FreeLibrary(IntPtr module);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)] private delegate bool Init(uint flags);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void Quit(uint flags);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr GetList(out int count);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr Open(int index);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void Release(IntPtr value);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void Update();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)] private delegate bool Button(IntPtr device, int button);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate short Axis(IntPtr device, int axis);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr GetName(int code);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int GetCode([MarshalAs(UnmanagedType.LPStr)] string name);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void Events([MarshalAs(UnmanagedType.I1)] bool enabled);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)] private delegate bool Hint(string name, string value);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int AttachVirtual(IntPtr description);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)] private delegate bool DetachVirtual(int instance);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)] private delegate bool SetVirtualButton(IntPtr joystick, int button, [MarshalAs(UnmanagedType.I1)] bool down);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)] private delegate bool SetVirtualAxis(IntPtr joystick, int axis, short position);

    private IntPtr dll;
    private bool initialized;
    private readonly string root;
    private readonly Dictionary<int, IntPtr> devices = new Dictionary<int, IntPtr>();
    private readonly Dictionary<string, object> functions = new Dictionary<string, object>();
    internal SdlInput(string root) { this.root = root; }
    internal int Connected { get { return devices.Count; } }

    private T Api<T>(string name) where T : class
    {
        object cached;
        if (functions.TryGetValue(name, out cached)) return (T)cached;
        IntPtr proc = GetProcAddress(dll, name);
        if (proc == IntPtr.Zero) throw new EntryPointNotFoundException(name);
        cached = Marshal.GetDelegateForFunctionPointer(proc, typeof(T));
        functions.Add(name, cached);
        return (T)cached;
    }

    private void Load()
    {
        if (dll != IntPtr.Zero) return;
        string folder = Path.Combine(root, "input-runtime-" + AssetHash.Substring(0, 10));
        string file = Path.Combine(folder, "SDL3.dll");
        if (!File.Exists(file) || LauncherUpdater.HashFile(file) != AssetHash) {
            Directory.CreateDirectory(folder);
            string pending = Path.Combine(folder, Guid.NewGuid().ToString("N") + ".tmp");
            try {
                using (Stream resource = Assembly.GetExecutingAssembly().GetManifestResourceStream("Controls.SDL3.dll"))
                using (var output = File.Create(pending)) resource.CopyTo(output);
                if (LauncherUpdater.HashFile(pending) != AssetHash) throw new InvalidDataException("Controller support could not be verified.");
                if (File.Exists(file)) File.Replace(pending, file, null);
                else File.Move(pending, file);
            } finally { if (File.Exists(pending)) File.Delete(pending); }
        }
        dll = LoadLibraryEx(file, IntPtr.Zero, 0x00000900); // DLL directory and System32 only
        if (dll == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "Could not load controller support.");
    }

    internal string NameForScan(int code)
    {
        Load();
        string name = Marshal.PtrToStringAnsi(Api<GetName>("SDL_GetScancodeName")(code));
        return !String.IsNullOrEmpty(name) && Api<GetCode>("SDL_GetScancodeFromName")(name) == code ? name : null;
    }

    internal int ButtonCode(string id) { Load(); return Api<GetCode>("SDL_GetGamepadButtonFromString")(id); }

    internal string[] PlaybackDevices()
    {
        Load();
        if (!Api<Init>("SDL_InitSubSystem")(Audio)) throw new InvalidOperationException("SDL could not enumerate playback devices.");
        var names=new List<string>();
        IntPtr list=IntPtr.Zero;
        try {
            int count; list=Api<GetList>("SDL_GetAudioPlaybackDevices")(out count);
            for(int i=0;list!=IntPtr.Zero&&i<count;i++){
                IntPtr p=Api<GetName>("SDL_GetAudioDeviceName")(Marshal.ReadInt32(list,i*sizeof(int)));
                if(p==IntPtr.Zero)continue;
                int n=0;while(n<4096&&Marshal.ReadByte(p,n)!=0)n++;
                if(n==0||n==4096)continue;
                byte[] bytes=new byte[n];Marshal.Copy(p,bytes,0,n);
                string name=System.Text.Encoding.UTF8.GetString(bytes);
                if(!names.Contains(name))names.Add(name);
            }
            return names.ToArray();
        } finally {
            if(list!=IntPtr.Zero)Api<Release>("SDL_free")(list);
            Api<Quit>("SDL_QuitSubSystem")(Audio);
        }
    }

    internal void Start()
    {
        Load();
        if (!initialized) {
            Api<Hint>("SDL_SetHint")("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");
            if (!Api<Init>("SDL_InitSubSystem")(Gamepad)) throw new InvalidOperationException("SDL could not start controller input.");
            initialized = true;
            Api<Events>("SDL_SetGamepadEventsEnabled")(false);
        }
    }

    internal HashSet<string> Snapshot()
    {
        Start();
        Api<Update>("SDL_UpdateGamepads")();
        int count;
        IntPtr ids = Api<GetList>("SDL_GetGamepads")(out count);
        var live = new HashSet<int>();
        try {
            for (int i = 0; ids != IntPtr.Zero && i < count; i++) {
                int id = Marshal.ReadInt32(ids, i * sizeof(int));
                live.Add(id);
                if (devices.ContainsKey(id)) continue;
                var device = Api<Open>("SDL_OpenGamepad")(id);
                if (device != IntPtr.Zero) devices.Add(id, device);
            }
        } finally {
            if (ids != IntPtr.Zero) Api<Release>("SDL_free")(ids);
        }
        foreach (var id in new List<int>(devices.Keys)) if (!live.Contains(id)) {
            Api<Release>("SDL_CloseGamepad")(devices[id]);
            devices.Remove(id);
        }
        var names = Api<GetName>("SDL_GetGamepadStringForButton");
        var button = Api<Button>("SDL_GetGamepadButton");
        var axis = Api<Axis>("SDL_GetGamepadAxis");
        var pressed = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var device in devices.Values) {
            for (int i = 0; i < 26; i++) {
                if (!button(device, i)) continue;
                string name = Marshal.PtrToStringAnsi(names(i));
                if (!String.IsNullOrEmpty(name)) pressed.Add(name);
            }
            if (axis(device, 4) > 16000) pressed.Add("lefttrigger");
            if (axis(device, 5) > 16000) pressed.Add("righttrigger");
        }
        return pressed;
    }

    internal void Stop()
    {
        if (!initialized) return;
        foreach (var device in devices.Values) Api<Release>("SDL_CloseGamepad")(device);
        devices.Clear();
        Api<Quit>("SDL_QuitSubSystem")(Gamepad);
        initialized = false;
    }

    internal void VerifyVirtualInput()
    {
        Start();
        // SDL 3.4.x x64: zero the descriptor, then set the version (size), type, axes and buttons.
        const int size = 136;
        IntPtr desc = Marshal.AllocHGlobal(size);
        int id;
        try {
            Marshal.Copy(new byte[size], 0, desc, size);
            Marshal.WriteInt32(desc, 0, size);
            Marshal.WriteInt16(desc, 4, 1);
            Marshal.WriteInt16(desc, 12, 6);
            Marshal.WriteInt16(desc, 14, 26);
            id = Api<AttachVirtual>("SDL_AttachVirtualJoystick")(desc);
        } finally { Marshal.FreeHGlobal(desc); }
        if (id == 0) throw new InvalidOperationException("SDL could not attach the virtual controller.");
        IntPtr joystick = IntPtr.Zero;
        try {
            joystick = Api<Open>("SDL_OpenJoystick")(id);
            Snapshot();
            if (joystick == IntPtr.Zero ||
                !Api<SetVirtualButton>("SDL_SetJoystickVirtualButton")(joystick, ButtonCode("a"), true) ||
                !Api<SetVirtualAxis>("SDL_SetJoystickVirtualAxis")(joystick, 5, 32767))
                throw new InvalidOperationException("SDL could not send test inputs.");
            var captured = Snapshot();
            if (!captured.Contains("a") || !captured.Contains("righttrigger"))
                throw new InvalidOperationException("The controller reader missed the virtual face button or trigger. Connected: " +
                    Connected + "; pressed: " + String.Join(", ", captured));
            if (!Api<SetVirtualButton>("SDL_SetJoystickVirtualButton")(joystick, ButtonCode("a"), false) ||
                !Api<SetVirtualAxis>("SDL_SetJoystickVirtualAxis")(joystick, 5, -32768))
                throw new InvalidOperationException("SDL could not release the test inputs.");
            captured = Snapshot();
            if (captured.Contains("a") || captured.Contains("righttrigger"))
                throw new InvalidOperationException("The controller reader did not release the virtual inputs.");
        } finally {
            if (joystick != IntPtr.Zero) Api<Release>("SDL_CloseJoystick")(joystick);
            Api<DetachVirtual>("SDL_DetachVirtualJoystick")(id);
            Stop();
        }
    }

    public void Dispose()
    {
        Stop();
        if (dll != IntPtr.Zero) { functions.Clear(); FreeLibrary(dll); dll = IntPtr.Zero; }
    }
}
