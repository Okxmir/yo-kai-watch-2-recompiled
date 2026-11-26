using System;
using System.Collections.Generic;
using System.Linq;

internal static class ControlBindings
{
    internal sealed class Binding
    {
        internal readonly string Id, Label, Key, Pad;
        internal Binding(string id, string label, string key, string pad)
        { Id = id; Label = label; Key = key; Pad = pad; }
    }

    internal static readonly Binding[] All = {
        new Binding("l", "L", "Q", "leftshoulder"), new Binding("zl", "ZL", "1", "lefttrigger"),
        new Binding("r", "R", "E", "rightshoulder"), new Binding("zr", "ZR", "3", "righttrigger"),
        new Binding("up", "D-pad Up", "Up", "dpup"), new Binding("down", "D-pad Down", "Down", "dpdown"),
        new Binding("left", "D-pad Left", "Left", "dpleft"), new Binding("right", "D-pad Right", "Right", "dpright"),
        new Binding("a", "A · Confirm", "K", "b"), new Binding("b", "B · Cancel", "J", "a"),
        new Binding("x", "X · Menu", "I", "y"), new Binding("y", "Y", "U", "x"),
        new Binding("select", "Select", "Backspace", "back"), new Binding("start", "Start", "Return", "start"),
        new Binding("circle_up", "Circle Pad Up", "W", ""), new Binding("circle_down", "Circle Pad Down", "S", ""),
        new Binding("circle_left", "Circle Pad Left", "A", ""), new Binding("circle_right", "Circle Pad Right", "D", ""),
    };
    internal static readonly string[] Left = { "l", "zl", "up", "down", "left", "right", "select" };
    internal static readonly string[] Right = { "r", "zr", "x", "y", "a", "b", "start" };
    internal static readonly string[] Circle = { "circle_up", "circle_down", "circle_left", "circle_right" };
    internal static Binding Find(string id) { return All.FirstOrDefault(x => x.Id == id); }

    private static readonly Dictionary<string, string> PadLabels = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase) {
        {"a", "South (Xbox A)"}, {"b", "East (Xbox B)"}, {"x", "West (Xbox X)"}, {"y", "North (Xbox Y)"},
        {"leftshoulder", "Left bumper"}, {"rightshoulder", "Right bumper"},
        {"lefttrigger", "Left trigger"}, {"righttrigger", "Right trigger"}, {"back", "View / Back"}, {"start", "Menu / Start"},
        {"leftstick", "L3"}, {"rightstick", "R3"}, {"dpup", "D-pad Up"}, {"dpdown", "D-pad Down"},
        {"dpleft", "D-pad Left"}, {"dpright", "D-pad Right"},
        {"misc1", "Share / Capture"}, {"paddle1", "Right paddle 1"}, {"paddle2", "Left paddle 1"},
        {"paddle3", "Right paddle 2"}, {"paddle4", "Left paddle 2"}, {"touchpad", "Touchpad click"},
        {"misc2", "Extra button 2"}, {"misc3", "Extra button 3"}, {"misc4", "Extra button 4"},
        {"misc5", "Extra button 5"}, {"misc6", "Extra button 6"}
    };

    internal static IEnumerable<KeyValuePair<string, string>> ControllerButtons()
    { return PadLabels.Where(x => x.Key != "rightstick"); }

    internal static string Display(string value, bool pad)
    {
        if (String.IsNullOrEmpty(value)) return "Unbound";
        string label;
        if (pad && PadLabels.TryGetValue(value, out label)) return label;
        return value == "Return" ? "Enter" : value;
    }

    private static readonly Dictionary<int, int> OtherScans = new Dictionary<int, int> {
        {0x01,41},{0x0C,45},{0x0D,46},{0x0E,42},{0x0F,43},{0x1A,47},{0x1B,48},{0x1C,40},{0x1D,224},
        {0x27,51},{0x28,52},{0x29,53},{0x2A,225},{0x2B,49},{0x33,54},{0x34,55},{0x35,56},{0x36,229},
        {0x37,85},{0x38,226},{0x39,44},{0x3A,57},{0x46,71},{0x47,95},{0x48,96},{0x49,97},{0x4A,86},
        {0x4B,92},{0x4C,93},{0x4D,94},{0x4E,87},{0x4F,89},{0x50,90},{0x51,91},{0x52,98},{0x53,99},{0x56,100},
        {0x57,68},{0x58,69},{0x145,83},{0x11C,88},{0x11D,228},{0x135,84},{0x137,70},{0x138,230},
        {0x147,74},{0x148,82},{0x149,75},{0x14B,80},{0x14D,79},{0x14F,77},{0x150,81},{0x151,78},
        {0x152,73},{0x153,76}
    };

    internal static int ScanCode(int winScan)
    {
        if (winScan >= 0x02 && winScan <= 0x0B) return 30 + winScan - 0x02;
        if (winScan >= 0x10 && winScan <= 0x19) return 4 + "QWERTYUIOP"[winScan - 0x10] - 'A';
        if (winScan >= 0x1E && winScan <= 0x26) return 4 + "ASDFGHJKL"[winScan - 0x1E] - 'A';
        if (winScan >= 0x2C && winScan <= 0x32) return 4 + "ZXCVBNM"[winScan - 0x2C] - 'A';
        if (winScan >= 0x3B && winScan <= 0x44) return 58 + winScan - 0x3B;
        int code;
        return OtherScans.TryGetValue(winScan, out code) ? code : 0;
    }

    internal static bool ReservedScan(int scan) { return scan == 43 || scan >= 62 && scan <= 69; }
}
