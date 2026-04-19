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

    public enum XAmeResult : int
    {
        OK = 0,
        InvalidArg = -1,
        ParseMidi = -2,
        ParseSf2 = -3,
        OutOfMemory = -4,
        NotInitialized = -5,
        ParseDls = -6,
        Unsupported = -7,
        Io = -8,
    }

    public enum SoundBankKind : uint
    {
        Auto = 0,
        Sf2 = 1,
        Dls = 2,
    }

    [Flags]
    public enum CompatibilityFlags : uint
    {
        None = 0,
        Sf2ZeroLengthLoopRetrigger = 1 << 0,
        EnableSf2SamplePitchCorrection = 1 << 1,
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct CreateOptions
    {
        public uint StructSize;
        public ulong MaxSampleDataBytes;
        public uint MaxSf2PdtaEntries;
        public uint MaxDlsPoolTableEntries;
        public CompatibilityFlags CompatibilityFlags;

        public static CreateOptions Default()
            => new CreateOptions { StructSize = (uint)Marshal.SizeOf<CreateOptions>() };
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct ChannelKeyEvent
    {
        public byte Channel;
        public byte Key;
        public byte IsNoteOn;
        public byte Reserved;
        public ushort Velocity;
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

    public static string GetVersion()
        => Marshal.PtrToStringAnsi(XAmeGetVersion()) ?? string.Empty;

    public static string GetLastError()
        => Marshal.PtrToStringAnsi(XAmeGetLastError()) ?? string.Empty;

    public sealed class Engine : IDisposable
    {
        private IntPtr _handle;
        private bool _disposed;
        private readonly uint _numChannels;

        public Engine(string midiPath, string soundBankPath,
                      SoundBankKind soundBankKind = SoundBankKind.Auto,
                      uint sampleRate = 44100, uint numChannels = 2)
            : this(midiPath, soundBankPath, soundBankKind, sampleRate, numChannels, null)
        {
        }

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

        public bool IsFinished
        {
            get
            {
                if (_disposed) return true;
                return XAmeIsFinished(_handle) != 0;
            }
        }

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

        public int GetChannelProgram(uint channel)
        {
            ThrowIfDisposed();
            return XAmeGetChannelProgram(_handle, channel);
        }

        public uint GetChannelActiveNoteCount(uint channel)
        {
            ThrowIfDisposed();
            return XAmeGetChannelActiveNoteCount(_handle, channel);
        }

        public uint GetChannelActiveKeyMaskWord(uint channel, uint wordIndex)
        {
            ThrowIfDisposed();
            return XAmeGetChannelActiveKeyMaskWord(_handle, channel, wordIndex);
        }

        public bool TryPopChannelKeyEvent(out ChannelKeyEvent channelKeyEvent)
        {
            ThrowIfDisposed();
            return XAmePopChannelKeyEvent(_handle, out channelKeyEvent) != 0;
        }

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

        public byte[] RenderAllBytes(uint chunkFrames = 4096)
        {
            var samples = RenderAll(chunkFrames);
            var bytes = new byte[samples.Length * sizeof(short)];
            Buffer.BlockCopy(samples, 0, bytes, 0, bytes.Length);
            return bytes;
        }

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

    public class XArkMidiException : Exception
    {
        public XAmeResult ErrorCode { get; }

        public XArkMidiException(XAmeResult code, string message)
            : base($"[{code}] {message}")
        {
            ErrorCode = code;
        }
    }
}

