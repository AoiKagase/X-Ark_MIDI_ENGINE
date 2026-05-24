using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Globalization;
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
    private const decimal Sf2SendScaleDefaultPercent = 100m;
    private readonly TextBox _midiPathTextBox = new() { Dock = DockStyle.Fill };
    private readonly TextBox _soundFontPathTextBox = new() { Dock = DockStyle.Fill };
    private readonly Button _browseMidiButton = new() { Text = "Open MIDI..." };
    private readonly Button _browseSoundFontButton = new() { Text = "Open Bank..." };
    private readonly Button _playButton = new() { Text = "Play", Width = 90 };
    private readonly Button _stopButton = new() { Text = "Stop", Width = 90, Enabled = false };
    private readonly Button _exportWavButton = new() { Text = "Export WAV...", Width = 110 };
    private readonly Button _effectsOptionsButton = new() { Text = "Effects...", Width = 90 };
    private readonly CheckBox _loopEnabledCheckBox = new() { AutoSize = true, Text = "Loop" };
    private readonly NumericUpDown _loopCountUpDown = new() {
        Width = 80,
        Minimum = 0,
        Maximum = uint.MaxValue,
        ThousandsSeparator = true,
        Enabled = false,
    };
    private readonly Label _statusLabel = new() { AutoSize = true, Text = "Idle" };
    private readonly OutputStageMeterControl _outputStageMeter = new() { Dock = DockStyle.Fill, MinimumSize = new Size(340, 46), Margin = new Padding(8, 0, 0, 0) };
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
    private readonly CheckBox _useSf2SpecModulatorResolverCheckBox = new() {
        AutoSize = true,
        Text = "SF2 2.04 modulator resolver",
    };
    private readonly CheckBox _internalEffectsCheckBox = new() {
        AutoSize = true,
        Text = "Internal effects",
        Checked = true,
    };
    private readonly NumericUpDown _sf2ReverbSendScaleUpDown = new() {
        Width = 64,
        Minimum = 0,
        Maximum = 200,
        Value = Sf2SendScaleDefaultPercent,
        Increment = 5,
    };
    private readonly NumericUpDown _sf2ChorusSendScaleUpDown = new() {
        Width = 64,
        Minimum = 0,
        Maximum = 200,
        Value = Sf2SendScaleDefaultPercent,
        Increment = 5,
    };
    private readonly NumericUpDown _reverbReturnScaleUpDown = new() {
        Width = 64,
        Minimum = 0,
        Maximum = 200,
        Value = Sf2SendScaleDefaultPercent,
        Increment = 5,
    };
    private readonly NumericUpDown _chorusReturnScaleUpDown = new() {
        Width = 64,
        Minimum = 0,
        Maximum = 200,
        Value = Sf2SendScaleDefaultPercent,
        Increment = 5,
    };
    private readonly NumericUpDown _masterReverbSendScaleUpDown = new() {
        Width = 64,
        Minimum = 0,
        Maximum = 200,
        Value = Sf2SendScaleDefaultPercent,
        Increment = 5,
    };
    private readonly NumericUpDown _chorusToReverbScaleUpDown = new() {
        Width = 64,
        Minimum = 0,
        Maximum = 200,
        Value = Sf2SendScaleDefaultPercent,
        Increment = 5,
    };
    private readonly NumericUpDown _outputGainScaleUpDown = new() {
        Width = 64,
        Minimum = 0,
        Maximum = 200,
        Value = Sf2SendScaleDefaultPercent,
        Increment = 5,
    };
    private readonly ComboBox _outputStageComboBox = new() {
        DropDownStyle = ComboBoxStyle.DropDownList,
        Width = 140,
    };
    private readonly GroupBox _channelLevelGroup = new() { Dock = DockStyle.Fill, Text = "Channel Levels" };
    private readonly ChannelLevelMeterControl _channelLevelMeter = new() { Dock = DockStyle.Fill, MinimumSize = new Size(0, 72) };
    private readonly DataGridView _channelGrid = new() { Dock = DockStyle.Fill };
    private readonly System.Windows.Forms.Timer _uiTimer = new() { Interval = 50 };
    private readonly BindingList<ChannelRow> _channels = new();
    private readonly OpenFileDialog _midiDialog = new() { Filter = "MIDI files (*.mid;*.midi)|*.mid;*.midi|All files (*.*)|*.*" };
    private readonly OpenFileDialog _soundFontDialog = new() { Filter = "Sound banks (*.sf2;*.dls)|*.sf2;*.dls|SoundFont (*.sf2)|*.sf2|DLS (*.dls)|*.dls|All files (*.*)|*.*" };
    private readonly SaveFileDialog _wavSaveDialog = new() { Filter = "WAV audio (*.wav)|*.wav|All files (*.*)|*.*", DefaultExt = "wav", AddExtension = true };
    private readonly Label _keyboardLabel = new() { AutoSize = true, Text = "Keyboard: Ch 1" };
    private readonly PianoKeyboardControl _keyboard = new() { Dock = DockStyle.Fill, Height = 120, MinimumSize = new Size(0, 120) };
    private readonly ToolTip _optionToolTip = new() {
        AutoPopDelay = 20000,
        InitialDelay = 300,
        ReshowDelay = 150,
        ShowAlways = true,
    };

    private WaveOutPlayer? _player;
    private Form? _effectsDialog;
    private bool _closingEffectsDialog;
    private bool _suppressMaskEvents;
    private bool _suppressSeekEvents;
    private bool _seekDragActive;
    private bool _seekRestartInFlight;
    private bool _exportInFlight;

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
        _closingEffectsDialog = true;
        _effectsDialog?.Close();
        base.OnFormClosing(e);
    }

    private void BuildLayout()
    {
        var root = new TableLayoutPanel {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 8,
            Padding = new Padding(12),
        };
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 124f));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 120f));

        var filePanel = new TableLayoutPanel {
            AutoSize = true,
            ColumnCount = 3,
            RowCount = 2,
            Dock = DockStyle.Fill,
        };
        filePanel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        filePanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
        filePanel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        filePanel.Controls.Add(CreateFieldLabel("MIDI"), 0, 0);
        filePanel.Controls.Add(_midiPathTextBox, 1, 0);
        filePanel.Controls.Add(_browseMidiButton, 2, 0);
        filePanel.Controls.Add(CreateFieldLabel("Bank"), 0, 1);
        filePanel.Controls.Add(_soundFontPathTextBox, 1, 1);
        filePanel.Controls.Add(_browseSoundFontButton, 2, 1);

        var playbackControls = new FlowLayoutPanel {
            AutoSize = true,
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.LeftToRight,
            Padding = new Padding(0, 8, 0, 0),
            WrapContents = false,
        };
        playbackControls.Controls.Add(_playButton);
        playbackControls.Controls.Add(_stopButton);
        playbackControls.Controls.Add(_exportWavButton);
        playbackControls.Controls.Add(_effectsOptionsButton);
        playbackControls.Controls.Add(new Label { AutoSize = true, Width = 12 });
        playbackControls.Controls.Add(_loopEnabledCheckBox);
        playbackControls.Controls.Add(CreateInlineLabel("Count"));
        playbackControls.Controls.Add(_loopCountUpDown);
        playbackControls.Controls.Add(new Label { AutoSize = true, Width = 20 });
        playbackControls.Controls.Add(_statusLabel);

        var controlPanel = new TableLayoutPanel {
            AutoSize = true,
            ColumnCount = 2,
            Dock = DockStyle.Fill,
            Margin = new Padding(0, 4, 0, 2),
        };
        controlPanel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        controlPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
        controlPanel.Controls.Add(playbackControls, 0, 0);
        controlPanel.Controls.Add(_outputStageMeter, 1, 0);

        var seekPanel = new TableLayoutPanel {
            AutoSize = true,
            Dock = DockStyle.Fill,
            ColumnCount = 2,
        };
        seekPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
        seekPanel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        seekPanel.Controls.Add(_seekTrackBar, 0, 0);
        seekPanel.Controls.Add(_timeLabel, 1, 0);

        ConfigureCreateOptionsPanel();
        ConfigureGrid();
        _channelLevelGroup.Controls.Add(_channelLevelMeter);

        root.Controls.Add(filePanel, 0, 0);
        root.Controls.Add(controlPanel, 0, 1);
        root.Controls.Add(seekPanel, 0, 2);
        root.Controls.Add(_createOptionsGroup, 0, 3);
        root.Controls.Add(_channelLevelGroup, 0, 4);
        root.Controls.Add(_channelGrid, 0, 5);
        root.Controls.Add(_keyboardLabel, 0, 6);
        root.Controls.Add(_keyboard, 0, 7);
        Controls.Add(root);
    }

    private void ConfigureCreateOptionsPanel()
    {
        var layout = new TableLayoutPanel {
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            Dock = DockStyle.Top,
            ColumnCount = 7,
            RowCount = 2,
            Padding = new Padding(6, 4, 6, 5),
        };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));

        layout.Controls.Add(CreateCompactLabel("Sample bytes"), 0, 0);
        layout.Controls.Add(_maxSampleDataBytesUpDown, 1, 0);
        layout.Controls.Add(CreateCompactLabel("SF2 pdta"), 2, 0);
        layout.Controls.Add(_maxSf2PdtaEntriesUpDown, 3, 0);
        layout.Controls.Add(CreateCompactLabel("DLS pool"), 4, 0);
        layout.Controls.Add(_maxDlsPoolTableEntriesUpDown, 5, 0);

        var flagsPanel = new FlowLayoutPanel {
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = true,
            Margin = new Padding(0, 0, 0, 0),
            Padding = new Padding(0),
        };
        flagsPanel.Controls.Add(_sf2ZeroLengthLoopRetriggerCheckBox);
        flagsPanel.Controls.Add(_enableSf2SamplePitchCorrectionCheckBox);
        flagsPanel.Controls.Add(_applySf2ChannelDefaultModulatorsCheckBox);
        flagsPanel.Controls.Add(_useSf2SpecModulatorResolverCheckBox);
        flagsPanel.Controls.Add(CreateInlineLabel("Output stage"));
        flagsPanel.Controls.Add(_outputStageComboBox);

        layout.Controls.Add(CreateCompactLabel("Compatibility"), 0, 1);
        layout.Controls.Add(flagsPanel, 1, 1);
        layout.SetColumnSpan(flagsPanel, 6);

        _createOptionsGroup.Controls.Add(layout);
        ConfigureCreateOptionToolTips();
        UpdateCreateOptionsEnabledState();
    }

    private static Label CreateFieldLabel(string text)
    {
        return new Label {
            AutoSize = true,
            Text = text,
            Anchor = AnchorStyles.Left | AnchorStyles.Top,
            TextAlign = ContentAlignment.TopLeft,
            Margin = new Padding(0, 6, 8, 0),
        };
    }

    private static Label CreateHintLabel(string text)
    {
        return new Label {
            AutoSize = true,
            Text = text,
            Anchor = AnchorStyles.Left | AnchorStyles.Top,
            TextAlign = ContentAlignment.TopLeft,
            Margin = new Padding(8, 6, 8, 0),
        };
    }

    private static Label CreateInlineLabel(string text)
    {
        return new Label {
            AutoSize = true,
            Text = text,
            Anchor = AnchorStyles.Left | AnchorStyles.Top,
            TextAlign = ContentAlignment.TopLeft,
            Margin = new Padding(8, 6, 2, 0),
        };
    }

    private static Label CreateCompactLabel(string text)
    {
        return new Label {
            AutoSize = true,
            Text = text,
            Anchor = AnchorStyles.Left,
            TextAlign = ContentAlignment.MiddleLeft,
            Margin = new Padding(0, 4, 6, 0),
        };
    }

    private void ConfigureCreateOptionToolTips()
    {
        _optionToolTip.SetToolTip(_maxSampleDataBytesUpDown,
            "読み込む音色バンクのデコード済みサンプル総量の上限です。0 の場合はエンジン既定値を使います。");
        _optionToolTip.SetToolTip(_maxSf2PdtaEntriesUpDown,
            "SF2 の pdta エントリ数の上限です。異常に大きい SF2 を制限したい場合に使います。0 の場合は既定値です。");
        _optionToolTip.SetToolTip(_maxDlsPoolTableEntriesUpDown,
            "DLS の pool table エントリ数の上限です。0 の場合はエンジン既定値を使います。");
        _optionToolTip.SetToolTip(_effectsOptionsButton,
            "エフェクト関連の互換設定と SF2 send 倍率を開きます。SF2 send 倍率は再生中にも反映されます。");
        _optionToolTip.SetToolTip(_sf2ZeroLengthLoopRetriggerCheckBox,
            "長さ 0 の SF2 ループを一部互換実装のように再トリガーします。古い音源向けの互換動作です。");
        _optionToolTip.SetToolTip(_enableSf2SamplePitchCorrectionCheckBox,
            "SF2 サンプルに含まれる pitch correction を反映します。音程がずれて聞こえるバンク向けの補正です。");
        _optionToolTip.SetToolTip(_multiplySf2MidiEffectsSendsCheckBox,
            "既定の SF2 modulator 駆動ではなく、SF2 send と MIDI チャンネル send を乗算してエフェクト送信量を決めます。旧互換向けです。");
        _optionToolTip.SetToolTip(_applySf2ChannelDefaultModulatorsCheckBox,
            "旧互換経路で CC7、CC10、CC11 の SF2 暗黙 default modulator を有効にします。SF2 2.04 modulator resolver が ON の場合は resolver 側が default modulators を扱うため無効化されます。");
        _optionToolTip.SetToolTip(_useSf2SpecModulatorResolverCheckBox,
            "SoundFont 2.04 仕様寄りの modulator resolver を使います。implicit default modulators も resolver 側で扱います。停止後の次回再生から反映されます。");
        _optionToolTip.SetToolTip(_internalEffectsCheckBox,
            "合成後の内部リバーブ/コーラス処理を有効にします。OFF にすると SF2/MIDI のエフェクト send はドライ出力へ加算されません。");
        _optionToolTip.SetToolTip(_sf2ReverbSendScaleUpDown,
            "SF2 の preset/modulator 由来リバーブ send に掛ける倍率です。100 でバンク指定値、0 で SF2 リバーブ send を無効化します。再生中にも反映されます。");
        _optionToolTip.SetToolTip(_sf2ChorusSendScaleUpDown,
            "SF2 の preset/modulator 由来コーラス send に掛ける倍率です。100 でバンク指定値、0 で SF2 コーラス send を無効化します。再生中にも反映されます。");
        _optionToolTip.SetToolTip(_reverbReturnScaleUpDown,
            "内部リバーブの return 音量に掛ける倍率です。100 で既定値、0 で内部リバーブ return を無音にします。再生中にも反映されます。");
        _optionToolTip.SetToolTip(_chorusReturnScaleUpDown,
            "内部コーラスの return 音量に掛ける倍率です。100 で既定値、0 で内部コーラス return を無音にします。再生中にも反映されます。");
        _optionToolTip.SetToolTip(_masterReverbSendScaleUpDown,
            "ドライ音から内部リバーブへ送る master send の倍率です。100 で既定値、0 でドライ音由来の全体リバーブを無効化します。再生中にも反映されます。");
        _optionToolTip.SetToolTip(_chorusToReverbScaleUpDown,
            "内部コーラス return をリバーブへ送る量の倍率です。100 で既定値、0 でコーラスからリバーブへの回り込みを無効化します。再生中にも反映されます。");
        _optionToolTip.SetToolTip(_outputGainScaleUpDown,
            "最終出力ゲインに掛ける倍率です。100 で既定値、0 で無音、200 で 2 倍です。再生中にも反映されます。");
        _outputStageComboBox.Items.AddRange(new object[] { "Standard", "Natural", "Warm", "Loud" });
        _outputStageComboBox.SelectedIndex = 0;
        _optionToolTip.SetToolTip(_outputStageComboBox,
            "合成後の出力段です。Natural は控えめ、Warm は耳当たり重視、Loud は音量感寄りです。停止後の次回再生から反映されます。");
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
        _channelGrid.BackgroundColor = SystemColors.Window;
        _channelGrid.BorderStyle = BorderStyle.FixedSingle;
        _channelGrid.EnableHeadersVisualStyles = false;
        _channelGrid.GridColor = Color.FromArgb(224, 224, 224);
        _channelGrid.DefaultCellStyle.BackColor = SystemColors.Window;
        _channelGrid.DefaultCellStyle.ForeColor = SystemColors.ControlText;
        _channelGrid.DefaultCellStyle.SelectionBackColor = Color.FromArgb(210, 228, 250);
        _channelGrid.DefaultCellStyle.SelectionForeColor = SystemColors.ControlText;
        _channelGrid.AlternatingRowsDefaultCellStyle.BackColor = Color.FromArgb(248, 250, 252);
        _channelGrid.ColumnHeadersDefaultCellStyle.BackColor = Color.FromArgb(240, 240, 240);
        _channelGrid.ColumnHeadersDefaultCellStyle.ForeColor = SystemColors.ControlText;
        _channelGrid.ColumnHeadersDefaultCellStyle.SelectionBackColor = Color.FromArgb(240, 240, 240);
        _channelGrid.ColumnHeadersDefaultCellStyle.SelectionForeColor = SystemColors.ControlText;
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
            AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill,
            MinimumWidth = 260,
            ReadOnly = true,
        });
        _channelGrid.Columns.Add(new DataGridViewTextBoxColumn {
            DataPropertyName = nameof(ChannelRow.ActiveNotes),
            HeaderText = "Notes",
            Width = 60,
            ReadOnly = true,
        });
        _channelGrid.Columns.Add(new DataGridViewTextBoxColumn {
            DataPropertyName = nameof(ChannelRow.DryPeak),
            HeaderText = "Dry",
            Width = 58,
            ReadOnly = true,
        });
        _channelGrid.Columns.Add(new DataGridViewTextBoxColumn {
            DataPropertyName = nameof(ChannelRow.ReverbSendPeak),
            HeaderText = "RvbPk",
            Width = 58,
            ReadOnly = true,
        });
        _channelGrid.Columns.Add(new DataGridViewTextBoxColumn {
            DataPropertyName = nameof(ChannelRow.ChorusSendPeak),
            HeaderText = "ChoPk",
            Width = 58,
            ReadOnly = true,
        });
        _channelGrid.Columns.Add(new DataGridViewTextBoxColumn {
            DataPropertyName = nameof(ChannelRow.ControllerSummary),
            HeaderText = "Vol Pan Rev Cho",
            Width = 145,
            ReadOnly = true,
        });
        _channelGrid.Columns.Add(new DataGridViewTextBoxColumn {
            Name = nameof(ChannelRow.Lamp),
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
        _exportWavButton.Click += async (_, _) => await ExportWavAsync();
        _effectsOptionsButton.Click += (_, _) => ShowEffectsOptionsDialog();
        _loopEnabledCheckBox.CheckedChanged += (_, _) => {
            _loopCountUpDown.Enabled = _loopEnabledCheckBox.Checked;
            ApplyLoopToPlayer();
        };
        _loopCountUpDown.ValueChanged += (_, _) => ApplyLoopToPlayer();
        _sf2ReverbSendScaleUpDown.ValueChanged += (_, _) => ApplySf2SendScalesToPlayer();
        _sf2ChorusSendScaleUpDown.ValueChanged += (_, _) => ApplySf2SendScalesToPlayer();
        _useSf2SpecModulatorResolverCheckBox.CheckedChanged += (_, _) => {
            if (_useSf2SpecModulatorResolverCheckBox.Checked) {
                _applySf2ChannelDefaultModulatorsCheckBox.Checked = false;
            }
            UpdateCreateOptionsEnabledState();
        };
        _reverbReturnScaleUpDown.ValueChanged += (_, _) => ApplyEffectMixScalesToPlayer();
        _chorusReturnScaleUpDown.ValueChanged += (_, _) => ApplyEffectMixScalesToPlayer();
        _masterReverbSendScaleUpDown.ValueChanged += (_, _) => ApplyEffectMixScalesToPlayer();
        _chorusToReverbScaleUpDown.ValueChanged += (_, _) => ApplyEffectMixScalesToPlayer();
        _outputGainScaleUpDown.ValueChanged += (_, _) => ApplyOutputGainScaleToPlayer();
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

    private void ShowEffectsOptionsDialog()
    {
        if (_effectsDialog is null || _effectsDialog.IsDisposed) {
            _effectsDialog = CreateEffectsOptionsDialog();
        }

        UpdateCreateOptionsEnabledState();
        if (_effectsDialog.Visible) {
            _effectsDialog.Activate();
            return;
        }

        _effectsDialog.StartPosition = FormStartPosition.Manual;
        var location = _effectsOptionsButton.PointToScreen(new Point(0, _effectsOptionsButton.Height + 2));
        _effectsDialog.Location = location;
        _effectsDialog.Show(this);
    }

    private Form CreateEffectsOptionsDialog()
    {
        var dialog = new Form {
            Text = "Effect Options",
            FormBorderStyle = FormBorderStyle.FixedDialog,
            MaximizeBox = false,
            MinimizeBox = false,
            ShowInTaskbar = false,
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            Padding = new Padding(12),
        };

        var root = new TableLayoutPanel {
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            ColumnCount = 3,
            RowCount = 10,
            Dock = DockStyle.Fill,
        };
        root.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        root.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        root.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));

        var flagsPanel = new FlowLayoutPanel {
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            FlowDirection = FlowDirection.TopDown,
            WrapContents = false,
            Dock = DockStyle.Fill,
            Margin = new Padding(0, 0, 0, 8),
        };
        flagsPanel.Controls.Add(_internalEffectsCheckBox);
        flagsPanel.Controls.Add(_multiplySf2MidiEffectsSendsCheckBox);

        root.Controls.Add(flagsPanel, 0, 0);
        root.SetColumnSpan(flagsPanel, 3);
        root.Controls.Add(CreateCompactLabel("SF2 reverb send"), 0, 1);
        root.Controls.Add(_sf2ReverbSendScaleUpDown, 1, 1);
        root.Controls.Add(CreateCompactLabel("SF2 chorus send"), 0, 2);
        root.Controls.Add(_sf2ChorusSendScaleUpDown, 1, 2);
        root.Controls.Add(CreateHintLabel("%"), 2, 1);
        root.Controls.Add(CreateHintLabel("%"), 2, 2);
        root.Controls.Add(CreateCompactLabel("Reverb return"), 0, 3);
        root.Controls.Add(_reverbReturnScaleUpDown, 1, 3);
        root.Controls.Add(CreateHintLabel("%"), 2, 3);
        root.Controls.Add(CreateCompactLabel("Chorus return"), 0, 4);
        root.Controls.Add(_chorusReturnScaleUpDown, 1, 4);
        root.Controls.Add(CreateHintLabel("%"), 2, 4);
        root.Controls.Add(CreateCompactLabel("Master reverb send"), 0, 5);
        root.Controls.Add(_masterReverbSendScaleUpDown, 1, 5);
        root.Controls.Add(CreateHintLabel("%"), 2, 5);
        root.Controls.Add(CreateCompactLabel("Chorus to reverb"), 0, 6);
        root.Controls.Add(_chorusToReverbScaleUpDown, 1, 6);
        root.Controls.Add(CreateHintLabel("%"), 2, 6);
        root.Controls.Add(CreateCompactLabel("Output gain"), 0, 7);
        root.Controls.Add(_outputGainScaleUpDown, 1, 7);
        root.Controls.Add(CreateHintLabel("%"), 2, 7);

        var buttonPanel = new FlowLayoutPanel {
            AutoSize = true,
            FlowDirection = FlowDirection.RightToLeft,
            Dock = DockStyle.Fill,
            Margin = new Padding(0, 10, 0, 0),
        };
        var closeButton = new Button { Text = "Close", Width = 90 };
        closeButton.Click += (_, _) => dialog.Hide();
        buttonPanel.Controls.Add(closeButton);

        root.Controls.Add(buttonPanel, 0, 9);
        root.SetColumnSpan(buttonPanel, 3);
        dialog.Controls.Add(root);
        dialog.FormClosing += (_, e) => {
            if (!_closingEffectsDialog && e.CloseReason == CloseReason.UserClosing) {
                e.Cancel = true;
                dialog.Hide();
            }
        };
        return dialog;
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

    private async Task ExportWavAsync()
    {
        if (_exportInFlight) {
            return;
        }
        if (_player is not null) {
            MessageBox.Show(this, "Stop playback before exporting WAV.", "X-Ark MIDI GUI Player", MessageBoxButtons.OK, MessageBoxIcon.Information);
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
        if (_loopEnabledCheckBox.Checked && DecimalToUInt32(_loopCountUpDown.Value) == 0) {
            MessageBox.Show(this, "Infinite loop export is not available. Set a finite loop count first.",
                "X-Ark MIDI GUI Player", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        if (!string.IsNullOrWhiteSpace(_midiPathTextBox.Text)) {
            _wavSaveDialog.FileName = Path.ChangeExtension(Path.GetFileName(_midiPathTextBox.Text), ".wav");
        }
        if (_wavSaveDialog.ShowDialog(this) != DialogResult.OK) {
            return;
        }

        var midiPath = _midiPathTextBox.Text;
        var soundFontPath = _soundFontPathTextBox.Text;
        var outputPath = _wavSaveDialog.FileName;
        var createOptions = CreatePlayerOptions();
        var muteMask = BuildMuteMaskFromRows();
        var soloMask = BuildSoloMaskFromRows();
        var loopEnabled = _loopEnabledCheckBox.Checked;
        var loopCount = DecimalToUInt32(_loopCountUpDown.Value);
        var sf2ReverbSendScale = Sf2SendScaleFromPercent(_sf2ReverbSendScaleUpDown.Value);
        var sf2ChorusSendScale = Sf2SendScaleFromPercent(_sf2ChorusSendScaleUpDown.Value);
        var reverbReturnScale = EffectScaleFromPercent(_reverbReturnScaleUpDown.Value);
        var chorusReturnScale = EffectScaleFromPercent(_chorusReturnScaleUpDown.Value);
        var masterReverbSendScale = EffectScaleFromPercent(_masterReverbSendScaleUpDown.Value);
        var chorusToReverbScale = EffectScaleFromPercent(_chorusToReverbScaleUpDown.Value);
        var outputGainScale = EffectScaleFromPercent(_outputGainScaleUpDown.Value);
        var progress = new Progress<WavExportProgress>(p => {
            _statusLabel.Text = p.TotalSeconds > 0.0
                ? $"Exporting {FormatPlaybackTime(p.CurrentSeconds)} / {FormatPlaybackTime(p.TotalSeconds)}"
                : $"Exporting {FormatPlaybackTime(p.CurrentSeconds)}";
        });

        _exportInFlight = true;
        UpdateCreateOptionsEnabledState();
        _statusLabel.Text = "Exporting";
        try {
            await Task.Run(() => RenderWavFile(
                midiPath,
                soundFontPath,
                outputPath,
                createOptions,
                muteMask,
                soloMask,
                loopEnabled,
                loopCount,
                sf2ReverbSendScale,
                sf2ChorusSendScale,
                reverbReturnScale,
                chorusReturnScale,
                masterReverbSendScale,
                chorusToReverbScale,
                outputGainScale,
                progress));
            _statusLabel.Text = "Exported WAV";
            MessageBox.Show(this, "WAV export completed.", "X-Ark MIDI GUI Player", MessageBoxButtons.OK, MessageBoxIcon.Information);
        } catch (Exception ex) {
            _statusLabel.Text = "Export error";
            MessageBox.Show(this, ex.Message, "X-Ark MIDI GUI Player", MessageBoxButtons.OK, MessageBoxIcon.Error);
        } finally {
            _exportInFlight = false;
            UpdateCreateOptionsEnabledState();
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
            var player = new WaveOutPlayer(
                _midiPathTextBox.Text,
                _soundFontPathTextBox.Text,
                CreatePlayerOptions(),
                Sf2SendScaleFromPercent(_sf2ReverbSendScaleUpDown.Value),
                Sf2SendScaleFromPercent(_sf2ChorusSendScaleUpDown.Value),
                EffectScaleFromPercent(_reverbReturnScaleUpDown.Value),
                EffectScaleFromPercent(_chorusReturnScaleUpDown.Value),
                EffectScaleFromPercent(_masterReverbSendScaleUpDown.Value),
                EffectScaleFromPercent(_chorusToReverbScaleUpDown.Value),
                EffectScaleFromPercent(_outputGainScaleUpDown.Value),
                startPositionSeconds);
            player.PlaybackStopped += OnPlaybackStopped;
            _player = player;
            ApplyMasksToPlayer();
            ApplyLoopToPlayer();
            ApplySf2SendScalesToPlayer();
            ApplyEffectMixScalesToPlayer();
            ApplyOutputGainScaleToPlayer();
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
            _outputStageMeter.Clear();
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

    private uint BuildMuteMaskFromRows()
    {
        uint muteMask = 0;
        for (int i = 0; i < ChannelCount; ++i) {
            if (!_channels[i].On) {
                muteMask |= 1u << i;
            }
        }
        return muteMask;
    }

    private uint BuildSoloMaskFromRows()
    {
        uint soloMask = 0;
        for (int i = 0; i < ChannelCount; ++i) {
            if (_channels[i].Solo) {
                soloMask |= 1u << i;
            }
        }
        return soloMask;
    }

    private void ApplyLoopToPlayer()
    {
        _player?.SetLoop(_loopEnabledCheckBox.Checked, DecimalToUInt32(_loopCountUpDown.Value));
    }

    private void ApplySf2SendScalesToPlayer()
    {
        _player?.SetSf2EffectSendScale(
            Sf2SendScaleFromPercent(_sf2ReverbSendScaleUpDown.Value),
            Sf2SendScaleFromPercent(_sf2ChorusSendScaleUpDown.Value));
    }

    private void ApplyEffectMixScalesToPlayer()
    {
        _player?.SetEffectMixScale(
            EffectScaleFromPercent(_reverbReturnScaleUpDown.Value),
            EffectScaleFromPercent(_chorusReturnScaleUpDown.Value),
            EffectScaleFromPercent(_masterReverbSendScaleUpDown.Value),
            EffectScaleFromPercent(_chorusToReverbScaleUpDown.Value));
    }

    private void ApplyOutputGainScaleToPlayer()
    {
        _player?.SetOutputGainScale(EffectScaleFromPercent(_outputGainScaleUpDown.Value));
    }

    private void RefreshUiState()
    {
        if (_player is null) {
            for (int i = 0; i < ChannelCount; ++i) {
                _channels[i].ActiveNotes = 0;
                _channels[i].DryPeak = string.Empty;
                _channels[i].ReverbSendPeak = string.Empty;
                _channels[i].ChorusSendPeak = string.Empty;
                _channels[i].ControllerSummary = string.Empty;
                _channels[i].Lamp = string.Empty;
            }
            _keyboard.ActiveKeyMasks = new uint[KeyMaskWordCount];
            _keyboard.ClearTransientEvents();
            _channelLevelMeter.Clear();
            UpdateLampStyles();
            UpdateKeyboardLabel();
            _outputStageMeter.Clear();
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
                row.DryPeak = FormatPercent(snapshot.AudioPeaks[i]);
                row.ReverbSendPeak = FormatPercent(snapshot.ReverbSendPeaks[i]);
                row.ChorusSendPeak = FormatPercent(snapshot.ChorusSendPeaks[i]);
                row.ControllerSummary = string.Format(
                    CultureInfo.InvariantCulture,
                    "{0}/{1}/{2}/{3}",
                    FormatPercent(snapshot.Volumes[i]),
                    FormatPercent(snapshot.Pans[i]),
                    FormatPercent(snapshot.ReverbSends[i]),
                    FormatPercent(snapshot.ChorusSends[i]));
                row.Lamp = snapshot.ActiveNotes[i] > 0 ? "ON" : string.Empty;
                row.On = (snapshot.MuteMask & (1u << i)) == 0;
                row.Solo = (snapshot.SoloMask & (1u << i)) != 0;
            }
        } finally {
            _suppressMaskEvents = false;
        }
        var selectedChannel = SelectedChannelIndex();
        _channelLevelMeter.SetLevels(snapshot.AudioPeaks, snapshot.ReverbSendPeaks, snapshot.ChorusSendPeaks, snapshot.MuteMask, snapshot.SoloMask);
        _keyboard.ActiveKeyMasks = snapshot.ActiveKeyMasks[selectedChannel];
        _keyboard.ApplyChannelEvents(selectedChannel, channelEvents);
        UpdateLampStyles();
        UpdateKeyboardLabel();
        _statusLabel.Text = _player.IsFinished ? "Finished" : "Playing";
        _outputStageMeter.Meter = _player.LatestOutputStageMeter;
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
            await player.SeekSecondsAsync(targetSeconds);
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
        var lampColumnIndex = _channelGrid.Columns[nameof(ChannelRow.Lamp)]?.Index ?? -1;
        if (lampColumnIndex < 0) {
            return;
        }
        for (int rowIndex = 0; rowIndex < _channelGrid.Rows.Count; ++rowIndex) {
            var row = _channelGrid.Rows[rowIndex];
            var lampCell = row.Cells[lampColumnIndex];
            var isOn = _channels[rowIndex].ActiveNotes > 0;
            lampCell.Style.BackColor = isOn ? Color.FromArgb(216, 255, 216) : Color.FromArgb(245, 245, 245);
            lampCell.Style.ForeColor = isOn ? Color.FromArgb(0, 96, 32) : Color.FromArgb(128, 128, 128);
            lampCell.Style.SelectionBackColor = isOn ? Color.FromArgb(180, 235, 190) : Color.FromArgb(230, 230, 230);
            lampCell.Style.SelectionForeColor = lampCell.Style.ForeColor;
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
        if (_applySf2ChannelDefaultModulatorsCheckBox.Checked &&
            !_useSf2SpecModulatorResolverCheckBox.Checked) {
            flags |= XArkMidiEngine.CompatibilityFlags.ApplySf2ChannelDefaultModulators;
        }
        if (_useSf2SpecModulatorResolverCheckBox.Checked) {
            flags |= XArkMidiEngine.CompatibilityFlags.UseSf2SpecModulatorResolver;
        }
        if (!_internalEffectsCheckBox.Checked) {
            flags |= XArkMidiEngine.CompatibilityFlags.DisableInternalEffects;
        }
        if (_outputStageComboBox.SelectedIndex == 1) {
            flags |= XArkMidiEngine.CompatibilityFlags.EnableEnhancedOutputStage;
            flags |= XArkMidiEngine.CompatibilityFlags.EnhancedOutputStageNatural;
        } else if (_outputStageComboBox.SelectedIndex == 2) {
            flags |= XArkMidiEngine.CompatibilityFlags.EnableEnhancedOutputStage;
            flags |= XArkMidiEngine.CompatibilityFlags.EnhancedOutputStageWarm;
        } else if (_outputStageComboBox.SelectedIndex == 3) {
            flags |= XArkMidiEngine.CompatibilityFlags.EnableEnhancedOutputStage;
        }
        options.CompatibilityFlags = flags;
        return options;
    }

    private void UpdateCreateOptionsEnabledState()
    {
        var idle = _player is null && !_exportInFlight;
        _createOptionsGroup.Enabled = !_exportInFlight;
        _effectsOptionsButton.Enabled = !_exportInFlight;
        _maxSampleDataBytesUpDown.Enabled = idle;
        _maxSf2PdtaEntriesUpDown.Enabled = idle;
        _maxDlsPoolTableEntriesUpDown.Enabled = idle;
        _sf2ZeroLengthLoopRetriggerCheckBox.Enabled = idle;
        _enableSf2SamplePitchCorrectionCheckBox.Enabled = idle;
        _multiplySf2MidiEffectsSendsCheckBox.Enabled = idle;
        _useSf2SpecModulatorResolverCheckBox.Enabled = idle;
        _applySf2ChannelDefaultModulatorsCheckBox.Enabled =
            idle && !_useSf2SpecModulatorResolverCheckBox.Checked;
        _internalEffectsCheckBox.Enabled = idle;
        _outputStageComboBox.Enabled = idle;
        _sf2ReverbSendScaleUpDown.Enabled = !_exportInFlight;
        _sf2ChorusSendScaleUpDown.Enabled = !_exportInFlight;
        _reverbReturnScaleUpDown.Enabled = !_exportInFlight;
        _chorusReturnScaleUpDown.Enabled = !_exportInFlight;
        _masterReverbSendScaleUpDown.Enabled = !_exportInFlight;
        _chorusToReverbScaleUpDown.Enabled = !_exportInFlight;
        _outputGainScaleUpDown.Enabled = !_exportInFlight;
        _exportWavButton.Enabled = idle;
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

    private static string FormatPercent(float value)
    {
        if (!float.IsFinite(value)) {
            value = 0.0f;
        }
        var percent = Math.Clamp((int)MathF.Round(value * 100.0f), 0, 999);
        return percent.ToString(CultureInfo.InvariantCulture);
    }

    private static uint DecimalToUInt32(decimal value)
    {
        return decimal.ToUInt32(decimal.Truncate(value));
    }

    private static ulong DecimalToUInt64(decimal value)
    {
        return decimal.ToUInt64(decimal.Truncate(value));
    }

    private static float Sf2SendScaleFromPercent(decimal percent)
    {
        return (float)(Math.Clamp(percent, 0m, 200m) / 100m);
    }

    private static float EffectScaleFromPercent(decimal percent)
    {
        return (float)(Math.Clamp(percent, 0m, 200m) / 100m);
    }

    private static void RenderWavFile(
        string midiPath,
        string soundFontPath,
        string outputPath,
        XArkMidiEngine.CreateOptions createOptions,
        uint muteMask,
        uint soloMask,
        bool loopEnabled,
        uint loopCount,
        float sf2ReverbSendScale,
        float sf2ChorusSendScale,
        float reverbReturnScale,
        float chorusReturnScale,
        float masterReverbSendScale,
        float chorusToReverbScale,
        float outputGainScale,
        IProgress<WavExportProgress>? progress)
    {
        using var engine = new XArkMidiEngine.Engine(
            midiPath,
            soundFontPath,
            WaveOutPlayer.DetectSoundBankKind(soundFontPath),
            WaveOutPlayer.SampleRate,
            WaveOutPlayer.NumChannels,
            createOptions);
        engine.ChannelMuteMask = muteMask;
        engine.ChannelSoloMask = soloMask;
        engine.SetLoop(loopEnabled, loopCount);
        engine.SetSf2EffectSendScale(sf2ReverbSendScale, sf2ChorusSendScale);
        engine.SetEffectMixScale(reverbReturnScale, chorusReturnScale, masterReverbSendScale, chorusToReverbScale);
        engine.SetOutputGainScale(outputGainScale);

        var estimatedFrames = engine.LengthFramesEstimate;
        if (loopEnabled) {
            estimatedFrames = checked(estimatedFrames * ((ulong)loopCount + 1UL));
        }

        using var writer = new WavDumpWriter(outputPath, WaveOutPlayer.SampleRate, WaveOutPlayer.NumChannels, bitsPerSample: 16);
        var buffer = new short[WaveOutPlayer.FramesPerBuffer * WaveOutPlayer.NumChannels];
        ulong lastReportedFrame = 0;
        while (!engine.IsFinished) {
            var written = engine.Render(buffer, WaveOutPlayer.FramesPerBuffer);
            if (written == 0) {
                break;
            }

            writer.WriteInterleavedI16(buffer, checked((int)(written * WaveOutPlayer.NumChannels)));
            var currentFrame = engine.CurrentFramePosition;
            if (currentFrame - lastReportedFrame >= WaveOutPlayer.SampleRate / 4 || engine.IsFinished) {
                lastReportedFrame = currentFrame;
                progress?.Report(new WavExportProgress(
                    currentFrame / (double)WaveOutPlayer.SampleRate,
                    estimatedFrames / (double)WaveOutPlayer.SampleRate));
            }
        }
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
    private string _dryPeak = string.Empty;
    private string _reverbSendPeak = string.Empty;
    private string _chorusSendPeak = string.Empty;
    private string _controllerSummary = string.Empty;
    private string _lamp = string.Empty;

    public int Channel { get => _channel; set => SetField(ref _channel, value, nameof(Channel)); }
    public bool On { get => _on; set => SetField(ref _on, value, nameof(On)); }
    public bool Solo { get => _solo; set => SetField(ref _solo, value, nameof(Solo)); }
    public int ProgramNumber { get => _programNumber; set => SetField(ref _programNumber, value, nameof(ProgramNumber)); }
    public string ProgramName { get => _programName; set => SetField(ref _programName, value, nameof(ProgramName)); }
    public int ActiveNotes { get => _activeNotes; set => SetField(ref _activeNotes, value, nameof(ActiveNotes)); }
    public string DryPeak { get => _dryPeak; set => SetField(ref _dryPeak, value, nameof(DryPeak)); }
    public string ReverbSendPeak { get => _reverbSendPeak; set => SetField(ref _reverbSendPeak, value, nameof(ReverbSendPeak)); }
    public string ChorusSendPeak { get => _chorusSendPeak; set => SetField(ref _chorusSendPeak, value, nameof(ChorusSendPeak)); }
    public string ControllerSummary { get => _controllerSummary; set => SetField(ref _controllerSummary, value, nameof(ControllerSummary)); }
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
    public const int SampleRate = 44100;
    public const int NumChannels = 2;
    public const int FramesPerBuffer = 2048;
    private const int BufferCount = 4;

    private readonly string _midiPath;
    private readonly string _soundFontPath;
    private readonly XArkMidiEngine.CreateOptions _createOptions;
    private readonly float _initialSf2ReverbSendScale;
    private readonly float _initialSf2ChorusSendScale;
    private readonly float _initialReverbReturnScale;
    private readonly float _initialChorusReturnScale;
    private readonly float _initialMasterReverbSendScale;
    private readonly float _initialChorusToReverbScale;
    private readonly float _initialOutputGainScale;
    private readonly ulong _startFramePosition;
    private readonly List<WaveBuffer> _buffers = new();
    private readonly object _engineLock = new();
    private XArkMidiEngine.Engine? _engine;
    private IntPtr _waveOut = IntPtr.Zero;
    private CancellationTokenSource? _cts;
    private Task? _playTask;
    private uint _pendingMuteMask;
    private uint _pendingSoloMask;
    private bool _pendingLoopEnabled;
    private uint _pendingLoopCount;
    private float _pendingSf2ReverbSendScale;
    private float _pendingSf2ChorusSendScale;
    private float _pendingReverbReturnScale;
    private float _pendingChorusReturnScale;
    private float _pendingMasterReverbSendScale;
    private float _pendingChorusToReverbScale;
    private float _pendingOutputGainScale;
    private int _pendingMaskDirty;
    private int _pendingLoopDirty;
    private int _pendingSf2SendScaleDirty;
    private int _pendingEffectMixScaleDirty;
    private int _pendingOutputGainScaleDirty;
    private int _suppressPlaybackStopped;
    private Exception? _playbackException;
    private WavDumpWriter? _dumpWriter;
    private ulong _lengthFramesEstimate;
    private XArkMidiEngine.OutputStageMeter _latestOutputStageMeter;

    public event EventHandler? PlaybackStopped;

    public WaveOutPlayer(
        string midiPath,
        string soundFontPath,
        XArkMidiEngine.CreateOptions createOptions,
        float initialSf2ReverbSendScale,
        float initialSf2ChorusSendScale,
        float initialReverbReturnScale,
        float initialChorusReturnScale,
        float initialMasterReverbSendScale,
        float initialChorusToReverbScale,
        float initialOutputGainScale,
        double startPositionSeconds = 0.0)
    {
        _midiPath = midiPath;
        _soundFontPath = soundFontPath;
        _createOptions = createOptions;
        _initialSf2ReverbSendScale = initialSf2ReverbSendScale;
        _initialSf2ChorusSendScale = initialSf2ChorusSendScale;
        _initialReverbReturnScale = initialReverbReturnScale;
        _initialChorusReturnScale = initialChorusReturnScale;
        _initialMasterReverbSendScale = initialMasterReverbSendScale;
        _initialChorusToReverbScale = initialChorusToReverbScale;
        _initialOutputGainScale = initialOutputGainScale;
        _pendingSf2ReverbSendScale = initialSf2ReverbSendScale;
        _pendingSf2ChorusSendScale = initialSf2ChorusSendScale;
        _pendingReverbReturnScale = initialReverbReturnScale;
        _pendingChorusReturnScale = initialChorusReturnScale;
        _pendingMasterReverbSendScale = initialMasterReverbSendScale;
        _pendingChorusToReverbScale = initialChorusToReverbScale;
        _pendingOutputGainScale = initialOutputGainScale;
        _startFramePosition = startPositionSeconds <= 0.0
            ? 0
            : (ulong)Math.Round(startPositionSeconds * SampleRate);
    }

    public bool IsFinished => _engine?.IsFinished ?? true;
    public XArkMidiEngine.OutputStageMeter LatestOutputStageMeter
    {
        get {
            lock (_engineLock) {
                return _latestOutputStageMeter;
            }
        }
    }

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

    public void SetLoop(bool enabled, uint loopCount)
    {
        _pendingLoopEnabled = enabled;
        _pendingLoopCount = loopCount;
        Interlocked.Exchange(ref _pendingLoopDirty, 1);
    }

    public void SetSf2EffectSendScale(float reverbScale, float chorusScale)
    {
        lock (_engineLock) {
            _pendingSf2ReverbSendScale = reverbScale;
            _pendingSf2ChorusSendScale = chorusScale;
            Interlocked.Exchange(ref _pendingSf2SendScaleDirty, 1);
        }
    }

    public void SetEffectMixScale(float reverbReturnScale, float chorusReturnScale,
                                  float masterReverbSendScale, float chorusToReverbScale)
    {
        lock (_engineLock) {
            _pendingReverbReturnScale = reverbReturnScale;
            _pendingChorusReturnScale = chorusReturnScale;
            _pendingMasterReverbSendScale = masterReverbSendScale;
            _pendingChorusToReverbScale = chorusToReverbScale;
            Interlocked.Exchange(ref _pendingEffectMixScaleDirty, 1);
        }
    }

    public void SetOutputGainScale(float scale)
    {
        lock (_engineLock) {
            _pendingOutputGainScale = scale;
            Interlocked.Exchange(ref _pendingOutputGainScaleDirty, 1);
        }
    }

    public async Task SeekSecondsAsync(double seconds)
    {
        var playTask = _playTask;
        if (playTask is null) {
            return;
        }

        Interlocked.Exchange(ref _suppressPlaybackStopped, 1);
        try {
            _cts?.Cancel();
            if (_waveOut != IntPtr.Zero) {
                NativeMethods.waveOutReset(_waveOut);
            }
            await playTask;

            var playbackException = ConsumePlaybackException();
            if (playbackException is not null) {
                throw playbackException;
            }

            _cts?.Dispose();
            _cts = null;
            _playTask = null;

            lock (_engineLock) {
                _engine?.SeekSeconds(seconds);
            }

            _cts = new CancellationTokenSource();
            _playTask = Task.Run(() => PlaybackLoop(_cts.Token));
        } finally {
            Interlocked.Exchange(ref _suppressPlaybackStopped, 0);
        }
    }

    public ChannelSnapshot GetChannelSnapshot()
    {
        lock (_engineLock) {
            if (_engine is null) {
                return ChannelSnapshot.Empty;
            }
            var programs = new int[ChannelCount];
            var activeNotes = new uint[ChannelCount];
            var audioPeaks = new float[ChannelCount];
            var reverbSendPeaks = new float[ChannelCount];
            var chorusSendPeaks = new float[ChannelCount];
            var volumes = new float[ChannelCount];
            var pans = new float[ChannelCount];
            var reverbSends = new float[ChannelCount];
            var chorusSends = new float[ChannelCount];
            var activeKeyMasks = new uint[ChannelCount][];
            for (uint ch = 0; ch < ChannelCount; ++ch) {
                programs[ch] = _engine.GetChannelProgram(ch);
                activeNotes[ch] = _engine.GetChannelActiveNoteCount(ch);
                audioPeaks[ch] = _engine.GetChannelAudioPeak(ch);
                reverbSendPeaks[ch] = _engine.GetChannelReverbSendPeak(ch);
                chorusSendPeaks[ch] = _engine.GetChannelChorusSendPeak(ch);
                volumes[ch] = _engine.GetChannelVolume(ch);
                pans[ch] = _engine.GetChannelPan(ch);
                reverbSends[ch] = _engine.GetChannelReverbSend(ch);
                chorusSends[ch] = _engine.GetChannelChorusSend(ch);
                var channelMasks = new uint[KeyMaskWordCount];
                for (uint wordIndex = 0; wordIndex < KeyMaskWordCount; ++wordIndex) {
                    channelMasks[wordIndex] = _engine.GetChannelActiveKeyMaskWord(ch, wordIndex);
                }
                activeKeyMasks[ch] = channelMasks;
            }
            return new ChannelSnapshot(
                programs,
                activeNotes,
                audioPeaks,
                reverbSendPeaks,
                chorusSendPeaks,
                volumes,
                pans,
                reverbSends,
                chorusSends,
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
                        if (Interlocked.Exchange(ref _pendingLoopDirty, 0) != 0) {
                            _engine.SetLoop(_pendingLoopEnabled, _pendingLoopCount);
                        }
                        if (Interlocked.Exchange(ref _pendingSf2SendScaleDirty, 0) != 0) {
                            _engine.SetSf2EffectSendScale(_pendingSf2ReverbSendScale, _pendingSf2ChorusSendScale);
                        }
                        if (Interlocked.Exchange(ref _pendingEffectMixScaleDirty, 0) != 0) {
                            _engine.SetEffectMixScale(
                                _pendingReverbReturnScale,
                                _pendingChorusReturnScale,
                                _pendingMasterReverbSendScale,
                                _pendingChorusToReverbScale);
                        }
                        if (Interlocked.Exchange(ref _pendingOutputGainScaleDirty, 0) != 0) {
                            _engine.SetOutputGainScale(_pendingOutputGainScale);
                        }
                        if (_engine.IsFinished) {
                            playbackFinished = true;
                            continue;
                        }
                        written = _engine.Render(buffer.Samples, FramesPerBuffer);
                        if (_engine.TryGetOutputStageMeter(out var meter)) {
                            _latestOutputStageMeter = meter;
                        } else {
                            _latestOutputStageMeter = default;
                        }
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
            if (Interlocked.CompareExchange(ref _suppressPlaybackStopped, 0, 0) == 0) {
                _dumpWriter?.Dispose();
                _dumpWriter = null;
                PlaybackStopped?.Invoke(this, EventArgs.Empty);
            }
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
        _pendingLoopEnabled = false;
        _pendingLoopCount = 0;
        _pendingSf2ReverbSendScale = _initialSf2ReverbSendScale;
        _pendingSf2ChorusSendScale = _initialSf2ChorusSendScale;
        _pendingReverbReturnScale = _initialReverbReturnScale;
        _pendingChorusReturnScale = _initialChorusReturnScale;
        _pendingMasterReverbSendScale = _initialMasterReverbSendScale;
        _pendingChorusToReverbScale = _initialChorusToReverbScale;
        _pendingOutputGainScale = _initialOutputGainScale;
        _lengthFramesEstimate = 0;
        _latestOutputStageMeter = default;
        Interlocked.Exchange(ref _pendingMaskDirty, 0);
        Interlocked.Exchange(ref _pendingLoopDirty, 0);
        Interlocked.Exchange(ref _pendingSf2SendScaleDirty, 0);
        Interlocked.Exchange(ref _pendingEffectMixScaleDirty, 0);
        Interlocked.Exchange(ref _pendingOutputGainScaleDirty, 0);
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
        engine.SetSf2EffectSendScale(_initialSf2ReverbSendScale, _initialSf2ChorusSendScale);
        engine.SetEffectMixScale(
            _initialReverbReturnScale,
            _initialChorusReturnScale,
            _initialMasterReverbSendScale,
            _initialChorusToReverbScale);
        engine.SetOutputGainScale(_initialOutputGainScale);
        _lengthFramesEstimate = engine.LengthFramesEstimate;
        if (_startFramePosition != 0) {
            engine.SeekFrames(_startFramePosition);
        }
        return engine;
    }

    public static XArkMidiEngine.SoundBankKind DetectSoundBankKind(string path)
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

public readonly record struct ChannelSnapshot(
    int[] Programs,
    uint[] ActiveNotes,
    float[] AudioPeaks,
    float[] ReverbSendPeaks,
    float[] ChorusSendPeaks,
    float[] Volumes,
    float[] Pans,
    float[] ReverbSends,
    float[] ChorusSends,
    uint[][] ActiveKeyMasks,
    uint MuteMask,
    uint SoloMask)
{
    public static ChannelSnapshot Empty { get; } = new(
        new int[16],
        new uint[16],
        new float[16],
        new float[16],
        new float[16],
        new float[16],
        CreateDefaultPans(),
        new float[16],
        new float[16],
        CreateEmptyKeyMasks(),
        0,
        0);

    private static uint[][] CreateEmptyKeyMasks()
    {
        var result = new uint[16][];
        for (int i = 0; i < result.Length; ++i) {
            result[i] = new uint[4];
        }
        return result;
    }

    private static float[] CreateDefaultPans()
    {
        var result = new float[16];
        Array.Fill(result, 0.5f);
        return result;
    }
}

public readonly record struct WavExportProgress(double CurrentSeconds, double TotalSeconds);

internal sealed class ChannelLevelMeterControl : Control
{
    private static readonly Color DryLegendColor = Color.FromArgb(64, 150, 94);
    private static readonly Color ReverbLegendColor = Color.FromArgb(128, 92, 172);
    private static readonly Color ChorusLegendColor = Color.FromArgb(52, 142, 176);

    private readonly float[] _dryLevels = new float[16];
    private readonly float[] _reverbLevels = new float[16];
    private readonly float[] _chorusLevels = new float[16];
    private uint _muteMask;
    private uint _soloMask;

    public ChannelLevelMeterControl()
    {
        DoubleBuffered = true;
        ResizeRedraw = true;
        BackColor = SystemColors.Control;
        Font = SystemFonts.MessageBoxFont ?? new Font(FontFamily.GenericSansSerif, 8.0f, FontStyle.Regular);
    }

    public void SetLevels(
        IReadOnlyList<float> audioPeaks,
        IReadOnlyList<float> reverbSendPeaks,
        IReadOnlyList<float> chorusSendPeaks,
        uint muteMask,
        uint soloMask)
    {
        _muteMask = muteMask;
        _soloMask = soloMask;
        for (int i = 0; i < _dryLevels.Length; ++i) {
            _dryLevels[i] = SmoothLevel(_dryLevels[i], i < audioPeaks.Count ? audioPeaks[i] : 0.0f);
            _reverbLevels[i] = SmoothLevel(_reverbLevels[i], i < reverbSendPeaks.Count ? reverbSendPeaks[i] : 0.0f);
            _chorusLevels[i] = SmoothLevel(_chorusLevels[i], i < chorusSendPeaks.Count ? chorusSendPeaks[i] : 0.0f);
        }
        Invalidate();
    }

    public void Clear()
    {
        Array.Clear(_dryLevels, 0, _dryLevels.Length);
        Array.Clear(_reverbLevels, 0, _reverbLevels.Length);
        Array.Clear(_chorusLevels, 0, _chorusLevels.Length);
        _muteMask = 0;
        _soloMask = 0;
        Invalidate();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        e.Graphics.Clear(BackColor);

        var width = ClientSize.Width;
        var height = ClientSize.Height;
        if (width <= 0 || height <= 0) {
            return;
        }

        const int channelCount = 16;
        const int gap = 4;
        const int legendHeight = 18;
        const int labelHeight = 18;
        var meterTop = 6 + legendHeight;
        var meterHeight = Math.Max(12, height - labelHeight - meterTop - 6);
        var slotWidth = Math.Max(10, (width - gap * (channelCount - 1)) / channelCount);

        using var labelBrush = new SolidBrush(ForeColor);
        using var framePen = new Pen(Color.FromArgb(150, 150, 150));
        using var mutedBrush = new SolidBrush(Color.FromArgb(214, 214, 214));
        using var backBrush = new SolidBrush(Color.FromArgb(232, 232, 232));

        DrawLegend(e.Graphics, width);

        for (int i = 0; i < channelCount; ++i) {
            var x = i * (slotWidth + gap);
            var frame = new Rectangle(x, meterTop, slotWidth, meterHeight);
            var isMuted = (_muteMask & (1u << i)) != 0;
            var isSoloed = (_soloMask & (1u << i)) != 0;
            e.Graphics.FillRectangle(isMuted ? mutedBrush : backBrush, frame);
            e.Graphics.DrawRectangle(framePen, frame);

            var innerWidth = Math.Max(1, slotWidth - 2);
            var barGap = innerWidth >= 9 ? 1 : 0;
            var barWidth = Math.Max(1, (innerWidth - barGap * 2) / 3);
            DrawSubMeter(e.Graphics, x + 1, meterTop + 1, barWidth, meterHeight - 2, _dryLevels[i], ChannelColor(_dryLevels[i], isSoloed));
            DrawSubMeter(e.Graphics, x + 1 + barWidth + barGap, meterTop + 1, barWidth, meterHeight - 2, _reverbLevels[i], ReverbLegendColor);
            DrawSubMeter(e.Graphics, x + 1 + (barWidth + barGap) * 2, meterTop + 1, barWidth, meterHeight - 2, _chorusLevels[i], ChorusLegendColor);

            var label = (i + 1).ToString();
            var labelSize = e.Graphics.MeasureString(label, Font);
            e.Graphics.DrawString(label, Font, labelBrush, x + (slotWidth - labelSize.Width) * 0.5f, meterTop + meterHeight + 1);
        }
    }

    private void DrawLegend(Graphics graphics, int width)
    {
        const int swatchSize = 8;
        const int itemGap = 14;
        var labels = new[] {
            ("Dry", DryLegendColor),
            ("Rev Send", ReverbLegendColor),
            ("Cho Send", ChorusLegendColor),
        };

        var itemWidths = new int[labels.Length];
        var totalWidth = 0;
        for (int i = 0; i < labels.Length; ++i) {
            itemWidths[i] = swatchSize + 4 + (int)Math.Ceiling(graphics.MeasureString(labels[i].Item1, Font).Width);
            totalWidth += itemWidths[i];
            if (i > 0) {
                totalWidth += itemGap;
            }
        }
        if (totalWidth > width - 8) {
            return;
        }

        var x = Math.Max(0, width - totalWidth - 4);
        const int y = 4;
        using var textBrush = new SolidBrush(ForeColor);
        for (int i = 0; i < labels.Length; ++i) {
            using var swatchBrush = new SolidBrush(labels[i].Item2);
            graphics.FillRectangle(swatchBrush, x, y + 3, swatchSize, swatchSize);
            graphics.DrawString(labels[i].Item1, Font, textBrush, x + swatchSize + 4, y);
            x += itemWidths[i] + itemGap;
        }
    }

    private static Color ChannelColor(float level, bool soloed)
    {
        if (soloed) {
            return Color.FromArgb(80, 132, 210);
        }
        if (level >= 0.85f) {
            return Color.FromArgb(214, 70, 56);
        }
        if (level >= 0.55f) {
            return Color.FromArgb(226, 156, 48);
        }
        return DryLegendColor;
    }

    private static float SmoothLevel(float current, float peak)
    {
        var target = MathF.Sqrt(Math.Clamp(peak, 0.0f, 1.0f));
        return Math.Max(target, current * 0.86f);
    }

    private static void DrawSubMeter(Graphics graphics, int x, int y, int width, int height, float level, Color color)
    {
        var fillHeight = Math.Clamp((int)Math.Round(height * level), 0, height);
        if (fillHeight <= 0) {
            return;
        }
        using var fillBrush = new SolidBrush(color);
        graphics.FillRectangle(fillBrush, x, y + height - fillHeight, width, fillHeight);
    }
}

internal sealed class OutputStageMeterControl : Control
{
    private XArkMidiEngine.OutputStageMeter _meter;

    public OutputStageMeterControl()
    {
        DoubleBuffered = true;
        ResizeRedraw = true;
        BackColor = SystemColors.Control;
        Font = SystemFonts.MessageBoxFont ?? new Font(FontFamily.GenericSansSerif, 8.0f, FontStyle.Regular);
    }

    public XArkMidiEngine.OutputStageMeter Meter
    {
        get => _meter;
        set
        {
            _meter = value;
            Invalidate();
        }
    }

    public void Clear()
    {
        Meter = default;
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        e.Graphics.Clear(BackColor);

        using var textBrush = new SolidBrush(ForeColor);
        const int modeWidth = 118;
        if (_meter.ProcessedFrames == 0) {
            e.Graphics.DrawString("Out: --", Font, textBrush, 0, 14);
            return;
        }

        var modeText = $"Out: {_meter.Mode}";
        var modeSize = e.Graphics.MeasureString(modeText, Font);
        if (modeSize.Width <= modeWidth - 4) {
            e.Graphics.DrawString(modeText, Font, textBrush, 0, 2);
        } else {
            e.Graphics.DrawString("Out", Font, textBrush, 0, 2);
            e.Graphics.DrawString(_meter.Mode.ToString(), Font, textBrush, 0, 18);
        }

        const int labelWidth = 34;
        const int barHeight = 7;
        const int gapX = 10;
        const int rowGap = 16;
        const int leftX = modeWidth + labelWidth;
        const int topY = 3;
        int availableWidth = Math.Max(72, ClientSize.Width - leftX - gapX - labelWidth - 2);
        int barWidth = Math.Max(24, availableWidth / 2);

        DrawBar(e.Graphics, "In", _meter.InputPeak, 1.20f, leftX, topY, barWidth, barHeight, labelWidth, PeakColor(_meter.InputPeak));
        DrawBar(e.Graphics, "Out", _meter.OutputPeak, 1.00f, leftX + barWidth + gapX + labelWidth, topY, barWidth, barHeight, labelWidth, PeakColor(_meter.OutputPeak));
        DrawBar(e.Graphics, "Dense", _meter.DensityGain, 1.00f, leftX, topY + rowGap, barWidth, barHeight, labelWidth, GainColor(_meter.DensityGain));
        DrawBar(e.Graphics, "Peak", _meter.PeakGain, 1.00f, leftX + barWidth + gapX + labelWidth, topY + rowGap, barWidth, barHeight, labelWidth, GainColor(_meter.PeakGain));
    }

    private void DrawBar(Graphics graphics, string label, float value, float scale, int x, int y, int width, int height, int labelWidth, Color fillColor)
    {
        using var textBrush = new SolidBrush(ForeColor);
        graphics.DrawString(label, Font, textBrush, x - labelWidth, y - 4);

        var frame = new Rectangle(x, y, width, height);
        using var backBrush = new SolidBrush(Color.FromArgb(230, 230, 230));
        using var borderPen = new Pen(Color.FromArgb(150, 150, 150));
        graphics.FillRectangle(backBrush, frame);
        graphics.DrawRectangle(borderPen, frame);

        float normalized = scale <= 0.0f ? 0.0f : Math.Clamp(value / scale, 0.0f, 1.0f);
        int fillWidth = Math.Clamp((int)Math.Round((width - 2) * normalized), 0, width - 2);
        if (fillWidth > 0) {
            using var fillBrush = new SolidBrush(fillColor);
            graphics.FillRectangle(fillBrush, x + 1, y + 1, fillWidth, Math.Max(1, height - 2));
        }
    }

    private static Color PeakColor(float value)
    {
        if (value >= 0.98f) {
            return Color.FromArgb(214, 70, 56);
        }
        if (value >= 0.85f) {
            return Color.FromArgb(226, 156, 48);
        }
        return Color.FromArgb(64, 150, 94);
    }

    private static Color GainColor(float value)
    {
        if (value <= 0.80f) {
            return Color.FromArgb(76, 128, 200);
        }
        if (value <= 0.94f) {
            return Color.FromArgb(84, 158, 168);
        }
        return Color.FromArgb(120, 172, 88);
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
