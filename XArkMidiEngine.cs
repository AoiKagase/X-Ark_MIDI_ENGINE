/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

using System;
using System.Runtime.InteropServices;

public static class XArkMidiEngine
{
    // 拡張子なしで指定すると .NET がプラットフォーム毎に自動解決する
    // Windows: XArkMidiEngine.dll / Linux: libXArkMidiEngine.so
    private const string DllName = "XArkMidiEngine";

    /// <summary>
    /// Result codes returned by the native library.
    /// ネイティブライブラリが返す結果コードです。
    /// </summary>
    public enum XAmeResult : int
    {
        /// <summary>Operation completed successfully. 正常に完了しました。</summary>
        OK = 0,
        /// <summary>One or more arguments were invalid. 1 つ以上の引数が不正です。</summary>
        InvalidArg = -1,
        /// <summary>Failed to parse the input MIDI file. 入力 MIDI ファイルの解析に失敗しました。</summary>
        ParseMidi = -2,
        /// <summary>Failed to parse the input SF2 sound bank. 入力 SF2 サウンドバンクの解析に失敗しました。</summary>
        ParseSf2 = -3,
        /// <summary>Memory allocation failed. メモリ確保に失敗しました。</summary>
        OutOfMemory = -4,
        /// <summary>Engine or required subsystem was not initialized. エンジンまたは必要なサブシステムが初期化されていません。</summary>
        NotInitialized = -5,
        /// <summary>Failed to parse the input DLS sound bank. 入力 DLS サウンドバンクの解析に失敗しました。</summary>
        ParseDls = -6,
        /// <summary>Requested feature or input format is not supported. 要求された機能または入力形式は未対応です。</summary>
        Unsupported = -7,
        /// <summary>I/O error while reading an input file. 入力ファイルの読み込み中に I/O エラーが発生しました。</summary>
        Io = -8,
    }

    /// <summary>
    /// Sound bank parser selection.
    /// サウンドバンクの解析方式を指定します。
    /// </summary>
    public enum SoundBankKind : uint
    {
        /// <summary>Detect the sound bank type automatically. 自動判定します。</summary>
        Auto = 0,
        /// <summary>Force SoundFont 2 parsing. SoundFont 2 として解析します。</summary>
        Sf2 = 1,
        /// <summary>Force DLS parsing. DLS として解析します。</summary>
        Dls = 2,
    }

    [Flags]
    /// <summary>
    /// Compatibility switches applied during engine creation.
    /// エンジン生成時に適用する互換設定です。
    /// </summary>
    public enum CompatibilityFlags : uint
    {
        /// <summary>Use the engine defaults. エンジン既定動作を使用します。</summary>
        None = 0,
        /// <summary>Retrigger zero-length SF2 loops for compatibility with some banks. 一部バンク互換のため長さ 0 の SF2 ループを再トリガーします。</summary>
        Sf2ZeroLengthLoopRetrigger = 1 << 0,
        /// <summary>Apply pitch correction for SF2 sample playback. SF2 サンプル再生時のピッチ補正を有効にします。</summary>
        EnableSf2SamplePitchCorrection = 1 << 1,
        /// <summary>Enable the experimental post-mix output stage for extra headroom and smoother loudness. 実験的な post-mix 出力段を有効化し、ヘッドルームと滑らかな音量感を調整します。</summary>
        EnableEnhancedOutputStage = 1 << 4,
        /// <summary>Use the natural preset for the enhanced output stage. If unset, enhanced output uses the louder preset. enhanced output stage の Natural プリセットを使います。未指定時は音量感寄りのプリセットです。</summary>
        EnhancedOutputStageNatural = 1 << 5,
        /// <summary>Use the warm preset for the enhanced output stage. Natural takes precedence if both preset flags are set. enhanced output stage の Warm プリセットを使います。Natural と同時指定された場合は Natural を優先します。</summary>
        EnhancedOutputStageWarm = 1 << 6,
        /// <summary>Disable internal post-mix reverb/chorus processing while keeping dry rendering active. 内部 post-mix リバーブ/コーラス処理を無効化し、ドライ出力のみを維持します。</summary>
        DisableInternalEffects = 1 << 7,
        /// <summary>Opt in to the SoundFont 2.04 spec-oriented modulator resolver. Legacy compatibility behavior remains the default. SoundFont 2.04 仕様寄りの modulator resolver を明示的に有効化します。既定は旧互換動作です。</summary>
        UseSf2SpecModulatorResolver = 1 << 8,
    }

    /// <summary>
    /// High-level compatibility mode override.
    /// 上位互換モードの上書き設定です。
    /// </summary>
    public enum CompatibilityMode : uint
    {
        /// <summary>Keep engine defaults and interpret <see cref="CompatibilityFlags"/> as usual. エンジン既定動作を維持します。</summary>
        EngineDefault = 0,
        /// <summary>Force legacy SF2 compatibility behavior. SF2 の旧互換動作を強制します。</summary>
        Sf2Legacy = 1,
        /// <summary>Force SF2 spec-oriented resolver behavior. SF2 仕様寄り resolver 動作を強制します。</summary>
        Sf2Spec204 = 2,
        /// <summary>Use SF2 spec behavior as a base for X-Ark's independent tuned rendering mode. This is not Sound Blaster, Audigy, Creative, EMU8000, or other hardware emulation, and currently may behave the same as <see cref="Sf2Spec204"/>. SF2_SPEC_204 を土台にする独立 tuned モードです。現時点では <see cref="Sf2Spec204"/> と同等動作の場合があります。</summary>
        Sf2RenderTuned = 3,
    }

    [StructLayout(LayoutKind.Sequential)]
    /// <summary>
    /// Optional limits and compatibility overrides used during engine creation.
    /// エンジン生成時の任意制限値と互換設定です。
    /// </summary>
    public struct CreateOptions
    {
        /// <summary>
        /// Size of this structure in bytes. Set automatically by <see cref="Default"/>.
        /// この構造体のサイズです。<see cref="Default"/> で自動設定されます。
        /// </summary>
        public uint StructSize;
        /// <summary>
        /// Maximum total decoded sample data allowed for the loaded bank. 0 uses the native default.
        /// 読み込むバンクのデコード済みサンプル総量上限です。0 でネイティブ既定値を使用します。
        /// </summary>
        public ulong MaxSampleDataBytes;
        /// <summary>
        /// Maximum number of SF2 pdta entries allowed while parsing. 0 uses the native default.
        /// SF2 の pdta エントリ数上限です。0 でネイティブ既定値を使用します。
        /// </summary>
        public uint MaxSf2PdtaEntries;
        /// <summary>
        /// Maximum number of DLS pool table entries allowed while parsing. 0 uses the native default.
        /// DLS の pool table エントリ数上限です。0 でネイティブ既定値を使用します。
        /// </summary>
        public uint MaxDlsPoolTableEntries;
        /// <summary>
        /// Bitwise OR of compatibility flags.
        /// 互換フラグの OR 値です。
        /// </summary>
        public CompatibilityFlags CompatibilityFlags;

        /// <summary>
        /// Optional UTF-16 path to an external SF2 ROM sample bank used by ROM-backed samples.
        /// NULL の場合は UTF-8 側を参照し、両方 NULL の場合は ROM サンプル参照を無効化します。
        /// </summary>
        [MarshalAs(UnmanagedType.LPWStr)]
        public string Sf2RomBankPath;
        /// <summary>
        /// Optional UTF-8 path to an external SF2 ROM sample bank used by ROM-backed samples.
        /// <see cref="Sf2RomBankPath"/> が指定されている場合はそちらが優先されます。
        /// </summary>
        [MarshalAs(UnmanagedType.LPUTF8Str)]
        public string Sf2RomBankPathUtf8;

        /// <summary>
        /// Optional high-level compatibility mode override.
        /// 互換モード上書き設定です。
        /// </summary>
        public CompatibilityMode CompatibilityMode;

        /// <summary>
        /// Create an option block initialized with the correct native structure size.
        /// ネイティブ構造体サイズを正しく初期化したオプションを生成します。
        /// </summary>
        public static CreateOptions Default()
            => new CreateOptions { StructSize = (uint)Marshal.SizeOf<CreateOptions>() };
    }

    [StructLayout(LayoutKind.Sequential)]
    /// <summary>
    /// A channel/key event popped from the native event queue.
    /// ネイティブイベントキューから取得したチャンネル/キーイベントです。
    /// </summary>
    public struct ChannelKeyEvent
    {
        /// <summary>MIDI channel number in the range [0, 15]. MIDI チャンネル番号です。</summary>
        public byte Channel;
        /// <summary>MIDI key number in the range [0, 127]. MIDI キー番号です。</summary>
        public byte Key;
        /// <summary>Non-zero for note-on, zero for note-off. 0 以外で note-on、0 で note-off を表します。</summary>
        public byte IsNoteOn;
        /// <summary>Reserved for future use. 将来拡張用の予約領域です。</summary>
        public byte Reserved;
        /// <summary>MIDI velocity associated with the event. イベントに対応するベロシティ値です。</summary>
        public ushort Velocity;
        /// <summary>Reserved for future use. 将来拡張用の予約領域です。</summary>
        public ushort Reserved2;
    }

    /// <summary>
    /// Output stage preset reported by the native output-stage meter.
    /// ネイティブ出力段メーターが返す出力段プリセットです。
    /// </summary>
    public enum OutputStageMode : uint
    {
        Standard = 0,
        Natural = 1,
        Warm = 2,
        Loud = 3,
    }

    [StructLayout(LayoutKind.Sequential)]
    /// <summary>
    /// Meter values captured during the most recent render call.
    /// 直近のレンダリング呼び出しで取得した出力段メーター値です。
    /// </summary>
    public struct OutputStageMeter
    {
        public OutputStageMode Mode;
        public float InputPeak;
        public float OutputPeak;
        public float DensityGain;
        public float PeakGain;
        public uint ProcessedFrames;
    }

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern XAmeResult XAmeCreateEngineFromPathsUtf8(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string midiPath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string soundBankPath,
        SoundBankKind soundBankKind,
        uint sampleRate,
        uint numChannels,
        out IntPtr outEngine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern XAmeResult XAmeCreateEngineWithOptionsUtf8(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string midiPath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string soundBankPath,
        SoundBankKind soundBankKind,
        uint sampleRate,
        uint numChannels,
        ref CreateOptions options,
        out IntPtr outEngine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern XAmeResult XAmeRender(
        IntPtr engine,
        short[] outBuffer,
        uint numFrames,
        out uint outWritten);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern XAmeResult XAmeReset(IntPtr engine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern XAmeResult XAmeSeekFrames(
        IntPtr engine,
        ulong framePosition);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern XAmeResult XAmeSetChannelMuteMask(
        IntPtr engine,
        uint channelMask);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern XAmeResult XAmeSetLoop(
        IntPtr engine,
        int enabled,
        uint loopCount);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int XAmeGetLoopEnabled(IntPtr engine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern uint XAmeGetLoopCount(IntPtr engine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern XAmeResult XAmeSetChannelSoloMask(
        IntPtr engine,
        uint channelMask);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern uint XAmeGetChannelMuteMask(IntPtr engine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern uint XAmeGetChannelSoloMask(IntPtr engine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int XAmeGetChannelProgram(IntPtr engine, uint channel);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern uint XAmeGetChannelActiveNoteCount(IntPtr engine, uint channel);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern float XAmeGetChannelAudioPeak(IntPtr engine, uint channel);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern float XAmeGetChannelReverbSendPeak(IntPtr engine, uint channel);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern float XAmeGetChannelChorusSendPeak(IntPtr engine, uint channel);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern float XAmeGetChannelVolume(IntPtr engine, uint channel);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern float XAmeGetChannelPan(IntPtr engine, uint channel);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern float XAmeGetChannelReverbSend(IntPtr engine, uint channel);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern float XAmeGetChannelChorusSend(IntPtr engine, uint channel);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern XAmeResult XAmeSetSf2EffectSendScale(
        IntPtr engine,
        float reverbScale,
        float chorusScale);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern float XAmeGetSf2ReverbSendScale(IntPtr engine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern float XAmeGetSf2ChorusSendScale(IntPtr engine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern XAmeResult XAmeSetEffectMixScale(
        IntPtr engine,
        float reverbReturnScale,
        float chorusReturnScale,
        float masterReverbSendScale,
        float chorusToReverbScale);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern float XAmeGetReverbReturnScale(IntPtr engine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern float XAmeGetChorusReturnScale(IntPtr engine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern float XAmeGetMasterReverbSendScale(IntPtr engine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern float XAmeGetChorusToReverbScale(IntPtr engine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern XAmeResult XAmeSetOutputGainScale(IntPtr engine, float scale);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern float XAmeGetOutputGainScale(IntPtr engine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern uint XAmeGetChannelActiveKeyMaskWord(IntPtr engine, uint channel, uint wordIndex);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int XAmePopChannelKeyEvent(IntPtr engine, out ChannelKeyEvent outEvent);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern ulong XAmeGetCurrentFramePosition(IntPtr engine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern ulong XAmeGetLengthFramesEstimate(IntPtr engine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern XAmeResult XAmeGetOutputStageMeter(IntPtr engine, out OutputStageMeter outMeter);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int XAmeIsFinished(IntPtr engine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern void XAmeDestroyEngine(IntPtr engine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr XAmeGetVersion();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr XAmeGetLastError();

    /// <summary>
    /// Get the native library version string.
    /// ネイティブライブラリのバージョン文字列を取得します。
    /// </summary>
    public static string GetVersion()
        => Marshal.PtrToStringAnsi(XAmeGetVersion()) ?? string.Empty;

    /// <summary>
    /// Get the last human-readable error string reported by the native library.
    /// ネイティブライブラリが返した直近の可読エラー文字列を取得します。
    /// </summary>
    public static string GetLastError()
        => Marshal.PtrToStringAnsi(XAmeGetLastError()) ?? string.Empty;

    /// <summary>
    /// Managed wrapper around a native X-Ark MIDI engine instance.
    /// ネイティブ X-Ark MIDI エンジンのマネージドラッパーです。
    /// </summary>
    public sealed class Engine : IDisposable
    {
        private IntPtr _handle;
        private bool _disposed;
        private readonly uint _numChannels;
        private readonly uint _sampleRate;

        /// <summary>
        /// Create an engine using default creation options.
        /// 既定オプションでエンジンを生成します。
        /// </summary>
        /// <param name="midiPath">Path to the input MIDI file. 入力 MIDI ファイルのパスです。</param>
        /// <param name="soundBankPath">Path to the SF2 or DLS sound bank. SF2 または DLS サウンドバンクのパスです。</param>
        /// <param name="soundBankKind">Explicit sound bank kind or automatic detection. サウンドバンク種別の明示指定または自動判定です。</param>
        /// <param name="sampleRate">Output sample rate in Hz. 出力サンプルレートです。</param>
        /// <param name="numChannels">Number of output channels. Must be 1 or 2. 出力チャンネル数です。</param>
        public Engine(string midiPath, string soundBankPath,
                      SoundBankKind soundBankKind = SoundBankKind.Auto,
                      uint sampleRate = 44100, uint numChannels = 2)
            : this(midiPath, soundBankPath, soundBankKind, sampleRate, numChannels, null)
        {
        }

        /// <summary>
        /// Create an engine using explicit native creation options.
        /// 明示的なネイティブ生成オプションでエンジンを生成します。
        /// </summary>
        /// <param name="midiPath">Path to the input MIDI file. 入力 MIDI ファイルのパスです。</param>
        /// <param name="soundBankPath">Path to the SF2 or DLS sound bank. SF2 または DLS サウンドバンクのパスです。</param>
        /// <param name="soundBankKind">Explicit sound bank kind or automatic detection. サウンドバンク種別の明示指定または自動判定です。</param>
        /// <param name="sampleRate">Output sample rate in Hz. 出力サンプルレートです。</param>
        /// <param name="numChannels">Number of output channels. Must be 1 or 2. 出力チャンネル数です。</param>
        /// <param name="options">Optional creation limits and compatibility flags. 任意の制限値および互換フラグです。</param>
        /// <exception cref="ArgumentException">Thrown when a required path is empty or the output buffer is too small. 必須パスが空、またはバッファ不足時に送出されます。</exception>
        /// <exception cref="ArgumentOutOfRangeException">Thrown when <paramref name="numChannels"/> is outside the supported range. 対応範囲外のチャンネル数指定時に送出されます。</exception>
        /// <exception cref="XArkMidiException">Thrown when native engine creation fails. ネイティブ側の生成失敗時に送出されます。</exception>
        public Engine(string midiPath, string soundBankPath,
                      SoundBankKind soundBankKind,
                      uint sampleRate,
                      uint numChannels,
                      CreateOptions? options)
        {
            if (string.IsNullOrWhiteSpace(midiPath))
                throw new ArgumentException("midiPath is null or empty", nameof(midiPath));
            if (string.IsNullOrWhiteSpace(soundBankPath))
                throw new ArgumentException("soundBankPath is null or empty", nameof(soundBankPath));
            if (numChannels < 1 || numChannels > 2)
                throw new ArgumentOutOfRangeException(nameof(numChannels), "Must be 1 or 2");

            XAmeResult result;
            if (options.HasValue)
            {
                var nativeOptions = options.Value;
                if (nativeOptions.StructSize == 0)
                    nativeOptions.StructSize = (uint)Marshal.SizeOf<CreateOptions>();
                result = XAmeCreateEngineWithOptionsUtf8(
                    midiPath,
                    soundBankPath,
                    soundBankKind,
                    sampleRate,
                    numChannels,
                    ref nativeOptions,
                    out _handle);
            }
            else
            {
                result = XAmeCreateEngineFromPathsUtf8(
                    midiPath,
                    soundBankPath,
                    soundBankKind,
                    sampleRate,
                    numChannels,
                    out _handle);
            }

            if (result != XAmeResult.OK)
                throw new XArkMidiException(result, GetLastError());

            _numChannels = numChannels;
            _sampleRate = sampleRate;
        }

        /// <summary>
        /// Render up to <paramref name="numFrames"/> frames into <paramref name="buffer"/>.
        /// <paramref name="buffer"/> に最大 <paramref name="numFrames"/> フレームを書き込みます。
        /// </summary>
        /// <param name="buffer">Interleaved PCM output buffer. Must have room for <c>numFrames * channelCount</c> samples. インターリーブ PCM 出力バッファです。</param>
        /// <param name="numFrames">Maximum number of frames to render. 最大レンダリングフレーム数です。</param>
        /// <returns>The number of frames actually written. 実際に書き込まれたフレーム数を返します。</returns>
        /// <exception cref="ArgumentException">Thrown when <paramref name="buffer"/> is null or too small. バッファが null または不足している場合に送出されます。</exception>
        /// <exception cref="ObjectDisposedException">Thrown after the engine has been disposed. Dispose 後に呼ばれた場合に送出されます。</exception>
        /// <exception cref="XArkMidiException">Thrown when native rendering fails. ネイティブ側レンダリング失敗時に送出されます。</exception>
        public uint Render(short[] buffer, uint numFrames)
        {
            ThrowIfDisposed();
            var requiredSamples = checked((int)(numFrames * _numChannels));
            if (buffer == null || buffer.Length < requiredSamples)
                throw new ArgumentException("buffer is smaller than required", nameof(buffer));

            var result = XAmeRender(_handle, buffer, numFrames, out uint written);
            if (result != XAmeResult.OK)
                throw new XArkMidiException(result, GetLastError());
            return written;
        }

        /// <summary>
        /// Reset playback to the beginning while keeping the loaded MIDI, sound bank, and user options.
        /// 読み込み済み MIDI / サウンドバンクとユーザー設定を保持したまま、再生位置を先頭へ戻します。
        /// </summary>
        public void Reset()
        {
            ThrowIfDisposed();
            var result = XAmeReset(_handle);
            if (result != XAmeResult.OK)
                throw new XArkMidiException(result, GetLastError());
        }

        /// <summary>
        /// Seek to an output frame position.
        /// 出力フレーム位置へシークします。
        /// </summary>
        public void SeekFrames(ulong framePosition)
        {
            ThrowIfDisposed();
            var result = XAmeSeekFrames(_handle, framePosition);
            if (result != XAmeResult.OK)
                throw new XArkMidiException(result, GetLastError());
        }

        /// <summary>
        /// Seek to a playback time in seconds.
        /// 秒単位の再生位置へシークします。
        /// </summary>
        public void SeekSeconds(double seconds)
        {
            if (!double.IsFinite(seconds))
                throw new ArgumentOutOfRangeException(nameof(seconds), "Must be finite");
            var clampedSeconds = Math.Max(0.0, seconds);
            SeekFrames((ulong)Math.Round(clampedSeconds * _sampleRate));
        }

        /// <summary>
        /// Gets whether the engine has finished rendering all audio.
        /// 全音声のレンダリングが完了したかを取得します。
        /// </summary>
        public bool IsFinished
        {
            get
            {
                if (_disposed) return true;
                return XAmeIsFinished(_handle) != 0;
            }
        }

        /// <summary>
        /// Gets or sets whether whole-MIDI looping is enabled.
        /// MIDI 全体のループが有効かどうかを取得または設定します。
        /// </summary>
        public bool LoopEnabled
        {
            get
            {
                ThrowIfDisposed();
                return XAmeGetLoopEnabled(_handle) != 0;
            }
            set
            {
                SetLoop(value, LoopCount);
            }
        }

        /// <summary>
        /// Gets or sets the number of additional repeats after the first play. 0 means infinite when looping is enabled.
        /// 初回再生後の追加ループ回数を取得または設定します。ループ有効時の 0 は無限です。
        /// </summary>
        public uint LoopCount
        {
            get
            {
                ThrowIfDisposed();
                return XAmeGetLoopCount(_handle);
            }
            set
            {
                SetLoop(LoopEnabled, value);
            }
        }

        /// <summary>
        /// Configure whole-MIDI looping.
        /// MIDI 全体のループ設定を行います。
        /// </summary>
        public void SetLoop(bool enabled, uint loopCount)
        {
            ThrowIfDisposed();
            var result = XAmeSetLoop(_handle, enabled ? 1 : 0, loopCount);
            if (result != XAmeResult.OK)
                throw new XArkMidiException(result, GetLastError());
        }

        /// <summary>
        /// Gets or sets the 16-bit mute mask for MIDI channels 0-15.
        /// MIDI チャンネル 0-15 の 16bit ミュートマスクを取得または設定します。
        /// </summary>
        public uint ChannelMuteMask
        {
            get
            {
                ThrowIfDisposed();
                return XAmeGetChannelMuteMask(_handle);
            }
            set
            {
                ThrowIfDisposed();
                var result = XAmeSetChannelMuteMask(_handle, value);
                if (result != XAmeResult.OK)
                    throw new XArkMidiException(result, GetLastError());
            }
        }

        /// <summary>
        /// Gets or sets the 16-bit solo mask for MIDI channels 0-15.
        /// MIDI チャンネル 0-15 の 16bit ソロマスクを取得または設定します。
        /// When non-zero, only selected channels are rendered.
        /// 非 0 の場合、選択されたチャンネルのみがレンダリングされます。
        /// </summary>
        public uint ChannelSoloMask
        {
            get
            {
                ThrowIfDisposed();
                return XAmeGetChannelSoloMask(_handle);
            }
            set
            {
                ThrowIfDisposed();
                var result = XAmeSetChannelSoloMask(_handle, value);
                if (result != XAmeResult.OK)
                    throw new XArkMidiException(result, GetLastError());
            }
        }

        /// <summary>
        /// Get the current program number for a MIDI channel.
        /// 指定 MIDI チャンネルの現在のプログラム番号を取得します。
        /// </summary>
        public int GetChannelProgram(uint channel)
        {
            ThrowIfDisposed();
            return XAmeGetChannelProgram(_handle, channel);
        }

        /// <summary>
        /// Get the number of currently active notes for a MIDI channel.
        /// 指定 MIDI チャンネルで現在発音中のノート数を取得します。
        /// </summary>
        public uint GetChannelActiveNoteCount(uint channel)
        {
            ThrowIfDisposed();
            return XAmeGetChannelActiveNoteCount(_handle, channel);
        }

        /// <summary>
        /// Get the peak dry audio contribution for a MIDI channel from the most recent render call.
        /// 直近レンダリングでの指定 MIDI チャンネルのドライ音声ピークを取得します。
        /// </summary>
        public float GetChannelAudioPeak(uint channel)
        {
            ThrowIfDisposed();
            return XAmeGetChannelAudioPeak(_handle, channel);
        }

        /// <summary>
        /// Get the peak pre-effect reverb-send audio contribution for a MIDI channel from the most recent render call.
        /// 直近レンダリングでの指定 MIDI チャンネルのリバーブ送り音声ピークを取得します。
        /// </summary>
        public float GetChannelReverbSendPeak(uint channel)
        {
            ThrowIfDisposed();
            return XAmeGetChannelReverbSendPeak(_handle, channel);
        }

        /// <summary>
        /// Get the peak pre-effect chorus-send audio contribution for a MIDI channel from the most recent render call.
        /// 直近レンダリングでの指定 MIDI チャンネルのコーラス送り音声ピークを取得します。
        /// </summary>
        public float GetChannelChorusSendPeak(uint channel)
        {
            ThrowIfDisposed();
            return XAmeGetChannelChorusSendPeak(_handle, channel);
        }

        /// <summary>
        /// Get current channel volume controller value normalized to 0..1.
        /// 現在のチャンネル音量コントローラー値を 0..1 で取得します。
        /// </summary>
        public float GetChannelVolume(uint channel)
        {
            ThrowIfDisposed();
            return XAmeGetChannelVolume(_handle, channel);
        }

        /// <summary>
        /// Get current channel pan controller value normalized to 0..1.
        /// 現在のチャンネルパンコントローラー値を 0..1 で取得します。
        /// </summary>
        public float GetChannelPan(uint channel)
        {
            ThrowIfDisposed();
            return XAmeGetChannelPan(_handle, channel);
        }

        /// <summary>
        /// Get current channel reverb send controller value normalized to 0..1.
        /// 現在のチャンネルリバーブセンド値を 0..1 で取得します。
        /// </summary>
        public float GetChannelReverbSend(uint channel)
        {
            ThrowIfDisposed();
            return XAmeGetChannelReverbSend(_handle, channel);
        }

        /// <summary>
        /// Get current channel chorus send controller value normalized to 0..1.
        /// 現在のチャンネルコーラスセンド値を 0..1 で取得します。
        /// </summary>
        public float GetChannelChorusSend(uint channel)
        {
            ThrowIfDisposed();
            return XAmeGetChannelChorusSend(_handle, channel);
        }

        /// <summary>
        /// Set global SF2 preset/modulator effect send scales. 1.0 keeps bank-authored sends unchanged.
        /// SF2 の preset/modulator 由来エフェクト send 倍率を設定します。1.0 でバンク指定値を維持します。
        /// </summary>
        public void SetSf2EffectSendScale(float reverbScale, float chorusScale)
        {
            ThrowIfDisposed();
            if (!float.IsFinite(reverbScale))
                throw new ArgumentOutOfRangeException(nameof(reverbScale), "Must be finite");
            if (!float.IsFinite(chorusScale))
                throw new ArgumentOutOfRangeException(nameof(chorusScale), "Must be finite");
            var result = XAmeSetSf2EffectSendScale(_handle, reverbScale, chorusScale);
            if (result != XAmeResult.OK)
                throw new XArkMidiException(result, GetLastError());
        }

        /// <summary>
        /// Gets the current global SF2 reverb send scale.
        /// 現在の SF2 リバーブ send 倍率を取得します。
        /// </summary>
        public float Sf2ReverbSendScale
        {
            get {
                ThrowIfDisposed();
                return XAmeGetSf2ReverbSendScale(_handle);
            }
        }

        /// <summary>
        /// Gets the current global SF2 chorus send scale.
        /// 現在の SF2 コーラス send 倍率を取得します。
        /// </summary>
        public float Sf2ChorusSendScale
        {
            get {
                ThrowIfDisposed();
                return XAmeGetSf2ChorusSendScale(_handle);
            }
        }

        /// <summary>
        /// Set internal effect mix scales. 1.0 keeps the engine defaults unchanged.
        /// 内部エフェクトのミックス倍率を設定します。1.0 で既定値を維持します。
        /// </summary>
        public void SetEffectMixScale(float reverbReturnScale, float chorusReturnScale,
                                      float masterReverbSendScale, float chorusToReverbScale)
        {
            ThrowIfDisposed();
            if (!float.IsFinite(reverbReturnScale))
                throw new ArgumentOutOfRangeException(nameof(reverbReturnScale), "Must be finite");
            if (!float.IsFinite(chorusReturnScale))
                throw new ArgumentOutOfRangeException(nameof(chorusReturnScale), "Must be finite");
            if (!float.IsFinite(masterReverbSendScale))
                throw new ArgumentOutOfRangeException(nameof(masterReverbSendScale), "Must be finite");
            if (!float.IsFinite(chorusToReverbScale))
                throw new ArgumentOutOfRangeException(nameof(chorusToReverbScale), "Must be finite");
            var result = XAmeSetEffectMixScale(
                _handle,
                reverbReturnScale,
                chorusReturnScale,
                masterReverbSendScale,
                chorusToReverbScale);
            if (result != XAmeResult.OK)
                throw new XArkMidiException(result, GetLastError());
        }

        public float ReverbReturnScale
        {
            get {
                ThrowIfDisposed();
                return XAmeGetReverbReturnScale(_handle);
            }
        }

        public float ChorusReturnScale
        {
            get {
                ThrowIfDisposed();
                return XAmeGetChorusReturnScale(_handle);
            }
        }

        public float MasterReverbSendScale
        {
            get {
                ThrowIfDisposed();
                return XAmeGetMasterReverbSendScale(_handle);
            }
        }

        public float ChorusToReverbScale
        {
            get {
                ThrowIfDisposed();
                return XAmeGetChorusToReverbScale(_handle);
            }
        }

        /// <summary>
        /// Set final output gain scale. 1.0 keeps the engine default unchanged.
        /// 最終出力ゲイン倍率を設定します。1.0 で既定値を維持します。
        /// </summary>
        public void SetOutputGainScale(float scale)
        {
            ThrowIfDisposed();
            if (!float.IsFinite(scale))
                throw new ArgumentOutOfRangeException(nameof(scale), "Must be finite");
            var result = XAmeSetOutputGainScale(_handle, scale);
            if (result != XAmeResult.OK)
                throw new XArkMidiException(result, GetLastError());
        }

        public float OutputGainScale
        {
            get {
                ThrowIfDisposed();
                return XAmeGetOutputGainScale(_handle);
            }
        }

        /// <summary>
        /// Get one 32-bit word from the active key bitset for a MIDI channel.
        /// アクティブキーのビットセット 32bit 分を取得します。
        /// </summary>
        public uint GetChannelActiveKeyMaskWord(uint channel, uint wordIndex)
        {
            ThrowIfDisposed();
            return XAmeGetChannelActiveKeyMaskWord(_handle, channel, wordIndex);
        }

        /// <summary>
        /// Pop the oldest queued channel key event.
        /// 最も古いチャンネルキーイベントを取り出します。
        /// </summary>
        /// <returns><see langword="true"/> if an event was returned. イベントが返された場合は true です。</returns>
        public bool TryPopChannelKeyEvent(out ChannelKeyEvent channelKeyEvent)
        {
            ThrowIfDisposed();
            return XAmePopChannelKeyEvent(_handle, out channelKeyEvent) != 0;
        }

        /// <summary>
        /// Get the current rendered frame position.
        /// 現在のレンダリング済みフレーム位置を取得します。
        /// </summary>
        public ulong CurrentFramePosition
        {
            get
            {
                ThrowIfDisposed();
                return XAmeGetCurrentFramePosition(_handle);
            }
        }

        /// <summary>
        /// Get an estimated total song length in frames, excluding tail effects.
        /// エフェクトテールを除く概算の総フレーム長を取得します。
        /// </summary>
        public ulong LengthFramesEstimate
        {
            get
            {
                ThrowIfDisposed();
                return XAmeGetLengthFramesEstimate(_handle);
            }
        }

        /// <summary>
        /// Get output-stage meter values from the most recent render call.
        /// 直近のレンダリング呼び出しの出力段メーター値を取得します。
        /// </summary>
        public OutputStageMeter GetOutputStageMeter()
        {
            ThrowIfDisposed();
            var result = XAmeGetOutputStageMeter(_handle, out var meter);
            if (result != XAmeResult.OK)
                throw new XArkMidiException(result, GetLastError());
            return meter;
        }

        /// <summary>
        /// Try to get output-stage meter values from the most recent render call.
        /// 直近のレンダリング呼び出しの出力段メーター値の取得を試みます。
        /// </summary>
        public bool TryGetOutputStageMeter(out OutputStageMeter meter)
        {
            ThrowIfDisposed();
            try {
                var result = XAmeGetOutputStageMeter(_handle, out meter);
                return result == XAmeResult.OK;
            } catch (EntryPointNotFoundException) {
                meter = default;
                return false;
            }
        }

        /// <summary>
        /// Render the full MIDI stream into a newly allocated sample array.
        /// MIDI 全体を新規確保したサンプル配列へレンダリングします。
        /// </summary>
        /// <param name="chunkFrames">Chunk size used for incremental rendering. 分割レンダリング時のチャンクサイズです。</param>
        /// <returns>Interleaved PCM samples. インターリーブ済み PCM サンプルを返します。</returns>
        public short[] RenderAll(uint chunkFrames = 4096)
        {
            ThrowIfDisposed();
            var buf = new short[chunkFrames * _numChannels];
            var result = new short[buf.Length];
            int totalSamples = 0;

            while (!IsFinished)
            {
                uint written = Render(buf, chunkFrames);
                if (written == 0) break;

                int writtenSamples = checked((int)(written * _numChannels));
                int requiredSamples = checked(totalSamples + writtenSamples);
                if (requiredSamples > result.Length)
                {
                    int newLength = result.Length;
                    while (newLength < requiredSamples)
                    {
                        newLength = checked(newLength * 2);
                    }
                    Array.Resize(ref result, newLength);
                }

                Array.Copy(buf, 0, result, totalSamples, writtenSamples);
                totalSamples = requiredSamples;
            }

            Array.Resize(ref result, totalSamples);
            return result;
        }

        /// <summary>
        /// Render the full MIDI stream into a little-endian PCM byte array.
        /// MIDI 全体をリトルエンディアン PCM バイト列へレンダリングします。
        /// </summary>
        /// <param name="chunkFrames">Chunk size used for incremental rendering. 分割レンダリング時のチャンクサイズです。</param>
        public byte[] RenderAllBytes(uint chunkFrames = 4096)
        {
            var samples = RenderAll(chunkFrames);
            var bytes = new byte[samples.Length * sizeof(short)];
            Buffer.BlockCopy(samples, 0, bytes, 0, bytes.Length);
            return bytes;
        }

        /// <summary>
        /// Release the native engine handle.
        /// ネイティブエンジンハンドルを解放します。
        /// </summary>
        public void Dispose()
        {
            if (!_disposed)
            {
                if (_handle != IntPtr.Zero)
                {
                    XAmeDestroyEngine(_handle);
                    _handle = IntPtr.Zero;
                }
                _disposed = true;
            }
        }

        private void ThrowIfDisposed()
        {
            if (_disposed)
                throw new ObjectDisposedException(nameof(Engine));
        }
    }

    /// <summary>
    /// Exception thrown when the native library reports an error.
    /// ネイティブライブラリがエラーを返した際の例外です。
    /// </summary>
    public class XArkMidiException : Exception
    {
        /// <summary>
        /// Native error code returned by the engine.
        /// エンジンが返したネイティブエラーコードです。
        /// </summary>
        public XAmeResult ErrorCode { get; }

        /// <summary>
        /// Create an exception from a native error code and message.
        /// ネイティブエラーコードとメッセージから例外を生成します。
        /// </summary>
        public XArkMidiException(XAmeResult code, string message)
            : base($"[{code}] {message}")
        {
            ErrorCode = code;
        }
    }
}

