param([ValidateSet('xinput','psinput','dinput')][string]$Mode='xinput',
      [ValidateSet('initial','restored')][string]$Stage='initial')
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class LilSmokeXInputRead {
    [StructLayout(LayoutKind.Sequential)] public struct Pad {
        public ushort buttons; public byte leftTrigger, rightTrigger;
        public short leftX, leftY, rightX, rightY;
    }
    [StructLayout(LayoutKind.Sequential)] public struct State { public uint packet; public Pad pad; }
    [DllImport("xinput1_4.dll", EntryPoint="XInputGetState")]
    public static extern uint Read(uint index, out State state);
}
'@
$devices=@(Get-PnpDevice -PresentOnly | Where-Object {$_.InstanceId -match '^(USB|HID)\\VID_'})
$states=@()
for($slot=0;$slot -lt 4;$slot++) {
    $state=[LilSmokeXInputRead+State]::new()
    $error=[LilSmokeXInputRead]::Read($slot,[ref]$state)
    $states+=[pscustomobject]@{slot=$slot;error=$error;packet=$state.packet;buttons=$state.pad.buttons;
        leftTrigger=$state.pad.leftTrigger;rightTrigger=$state.pad.rightTrigger;
        leftX=$state.pad.leftX;leftY=$state.pad.leftY;rightX=$state.pad.rightX;rightY=$state.pad.rightY}
}
$report=[ordered]@{mode=$Mode;read_only=$true;keys_injected=$false;drivers_changed=$false;
    devices=@($devices|Select-Object Class,FriendlyName,InstanceId);xinput=$states}
$out=Join-Path ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))) 'test-results\sm64-readable-rk707-2026-10-11'
[IO.Directory]::CreateDirectory($out)|Out-Null
$name=if($Stage -eq 'initial'){"rk707-windows-$Mode.json"}else{"rk707-windows-$Mode-restored.json"}
$report|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $out $name) -Encoding UTF8
$report|ConvertTo-Json -Depth 6
