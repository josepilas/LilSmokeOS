$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$evidence=Join-Path $project 'test-results\sm64-readable-rk707-2026-10-11'
Start-Transcript -Path (Join-Path $evidence 'xinput-port-cycle.log') -Append|Out-Null
$state=& 'C:\Program Files\usbipd-win\usbipd.exe' state|ConvertFrom-Json
$root=@($state.Devices|Where-Object {
    $_.BusId -eq '1-4' -and $_.IsForced -and $_.PersistedGuid -and
    $_.InstanceId -match '^USB\\VID_045E&PID_028E\\'
})
if($root.Count -ne 1){throw 'Expected only the recorded force-bound receiver; no port cycled'}
$parent=(Get-PnpDeviceProperty -InstanceId $root[0].InstanceId -KeyName DEVPKEY_Device_Parent).Data
if($parent -notmatch '^USB\\ROOT_HUB30\\'){throw 'Unexpected hub identity; no port cycled'}
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
public static class LilSmokeReceiverPort {
    [StructLayout(LayoutKind.Sequential)] struct InterfaceData {
        public int size; public Guid guid; public uint flags; public UIntPtr reserved;
    }
    [StructLayout(LayoutKind.Sequential)] struct DeviceData {
        public int size; public Guid guid; public uint node; public UIntPtr reserved;
    }
    [StructLayout(LayoutKind.Sequential,Pack=1)] struct UsbDescriptor {
        public byte length,type; public ushort usb; public byte deviceClass,subclass,protocol,packetSize;
        public ushort vendor,product,release; public byte manufacturer,name,serial,configurations;
    }
    [StructLayout(LayoutKind.Sequential,Pack=1)] struct Connection {
        public uint index; public UsbDescriptor descriptor;
        public byte configuration,speed,isHub; public ushort address; public uint pipes,status;
    }
    [DllImport("setupapi.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern IntPtr SetupDiGetClassDevs(ref Guid guid,string enumerator,IntPtr parent,uint flags);
    [DllImport("setupapi.dll",SetLastError=true)]
    static extern bool SetupDiEnumDeviceInterfaces(IntPtr set,IntPtr info,ref Guid guid,uint index,ref InterfaceData data);
    [DllImport("setupapi.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr set,ref InterfaceData data,IntPtr detail,uint size,out uint required,ref DeviceData device);
    [DllImport("setupapi.dll")] static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);
    [DllImport("cfgmgr32.dll",CharSet=CharSet.Unicode)]
    static extern uint CM_Get_Device_ID(uint node,StringBuilder id,uint length,uint flags);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern SafeFileHandle CreateFile(string path,uint access,uint sharing,IntPtr security,uint creation,uint flags,IntPtr template);
    [DllImport("kernel32.dll",SetLastError=true)]
    static extern bool DeviceIoControl(SafeFileHandle handle,uint code,byte[] input,uint inputSize,byte[] output,uint outputSize,out uint returned,IntPtr overlapped);
    static void Check(bool result) { if(!result)throw new Win32Exception(Marshal.GetLastWin32Error()); }
    static string HubPath(string expected) {
        Guid guid=new Guid("f18a0e88-c30c-11d0-8815-00a0c906bed8");
        IntPtr set=SetupDiGetClassDevs(ref guid,null,IntPtr.Zero,0x12);
        if(set==new IntPtr(-1))throw new Win32Exception(Marshal.GetLastWin32Error());
        try {
            for(uint i=0;;++i) {
                InterfaceData data=new InterfaceData(); data.size=Marshal.SizeOf(typeof(InterfaceData));
                if(!SetupDiEnumDeviceInterfaces(set,IntPtr.Zero,ref guid,i,ref data)) {
                    int error=Marshal.GetLastWin32Error(); if(error==259)break; throw new Win32Exception(error);
                }
                uint needed; DeviceData device=new DeviceData(); device.size=Marshal.SizeOf(typeof(DeviceData));
                SetupDiGetDeviceInterfaceDetail(set,ref data,IntPtr.Zero,0,out needed,ref device);
                if(needed<8 || needed>65536)throw new InvalidOperationException("Invalid hub path size");
                IntPtr buffer=Marshal.AllocHGlobal((int)needed);
                try {
                    Marshal.WriteInt32(buffer,IntPtr.Size==8?8:6);
                    Check(SetupDiGetDeviceInterfaceDetail(set,ref data,buffer,needed,out needed,ref device));
                    StringBuilder id=new StringBuilder(512);
                    if(CM_Get_Device_ID(device.node,id,512,0)!=0)throw new InvalidOperationException("Cannot identify hub");
                    if(String.Equals(id.ToString(),expected,StringComparison.OrdinalIgnoreCase))
                        return Marshal.PtrToStringUni(IntPtr.Add(buffer,4));
                } finally { Marshal.FreeHGlobal(buffer); }
            }
        } finally { SetupDiDestroyDeviceInfoList(set); }
        throw new InvalidOperationException("Recorded parent hub not found");
    }
    public static uint CycleReceiver(string hub) {
        using(SafeFileHandle handle=CreateFile(HubPath(hub),0xC0000000,3,IntPtr.Zero,3,0,IntPtr.Zero)) {
            if(handle.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error());
            byte[] info=new byte[1024]; BitConverter.GetBytes((uint)4).CopyTo(info,0); uint returned;
            Check(DeviceIoControl(handle,0x220448,info,(uint)info.Length,info,(uint)info.Length,out returned,IntPtr.Zero));
            if(returned<Marshal.SizeOf(typeof(Connection)))throw new InvalidOperationException("Short USB identity response");
            GCHandle pin=GCHandle.Alloc(info,GCHandleType.Pinned);
            Connection connection;
            try { connection=(Connection)Marshal.PtrToStructure(pin.AddrOfPinnedObject(),typeof(Connection)); }
            finally { pin.Free(); }
            if(connection.index!=4 || connection.descriptor.length!=18 || connection.descriptor.type!=1 ||
               connection.descriptor.vendor!=0x045e || connection.descriptor.product!=0x028e || connection.isHub!=0)
                throw new InvalidOperationException("USB port identity changed; no port cycled");
            // This resets connection 4 only. IOCTL_USB_RESET_HUB is never used.
            byte[] cycle=new byte[8]; BitConverter.GetBytes((uint)4).CopyTo(cycle,0);
            Check(DeviceIoControl(handle,0x220444,cycle,8,cycle,8,out returned,IntPtr.Zero));
            return BitConverter.ToUInt32(cycle,4);
        }
    }
}
'@
$result=[LilSmokeReceiverPort]::CycleReceiver($parent)
if($result -ne 0){throw "USB port cycle returned status $result"}
[ordered]@{passed=$true;receiver=$root[0].InstanceId;hub=$parent;connection=4;
    vendor='045e';product='028e';sd_port_modified=$false;whole_hub_reset=$false}|
    ConvertTo-Json|Set-Content -LiteralPath (Join-Path $evidence 'xinput-port-cycle.json') -Encoding UTF8
Stop-Transcript|Out-Null
