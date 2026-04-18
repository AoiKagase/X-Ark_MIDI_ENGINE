using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace XArkMidiGuiPlayer;

public sealed class MainForm : Form
{
    private const int ChannelCount = 16;
    private readonly TextBox _midiPathTextBox = new() { Width = 520 };
    private readonly TextBox _soundFontPathTextBox = new() { Width = 520 };
    private readonly Button _browseMidiButton = new() { Text = "Open MIDI..." };
    private readonly Button _browseSoundFontButton = new() { Text = "Open SF2..." };
    private readonly Button _playButton = new() { Text = "Play", Width = 90 };
    private readonly Button _stopButton = new() { Text = "Stop", Width = 90, Enabled = false };
    private readonly Label _statusLabel = new() { AutoSize = true, Text = "Idle" };
    private readonly DataGridView _channelGrid = new() { Dock = DockStyle.Fill };
    private readonly System.Windows.Forms.Timer _uiTimer = new() { Interval = 50 };
    private readonly BindingList<ChannelRow> _channels = new();
    private readonly OpenFileDialog _midiDialog = new() { Filter = "MIDI files (*.mid;*.midi)|*.mid;*.midi|All files (*.*)|*.*" };
    private readonly OpenFileDialog _soundFontDialog = new() { Filter = "SoundFont (*.sf2)|*.sf2|All files (*.*)|*.*" };

    private WaveOutPlayer? _player;
    private bool _suppressMaskEvents;

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
            RowCount = 3,
            Padding = new Padding(12),
        };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));

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
        filePanel.Controls.Add(new Label { AutoSize = true, Text = "SF2", Anchor = AnchorStyles.Left }, 0, 1);
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

        ConfigureGrid();

        root.Controls.Add(filePanel, 0, 0);
        root.Controls.Add(controlPanel, 0, 1);
        root.Controls.Add(_channelGrid, 0, 2);
        Controls.Add(root);
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

    private async Task StartPlaybackAsync()
    {
        if (_player is not null) {
            return;
        }
        if (!File.Exists(_midiPathTextBox.Text)) {
            MessageBox.Show(this, "MIDI file not found.", "X-Ark MIDI GUI Player", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        if (!File.Exists(_soundFontPathTextBox.Text)) {
            MessageBox.Show(this, "SF2 file not found.", "X-Ark MIDI GUI Player", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        try {
            var player = new WaveOutPlayer(_midiPathTextBox.Text, _soundFontPathTextBox.Text);
            player.PlaybackStopped += OnPlaybackStopped;
            _player = player;
            ApplyMasksToPlayer();
            _playButton.Enabled = false;
            _stopButton.Enabled = true;
            _statusLabel.Text = "Playing";
            await player.StartAsync();
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
            UpdateLampStyles();
            return;
        }

        var snapshot = _player.GetChannelSnapshot();
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
        UpdateLampStyles();
        _statusLabel.Text = _player.IsFinished ? "Finished" : "Playing";
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
    private const int SampleRate = 44100;
    private const int NumChannels = 2;
    private const int FramesPerBuffer = 2048;
    private const int BufferCount = 4;

    private readonly string _midiPath;
    private readonly string _soundFontPath;
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

    public event EventHandler? PlaybackStopped;

    public WaveOutPlayer(string midiPath, string soundFontPath)
    {
        _midiPath = midiPath;
        _soundFontPath = soundFontPath;
    }

    public bool IsFinished => _engine?.IsFinished ?? true;

    public Task StartAsync()
    {
        if (_playTask is not null) {
            return _playTask;
        }

        try {
            _engine = new XArkMidiEngine.Engine(_midiPath, _soundFontPath,
                XArkMidiEngine.SoundBankKind.Sf2, SampleRate, NumChannels,
                XArkMidiEngine.CreateOptions.Default());

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
                result = NativeMethods.waveOutPrepareHeader(_waveOut, ref buffer.Header, Marshal.SizeOf<WaveHeader>());
                if (result != 0) {
                    buffer.Dispose();
                    throw new InvalidOperationException($"waveOutPrepareHeader failed: {result}");
                }
                _buffers.Add(buffer);
            }

            _cts = new CancellationTokenSource();
            _playTask = Task.Run(() => PlaybackLoop(_cts.Token));
            return _playTask;
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
            for (uint ch = 0; ch < ChannelCount; ++ch) {
                programs[ch] = _engine.GetChannelProgram(ch);
                activeNotes[ch] = _engine.GetChannelActiveNoteCount(ch);
            }
            return new ChannelSnapshot(
                programs,
                activeNotes,
                _engine.ChannelMuteMask,
                _engine.ChannelSoloMask);
        }
    }

    private void PlaybackLoop(CancellationToken cancellationToken)
    {
        try {
            bool playbackFinished = false;
            while (!cancellationToken.IsCancellationRequested) {
                int queuedCount = 0;
                foreach (var buffer in _buffers) {
                    if ((buffer.Header.dwFlags & NativeConstants.WHDR_INQUEUE) != 0) {
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

                    buffer.Header.dwBufferLength = (uint)(written * NumChannels * sizeof(short));
                    buffer.Header.dwFlags &= ~NativeConstants.WHDR_DONE;
                    var result = NativeMethods.waveOutWrite(_waveOut, ref buffer.Header, Marshal.SizeOf<WaveHeader>());
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
                NativeMethods.waveOutUnprepareHeader(_waveOut, ref buffer.Header, Marshal.SizeOf<WaveHeader>());
            }
            NativeMethods.waveOutClose(_waveOut);
            _waveOut = IntPtr.Zero;
        }

        foreach (var buffer in _buffers) {
            buffer.Dispose();
        }
        _buffers.Clear();
        _engine?.Dispose();
        _engine = null;
        _cts?.Dispose();
        _cts = null;
        _playTask = null;
        _pendingMuteMask = 0;
        _pendingSoloMask = 0;
        Interlocked.Exchange(ref _pendingMaskDirty, 0);
    }

    public Exception? ConsumePlaybackException()
    {
        var ex = _playbackException;
        _playbackException = null;
        return ex;
    }
}

public readonly record struct ChannelSnapshot(int[] Programs, uint[] ActiveNotes, uint MuteMask, uint SoloMask)
{
    public static ChannelSnapshot Empty { get; } = new(new int[16], new uint[16], 0, 0);
}

internal sealed class WaveBuffer : IDisposable
{
    private GCHandle _sampleHandle;

    public short[] Samples { get; }
    public WaveHeader Header;

    public WaveBuffer(int sampleCount)
    {
        Samples = new short[sampleCount];
        _sampleHandle = GCHandle.Alloc(Samples, GCHandleType.Pinned);
        Header = new WaveHeader {
            lpData = _sampleHandle.AddrOfPinnedObject(),
            dwBufferLength = (uint)(sampleCount * sizeof(short)),
        };
    }

    public void Dispose()
    {
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
        ref WaveHeader lpWaveOutHdr,
        int uSize);

    [DllImport("winmm.dll")]
    public static extern int waveOutWrite(
        IntPtr hWaveOut,
        ref WaveHeader lpWaveOutHdr,
        int uSize);

    [DllImport("winmm.dll")]
    public static extern int waveOutUnprepareHeader(
        IntPtr hWaveOut,
        ref WaveHeader lpWaveOutHdr,
        int uSize);

    [DllImport("winmm.dll")]
    public static extern int waveOutReset(IntPtr hWaveOut);

    [DllImport("winmm.dll")]
    public static extern int waveOutClose(IntPtr hWaveOut);
}
