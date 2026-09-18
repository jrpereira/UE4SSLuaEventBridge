param(
    [int]$TargetProcessId,
    [ValidateSet("Fountain","Sweep")][string]$Schedule = "Fountain",
    [ValidateRange(1,60)][int]$SecondsPerStage = 5,
    [string]$ApplicationDirectory,
    [string]$Output = 'C:/TestResults/sender-results.csv',
    [ValidateRange(0,120)][int]$StartupSettleSeconds = 10,
    [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class BridgeBenchInput {
    [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT {
        public ushort vk, scan; public uint flags, time; public UIntPtr extra;
    }
    [StructLayout(LayoutKind.Explicit, Size=32)] public struct INPUTUNION {
        [FieldOffset(0)] public KEYBDINPUT keyboard;
    }
    [StructLayout(LayoutKind.Sequential)] public struct INPUT {
        public uint type; public INPUTUNION data;
    }
    [DllImport("user32.dll", SetLastError=true)]
    static extern uint SendInput(uint count, INPUT[] inputs, int size);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr CreateWaitableTimerEx(IntPtr attr, string name, uint flags, uint access);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool SetWaitableTimer(IntPtr timer, ref long due, int period, IntPtr callback, IntPtr arg, bool resume);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    static IntPtr timer;
    static bool spinOnly;
    public static void SetSpinMode(bool enabled){spinOnly=enabled;}
    public static void Pause(double seconds) {
        if(timer==IntPtr.Zero) timer=CreateWaitableTimerEx(IntPtr.Zero,null,2,0x1F0003);
        if(timer==IntPtr.Zero) throw new InvalidOperationException("High resolution timer unavailable: "+Marshal.GetLastWin32Error());
        long due=-(long)Math.Max(1,seconds*10000000);
        if(!SetWaitableTimer(timer,ref due,0,IntPtr.Zero,IntPtr.Zero,false) || WaitForSingleObject(timer,1000)!=0)
            throw new InvalidOperationException("High resolution timer wait failed");
    }
    static void Deadline(System.Diagnostics.Stopwatch clock, double deadline) {
        for(;;){
            double left=deadline-clock.Elapsed.TotalSeconds;
            if(left<=0)return;
            if(!spinOnly && left>0.002)Pause(left-0.001);else System.Threading.Thread.SpinWait(50);
        }
    }
    public static double Calibrate(int rate,int count,bool spin){
        spinOnly=spin;
        var clock=System.Diagnostics.Stopwatch.StartNew();
        for(int i=0;i<count*2;i++)Deadline(clock,clock.Elapsed.TotalSeconds+0.5/rate);
        spinOnly=false;return count/clock.Elapsed.TotalSeconds;
    }
    public static double[] Sweep(int pid,int count,int rate){
        var clock=System.Diagnostics.Stopwatch.StartNew();
        double previous=0,sum=0,max=0,half=0.5/rate;
        for(int i=0;i<count;i++){
            if(!HasFocus(pid))throw new InvalidOperationException("Fixture lost focus; sweep aborted");
            Key(0x78,true);double down=clock.Elapsed.TotalSeconds;
            if(i>0){double delta=(down-previous)*1000;sum+=delta;max=Math.Max(max,delta);}
            previous=down;Deadline(clock,down+half);
            Key(0x78,false);Deadline(clock,clock.Elapsed.TotalSeconds+half);
        }
        return new[]{clock.Elapsed.TotalSeconds,count>1?sum/(count-1):0,max};
    }
    public const int EventCount=600;
    public const int PoolSize=20;
    public const double HoldSeconds=0.100;
    public static int Rate(int index) {
        if(index<0 || index>=EventCount)throw new ArgumentOutOfRangeException("index");
        int slot=index%30, rate=4;
        while(slot>=rate){slot-=rate;rate++;}
        return rate;
    }
    public static double Interval(int index) { return 1.0/Rate(index); }
    public static double PlannedDown(int index) {
        int slot=index%30, rate=4;
        while(slot>=rate){slot-=rate;rate++;}
        // Place the final release at the end of the 100-second window.
        return 0.025+(index/30)*5.0+(rate-4)+(double)slot/rate;
    }
    public static double PacingSeconds { get { return PlannedDown(EventCount-1)+HoldSeconds; } }
    public static ushort KeyCode(int index) { return (ushort)(0x41+index%PoolSize); }
    public static double FountainStartedSeconds;
    public static double FountainFinishedSeconds;
    public static double TimestampSeconds() {
        return (double)System.Diagnostics.Stopwatch.GetTimestamp()/System.Diagnostics.Stopwatch.Frequency;
    }
    public static double[,] Fountain(int pid) {
        var rows=new double[EventCount,13];
        var clock=System.Diagnostics.Stopwatch.StartNew();
        FountainStartedSeconds=TimestampSeconds();
        int next=0, finished=0, active=0;
        double nextDown=PlannedDown(0);
        var due=new double[PoolSize];var pressed=new double[PoolSize];
        var owner=new int[PoolSize];
        for(int k=0;k<PoolSize;k++)owner[k]=-1;
        // Each key has an independent release deadline. Waiting for a key-up
        // must never block a different key's earlier key-down deadline.
        try {
            while(finished<EventCount){
                int releaseKey=-1;double releaseAt=double.PositiveInfinity;
                for(int k=0;k<PoolSize;k++){
                    if(owner[k]>=0 && due[k]<releaseAt){releaseAt=due[k];releaseKey=k;}
                }
                bool release=releaseKey>=0 && (next==EventCount || releaseAt<=nextDown);
                Deadline(clock,release ? releaseAt : nextDown);
                if(!HasFocus(pid))throw new InvalidOperationException(FocusError(pid)+"; next event="+next);
                if(release){
                    int index=owner[releaseKey];
                    double released=clock.Elapsed.TotalSeconds;
                    Key(KeyCode(index),false);
                    double elapsed=clock.Elapsed.TotalSeconds;
                    rows[index,0]=index+1;rows[index,1]=Interval(index);
                    rows[index,2]=PlannedDown(index)+HoldSeconds;
                    rows[index,3]=elapsed;rows[index,4]=elapsed-rows[index,2];
                    rows[index,5]=index/30+1;rows[index,6]=Rate(index)-3;
                    rows[index,7]=KeyCode(index);rows[index,8]=HoldSeconds;
                    rows[index,9]=released-pressed[releaseKey];
                    rows[index,10]=pressed[releaseKey];rows[index,11]=PlannedDown(index);
                    owner[releaseKey]=-1;finished++;active--;
                } else {
                    int key=next%PoolSize;
                    if(owner[key]>=0)throw new InvalidOperationException("Key re-pressed before release");
                    Key(KeyCode(next),true);
                    pressed[key]=clock.Elapsed.TotalSeconds;
                    due[key]=pressed[key]+HoldSeconds;owner[key]=next;active++;
                    rows[next,12]=active;
                    // Shift later presses when late rather than catching up in bursts.
                    nextDown=pressed[key]+Interval(next);next++;
                }
            }
        } finally {
            for(int k=0;k<PoolSize;k++)if(owner[k]>=0)Key(KeyCode(owner[k]),false);
        }
        FountainFinishedSeconds=TimestampSeconds();
        return rows;
    }
    public static void CloseTimer(){ if(timer!=IntPtr.Zero){CloseHandle(timer);timer=IntPtr.Zero;} }
    public static void Key(ushort vk, bool down) {
        var input = new INPUT { type=1 };
        input.data.keyboard.vk=vk;
        input.data.keyboard.flags=down ? 0u : 2u;
        if(SendInput(1, new[]{input}, Marshal.SizeOf(typeof(INPUT)))!=1)
            throw new InvalidOperationException("SendInput failed: " + Marshal.GetLastWin32Error());
    }
    public static string FocusError(int expected) {
        uint actual; GetWindowThreadProcessId(GetForegroundWindow(),out actual);
        string name="unknown", title="";
        try { var p=System.Diagnostics.Process.GetProcessById((int)actual);name=p.ProcessName;title=p.MainWindowTitle; } catch {}
        return "Fixture lost focus; expected pid="+expected+", foreground pid="+actual+", process="+name+", title="+title;
    }
    public static bool HasFocus(int pid) {
        uint actual; GetWindowThreadProcessId(GetForegroundWindow(),out actual);
        return actual==(uint)pid;
    }
}
"@
if ([IntPtr]::Size -ne 8 -or [Runtime.InteropServices.Marshal]::SizeOf([type][BridgeBenchInput+INPUT]) -ne 40) {
    throw 'Use 64-bit PowerShell; INPUT layout must be 40 bytes'
}
if ($ValidateOnly) {
    $expected=0.0; $perKey=@{}; $perSecond=@{}; $overlaps=0
    for($index=0;$index -lt [BridgeBenchInput]::EventCount;$index++){
        $interval=[BridgeBenchInput]::Interval($index)
        $rate=[BridgeBenchInput]::Rate($index)
        $key=[BridgeBenchInput]::KeyCode($index)
        $second=[int][Math]::Floor($index/30)*5+$rate-3
        if($second -ne ([int][Math]::Floor([BridgeBenchInput]::PlannedDown($index))+1)){throw 'Press outside its nominal second'}
        if($interval -lt 0.125 -or $interval -gt 0.25 -or [BridgeBenchInput]::HoldSeconds -ne 0.100){throw 'Invalid pacing/hold'}
        if($index -gt 0 -and [BridgeBenchInput]::PlannedDown($index) -lt ([BridgeBenchInput]::PlannedDown($index-1)+[BridgeBenchInput]::HoldSeconds)-1e-9){$overlaps++}
        if($index -ge 20 -and [BridgeBenchInput]::PlannedDown($index) -lt ([BridgeBenchInput]::PlannedDown($index-20)+[BridgeBenchInput]::HoldSeconds)){throw 'Same-key overlap'}
        $perKey[$key]++;$perSecond[$second]++;$expected+=$interval
    }
    if($perKey.Count -ne 20 -or @($perKey.Values|Where-Object {$_ -ne 30}).Count){throw 'Uneven key pool'}
    for($second=1;$second -le 100;$second++){
        if($perSecond[$second] -ne (($second-1)%5+4)){throw 'Incorrect presses per second'}
    }
    if([Math]::Abs($expected-100.0) -gt 0.0000001){throw 'Incorrect pacing sum'}
    if([Math]::Abs([BridgeBenchInput]::PacingSeconds-100) -gt 1e-9){throw 'Final release validation failed'}
    Write-Output "Sender compiled: 20 cycles, 600 taps, 20 keys, 30 taps/key, 4-8 presses/sec, 100ms holds, $overlaps planned overlapping starts; independent key-up deadlines; pacing=100s; no input sent"
    return
}
if($env:BRIDGE_BENCH_GUEST -ne '1' -or -not (Test-Path C:/TestInput/manifest.json)) {throw 'Guest-only sender'}
$process = Get-Process -Id $TargetProcessId
$root=[IO.Path]::GetFullPath($ApplicationDirectory).TrimEnd('\')+'\'
if(-not $root.StartsWith('C:\Work\Application\',[StringComparison]::OrdinalIgnoreCase) -or
   -not $process.Path.StartsWith($root,[StringComparison]::OrdinalIgnoreCase)) {throw 'Target is outside staged application'}
function Wait-Marker([string]$Name,[int]$Timeout=30) {
    $deadline=[DateTime]::UtcNow.AddSeconds($Timeout)
    do {
        if(Test-Path C:/TestResults/failure.txt){throw (Get-Content C:/TestResults/failure.txt -Raw)}
        if($process.HasExited){throw 'Application exited'}
        if(Test-Path "C:/TestResults/$Name"){return}
        Start-Sleep -Milliseconds 200
    } while([DateTime]::UtcNow -lt $deadline)
    throw "Timeout waiting for $Name"
}
Wait-Marker 'ready.txt' 120
$process.Refresh()
if(-not [BridgeBenchInput]::SetForegroundWindow($process.MainWindowHandle)){throw 'Cannot focus test application'}
function Check-Focus {
    if (-not [BridgeBenchInput]::HasFocus($TargetProcessId)) { throw 'Fixture lost focus; sweep aborted' }
}
function Control-Key([uint16]$VirtualKey) {
    Check-Focus
    try {
        [BridgeBenchInput]::Key($VirtualKey, $true)
        Start-Sleep -Milliseconds 100
    } finally { [BridgeBenchInput]::Key($VirtualKey, $false) }
}
if($Schedule -eq 'Fountain') {
    # Sleep with a short finishing spin, not a full-run busy loop that competes
    # with software rendering. Buffer timing rows; write only after sending.
    [BridgeBenchInput]::SetSpinMode($false)
    Control-Key 0x77
    Wait-Marker 'armed.txt'
    # Rendering setup may asynchronously recreate the application window.
    Start-Sleep -Seconds $StartupSettleSeconds
    $process.Refresh()
    if(-not [BridgeBenchInput]::SetForegroundWindow($process.MainWindowHandle)){throw 'Cannot refocus configured fixture'}
    Start-Sleep -Milliseconds 250
    Check-Focus
    Control-Key 0x76
    Wait-Marker 'begin-1.txt'
    # Process.TotalProcessorTime uses Windows process user+kernel accounting.
    # Refresh immediately before the fountain; end counter is captured inside
    # Unreal after the final callback workload/bookkeeping, before acknowledgement.
    $process.Refresh()
    $cpuStart=$process.TotalProcessorTime.Ticks
    try { $trace=[BridgeBenchInput]::Fountain($TargetProcessId) }
    finally {
        # Fountain releases only keys it still owns on failure.
        [BridgeBenchInput]::CloseTimer()
    }
    $eventCount=[BridgeBenchInput]::EventCount
    $last=$eventCount-1
    $traceRows=for($index=0;$index -lt $eventCount;$index++){
        [pscustomobject]@{event=$trace[$index,0];interval_seconds=$trace[$index,1];
            minimum_seconds=$trace[$index,2];elapsed_seconds=$trace[$index,3];excess_elapsed_seconds=$trace[$index,4];
            cycle=$trace[$index,5];second_in_cycle=$trace[$index,6];key=[string][char][int]$trace[$index,7];
            requested_hold_seconds=$trace[$index,8];observed_hold_lower_bound_seconds=$trace[$index,9];
            actual_down_seconds=$trace[$index,10];planned_down_seconds=$trace[$index,11];active_keys_after_down=$trace[$index,12]}
    }
    $traceRows | Export-Csv -NoTypeInformation C:/TestResults/fountain-timing.csv
    # Completion is published by callback 600, never inferred from a fixed drain.
    # Read the callback timestamp so marker polling does not enter the measurement.
    function Export-Accounting([string]$Status) {
        $cpuEnd=[long](Get-Content C:/TestResults/process-cpu-end.txt -Raw)
        if($cpuEnd -lt $cpuStart){throw 'Non-monotonic process CPU accounting'}
        $execution=($cpuEnd-$cpuStart)/10000000.0
        [pscustomobject]@{status=$Status;execution_cpu_seconds=$execution;
            pacing_seconds=$trace[$last,2];processing_seconds=($execution+$trace[$last,2]);
            cpu_start_ticks=$cpuStart;cpu_end_ticks=$cpuEnd;
            scope='Unreal process all threads; sender excluded';
            boundary=if($Status -eq 'complete'){'before fountain to final callback finished'}else{'before fountain to post-timeout diagnostic snapshot'}} |
            Export-Csv -NoTypeInformation C:/TestResults/process-accounting.csv
    }
    try { Wait-Marker 'fountain-complete.txt' 30 }
    catch {
        $completionError=$_
        Control-Key 0x79
        Wait-Marker 'end-1.txt'
        Export-Accounting 'incomplete'
        [pscustomobject]@{schedule='Fountain';status='incomplete';sent_events=$eventCount;
            received_callbacks=[int](Get-Content C:/TestResults/end-1.txt);
            minimum_seconds=$trace[$last,2];sender_elapsed_seconds=$trace[$last,3]} |
            Export-Csv -NoTypeInformation -LiteralPath $Output
        throw $completionError
    }
    $completed=[double]::Parse((Get-Content C:/TestResults/fountain-complete.txt -Raw),[Globalization.CultureInfo]::InvariantCulture)
    $totalElapsed=[Math]::Max($completed,[BridgeBenchInput]::FountainFinishedSeconds)-[BridgeBenchInput]::FountainStartedSeconds
    if($completed -lt [BridgeBenchInput]::FountainStartedSeconds){throw 'Invalid callback completion timestamp'}
    Control-Key 0x79
    Wait-Marker 'end-1.txt'
    Export-Accounting 'complete'
    $received=[int](Get-Content C:/TestResults/end-1.txt)
    [pscustomobject]@{schedule='Fountain';sent_events=$eventCount;received_callbacks=$received;
        delivery_matches=($received -eq $eventCount);minimum_seconds=$trace[$last,2];
        sender_elapsed_seconds=$trace[$last,3];elapsed_seconds=$totalElapsed;
        excess_elapsed_seconds=($totalElapsed-$trace[$last,2])} |
        Export-Csv -NoTypeInformation -LiteralPath $Output
    if($received -ne $eventCount){throw 'Callback count mismatch; comparison is invalid'}
    Write-Output "Fountain completed: $eventCount events sent; $received callbacks; elapsed time includes final callback completion"
    return
}
$calibration=@()
try {
    foreach($rate in 10,100){foreach($spin in $false,$true){
        $calibration += [pscustomobject]@{target_rate=$rate;spin_only=$spin;actual_rate=[BridgeBenchInput]::Calibrate($rate,$rate,$spin)}
    }}
    $calibration | Export-Csv -NoTypeInformation C:/TestResults/pacing-calibration.csv
    $timerRate=($calibration | Where-Object {$_.target_rate -eq 100 -and -not $_.spin_only}).actual_rate
    $spinRate=($calibration | Where-Object {$_.target_rate -eq 100 -and $_.spin_only}).actual_rate
    $useSpin=$spinRate -gt $timerRate
    [BridgeBenchInput]::SetSpinMode($useSpin)
    "spin_only=$useSpin (selected by input-free calibration)" | Set-Content C:/TestResults/pacing-mode.txt
} finally { [BridgeBenchInput]::CloseTimer() }
$rows = @()
Control-Key 0x77 # F8: initialize native bridge subscription
Wait-Marker 'armed.txt'
try {
    foreach ($rate in 10,20,30,40,50,60,70,80,90,100) {
        Control-Key 0x76 # F7: begin stage
        Wait-Marker ("begin-{0}.txt" -f ($rate/10))
        $count = $rate * $SecondsPerStage
        $stats=[BridgeBenchInput]::Sweep($TargetProcessId,$count,$rate)
        $elapsed=$stats[0]
        # Permit pending bridge callbacks to drain before closing this stage.
        Start-Sleep -Milliseconds 500
        Control-Key 0x79 # F10: finish and write callback metrics
        Wait-Marker ("end-{0}.txt" -f ($rate/10))
        $received=[int](Get-Content ("C:/TestResults/end-{0}.txt" -f ($rate/10)))
        $rows += [pscustomobject]@{
            received_callbacks = $received
            delivery_matches = ($received -eq $count)
            rate_matches = ([Math]::Abs(($count/$elapsed)/$rate - 1) -le 0.05)
            target_presses_per_second = $rate
            sent_presses = $count
            send_seconds = $elapsed
            actual_presses_per_second = $count / $elapsed
            mean_press_interval_ms = $stats[1]
            max_press_interval_ms = $stats[2]
        }
        $rows | Export-Csv -NoTypeInformation -LiteralPath $Output
        Write-Output ("Target {0}/s: sent {1}, actual {2:F2}/s" -f $rate,$count,($count/$elapsed))
        Start-Sleep -Milliseconds 500
    }
} finally {
    [BridgeBenchInput]::Key(0x78, $false)
    [BridgeBenchInput]::CloseTimer()
}
$failures=@()
if(@($rows | Where-Object {-not $_.delivery_matches}).Count){$failures+='delivery_mismatch'}
if(@($rows | Where-Object {-not $_.rate_matches}).Count){$failures+='rate_mismatch (>5% from requested rate)'}
if($failures.Count){throw (($failures -join '; ') + ': see sender-results.csv')}
