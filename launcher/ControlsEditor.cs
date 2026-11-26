using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Interop;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Threading;

internal sealed partial class Launcher
{
    private Grid controlsRoot;
    private SdlInput inputReader;
    private DispatcherTimer inputTimer;
    private bool editingPad, fillingPadChoice;
    private string captureId;
    private int previousPadCount;
    private HashSet<string> previousPresses;
    private string controlMessage;
    private IDictionary<string, string> gameBindings = new Dictionary<string, string>();
    private readonly Dictionary<string, Button> bindingTiles = new Dictionary<string, Button>();
    private readonly Dictionary<string, Button> diagramButtons = new Dictionary<string, Button>();
    private readonly Dictionary<string, TextBlock> bindingLabels = new Dictionary<string, TextBlock>();

    private T Part<T>(string name) where T : class { return (T)controlsRoot.FindName(name); }

    private void InitializeControls()
    {
        using (var markup = Assembly.GetExecutingAssembly().GetManifestResourceStream("ControlsWorkbench.xaml"))
            controlsRoot = (Grid)XamlReader.Load(markup);
        Find<ContentControl>("ControlHost").Content = controlsRoot;
        inputReader = new SdlInput(core.Root);
        inputTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(28) };
        inputTimer.Tick += (s, e) => PollController();
        window.SourceInitialized += (s, e) => {
            var source = PresentationSource.FromVisual(window) as HwndSource;
            if (source != null) source.AddHook(ReadControlKey);
        };
        window.Closed += (s, e) => { inputTimer.Stop(); inputReader.Dispose(); };

        foreach (var id in ControlBindings.Left) Part<StackPanel>("LeftBindings").Children.Add(CreateBindingTile(id, ""));
        foreach (var id in ControlBindings.Right) Part<StackPanel>("RightBindings").Children.Add(CreateBindingTile(id, ""));
        string[] arrows = { "↑", "↓", "←", "→" };
        for (int i = 0; i < ControlBindings.Circle.Length; i++)
            Part<UniformGrid>("CircleBindings").Children.Add(CreateBindingTile(ControlBindings.Circle[i], arrows[i]));
        foreach (var binding in ControlBindings.All.Where(x => !x.Id.StartsWith("circle_", StringComparison.Ordinal))) {
            string id = binding.Id;
            string suffix = id == "zl" || id == "zr" ? id.ToUpperInvariant() : Char.ToUpperInvariant(id[0]) + id.Substring(1);
            var button = Part<Button>("Hit" + suffix);
            if (button == null) throw new InvalidDataException("Missing 3DS button: " + suffix);
            button.ToolTip = "Remap " + binding.Label;
            System.Windows.Automation.AutomationProperties.SetName(button, "Remap " + binding.Label);
            button.Click += (s, e) => BeginBinding(id);
            diagramButtons.Add(id, button);
        }
        Part<Button>("KeyboardMode").Click += (s, e) => SwitchInputMode(false);
        Part<Button>("ControllerMode").Click += (s, e) => SwitchInputMode(true);
        Part<Button>("ResetBindings").Click += (s, e) => {
            if (MessageBox.Show(window, editingPad ? "Restore the default controller bindings?" : "Restore the default keyboard bindings?",
                "Reset controls", MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes)
                ResetControlMode();
        };
        Part<Button>("Unbind").Click += (s, e) => AssignControl("");
        Part<Button>("CancelCapture").Click += (s, e) => CancelCapture("Binding unchanged.");
        var choice = Part<ComboBox>("PadChoice");
        choice.Items.Add(new ComboBoxItem { Content = "Choose button…", Tag = null });
        foreach (var item in ControlBindings.ControllerButtons()) choice.Items.Add(new ComboBoxItem { Content = item.Value, Tag = item.Key });
        choice.SelectedIndex = 0;
        choice.SelectionChanged += (s, e) => {
            var item = choice.SelectedItem as ComboBoxItem;
            if (!fillingPadChoice && captureId != null && editingPad && item != null && item.Tag is string)
                AssignControl((string)item.Tag);
        };
        RefreshBindings();
    }

    private Button CreateBindingTile(string id, string symbol)
    {
        var binding = ControlBindings.Find(id);
        var tile = new Button { Style = (Style)controlsRoot.Resources["BindingTile"] };
        var layout = new Grid();
        layout.ColumnDefinitions.Add(new ColumnDefinition { Width = symbol == "" ? new GridLength(1, GridUnitType.Star) : GridLength.Auto });
        layout.ColumnDefinitions.Add(new ColumnDefinition { Width = symbol == "" ? GridLength.Auto : new GridLength(1, GridUnitType.Star) });
        var label = new TextBlock {
            Text = symbol == "" ? binding.Label : symbol,
            FontSize = symbol == "" ? 12 : 11, FontWeight = FontWeights.SemiBold,
            VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis
        };
        var value = new TextBlock { FontSize = 11, FontWeight = FontWeights.SemiBold,
            VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis, TextAlignment = TextAlignment.Center };
        var cap = new Border {
            CornerRadius = new CornerRadius(4), Background = Brush("#39445B"), Padding = new Thickness(6, 1, 6, 1),
            Margin = new Thickness(4, 0, 0, 0), MaxWidth = symbol == "" ? 100 : 63, Child = value
        };
        Grid.SetColumn(cap, 1);
        layout.Children.Add(label);
        layout.Children.Add(cap);
        tile.Content = layout;
        if (symbol != "") {
            tile.Margin = new Thickness(2, 0, 2, 0);
            tile.Padding = new Thickness(4, 4, 4, 4);
            cap.Margin = new Thickness(3, 0, 0, 0);
            cap.Padding = new Thickness(2, 1, 2, 1);
        }
        tile.Click += (s, e) => BeginBinding(id);
        bindingTiles.Add(id, tile);
        bindingLabels.Add(id, value);
        return tile;
    }

    private static Brush Brush(string color) { return (Brush)new BrushConverter().ConvertFromString(color); }
    private static string BindingKey(string id, bool pad) { return (pad ? "pad_" : "key_") + id; }
    private static string PreferenceKey(string id, bool pad) { return "binding_" + BindingKey(id, pad); }

    private string BindingValue(ControlBindings.Binding input, bool pad)
    {
        string key = BindingKey(input.Id, pad);
        return Pref("binding_" + key, LauncherCore.Get(gameBindings, key, pad ? input.Pad : input.Key));
    }

    private void OpenControls()
    {
        gameBindings = exe == null ? new Dictionary<string, string>() :
            LauncherCore.ReadSettings(Path.Combine(core.Profile(exe), "settings.ini"));
        RefreshBindings();
        RefreshControlProfiles();
    }

    private void RefreshBindings()
    {
        Part<Button>("KeyboardMode").Tag = !editingPad ? "selected" : null;
        Part<Button>("ControllerMode").Tag = editingPad ? "selected" : null;
        foreach (var binding in ControlBindings.All) {
            var id = binding.Id;
            var label = bindingLabels[id];
            string value = BindingValue(binding, editingPad);
            bool active = captureId == id;
            string display = ControlBindings.Display(value, editingPad);
            bool compact = id.StartsWith("circle_", StringComparison.Ordinal);
            label.Text = active ? "…" : editingPad && String.IsNullOrEmpty(value) && compact ? "Stick" : display;
            label.Foreground = Brush(String.IsNullOrEmpty(value) && !active ? "#A6AFC1" : "#F2F5FF");
            var tile = bindingTiles[id];
            tile.Tag = active ? "selected" : null;
            tile.ToolTip = binding.Label + " · " + display;
            System.Windows.Automation.AutomationProperties.SetName(tile, "Remap " + binding.Label + ", " + display);
            Button button;
            if (diagramButtons.TryGetValue(id, out button)) button.Tag = active ? "selected" : null;
        }
        Part<FrameworkElement>("CaptureActions").Visibility = captureId == null ? Visibility.Collapsed : Visibility.Visible;
        Part<ComboBox>("PadChoice").Visibility = captureId != null && editingPad ? Visibility.Visible : Visibility.Collapsed;
        Part<TextBlock>("CaptureStatus").Text = controlMessage ?? "Click a mapping or a button on the 3DS to change it. Changes apply on the next launch.";
        Part<TextBlock>("ModeHint").Text = editingPad
            ? "Left stick moves the Circle Pad. R3 swaps the main screen; optional buttons can supplement the stick."
            : "Keys use physical QWERTY labels. Mouse clicks control the touchscreen. Tab and the display hotkeys are fixed.";
    }

    private void SwitchInputMode(bool pad)
    {
        if (editingPad == pad) return;
        CancelCapture(null);
        editingPad = pad;
        RefreshBindings();
    }

    private void BeginBinding(string id)
    {
        if (page != Controls || ControlBindings.Find(id) == null) return;
        CancelCapture(null);
        captureId = id;
        string label = ControlBindings.Find(id).Label;
        if (editingPad) {
            fillingPadChoice = true;
            Part<ComboBox>("PadChoice").SelectedIndex = 0;
            fillingPadChoice = false;
            previousPresses = null;
            previousPadCount = -1;
            controlMessage = "Press a controller button for " + label + ", or choose one.";
            inputTimer.Start();
        } else {
            controlMessage = "Press a key for " + label + ". Esc cancels.";
        }
        RefreshBindings();
    }

    private void PollController()
    {
        if (captureId == null || !editingPad || page != Controls) return;
        if (!window.IsActive) { previousPresses = null; return; }
        try {
            var pressed = inputReader.Snapshot();
            int count = inputReader.Connected;
            if (previousPresses == null || previousPadCount != count) {
                previousPresses = pressed;
                previousPadCount = count;
                controlMessage = count == 0 ? "No controller detected. Connect one or choose a button." :
                    "Press a controller button for " + ControlBindings.Find(captureId).Label + ".";
                Part<TextBlock>("CaptureStatus").Text = controlMessage;
                return;
            }
            string edge = pressed.FirstOrDefault(x => !previousPresses.Contains(x));
            previousPresses = pressed;
            if (edge == "rightstick") {
                Part<TextBlock>("CaptureStatus").Text = "R3 is reserved for the touchscreen inset.";
            } else if (edge != null) AssignControl(edge);
        } catch (Exception) {
            inputTimer.Stop();
            inputReader.Stop();
            Part<TextBlock>("CaptureStatus").Text = "Controller capture is unavailable. Choose a button instead.";
        }
    }

    private IntPtr ReadControlKey(IntPtr hwnd, int message, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (captureId == null || page != Controls || (message != 0x100 && message != 0x104)) return IntPtr.Zero;
        long bits = lParam.ToInt64();
        if (editingPad && wParam.ToInt64() != 0x1B) return IntPtr.Zero;
        handled = true;
        if ((bits & (1L << 30)) != 0) return IntPtr.Zero;
        if (wParam.ToInt64() == 0x1B) {
            CancelCapture("Binding unchanged.");
            return IntPtr.Zero;
        }
        int scan = ControlBindings.ScanCode((int)((bits >> 16) & 0x1ff));
        if (ControlBindings.ReservedScan(scan)) {
            Part<TextBlock>("CaptureStatus").Text = "That key is reserved by the game. Try another key.";
            return IntPtr.Zero;
        }
        string value = null;
        try { if (scan != 0) value = inputReader.NameForScan(scan); }
        catch (Exception) { Part<TextBlock>("CaptureStatus").Text = "Keyboard capture could not start."; return IntPtr.Zero; }
        if (value != null) AssignControl(value);
        else Part<TextBlock>("CaptureStatus").Text = "That key cannot be saved by the game. Try another key.";
        return IntPtr.Zero;
    }

    private void AssignControl(string value)
    {
        if (captureId == null) return;
        var binding = ControlBindings.Find(captureId);
        var previous = ControlBindings.All.FirstOrDefault(x => x.Id != binding.Id && value != "" &&
            String.Equals(value, BindingValue(x, editingPad), StringComparison.OrdinalIgnoreCase));
        prefs[PreferenceKey(binding.Id, editingPad)] = value;
        SavePreferences();
        var note = binding.Label + " → " + ControlBindings.Display(value, editingPad) + ".";
        if (previous != null) note += " Also assigned to " + previous.Label + ".";
        CancelCapture(note);
    }

    private void CancelCapture(string note)
    {
        if (controlsRoot == null) return;
        inputTimer.Stop();
        inputReader.Stop();
        previousPresses = null;
        captureId = null;
        controlMessage = note;
        RefreshBindings();
    }

    private void ResetControlMode()
    {
        foreach (var binding in ControlBindings.All)
            prefs[PreferenceKey(binding.Id, editingPad)] = editingPad ? binding.Pad : binding.Key;
        SavePreferences();
        CancelCapture(editingPad ? "Controller defaults restored." : "Keyboard defaults restored.");
    }

    private void ApplyControlPreferences(IDictionary<string, string> settings)
    {
        foreach (var binding in ControlBindings.All) foreach (bool pad in new[] { false, true }) {
            string value;
            if (prefs.TryGetValue(PreferenceKey(binding.Id, pad), out value))
                settings[BindingKey(binding.Id, pad)] = value;
        }
    }

    private void VerifyControlsEditor()
    {
        SwitchInputMode(false);
        ResetControlMode();
        diagramButtons["b"].RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        if (captureId != "b") throw new InvalidOperationException("The 3DS B hotspot did not begin capture.");
        bool handled = false;
        ReadControlKey(IntPtr.Zero, 0x100, (IntPtr)0x09, (IntPtr)(0x0f << 16), ref handled);
        if (!handled || captureId != "b") throw new InvalidOperationException("The editor accepted Tab as a remap.");
        handled = false;
        ReadControlKey(IntPtr.Zero, 0x100, (IntPtr)0x47, (IntPtr)(0x22 << 16), ref handled);
        if (!handled || captureId != null || Pref("binding_key_b", "") != "G")
            throw new InvalidOperationException("The editor failed to capture or store the G key.");
        SwitchInputMode(true);
        ResetControlMode();
        inputReader.VerifyVirtualInput();
        foreach (var candidate in ControlBindings.ControllerButtons())
            if (candidate.Key != "lefttrigger" && candidate.Key != "righttrigger" && inputReader.ButtonCode(candidate.Key) < 0)
                throw new InvalidOperationException("Unrecognized SDL controller button: " + candidate.Key);
        diagramButtons["b"].RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        var choice = Part<ComboBox>("PadChoice");
        choice.SelectedItem = choice.Items.OfType<ComboBoxItem>().Single(x => object.Equals(x.Tag, "righttrigger"));
        if (captureId != null || Pref("binding_pad_b", "") != "righttrigger" || Pref("binding_key_b", "") != "G")
            throw new InvalidOperationException("Controller setup did not preserve the separate keyboard binding.");
        var persisted = LauncherCore.ReadSettings(core.Preferences);
        if (LauncherCore.Get(persisted, "binding_key_b", "") != "G" || LauncherCore.Get(persisted, "binding_pad_b", "") != "righttrigger")
            throw new InvalidOperationException("Remaps did not survive a settings reload.");
    }
}
