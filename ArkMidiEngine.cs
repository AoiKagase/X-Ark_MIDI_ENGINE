using System;
using System.Runtime.InteropServices;

public static class ArkMidiEngine
{
    private const string DllName = "ArkMidiEngine.dll";

    public enum AmeResult : int
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

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern AmeResult AmeCreateEngineFromPaths(
        string midiPath,
        string soundBankPath,
        SoundBankKind soundBankKind,
        uint sampleRate,
        uint numChannels,
        out IntPtr outEngine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern AmeResult AmeRender(
        IntPtr engine,
        short[] outBuffer,
        uint numFrames,
        out uint outWritten);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int AmeIsFinished(IntPtr engine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern void AmeDestroyEngine(IntPtr engine);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr AmeGetVersion();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr AmeGetLastError();

    public static string GetVersion()
        => Marshal.PtrToStringAnsi(AmeGetVersion()) ?? string.Empty;

    public static string GetLastError()
        => Marshal.PtrToStringAnsi(AmeGetLastError()) ?? string.Empty;

    public sealed class Engine : IDisposable
    {
        private IntPtr _handle;
        private bool _disposed;
        private readonly uint _numChannels;

        public Engine(string midiPath, string soundBankPath,
                      SoundBankKind soundBankKind = SoundBankKind.Auto,
                      uint sampleRate = 44100, uint numChannels = 2)
        {
            if (string.IsNullOrWhiteSpace(midiPath))
                throw new ArgumentException("midiPath is null or empty", nameof(midiPath));
            if (string.IsNullOrWhiteSpace(soundBankPath))
                throw new ArgumentException("soundBankPath is null or empty", nameof(soundBankPath));
            if (numChannels < 1 || numChannels > 2)
                throw new ArgumentOutOfRangeException(nameof(numChannels), "Must be 1 or 2");

            var result = AmeCreateEngineFromPaths(
                midiPath,
                soundBankPath,
                soundBankKind,
                sampleRate,
                numChannels,
                out _handle);

            if (result != AmeResult.OK)
                throw new ArkMidiException(result, GetLastError());

            _numChannels = numChannels;
        }

        public uint Render(short[] buffer, uint numFrames)
        {
            ThrowIfDisposed();
            var requiredSamples = checked((int)(numFrames * _numChannels));
            if (buffer == null || buffer.Length < requiredSamples)
                throw new ArgumentException("buffer is smaller than required", nameof(buffer));

            var result = AmeRender(_handle, buffer, numFrames, out uint written);
            if (result != AmeResult.OK)
                throw new ArkMidiException(result, GetLastError());
            return written;
        }

        public bool IsFinished
        {
            get
            {
                if (_disposed) return true;
                return AmeIsFinished(_handle) != 0;
            }
        }

        public short[] RenderAll(uint chunkFrames = 4096)
        {
            ThrowIfDisposed();
            var chunks = new System.Collections.Generic.List<short[]>();
            uint totalFrames = 0;
            var buf = new short[chunkFrames * _numChannels];

            while (!IsFinished)
            {
                uint written = Render(buf, chunkFrames);
                if (written == 0) break;

                var chunk = new short[written * _numChannels];
                Array.Copy(buf, chunk, (int)(written * _numChannels));
                chunks.Add(chunk);
                totalFrames += written;
            }

            var result = new short[totalFrames * _numChannels];
            int offset = 0;
            foreach (var c in chunks)
            {
                Array.Copy(c, 0, result, offset, c.Length);
                offset += c.Length;
            }
            return result;
        }

        public void Dispose()
        {
            if (!_disposed)
            {
                if (_handle != IntPtr.Zero)
                {
                    AmeDestroyEngine(_handle);
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

    public class ArkMidiException : Exception
    {
        public AmeResult ErrorCode { get; }

        public ArkMidiException(AmeResult code, string message)
            : base($"[{code}] {message}")
        {
            ErrorCode = code;
        }
    }
}
