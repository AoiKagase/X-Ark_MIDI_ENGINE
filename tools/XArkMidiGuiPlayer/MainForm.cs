using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace XArkMidiGuiPlayer;

public sealed class MainForm : Form
{
    private const int ChannelCount = 16;
    private const int KeyMaskWordCount = 4;
    private readonly TextBox _midiPathTextBox = new() { Width = 520 };
    private readonly TextBox _soundFontPathTextBox = new() { Width = 520 };
    private readonly Button _browseMidiButton = new() { Text = "Open MIDI..." };
    private readonly Button _browseSoundFontButton = new() { Text = "Open Bank..." };
    private readonly Button _playButton = new() { Text = "Play", Width = 90 };
    private readonly Button _stopButton = new() { Text = "Stop", Width = 90, Enabled = false };
    private readonly Label _statusLabel = new() { AutoSize = true, Text = "Idle" };
    private readonly TrackBar _seekTrackBar = new() { Dock = DockStyle.Fill, Minimum = 0, Maximum = 1, TickStyle = TickStyle.None, Enabled = false };
    private readonly Label _timeLabel = new() { AutoSize = true, Text = "00:00 / 00:00", Anchor = AnchorStyles.Left };
    private readonly GroupBox _createOptionsGroup = new() { Dock = DockStyle.Top, Text = "Engine Create Options", AutoSize = true };
    private readonly NumericUpDown _maxSampleDataBytesUpDown = new() {
        Width = 150,
        Minimum = 0,
        Maximum = decimal.MaxValue,
        Increment = 1024 * 1024,
        ThousandsSeparator = true,
    };
    private readonly NumericUpDown _maxSf2PdtaEntriesUpDown = new() {
        Width = 150,
        Minimum = 0,
        Maximum = uint.MaxValue,
        ThousandsSeparator = true,
    };
    private readonly NumericUpDown _maxDlsPoolTableEntriesUpDown = new() {
        Width = 150,
        Minimum = 0,
        Maximum = uint.MaxValue,
        ThousandsSeparator = true,
    };
    private readonly CheckBox _sf2ZeroLengthLoopRetriggerCheckBox = new() {
        AutoSize = true,
        Text = "SF2 zero-length loop retrigger",
        Checked = true,
    };
    private readonly CheckBox _enableSf2SamplePitchCorrectionCheckBox = new() {
        AutoSize = true,
        Text = "Enable SF2 sample pitch correction",
    };
    private readonly CheckBox _multiplySf2MidiEffectsSendsCheckBox = new() {
        AutoSize = true,
        Text = "Multiply SF2 MIDI effects sends",
    };
    private readonly CheckBox _applySf2ChannelDefaultModulatorsCheckBox = new() {
        AutoSize = true,
        Text = "Apply SF2 channel default modulators",
    };
    private readonly DataGridView _channelGrid = new() { Dock = DockStyle.Fill };
    private readonly System.Windows.Forms.Timer _uiTimer = new() { Interval = 50 };
    private readonly BindingList<ChannelRow> _channels = new();
    private readonly OpenFileDialog _midiDialog = new() { Filter = "MIDI files (*.mid;*.midi)|*.mid;*.midi|All files (*.*)|*.*" };
    private readonly OpenFileDialog _soundFontDialog = new() { Filter = "Sound banks (*.sf2;*.dls)|*.sf2;*.dls|SoundFont (*.sf2)|*.sf2|DLS (*.dls)|*.dls|All files (*.*)|*.*" };
    private readonly Label _keyboardLabel = new() { AutoSize = true, Text = "Keyboard: Ch 1" };
    private readonly PianoKeyboardControl _keyboard = new() { Dock = DockStyle.Fill, Height = 120, MinimumSize = new Size(0, 120) };
    private readonly ToolTip _optionToolTip = new() {
        AutoPopDelay = 20000,
        InitialDelay = 300,
        ReshowDelay = 150,
        ShowAlways = true,
    };

    private WaveOutPlayer? _player;
    private bool _suppressMaskEvents;
    private bool _suppressSeekEvents;
    private bool _seekDragActive;
    private bool _seekRestartInFlight;

    public MainForm()
    {
        Text = "X-Ark MIDI GUI Player";
        MinimumSize = new Size(980, 620);
        StartPosition = FormStartPosition.CenterScreen;

        for (int i = 0; i < ChannelCount; ++i) {
            _channels.Add(new ChannelRow {
                Channel = i + 1,
                On = true,
                Solo = false,
                ProgramNumber = 1,
                ProgramName = ProgramNameFor(i, 0),
                ActiveNotes = 0,
                Lamp = string.Empty,
            });
        }

        BuildLayout();
        WireEvents();
        _uiTimer.Start();
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        _uiTimer.Stop();
        StopPlayback();
        base.OnFormClosing(e);
    }

    private void BuildLayout()
    {
        var root = new TableLayoutPanel {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 7,
            Padding = new Padding(12),
        };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 120f));

        var filePanel = new TableLayoutPanel {
            AutoSize = true,
            ColumnCount = 3,
            RowCount = 2,
            Dock = DockStyle.Top,
        };
        filePanel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        filePanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
        filePanel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        filePanel.Controls.Add(new Label { AutoSize = true, Text = "MIDI", Anchor = AnchorStyles.Left }, 0, 0);
        filePanel.Controls.Add(_midiPathTextBox, 1, 0);
        filePanel.Controls.Add(_browseMidiButton, 2, 0);
        filePanel.Controls.Add(new Label { AutoSize = true, Text = "Bank", Anchor = AnchorStyles.Left }, 0, 1);
        filePanel.Controls.Add(_soundFontPathTextBox, 1, 1);
        filePanel.Controls.Add(_browseSoundFontButton, 2, 1);

        var controlPanel = new FlowLayoutPanel {
            AutoSize = true,
            Dock = DockStyle.Top,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false,
        };
        controlPanel.Controls.Add(_playButton);
        controlPanel.Controls.Add(_stopButton);
        controlPanel.Controls.Add(new Label { AutoSize = true, Width = 20 });
        controlPanel.Controls.Add(_statusLabel);

        var seekPanel = new TableLayoutPanel {
            AutoSize = true,
            Dock = DockStyle.Top,
            ColumnCount = 2,
        };
        seekPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
        seekPanel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        seekPanel.Controls.Add(_seekTrackBar, 0, 0);
        seekPanel.Controls.Add(_timeLabel, 1, 0);

        ConfigureCreateOptionsPanel();
        ConfigureGrid();

        root.Controls.Add(filePanel, 0, 0);
        root.Controls.Add(controlPanel, 0, 1);
        root.Controls.Add(seekPanel, 0, 2);
        root.Controls.Add(_createOptionsGroup, 0, 3);
        root.Controls.Add(_channelGrid, 0, 4);
        root.Controls.Add(_keyboardLabel, 0, 5);
        root.Controls.Add(_keyboard, 0, 6);
        Controls.Add(root);
    }

    private void ConfigureCreateOptionsPanel()
    {
        var layout = new TableLayoutPanel {
            AutoSize = true,
            Dock = DockStyle.Top,
            ColumnCount = 4,
            RowCount = 4,
            Padding = new Padding(8),
        };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));

        layout.Controls.Add(new Label { AutoSize = true, Text = "Max sample bytes", Anchor = AnchorStyles.Left }, 0, 0);
        layout.Controls.Add(_maxSampleDataBytesUpDown, 1, 0);
        layout.Controls.Add(new Label { AutoSize = true, Text = "0 = default", Anchor = AnchorStyles.Left }, 2, 0);

        layout.Controls.Add(new Label { AutoSize = true, Text = "Max SF2 pdta entries", Anchor = AnchorStyles.Left }, 0, 1);
        layout.Controls.Add(_maxSf2PdtaEntriesUpDown, 1, 1);
        layout.Controls.Add(new Label { AutoSize = true, Text = "0 = default", Anchor = AnchorStyles.Left }, 2, 1);

        layout.Controls.Add(new Label { AutoSize = true, Text = "Max DLS pool entries", Anchor = AnchorStyles.Left }, 0, 2);
        layout.Controls.Add(_maxDlsPoolTableEntriesUpDown, 1, 2);
        layout.Controls.Add(new Label { AutoSize = true, Text = "0 = default", Anchor = AnchorStyles.Left }, 2, 2);

        var flagsPanel = new FlowLayoutPanel {
            AutoSize = true,
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = true,
            Margin = new Padding(0, 6, 0, 0),
        };
        flagsPanel.Controls.Add(_sf2ZeroLengthLoopRetriggerCheckBox);
        flagsPanel.Controls.Add(_enableSf2SamplePitchCorrectionCheckBox);
        flagsPanel.Controls.Add(_multiplySf2MidiEffectsSendsCheckBox);
        flagsPanel.Controls.Add(_applySf2ChannelDefaultModulatorsCheckBox);

        layout.Controls.Add(new Label { AutoSize = true, Text = "Compatibility", Anchor = AnchorStyles.Left }, 0, 3);
        layout.Controls.Add(flagsPanel, 1, 3);
        layout.SetColumnSpan(flagsPanel, 3);

        _createOptionsGroup.Controls.Add(layout);
        ConfigureCreateOptionToolTips();
        UpdateCreateOptionsEnabledState();
    }

    private void ConfigureCreateOptionToolTips()
    {
        _optionToolTip.SetToolTip(_maxSampleDataBytesUpDown,
            "読み込む音色バンクのデコード済みサンプル総量の上限です。0 の場合はエンジン既定値を使います。");
        _optionToolTip.SetToolTip(_maxSf2PdtaEntriesUpDown,
            "SF2 の pdta エントリ数の上限です。異常に大きい SF2 を制限したい場合に使います。0 の場合は既定値です。");
        _optionToolTip.SetToolTip(_maxDlsPoolTableEntriesUpDown,
            "DLS の pool table エントリ数の上限です。0 の場合はエンジン既定値を使います。");
        _optionToolTip.SetToolTip(_sf2ZeroLengthLoopRetriggerCheckBox,
            "長さ 0 の SF2 ループを一部互換実装のように再トリガーします。古い音源向けの互換動作です。");
        _optionToolTip.SetToolTip(_enableSf2SamplePitchCorrectionCheckBox,
            "SF2 サンプルに含まれる pitch correction を反映します。音程がずれて聞こえるバンク向けの補正です。");
        _optionToolTip.SetToolTip(_multiplySf2MidiEffectsSendsCheckBox,
            "既定の SF2 modulator 駆動ではなく、SF2 send と MIDI チャンネル send を乗算してエフェクト送信量を決めます。旧互換向けです。");
        _optionToolTip.SetToolTip(_applySf2ChannelDefaultModulatorsCheckBox,
            "CC7、CC10、CC11 の SF2 暗黙 default modulator を有効にし、グローバルチャンネル処理の代わりに SF2 寄りの挙動を使います。");
    }

    private void ConfigureGrid()
    {
        _channelGrid.AutoGenerateColumns = false;
        _channelGrid.AllowUserToAddRows = false;
        _channelGrid.AllowUserToDeleteRows = false;
        _channelGrid.AllowUserToResizeRows = false;
        _channelGrid.MultiSelect = false;
        _channelGrid.RowHeadersVisible = false;
        _channelGrid.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
        _channelGrid.DataSource = _channels;

        _channelGrid.Columns.Add(new DataGridViewTextBoxColumn {
            DataPropertyName = nameof(ChannelRow.Channel),
            HeaderText = "Ch",
            Width = 50,
            ReadOnly = true,
        });
        _channelGrid.Columns.Add(new DataGridViewCheckBoxColumn {
            DataPropertyName = nameof(ChannelRow.On),
            HeaderText = "On",
            Width = 50,
        });
        _channelGrid.Columns.Add(new DataGridViewCheckBoxColumn {
            DataPropertyName = nameof(ChannelRow.Solo),
            HeaderText = "Solo",
            Width = 55,
        });
        _channelGrid.Columns.Add(new DataGridViewTextBoxColumn {
            DataPropertyName = nameof(ChannelRow.ProgramNumber),
            HeaderText = "Prog",
            Width = 60,
            ReadOnly = true,
        });
        _channelGrid.Columns.Add(new DataGridViewTextBoxColumn {
            DataPropertyName = nameof(ChannelRow.ProgramName),
            HeaderText = "Program Name",
            Width = 360,
            ReadOnly = true,
        });
        _channelGrid.Columns.Add(new DataGridViewTextBoxColumn {
            DataPropertyName = nameof(ChannelRow.ActiveNotes),
            HeaderText = "Notes",
            Width = 60,
            ReadOnly = true,
        });
        _channelGrid.Columns.Add(new DataGridViewTextBoxColumn {
            DataPropertyName = nameof(ChannelRow.Lamp),
            HeaderText = "NoteOn",
            Width = 80,
            ReadOnly = true,
        });
    }

    private void WireEvents()
    {
        _browseMidiButton.Click += (_, _) => BrowseFile(_midiDialog, _midiPathTextBox);
        _browseSoundFontButton.Click += (_, _) => BrowseFile(_soundFontDialog, _soundFontPathTextBox);
        _playButton.Click += async (_, _) => await StartPlaybackAsync();
        _stopButton.Click += (_, _) => StopPlayback();
        _uiTimer.Tick += (_, _) => RefreshUiState();
        _seekTrackBar.Scroll += (_, _) => RefreshSeekUi();
        _seekTrackBar.MouseDown += (_, _) => _seekDragActive = true;
        _seekTrackBar.MouseUp += async (_, _) => {
            _seekDragActive = false;
            await CommitSeekAsync();
        };
        _seekTrackBar.KeyUp += async (_, e) => {
            if (e.KeyCode is Keys.Left or Keys.Right or Keys.Home or Keys.End or Keys.PageDown or Keys.PageUp) {
                await CommitSeekAsync();
            }
        };
        _channelGrid.CurrentCellDirtyStateChanged += (_, _) => {
            if (_channelGrid.IsCurrentCellDirty) {
                _channelGrid.CommitEdit(DataGridViewDataErrorContexts.Commit);
            }
        };
        _channelGrid.CellValueChanged += (_, e) => {
            if (_suppressMaskEvents || e.RowIndex < 0) {
                return;
            }
            if (e.ColumnIndex == 1 || e.ColumnIndex == 2) {
                ApplyMasksToPlayer();
            }
        };
        _channelGrid.SelectionChanged += (_, _) => UpdateKeyboardLabel();
    }

    private void BrowseFile(OpenFileDialog dialog, TextBox textBox)
    {
        if (!string.IsNullOrWhiteSpace(textBox.Text)) {
            try {
                dialog.InitialDirectory = Path.GetDirectoryName(textBox.Text) ?? string.Empty;
            } catch {
            }
        }
        if (dialog.ShowDialog(this) == DialogResult.OK) {
            textBox.Text = dialog.FileName;
        }
    }

    private async Task StartPlaybackAsync(double startPositionSeconds = 0.0)
    {
        if (_player is not null) {
            return;
        }
        if (!File.Exists(_midiPathTextBox.Text)) {
            MessageBox.Show(this, "MIDI file not found.", "X-Ark MIDI GUI Player", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        if (!File.Exists(_soundFontPathTextBox.Text)) {
            MessageBox.Show(this, "Sound bank file not found.", "X-Ark MIDI GUI Player", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        try {
            var player = new WaveOutPlayer(_midiPathTextBox.Text, _soundFontPathTextBox.Text, CreatePlayerOptions(), startPositionSeconds);
            player.PlaybackStopped += OnPlaybackStopped;
            _player = player;
            ApplyMasksToPlayer();
            _playButton.Enabled = false;
            _stopButton.Enabled = true;
            _statusLabel.Text = startPositionSeconds > 0.0 ? "Seeking" : "Playing";
            UpdateCreateOptionsEnabledState();
            await player.StartAsync();
            RefreshSeekUi();
        } catch (Exception ex) {
            StopPlayback();
            MessageBox.Show(this, ex.Message, "X-Ark MIDI GUI Player", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void StopPlayback()
    {
        if (_player is null) {
            _playButton.Enabled = true;
            _stopButton.Enabled = false;
            _statusLabel.Text = "Idle";
            UpdateCreateOptionsEnabledState();
            return;
        }
        var player = _player;
        _player = null;
        player.PlaybackStopped -= OnPlaybackStopped;
        player.Dispose();
        var playbackException = player.ConsumePlaybackException();
        _playButton.Enabled = true;
        _stopButton.Enabled = false;
        _statusLabel.Text = playbackException is null ? "Stopped" : "Error";
        UpdateCreateOptionsEnabledState();
        RefreshUiState();
        if (playbackException is not null && !IsDisposed) {
            MessageBox.Show(this, playbackException.Message, "X-Ark MIDI GUI Player", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void OnPlaybackStopped(object? sender, EventArgs e)
    {
        if (InvokeRequired) {
            BeginInvoke(new Action(() => OnPlaybackStopped(sender, e)));
            return;
        }
        StopPlayback();
    }

    private void ApplyMasksToPlayer()
    {
        if (_player is null) {
            return;
        }

        uint muteMask = 0;
        uint soloMask = 0;
        for (int i = 0; i < ChannelCount; ++i) {
            if (!_channels[i].On) {
                muteMask |= 1u << i;
            }
            if (_channels[i].Solo) {
                soloMask |= 1u << i;
            }
        }
        _player.SetChannelMasks(muteMask, soloMask);
    }

    private void RefreshUiState()
    {
        if (_player is null) {
            for (int i = 0; i < ChannelCount; ++i) {
                _channels[i].ActiveNotes = 0;
                _channels[i].Lamp = string.Empty;
            }
            _keyboard.ActiveKeyMasks = new uint[KeyMaskWordCount];
            _keyboard.ClearTransientEvents();
            UpdateLampStyles();
            UpdateKeyboardLabel();
            RefreshSeekUi();
            return;
        }

        var snapshot = _player.GetChannelSnapshot();
        var channelEvents = _player.PopChannelKeyEvents();
        _suppressMaskEvents = true;
        try {
            for (int i = 0; i < ChannelCount; ++i) {
                var row = _channels[i];
                row.ProgramNumber = snapshot.Programs[i] + 1;
                row.ProgramName = ProgramNameFor(i, snapshot.Programs[i]);
                row.ActiveNotes = (int)snapshot.ActiveNotes[i];
                row.Lamp = snapshot.ActiveNotes[i] > 0 ? "ON" : string.Empty;
                row.On = (snapshot.MuteMask & (1u << i)) == 0;
                row.Solo = (snapshot.SoloMask & (1u << i)) != 0;
            }
        } finally {
            _suppressMaskEvents = false;
        }
        var selectedChannel = SelectedChannelIndex();
        _keyboard.ActiveKeyMasks = snapshot.ActiveKeyMasks[selectedChannel];
        _keyboard.ApplyChannelEvents(selectedChannel, channelEvents);
        UpdateLampStyles();
        UpdateKeyboardLabel();
        _statusLabel.Text = _player.IsFinished ? "Finished" : "Playing";
        RefreshSeekUi();
    }

    private async Task CommitSeekAsync()
    {
        if (_suppressSeekEvents || _seekRestartInFlight) {
            return;
        }
        var player = _player;
        if (player is null) {
            RefreshSeekUi();
            return;
        }

        var targetSeconds = TrackBarValueToSeconds(_seekTrackBar.Value);
        var currentSeconds = player.CurrentPositionSeconds;
        if (Math.Abs(targetSeconds - currentSeconds) < 0.15) {
            RefreshSeekUi();
            return;
        }

        _seekRestartInFlight = true;
        try {
            _statusLabel.Text = "Seeking";
            StopPlayback();
            await StartPlaybackAsync(targetSeconds);
        } finally {
            _seekRestartInFlight = false;
            RefreshSeekUi();
        }
    }

    private int SelectedChannelIndex()
    {
        if (_channelGrid.CurrentRow?.Index is int rowIndex &&
            rowIndex >= 0 && rowIndex < ChannelCount) {
            return rowIndex;
        }
        return 0;
    }

    private void UpdateKeyboardLabel()
    {
        var channelIndex = SelectedChannelIndex();
        _keyboardLabel.Text = $"Keyboard: Ch {channelIndex + 1}";
    }

    private void UpdateLampStyles()
    {
        for (int rowIndex = 0; rowIndex < _channelGrid.Rows.Count; ++rowIndex) {
            var row = _channelGrid.Rows[rowIndex];
            var lampCell = row.Cells[6];
            var isOn = _channels[rowIndex].ActiveNotes > 0;
            lampCell.Style.BackColor = isOn ? Color.FromArgb(216, 255, 216) : Color.FromArgb(245, 245, 245);
            lampCell.Style.ForeColor = isOn ? Color.FromArgb(0, 96, 32) : Color.FromArgb(128, 128, 128);
        }
    }

    private XArkMidiEngine.CreateOptions CreatePlayerOptions()
    {
        var options = XArkMidiEngine.CreateOptions.Default();
        options.MaxSampleDataBytes = DecimalToUInt64(_maxSampleDataBytesUpDown.Value);
        options.MaxSf2PdtaEntries = DecimalToUInt32(_maxSf2PdtaEntriesUpDown.Value);
        options.MaxDlsPoolTableEntries = DecimalToUInt32(_maxDlsPoolTableEntriesUpDown.Value);

        XArkMidiEngine.CompatibilityFlags flags = XArkMidiEngine.CompatibilityFlags.None;
        if (_sf2ZeroLengthLoopRetriggerCheckBox.Checked) {
            flags |= XArkMidiEngine.CompatibilityFlags.Sf2ZeroLengthLoopRetrigger;
        }
        if (_enableSf2SamplePitchCorrectionCheckBox.Checked) {
            flags |= XArkMidiEngine.CompatibilityFlags.EnableSf2SamplePitchCorrection;
        }
        if (_multiplySf2MidiEffectsSendsCheckBox.Checked) {
            flags |= XArkMidiEngine.CompatibilityFlags.MultiplySf2MidiEffectsSends;
        }
        if (_applySf2ChannelDefaultModulatorsCheckBox.Checked) {
            flags |= XArkMidiEngine.CompatibilityFlags.ApplySf2ChannelDefaultModulators;
        }
        options.CompatibilityFlags = flags;
        return options;
    }

    private void UpdateCreateOptionsEnabledState()
    {
        _createOptionsGroup.Enabled = _player is null;
    }

    private void RefreshSeekUi()
    {
        var player = _player;
        if (player is null) {
            _suppressSeekEvents = true;
            try {
                _seekTrackBar.Enabled = false;
                _seekTrackBar.Minimum = 0;
                _seekTrackBar.Maximum = 1;
                _seekTrackBar.Value = 0;
            } finally {
                _suppressSeekEvents = false;
            }
            _timeLabel.Text = "00:00 / 00:00";
            return;
        }

        var totalSeconds = Math.Max(0.0, player.TotalDurationSeconds);
        var currentSeconds = Math.Max(0.0, player.CurrentPositionSeconds);
        var displaySeconds = (_seekDragActive || _seekRestartInFlight)
            ? TrackBarValueToSeconds(_seekTrackBar.Value)
            : currentSeconds;
        var maximum = Math.Max(1, SecondsToTrackBarValue(totalSeconds));
        var desiredValue = Math.Clamp(SecondsToTrackBarValue(currentSeconds), 0, maximum);

        _suppressSeekEvents = true;
        try {
            _seekTrackBar.Enabled = totalSeconds > 0.0 && !_seekRestartInFlight;
            if (_seekTrackBar.Maximum != maximum) {
                _seekTrackBar.Maximum = maximum;
            }
            if (!_seekDragActive && _seekTrackBar.Value != desiredValue) {
                _seekTrackBar.Value = desiredValue;
            }
        } finally {
            _suppressSeekEvents = false;
        }

        _timeLabel.Text = $"{FormatPlaybackTime(displaySeconds)} / {FormatPlaybackTime(totalSeconds)}";
    }

    private static int SecondsToTrackBarValue(double seconds)
    {
        if (!double.IsFinite(seconds) || seconds <= 0.0) {
            return 0;
        }
        return (int)Math.Clamp(Math.Round(seconds * 1000.0), 0.0, int.MaxValue);
    }

    private static double TrackBarValueToSeconds(int value)
    {
        return value / 1000.0;
    }

    private static string FormatPlaybackTime(double seconds)
    {
        if (!double.IsFinite(seconds) || seconds < 0.0) {
            seconds = 0.0;
        }
        var time = TimeSpan.FromSeconds(seconds);
        return time.TotalHours >= 1.0
            ? $"{(int)time.TotalHours:00}:{time.Minutes:00}:{time.Seconds:00}"
            : $"{time.Minutes:00}:{time.Seconds:00}";
    }

    private static uint DecimalToUInt32(decimal value)
    {
        return decimal.ToUInt32(decimal.Truncate(value));
    }

    private static ulong DecimalToUInt64(decimal value)
    {
        return decimal.ToUInt64(decimal.Truncate(value));
    }

    private static string ProgramNameFor(int channelIndex, int zeroBasedProgram)
    {
        if (channelIndex == 9) {
            return "Drum Kit";
        }
        if (zeroBasedProgram < 0 || zeroBasedProgram >= GmProgramNames.Length) {
            return string.Empty;
        }
        return GmProgramNames[zeroBasedProgram];
    }

    private static readonly string[] GmProgramNames = {
        "Acoustic Grand Piano","Bright Acoustic Piano","Electric Grand Piano","Honky-tonk Piano",
        "Electric Piano 1","Electric Piano 2","Harpsichord","Clavi",
        "Celesta","Glockenspiel","Music Box","Vibraphone",
        "Marimba","Xylophone","Tubular Bells","Dulcimer",
        "Drawbar Organ","Percussive Organ","Rock Organ","Church Organ",
        "Reed Organ","Accordion","Harmonica","Tango Accordion",
        "Acoustic Guitar (nylon)","Acoustic Guitar (steel)","Electric Guitar (jazz)","Electric Guitar (clean)",
        "Electric Guitar (muted)","Overdriven Guitar","Distortion Guitar","Guitar Harmonics",
        "Acoustic Bass","Electric Bass (finger)","Electric Bass (pick)","Fretless Bass",
        "Slap Bass 1","Slap Bass 2","Synth Bass 1","Synth Bass 2",
        "Violin","Viola","Cello","Contrabass",
        "Tremolo Strings","Pizzicato Strings","Orchestral Harp","Timpani",
        "String Ensemble 1","String Ensemble 2","SynthStrings 1","SynthStrings 2",
        "Choir Aahs","Voice Oohs","Synth Voice","Orchestra Hit",
        "Trumpet","Trombone","Tuba","Muted Trumpet",
        "French Horn","Brass Section","SynthBrass 1","SynthBrass 2",
        "Soprano Sax","Alto Sax","Tenor Sax","Baritone Sax",
        "Oboe","English Horn","Bassoon","Clarinet",
        "Piccolo","Flute","Recorder","Pan Flute",
        "Blown Bottle","Shakuhachi","Whistle","Ocarina",
        "Lead 1 (square)","Lead 2 (sawtooth)","Lead 3 (calliope)","Lead 4 (chiff)",
        "Lead 5 (charang)","Lead 6 (voice)","Lead 7 (fifths)","Lead 8 (bass + lead)",
        "Pad 1 (new age)","Pad 2 (warm)","Pad 3 (polysynth)","Pad 4 (choir)",
        "Pad 5 (bowed)","Pad 6 (metallic)","Pad 7 (halo)","Pad 8 (sweep)",
        "FX 1 (rain)","FX 2 (soundtrack)","FX 3 (crystal)","FX 4 (atmosphere)",
        "FX 5 (brightness)","FX 6 (goblins)","FX 7 (echoes)","FX 8 (sci-fi)",
        "Sitar","Banjo","Shamisen","Koto",
        "Kalimba","Bag pipe","Fiddle","Shanai",
        "Tinkle Bell","Agogo","Steel Drums","Woodblock",
        "Taiko Drum","Melodic Tom","Synth Drum","Reverse Cymbal",
        "Guitar Fret Noise","Breath Noise","Seashore","Bird Tweet",
        "Telephone Ring","Helicopter","Applause","Gunshot"
    };
}

public sealed class ChannelRow : INotifyPropertyChanged
{
    private int _channel;
    private bool _on;
    private bool _solo;
    private int _programNumber;
    private string _programName = string.Empty;
    private int _activeNotes;
    private string _lamp = string.Empty;

    public int Channel { get => _channel; set => SetField(ref _channel, value, nameof(Channel)); }
    public bool On { get => _on; set => SetField(ref _on, value, nameof(On)); }
    public bool Solo { get => _solo; set => SetField(ref _solo, value, nameof(Solo)); }
    public int ProgramNumber { get => _programNumber; set => SetField(ref _programNumber, value, nameof(ProgramNumber)); }
    public string ProgramName { get => _programName; set => SetField(ref _programName, value, nameof(ProgramName)); }
    public int ActiveNotes { get => _activeNotes; set => SetField(ref _activeNotes, value, nameof(ActiveNotes)); }
    public string Lamp { get => _lamp; set => SetField(ref _lamp, value, nameof(Lamp)); }

    public event PropertyChangedEventHandler? PropertyChanged;

    private void SetField<T>(ref T field, T value, string propertyName)
    {
        if (EqualityComparer<T>.Default.Equals(field, value)) {
            return;
        }
        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}

public sealed class WaveOutPlayer : IDisposable
{
    private const int ChannelCount = 16;
    private const int KeyMaskWordCount = 4;
    private const int SampleRate = 44100;
    private const int NumChannels = 2;
    private const int FramesPerBuffer = 2048;
    private const int BufferCount = 4;

    private readonly string _midiPath;
    private readonly string _soundFontPath;
    private readonly XArkMidiEngine.CreateOptions _createOptions;
    private readonly ulong _startFramePosition;
    private readonly List<WaveBuffer> _buffers = new();
    private readonly object _engineLock = new();
    private XArkMidiEngine.Engine? _engine;
    private IntPtr _waveOut = IntPtr.Zero;
    private CancellationTokenSource? _cts;
    private Task? _playTask;
    private uint _pendingMuteMask;
    private uint _pendingSoloMask;
    private int _pendingMaskDirty;
    private Exception? _playbackException;
    private WavDumpWriter? _dumpWriter;
    private ulong _lengthFramesEstimate;

    public event EventHandler? PlaybackStopped;

    public WaveOutPlayer(string midiPath, string soundFontPath, XArkMidiEngine.CreateOptions createOptions, double startPositionSeconds = 0.0)
    {
        _midiPath = midiPath;
        _soundFontPath = soundFontPath;
        _createOptions = createOptions;
        _startFramePosition = startPositionSeconds <= 0.0
            ? 0
            : (ulong)Math.Round(startPositionSeconds * SampleRate);
    }

    public bool IsFinished => _engine?.IsFinished ?? true;
    public double CurrentPositionSeconds
    {
        get {
            lock (_engineLock) {
                return _engine is null ? 0.0 : _engine.CurrentFramePosition / (double)SampleRate;
            }
        }
    }
    public double TotalDurationSeconds => _lengthFramesEstimate / (double)SampleRate;

    public async Task StartAsync()
    {
        if (_playTask is not null) {
            await _playTask;
            return;
        }

        try {
            _engine = await Task.Run(CreateEngineAtPosition);

            var dumpPath = Environment.GetEnvironmentVariable("XARKMIDI_DUMP_WAV");
            if (!string.IsNullOrWhiteSpace(dumpPath)) {
                _dumpWriter = new WavDumpWriter(dumpPath, sampleRate: SampleRate, channels: (ushort)NumChannels, bitsPerSample: 16);
            }

            var format = new WaveFormatEx {
                wFormatTag = 1,
                nChannels = NumChannels,
                nSamplesPerSec = SampleRate,
                wBitsPerSample = 16,
                nBlockAlign = (ushort)(NumChannels * sizeof(short)),
                nAvgBytesPerSec = SampleRate * NumChannels * sizeof(short),
                cbSize = 0,
            };

            var result = NativeMethods.waveOutOpen(out _waveOut, unchecked((uint)-1), ref format, IntPtr.Zero, IntPtr.Zero, 0);
            if (result != 0) {
                throw new InvalidOperationException($"waveOutOpen failed: {result}");
            }

            for (int i = 0; i < BufferCount; ++i) {
                var buffer = new WaveBuffer(FramesPerBuffer * NumChannels);
                result = NativeMethods.waveOutPrepareHeader(_waveOut, buffer.HeaderPointer, Marshal.SizeOf<WaveHeader>());
                if (result != 0) {
                    buffer.Dispose();
                    throw new InvalidOperationException($"waveOutPrepareHeader failed: {result}");
                }
                _buffers.Add(buffer);
            }

            _cts = new CancellationTokenSource();
            _playTask = Task.Run(() => PlaybackLoop(_cts.Token));
        } catch {
            Dispose();
            throw;
        }
    }

    public void SetChannelMasks(uint muteMask, uint soloMask)
    {
        _pendingMuteMask = muteMask;
        _pendingSoloMask = soloMask;
        Interlocked.Exchange(ref _pendingMaskDirty, 1);
    }

    public ChannelSnapshot GetChannelSnapshot()
    {
        lock (_engineLock) {
            if (_engine is null) {
                return ChannelSnapshot.Empty;
            }
            var programs = new int[ChannelCount];
            var activeNotes = new uint[ChannelCount];
            var activeKeyMasks = new uint[ChannelCount][];
            for (uint ch = 0; ch < ChannelCount; ++ch) {
                programs[ch] = _engine.GetChannelProgram(ch);
                activeNotes[ch] = _engine.GetChannelActiveNoteCount(ch);
                var channelMasks = new uint[KeyMaskWordCount];
                for (uint wordIndex = 0; wordIndex < KeyMaskWordCount; ++wordIndex) {
                    channelMasks[wordIndex] = _engine.GetChannelActiveKeyMaskWord(ch, wordIndex);
                }
                activeKeyMasks[ch] = channelMasks;
            }
            return new ChannelSnapshot(
                programs,
                activeNotes,
                activeKeyMasks,
                _engine.ChannelMuteMask,
                _engine.ChannelSoloMask);
        }
    }

    public List<XArkMidiEngine.ChannelKeyEvent> PopChannelKeyEvents()
    {
        var result = new List<XArkMidiEngine.ChannelKeyEvent>();
        lock (_engineLock) {
            if (_engine is null) {
                return result;
            }
            while (_engine.TryPopChannelKeyEvent(out var channelKeyEvent)) {
                result.Add(channelKeyEvent);
            }
        }
        return result;
    }

    private void PlaybackLoop(CancellationToken cancellationToken)
    {
        try {
            bool playbackFinished = false;
            while (!cancellationToken.IsCancellationRequested) {
                int queuedCount = 0;
                foreach (var buffer in _buffers) {
                    if (buffer.IsInQueue()) {
                        ++queuedCount;
                        continue;
                    }
                    if (playbackFinished) {
                        continue;
                    }

                    uint written = 0;
                    lock (_engineLock) {
                        if (_engine is null) {
                            playbackFinished = true;
                            continue;
                        }
                        if (Interlocked.Exchange(ref _pendingMaskDirty, 0) != 0) {
                            _engine.ChannelMuteMask = _pendingMuteMask;
                            _engine.ChannelSoloMask = _pendingSoloMask;
                        }
                        if (_engine.IsFinished) {
                            playbackFinished = true;
                            continue;
                        }
                        written = _engine.Render(buffer.Samples, FramesPerBuffer);
                    }

                    if (written == 0) {
                        playbackFinished = true;
                        continue;
                    }

                    _dumpWriter?.WriteInterleavedI16(buffer.Samples, checked((int)(written * NumChannels)));

                    buffer.UpdateForWrite((uint)(written * NumChannels * sizeof(short)));
                    var result = NativeMethods.waveOutWrite(_waveOut, buffer.HeaderPointer, Marshal.SizeOf<WaveHeader>());
                    if (result != 0) {
                        throw new InvalidOperationException($"waveOutWrite failed: {result}");
                    }
                    ++queuedCount;
                }

                if (playbackFinished && queuedCount == 0) {
                    break;
                }

                Thread.Sleep(5);
            }
        } catch (Exception ex) {
            _playbackException = ex;
        } finally {
            _dumpWriter?.Dispose();
            _dumpWriter = null;
            PlaybackStopped?.Invoke(this, EventArgs.Empty);
        }
    }

    public void Dispose()
    {
        if (_cts is not null) {
            _cts.Cancel();
        }
        if (_waveOut != IntPtr.Zero) {
            NativeMethods.waveOutReset(_waveOut);
        }
        try {
            _playTask?.Wait(1000);
        } catch {
        }

        if (_waveOut != IntPtr.Zero) {
            foreach (var buffer in _buffers) {
                NativeMethods.waveOutUnprepareHeader(_waveOut, buffer.HeaderPointer, Marshal.SizeOf<WaveHeader>());
            }
            NativeMethods.waveOutClose(_waveOut);
            _waveOut = IntPtr.Zero;
        }

        foreach (var buffer in _buffers) {
            buffer.Dispose();
        }
        _buffers.Clear();
        _dumpWriter?.Dispose();
        _dumpWriter = null;
        _engine?.Dispose();
        _engine = null;
        _cts?.Dispose();
        _cts = null;
        _playTask = null;
        _pendingMuteMask = 0;
        _pendingSoloMask = 0;
        _lengthFramesEstimate = 0;
        Interlocked.Exchange(ref _pendingMaskDirty, 0);
    }

    public Exception? ConsumePlaybackException()
    {
        var ex = _playbackException;
        _playbackException = null;
        return ex;
    }

    private XArkMidiEngine.Engine CreateEngineAtPosition()
    {
        var engine = new XArkMidiEngine.Engine(_midiPath, _soundFontPath,
            DetectSoundBankKind(_soundFontPath), SampleRate, NumChannels,
            _createOptions);
        _lengthFramesEstimate = engine.LengthFramesEstimate;
        if (_startFramePosition == 0) {
            return engine;
        }

        var discardBuffer = new short[FramesPerBuffer * NumChannels];
        ulong remaining = _startFramePosition;
        while (remaining > 0 && !engine.IsFinished) {
            var requestFrames = (uint)Math.Min((ulong)FramesPerBuffer, remaining);
            var written = engine.Render(discardBuffer, requestFrames);
            if (written == 0) {
                break;
            }
            remaining -= written;
        }
        return engine;
    }

    private static XArkMidiEngine.SoundBankKind DetectSoundBankKind(string path)
    {
        var extension = Path.GetExtension(path);
        if (extension.Equals(".sf2", StringComparison.OrdinalIgnoreCase)) {
            return XArkMidiEngine.SoundBankKind.Sf2;
        }
        if (extension.Equals(".dls", StringComparison.OrdinalIgnoreCase)) {
            return XArkMidiEngine.SoundBankKind.Dls;
        }
        return XArkMidiEngine.SoundBankKind.Auto;
    }
}

internal sealed class WavDumpWriter : IDisposable
{
    private readonly FileStream _stream;
    private readonly long _riffSizePos;
    private readonly long _dataSizePos;
    private long _dataBytesWritten;
    private bool _disposed;

    public WavDumpWriter(string path, int sampleRate, ushort channels, ushort bitsPerSample)
    {
        var directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrWhiteSpace(directory)) {
            Directory.CreateDirectory(directory);
        }

        _stream = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.Read);

        WriteAscii("RIFF");
        _riffSizePos = _stream.Position;
        WriteU32LE(0); // patched on Dispose
        WriteAscii("WAVE");

        WriteAscii("fmt ");
        WriteU32LE(16);
        WriteU16LE(1); // PCM
        WriteU16LE(channels);
        WriteU32LE((uint)sampleRate);
        var blockAlign = checked((ushort)(channels * (bitsPerSample / 8)));
        var byteRate = checked((uint)(sampleRate * blockAlign));
        WriteU32LE(byteRate);
        WriteU16LE(blockAlign);
        WriteU16LE(bitsPerSample);

        WriteAscii("data");
        _dataSizePos = _stream.Position;
        WriteU32LE(0); // patched on Dispose
    }

    public void WriteInterleavedI16(short[] samples, int sampleCount)
    {
        if (_disposed) {
            return;
        }
        if (sampleCount <= 0) {
            return;
        }
        if (sampleCount > samples.Length) {
            throw new ArgumentOutOfRangeException(nameof(sampleCount));
        }

        var bytes = MemoryMarshal.AsBytes(samples.AsSpan(0, sampleCount));
        _stream.Write(bytes);
        _dataBytesWritten += bytes.Length;
    }

    public void Dispose()
    {
        if (_disposed) {
            return;
        }
        _disposed = true;

        try {
            var dataSize = _dataBytesWritten > uint.MaxValue ? uint.MaxValue : (uint)_dataBytesWritten;
            var riffSize = checked((uint)(36 + dataSize));

            _stream.Flush();
            _stream.Position = _riffSizePos;
            WriteU32LE(riffSize);
            _stream.Position = _dataSizePos;
            WriteU32LE(dataSize);
        } finally {
            _stream.Dispose();
        }
    }

    private void WriteAscii(string s)
    {
        var bytes = Encoding.ASCII.GetBytes(s);
        _stream.Write(bytes, 0, bytes.Length);
    }

    private void WriteU16LE(ushort value)
    {
        Span<byte> b = stackalloc byte[2];
        b[0] = (byte)(value & 0xFF);
        b[1] = (byte)((value >> 8) & 0xFF);
        _stream.Write(b);
    }

    private void WriteU32LE(uint value)
    {
        Span<byte> b = stackalloc byte[4];
        b[0] = (byte)(value & 0xFF);
        b[1] = (byte)((value >> 8) & 0xFF);
        b[2] = (byte)((value >> 16) & 0xFF);
        b[3] = (byte)((value >> 24) & 0xFF);
        _stream.Write(b);
    }
}

public readonly record struct ChannelSnapshot(int[] Programs, uint[] ActiveNotes, uint[][] ActiveKeyMasks, uint MuteMask, uint SoloMask)
{
    public static ChannelSnapshot Empty { get; } = new(new int[16], new uint[16], CreateEmptyKeyMasks(), 0, 0);

    private static uint[][] CreateEmptyKeyMasks()
    {
        var result = new uint[16][];
        for (int i = 0; i < result.Length; ++i) {
            result[i] = new uint[4];
        }
        return result;
    }
}

internal sealed class PianoKeyboardControl : Control
{
    private static readonly int[] WhiteKeySemitones = { 0, 2, 4, 5, 7, 9, 11 };

    private uint[] _activeKeyMasks = new uint[4];
    private readonly long[] _recentNoteOffUntilTicks = new long[128];

    public PianoKeyboardControl()
    {
        DoubleBuffered = true;
        ResizeRedraw = true;
        BackColor = Color.WhiteSmoke;
    }

    public uint[] ActiveKeyMasks
    {
        get => _activeKeyMasks;
        set
        {
            _activeKeyMasks = (value is not null && value.Length == 4) ? value : new uint[4];
            Invalidate();
        }
    }

    public void ClearTransientEvents()
    {
        Array.Clear(_recentNoteOffUntilTicks, 0, _recentNoteOffUntilTicks.Length);
        Invalidate();
    }

    public void ApplyChannelEvents(int channelIndex, IReadOnlyList<XArkMidiEngine.ChannelKeyEvent> events)
    {
        if (events.Count == 0) {
            return;
        }
        long now = Environment.TickCount64;
        const long noteOffFlashMs = 140;
        bool changed = false;
        foreach (var channelEvent in events) {
            if (channelEvent.Channel != channelIndex || channelEvent.Key >= 128) {
                continue;
            }
            if (channelEvent.IsNoteOn != 0) {
                _recentNoteOffUntilTicks[channelEvent.Key] = 0;
            } else {
                _recentNoteOffUntilTicks[channelEvent.Key] = now + noteOffFlashMs;
            }
            changed = true;
        }
        if (changed) {
            Invalidate();
        }
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);

        e.Graphics.Clear(Color.FromArgb(248, 248, 248));

        const int midiStart = 21;
        const int midiEnd = 108;
        const int totalWhiteKeys = 52;
        var whiteKeyWidth = Math.Max(8f, (float)ClientSize.Width / totalWhiteKeys);
        var whiteKeyHeight = Math.Max(60, ClientSize.Height - 1);
        var blackKeyWidth = whiteKeyWidth * 0.62f;
        var blackKeyHeight = whiteKeyHeight * 0.62f;

        var whiteRects = new Dictionary<int, RectangleF>();
        int whiteIndex = 0;
        for (int midiKey = midiStart; midiKey <= midiEnd; ++midiKey) {
            int semitone = midiKey % 12;
            if (Array.IndexOf(WhiteKeySemitones, semitone) < 0) {
                continue;
            }
            var rect = new RectangleF(whiteIndex * whiteKeyWidth, 0, whiteKeyWidth, whiteKeyHeight);
            whiteRects[midiKey] = rect;
            bool isActive = IsKeyActive(midiKey);
            bool isRecentOff = IsRecentNoteOff(midiKey);
            using var brush = new SolidBrush(
                isActive ? Color.FromArgb(167, 224, 255) :
                isRecentOff ? Color.FromArgb(255, 221, 221) :
                Color.White);
            e.Graphics.FillRectangle(brush, rect);
            e.Graphics.DrawRectangle(Pens.Gray, rect.X, rect.Y, rect.Width, rect.Height);
            ++whiteIndex;
        }

        for (int midiKey = midiStart; midiKey <= midiEnd; ++midiKey) {
            if (IsWhiteKey(midiKey)) {
                continue;
            }
            int leftWhiteKey = midiKey - 1;
            while (leftWhiteKey >= midiStart && !IsWhiteKey(leftWhiteKey)) {
                --leftWhiteKey;
            }
            int rightWhiteKey = midiKey + 1;
            while (rightWhiteKey <= midiEnd && !IsWhiteKey(rightWhiteKey)) {
                ++rightWhiteKey;
            }
            if (!whiteRects.TryGetValue(leftWhiteKey, out var leftRect)) {
                continue;
            }
            float x;
            if (whiteRects.TryGetValue(rightWhiteKey, out var rightRect)) {
                x = ((leftRect.Right + rightRect.Left) * 0.5f) - blackKeyWidth * 0.5f;
            } else {
                x = leftRect.Right - blackKeyWidth * 0.5f;
            }
            var rect = new RectangleF(x, 0, blackKeyWidth, blackKeyHeight);
            bool isActive = IsKeyActive(midiKey);
            bool isRecentOff = IsRecentNoteOff(midiKey);
            using var brush = new SolidBrush(
                isActive ? Color.FromArgb(72, 160, 220) :
                isRecentOff ? Color.FromArgb(170, 72, 72) :
                Color.FromArgb(32, 32, 32));
            e.Graphics.FillRectangle(brush, rect);
            e.Graphics.DrawRectangle(Pens.Black, rect.X, rect.Y, rect.Width, rect.Height);
        }
    }

    private bool IsKeyActive(int midiKey)
    {
        if (midiKey < 0 || midiKey >= 128) {
            return false;
        }
        int wordIndex = midiKey >> 5;
        uint bitMask = 1u << (midiKey & 31);
        return (_activeKeyMasks[wordIndex] & bitMask) != 0;
    }

    private bool IsRecentNoteOff(int midiKey)
    {
        return midiKey >= 0 &&
               midiKey < _recentNoteOffUntilTicks.Length &&
               _recentNoteOffUntilTicks[midiKey] > Environment.TickCount64;
    }

    private static bool IsWhiteKey(int midiKey)
    {
        return Array.IndexOf(WhiteKeySemitones, midiKey % 12) >= 0;
    }
}

internal sealed class WaveBuffer : IDisposable
{
    private GCHandle _sampleHandle;
    private readonly int _headerSize = Marshal.SizeOf<WaveHeader>();
    private static readonly int DwBufferLengthOffset = checked((int)Marshal.OffsetOf<WaveHeader>(nameof(WaveHeader.dwBufferLength)));
    private static readonly int DwFlagsOffset = checked((int)Marshal.OffsetOf<WaveHeader>(nameof(WaveHeader.dwFlags)));

    public short[] Samples { get; }
    public IntPtr HeaderPointer { get; }

    public WaveBuffer(int sampleCount)
    {
        Samples = new short[sampleCount];
        _sampleHandle = GCHandle.Alloc(Samples, GCHandleType.Pinned);
        var header = new WaveHeader {
            lpData = _sampleHandle.AddrOfPinnedObject(),
            dwBufferLength = (uint)(sampleCount * sizeof(short)),
        };
        HeaderPointer = Marshal.AllocHGlobal(_headerSize);
        Marshal.StructureToPtr(header, HeaderPointer, fDeleteOld: false);
    }

    public bool IsInQueue()
    {
        return (ReadFlags() & NativeConstants.WHDR_INQUEUE) != 0;
    }

    public void UpdateForWrite(uint bufferLengthBytes)
    {
        Marshal.WriteInt32(HeaderPointer, DwBufferLengthOffset, checked((int)bufferLengthBytes));
        var flags = ReadFlags() & ~NativeConstants.WHDR_DONE;
        Marshal.WriteInt32(HeaderPointer, DwFlagsOffset, unchecked((int)flags));
    }

    private uint ReadFlags()
    {
        return unchecked((uint)Marshal.ReadInt32(HeaderPointer, DwFlagsOffset));
    }

    public void Dispose()
    {
        if (HeaderPointer != IntPtr.Zero) {
            Marshal.FreeHGlobal(HeaderPointer);
        }
        if (_sampleHandle.IsAllocated) {
            _sampleHandle.Free();
        }
    }
}

internal static class NativeConstants
{
    public const uint WHDR_DONE = 0x00000001;
    public const uint WHDR_INQUEUE = 0x00000010;
}

[StructLayout(LayoutKind.Sequential)]
internal struct WaveFormatEx
{
    public ushort wFormatTag;
    public ushort nChannels;
    public uint nSamplesPerSec;
    public uint nAvgBytesPerSec;
    public ushort nBlockAlign;
    public ushort wBitsPerSample;
    public ushort cbSize;
}

[StructLayout(LayoutKind.Sequential)]
internal struct WaveHeader
{
    public IntPtr lpData;
    public uint dwBufferLength;
    public uint dwBytesRecorded;
    public IntPtr dwUser;
    public uint dwFlags;
    public uint dwLoops;
    public IntPtr lpNext;
    public IntPtr reserved;
}

internal static class NativeMethods
{
    [DllImport("winmm.dll")]
    public static extern int waveOutOpen(
        out IntPtr hWaveOut,
        uint uDeviceID,
        ref WaveFormatEx lpFormat,
        IntPtr dwCallback,
        IntPtr dwInstance,
        uint dwFlags);

    [DllImport("winmm.dll")]
    public static extern int waveOutPrepareHeader(
        IntPtr hWaveOut,
        IntPtr lpWaveOutHdr,
        int uSize);

    [DllImport("winmm.dll")]
    public static extern int waveOutWrite(
        IntPtr hWaveOut,
        IntPtr lpWaveOutHdr,
        int uSize);

    [DllImport("winmm.dll")]
    public static extern int waveOutUnprepareHeader(
        IntPtr hWaveOut,
        IntPtr lpWaveOutHdr,
        int uSize);

    [DllImport("winmm.dll")]
    public static extern int waveOutReset(IntPtr hWaveOut);

    [DllImport("winmm.dll")]
    public static extern int waveOutClose(IntPtr hWaveOut);
}
