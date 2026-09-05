using System.Diagnostics;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;

namespace EtwFpsHelper;

[SupportedOSPlatform("windows")]
internal static class Program
{
    private const string SharedMemoryName = "Esp32FpsSharedMem";

    private static readonly HashSet<string> DesktopApps = new(StringComparer.OrdinalIgnoreCase)
    {
        "chrome", "msedge", "firefox", "opera", "brave", "vivaldi", "iexplore", "chromium", "librewolf", "arc", "waterfox", "thorium",
        "WindowsTerminal", "conhost", "cmd", "powershell", "pwsh", "alacritty", "wezterm-gui", "mintty",
        "discord", "slack", "Teams", "ms-teams", "Spotify", "WhatsApp", "Signal", "Telegram", "Code", "Notion", "obsidian", "zoom", "Skype", "Element", "Viber", "LINE", "WeChat",
        "notepad", "notepad++", "sublime_text", "devenv", "rider64", "idea64", "pycharm64", "webstorm64",
        "steam", "steamwebhelper", "EpicGamesLauncher", "Battle.net", "GalaxyClient", "EADesktop", "UbisoftConnect",
        "vlc", "mpv", "mpc-hc64", "mpc-be64", "PotPlayerMini64", "wmplayer", "smplayer", "foobar2000", "aimp",
        "WINWORD", "EXCEL", "POWERPNT", "OUTLOOK", "Acrobat", "AcroRd32", "SumatraPDF", "thunderbird", "Mailspring", "OneNote", "GitHubDesktop", "7zFM", "WinRAR", "SnippingTool",
        "explorer", "ShellExperienceHost", "SearchHost", "StartMenuExperienceHost", "ApplicationFrameHost", "SystemSettings", "Taskmgr",
        "GHelper", "python", "py", "EtwFpsHelper"
    };

    [DllImport("user32.dll")]
    private static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    private static async Task Main()
    {
        using var monitor = new EtwFpsMonitor();
        using var mmf = MemoryMappedFile.CreateOrOpen(SharedMemoryName, 256);
        using var accessor = mmf.CreateViewAccessor();

        var monitorTask = Task.Run(() => monitor.Start());
        int lastPid = -1;

        try
        {
            while (true)
            {
                GetWindowThreadProcessId(GetForegroundWindow(), out uint rawPid);
                int pid = (int)rawPid;
                int targetPid = IsDesktopApp(pid) ? 0 : pid;

                if (targetPid != lastPid)
                {
                    lastPid = targetPid;
                    monitor.TargetPid = targetPid;
                }

                int fps = targetPid > 0 ? (int)Math.Round(monitor.SampleFps()) : 0;
                accessor.Write(0, fps);
                accessor.Write(4, targetPid);
                accessor.Write(8, monitor.IsRunning ? 1 : monitor.ErrorCode);
                accessor.Flush();

                await Task.Delay(300);
            }
        }
        finally
        {
            monitor.Stop();
            await monitorTask.WaitAsync(TimeSpan.FromSeconds(2));
        }
    }

    private static bool IsDesktopApp(int pid)
    {
        if (pid <= 0 || pid == Environment.ProcessId)
            return true;

        try
        {
            using var process = Process.GetProcessById(pid);
            return DesktopApps.Contains(process.ProcessName);
        }
        catch
        {
            return true;
        }
    }
}

internal sealed class EtwFpsMonitor : IDisposable
{
    private const uint ErrorSuccess = 0;
    private const uint ErrorAlreadyExists = 0xB7;
    private const uint EventControlCodeEnableProvider = 1;
    private const uint EventControlCodeDisableProvider = 0;
    private const uint EventTraceControlFlush = 3;
    private const byte TraceLevelInformation = 4;
    private const uint ProcessTraceModeRealTime = 0x00000100;
    private const uint ProcessTraceModeEventRecord = 0x10000000;
    private const uint ProcessTraceModeRawTimestamp = 0x00001000;
    private const uint WnodeFlagTracedGuid = 0x00020000;
    private const int EventDxgiPresentStart = 42;

    private static readonly Guid DxgiProviderId = new("CA11C036-0102-4A2D-A6AD-F03CFED5D3C9");
    private static readonly Guid DxgKrnlProviderId = new("802EC45A-1E99-4B83-9920-87C98277BA9D");

    private const ushort DxgKrnlTaskFlip = 14;
    private const byte DxgKrnlOpcodeStart = 1;
    private const ulong DxgKrnlKeywordPresent = 0x0000040000000000UL;
    private const uint EventFilterTypePid = 0x80000004;
    private const uint EventFilterTypeEventId = 0x80000200;
    private const uint EnableTraceParametersVersion2 = 2;
    private const string SessionName = "Esp32GHelperFpsSession";

    private const int RollingWindowSize = 360;
    private readonly long[] _frameTimes = new long[RollingWindowSize];
    private volatile int _frameHead;
    private volatile int _framesFilled;
    private volatile int _targetPid = -1;
    private int _lastTargetPid;
    private bool _dxgiActiveForCurrentPid;
    private long _sessionHandle;
    private long _traceHandle;
    private Timer? _flushTimer;
    private EventRecordCallback? _callbackRef;
    private int _stopped;

    public bool IsRunning { get; private set; }
    public int ErrorCode { get; private set; }

    public int TargetPid
    {
        get => _targetPid;
        set
        {
            if (_targetPid == value)
                return;

            _targetPid = value;
            _frameHead = 0;
            _framesFilled = 0;
            _dxgiActiveForCurrentPid = false;

            if (_sessionHandle == 0)
                return;

            if (value <= 0)
                PauseProviders();
            else
                ApplyKernelFilters(value);
        }
    }

    public void Start()
    {
        var staleProps = BuildSessionProperties();
        StopTrace(0, SessionName, ref staleProps);

        var props = BuildSessionProperties();
        uint result = StartTrace(out _sessionHandle, SessionName, ref props);
        if (result == ErrorAlreadyExists)
        {
            StopTrace(0, SessionName, ref staleProps);
            result = StartTrace(out _sessionHandle, SessionName, ref props);
        }

        if (result != ErrorSuccess)
        {
            ErrorCode = -(int)result;
            return;
        }

        EnableTraceEx2(_sessionHandle, DxgiProviderId, EventControlCodeEnableProvider,
            TraceLevelInformation, 0, 0, 0, IntPtr.Zero);
        EnableTraceEx2(_sessionHandle, DxgKrnlProviderId, EventControlCodeEnableProvider,
            TraceLevelInformation, DxgKrnlKeywordPresent, 0, 0, IntPtr.Zero);

        int targetPid = _targetPid;
        if (targetPid > 0)
            ApplyKernelFilters(targetPid);
        else
            PauseProviders();

        _callbackRef = OnEventRecord;
        IntPtr loggerName = Marshal.StringToHGlobalUni(SessionName);
        try
        {
            var logfile = new EventTraceLogfile
            {
                LoggerName = loggerName,
                ProcessTraceMode = ProcessTraceModeRealTime |
                                   ProcessTraceModeEventRecord |
                                   ProcessTraceModeRawTimestamp,
                EventRecordCallback = Marshal.GetFunctionPointerForDelegate(_callbackRef)
            };
            _traceHandle = OpenTrace(ref logfile);
        }
        finally
        {
            Marshal.FreeHGlobal(loggerName);
        }

        if (_traceHandle == 0 || _traceHandle == -1)
        {
            ErrorCode = -2;
            Stop();
            return;
        }

        IsRunning = true;
        ErrorCode = 1;
        _flushTimer = new Timer(_ => FlushSession(), null, 200, 200);
        ProcessTrace(new[] { _traceHandle }, 1, IntPtr.Zero, IntPtr.Zero);
        IsRunning = false;
    }

    public double SampleFps()
    {
        int filled = _framesFilled;
        if (filled < 2)
            return 0;

        long frequency = Stopwatch.Frequency;
        int head = _frameHead;
        long newest = _frameTimes[(head - 1 + RollingWindowSize) % RollingWindowSize];
        if (Stopwatch.GetTimestamp() - newest > 4 * frequency)
            return 0;

        long cutoff = newest - frequency;
        int count = 1;
        long oldest = newest;
        for (int i = 2; i <= filled; i++)
        {
            long timestamp = _frameTimes[(head - i + RollingWindowSize) % RollingWindowSize];
            if (timestamp < cutoff)
                break;
            oldest = timestamp;
            count++;
        }

        double elapsed = (double)(newest - oldest) / frequency;
        return elapsed > 0 ? (count - 1) / elapsed : 0;
    }

    public void Stop()
    {
        if (Interlocked.Exchange(ref _stopped, 1) != 0)
            return;

        _flushTimer?.Dispose();
        _flushTimer = null;

        if (_traceHandle != 0 && _traceHandle != -1)
            CloseTrace(_traceHandle);

        if (_sessionHandle != 0)
        {
            PauseProviders();
            var props = BuildSessionProperties();
            StopTrace(_sessionHandle, SessionName, ref props);
        }

        IsRunning = false;
    }

    public void Dispose() => Stop();

    private void OnEventRecord(ref EventRecord record)
    {
        bool isDxgiPresent = record.EventHeader.ProviderId == DxgiProviderId &&
                             record.EventHeader.Id == EventDxgiPresentStart;
        bool isDxgKrnlPresent = record.EventHeader.ProviderId == DxgKrnlProviderId &&
                                record.EventHeader.Task == DxgKrnlTaskFlip &&
                                record.EventHeader.Opcode == DxgKrnlOpcodeStart;

        if (!isDxgiPresent && !isDxgKrnlPresent)
            return;

        int targetPid = _targetPid;
        if (targetPid <= 0 || (int)record.EventHeader.ProcessId != targetPid)
            return;

        if (isDxgiPresent && record.UserDataLength >= 12)
        {
            uint dxgiFlags = (uint)Marshal.ReadInt32(record.UserData, 8);
            if ((dxgiFlags & 0x1) != 0)
                return;
        }

        if (isDxgiPresent)
            _dxgiActiveForCurrentPid = true;
        else if (_dxgiActiveForCurrentPid)
            return;

        if (targetPid != _lastTargetPid)
        {
            _lastTargetPid = targetPid;
            _frameHead = 0;
            _framesFilled = 0;
            return;
        }

        _frameTimes[_frameHead] = record.EventHeader.TimeStamp;
        _frameHead = (_frameHead + 1) % RollingWindowSize;
        if (_framesFilled < RollingWindowSize)
            _framesFilled++;
    }

    private void PauseProviders()
    {
        EnableTraceEx2(_sessionHandle, DxgiProviderId, EventControlCodeDisableProvider, 0, 0, 0, 0, IntPtr.Zero);
        EnableTraceEx2(_sessionHandle, DxgKrnlProviderId, EventControlCodeDisableProvider, 0, 0, 0, 0, IntPtr.Zero);
    }

    private void ApplyKernelFilters(int pid)
    {
        EnableProviderWithFilters(DxgiProviderId, 0, pid, true);
        EnableProviderWithFilters(DxgKrnlProviderId, DxgKrnlKeywordPresent, pid, false);
    }

    private void EnableProviderWithFilters(Guid providerId, ulong keyword, int pid, bool addEventIdFilter)
    {
        int filterCount = addEventIdFilter ? 2 : 1;
        int descriptorSize = Marshal.SizeOf<EventFilterDescriptor>();
        IntPtr descriptors = Marshal.AllocHGlobal(filterCount * descriptorSize);
        IntPtr pidBuffer = Marshal.AllocHGlobal(sizeof(uint));
        IntPtr eventIdBuffer = addEventIdFilter ? Marshal.AllocHGlobal(8) : IntPtr.Zero;
        IntPtr parametersBuffer = Marshal.AllocHGlobal(Marshal.SizeOf<EnableTraceParameters>());

        try
        {
            Marshal.WriteInt32(pidBuffer, pid);
            var pidDescriptor = new EventFilterDescriptor
            {
                Ptr = (ulong)pidBuffer.ToInt64(),
                Size = sizeof(uint),
                Type = EventFilterTypePid
            };
            Marshal.StructureToPtr(pidDescriptor, descriptors, false);

            if (addEventIdFilter)
            {
                Marshal.WriteByte(eventIdBuffer, 0, 1);
                Marshal.WriteByte(eventIdBuffer, 1, 0);
                Marshal.WriteInt16(eventIdBuffer, 2, 1);
                Marshal.WriteInt16(eventIdBuffer, 4, EventDxgiPresentStart);
                var eventDescriptor = new EventFilterDescriptor
                {
                    Ptr = (ulong)eventIdBuffer.ToInt64(),
                    Size = 6,
                    Type = EventFilterTypeEventId
                };
                Marshal.StructureToPtr(eventDescriptor, descriptors + descriptorSize, false);
            }

            var enableParameters = new EnableTraceParameters
            {
                Version = EnableTraceParametersVersion2,
                EnableFilterDesc = descriptors,
                FilterDescCount = (uint)filterCount
            };
            Marshal.StructureToPtr(enableParameters, parametersBuffer, false);

            EnableTraceEx2(_sessionHandle, providerId, EventControlCodeEnableProvider,
                TraceLevelInformation, keyword, 0, 0, parametersBuffer);
        }
        finally
        {
            Marshal.FreeHGlobal(parametersBuffer);
            if (eventIdBuffer != IntPtr.Zero)
                Marshal.FreeHGlobal(eventIdBuffer);
            Marshal.FreeHGlobal(pidBuffer);
            Marshal.FreeHGlobal(descriptors);
        }
    }

    private void FlushSession()
    {
        if (_sessionHandle == 0)
            return;
        var props = BuildSessionProperties();
        ControlTrace(_sessionHandle, null, ref props, EventTraceControlFlush);
    }

    private static EventTraceProperties BuildSessionProperties() => new()
    {
        Wnode = new WnodeHeader
        {
            BufferSize = (uint)Marshal.SizeOf<EventTraceProperties>(),
            Flags = WnodeFlagTracedGuid,
            ClientContext = 1
        },
        LogFileMode = ProcessTraceModeRealTime,
        LoggerNameOffset = (uint)Marshal.OffsetOf<EventTraceProperties>(nameof(EventTraceProperties.LoggerName)),
        BufferSize = 8,
        MinimumBuffers = 8,
        MaximumBuffers = 16
    };

    [StructLayout(LayoutKind.Sequential)]
    private struct WnodeHeader
    {
        public uint BufferSize;
        public uint ProviderId;
        public ulong HistoricalContext;
        public ulong TimeStamp;
        public Guid Guid;
        public uint ClientContext;
        public uint Flags;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct EventTraceProperties
    {
        public WnodeHeader Wnode;
        public uint BufferSize;
        public uint MinimumBuffers;
        public uint MaximumBuffers;
        public uint MaximumFileSize;
        public uint LogFileMode;
        public uint FlushTimer;
        public uint EnableFlags;
        public int AgeLimit;
        public uint NumberOfBuffers;
        public uint FreeBuffers;
        public uint EventsLost;
        public uint BuffersWritten;
        public uint LogBuffersLost;
        public uint RealTimeBuffersLost;
        public IntPtr LoggerThreadId;
        public uint LogFileNameOffset;
        public uint LoggerNameOffset;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 1024)] public string LoggerName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 1024)] public string LogFileName;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct EventRecord
    {
        public EventHeader EventHeader;
        public EtwBufferContext BufferContext;
        public ushort ExtendedDataCount;
        public ushort UserDataLength;
        public IntPtr ExtendedData;
        public IntPtr UserData;
        public IntPtr UserContext;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct EventHeader
    {
        public ushort Size;
        public ushort HeaderType;
        public ushort Flags;
        public ushort EventProperty;
        public uint ThreadId;
        public uint ProcessId;
        public long TimeStamp;
        public Guid ProviderId;
        public ushort Id;
        public byte Version;
        public byte Channel;
        public byte Level;
        public byte Opcode;
        public ushort Task;
        public ulong Keyword;
        public uint KernelTime;
        public uint UserTime;
        public Guid ActivityId;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct EtwBufferContext
    {
        public byte ProcessorNumber;
        public byte Alignment;
        public ushort LoggerId;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct EventFilterDescriptor
    {
        public ulong Ptr;
        public uint Size;
        public uint Type;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct EnableTraceParameters
    {
        public uint Version;
        public uint EnableProperty;
        public uint ControlFlags;
        public Guid SourceId;
        public IntPtr EnableFilterDesc;
        public uint FilterDescCount;
    }

    [StructLayout(LayoutKind.Explicit, Size = 448)]
    private struct EventTraceLogfile
    {
        [FieldOffset(8)] public IntPtr LoggerName;
        [FieldOffset(28)] public uint ProcessTraceMode;
        [FieldOffset(400)] public IntPtr BufferCallback;
        [FieldOffset(424)] public IntPtr EventRecordCallback;
        [FieldOffset(440)] public IntPtr Context;
    }

    private delegate void EventRecordCallback([In] ref EventRecord eventRecord);

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode)]
    private static extern uint StartTrace(out long sessionHandle, string sessionName, ref EventTraceProperties properties);

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode)]
    private static extern uint StopTrace(long sessionHandle, string sessionName, ref EventTraceProperties properties);

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode)]
    private static extern uint ControlTrace(long sessionHandle, string? sessionName, ref EventTraceProperties properties, uint controlCode);

    [DllImport("advapi32.dll")]
    private static extern uint EnableTraceEx2(long sessionHandle, in Guid providerId, uint controlCode,
        byte level, ulong matchAnyKeyword, ulong matchAllKeyword, uint timeout, IntPtr enableParameters);

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode)]
    private static extern long OpenTrace(ref EventTraceLogfile logfile);

    [DllImport("advapi32.dll")]
    private static extern uint ProcessTrace(long[] handles, uint count, IntPtr startTime, IntPtr endTime);

    [DllImport("advapi32.dll")]
    private static extern uint CloseTrace(long traceHandle);
}
