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
        /// <summary>Multiply SF2 preset send with MIDI channel send instead of summing them. SF2 の send と MIDI チャンネル send を加算ではなく乗算で合成します。</summary>
        MultiplySf2MidiEffectsSends = 1 << 2,
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
    private static extern XAmeResult XAmeSetChannelMuteMask(
        IntPtr engine,
        uint channelMask);

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
    private static extern uint XAmeGetChannelActiveKeyMaskWord(IntPtr engine, uint channel, uint wordIndex);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int XAmePopChannelKeyEvent(IntPtr engine, out ChannelKeyEvent outEvent);

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

