param(
    [int]$TargetProcessId,
    [ValidateRange(1,60)][int]$SecondsPerStage = 5,
    [string]$Output = 'sender-results.csv',
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
    public static void Key(ushort vk, bool down) {
        var input = new INPUT { type=1 };
        input.data.keyboard.vk=vk;
        input.data.keyboard.flags=down ? 0u : 2u;
        if(SendInput(1, new[]{input}, Marshal.SizeOf(typeof(INPUT)))!=1)
            throw new InvalidOperationException("SendInput failed: " + Marshal.GetLastWin32Error());
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
if ($ValidateOnly) { Write-Output 'Sender compiled; no input sent'; return }
$process = Get-Process -Id $TargetProcessId
if ($process.ProcessName -notlike 'BridgeBench*') { throw 'Only the isolated BridgeBench process is supported' }
if (-not [BridgeBenchInput]::SetForegroundWindow($process.MainWindowHandle)) {
    throw 'Cannot focus BridgeBench; focus the fixture and retry'
}
function Check-Focus {
    if (-not [BridgeBenchInput]::HasFocus($TargetProcessId)) { throw 'Fixture lost focus; sweep aborted' }
}
function Control-Key([ushort]$VirtualKey) {
    Check-Focus
    try {
        [BridgeBenchInput]::Key($VirtualKey, $true)
        Start-Sleep -Milliseconds 100
    } finally { [BridgeBenchInput]::Key($VirtualKey, $false) }
}
function Wait-Deadline([System.Diagnostics.Stopwatch]$Clock, [double]$Deadline) {
    while ($Clock.Elapsed.TotalSeconds -lt $Deadline) {
        if ($Deadline - $Clock.Elapsed.TotalSeconds -gt 0.002) {
            [Threading.Thread]::Sleep(1)
        } else { [Threading.Thread]::SpinWait(50) }
    }
}
$rows = @()
Control-Key 0x77 # F8: initialize native bridge subscription
Start-Sleep -Seconds 2
try {
    foreach ($rate in 10,20,30,40,50,60,70,80,90,100) {
        Control-Key 0x76 # F7: begin stage
        Start-Sleep -Milliseconds 200
        $clock = [Diagnostics.Stopwatch]::StartNew()
        $count = $rate * $SecondsPerStage
        $halfPeriod = 0.5 / $rate
        $previousDown = $null
        $intervals = [Collections.Generic.List[double]]::new()
        for ($i=0; $i -lt $count; $i++) {
            Check-Focus
            [BridgeBenchInput]::Key(0x78, $true) # F9
            $downAt = $clock.Elapsed.TotalSeconds
            if ($null -ne $previousDown) { $intervals.Add(($downAt-$previousDown)*1000) }
            $previousDown = $downAt
            Wait-Deadline $clock ($downAt + $halfPeriod)
            [BridgeBenchInput]::Key(0x78, $false)
            $upAt = $clock.Elapsed.TotalSeconds
            Wait-Deadline $clock ($upAt + $halfPeriod)
        }
        $elapsed = $clock.Elapsed.TotalSeconds
        # Permit pending bridge callbacks to drain before closing this stage.
        Start-Sleep -Milliseconds 500
        Control-Key 0x79 # F10: finish and write callback metrics
        $rows += [pscustomobject]@{
            target_presses_per_second = $rate
            sent_presses = $count
            send_seconds = $elapsed
            actual_presses_per_second = $count / $elapsed
            mean_press_interval_ms = ($intervals | Measure-Object -Average).Average
            max_press_interval_ms = ($intervals | Measure-Object -Maximum).Maximum
        }
        $rows | Export-Csv -NoTypeInformation -LiteralPath $Output
        Write-Output ("Target {0}/s: sent {1}, actual {2:F2}/s" -f $rate,$count,($count/$elapsed))
        Start-Sleep -Milliseconds 500
    }
} finally {
    [BridgeBenchInput]::Key(0x78, $false)
}