param(
    [Parameter(Mandatory=$true)][ValidateRange(1,100)][int]$DiskNumber,
    [Parameter(Mandatory=$true)][UInt64]$ExpectedDiskBytes,
    [Parameter(Mandatory=$true)][UInt64]$ReadBytes,
    [Parameter(Mandatory=$true)][string]$Output,
    [Parameter(Mandatory=$true)][string]$Report
)
$ErrorActionPreference='Stop'
$state=@{state='starting'; disk=$DiskNumber; bytes_read=0; physical_writes=0}
function Save-State {
    [IO.File]::WriteAllText($Report,($state | ConvertTo-Json -Depth 4))
}
Save-State
$stream=$null
$destination=$null
$handle=$null
try {
    $env:PSModulePath="$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules"
    Import-Module Storage
    $identity=[Security.Principal.WindowsIdentity]::GetCurrent()
    if (!([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Administrator access is required for a read-only physical handle.'
    }
    $disk=Get-Disk -Number $DiskNumber
    if ($disk.IsBoot -or $disk.IsSystem -or $disk.BusType -ne 'USB' -or $disk.Size -ne $ExpectedDiskBytes -or
        $ReadBytes -gt $disk.Size -or $ReadBytes -lt 512 -or $ReadBytes % 512 -ne 0) { throw 'Unexpected USB disk or geometry.' }
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class LilSmokeReadOnlyCard {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool DeviceIoControl(SafeFileHandle handle, uint code, IntPtr input, uint inputBytes, byte[] output, uint outputBytes, out uint returned, IntPtr overlapped);
    public static SafeFileHandle Open(string name) {
        var h=CreateFile(name,0x80000000,3,IntPtr.Zero,3,0,IntPtr.Zero);
        if(h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
        return h;
    }
    public static void Verify(SafeFileHandle h, uint disk, long size) {
        var n=new byte[12]; var s=new byte[8]; uint returned;
        if(!DeviceIoControl(h,0x002D1080,IntPtr.Zero,0,n,12,out returned,IntPtr.Zero) || returned<12 || BitConverter.ToUInt32(n,4)!=disk)
            throw new InvalidOperationException("Physical disk identity changed.");
        if(!DeviceIoControl(h,0x0007405C,IntPtr.Zero,0,s,8,out returned,IntPtr.Zero) || returned<8 || BitConverter.ToInt64(s,0)!=size)
            throw new InvalidOperationException("Physical disk capacity changed.");
    }
}
'@
    $handle=[LilSmokeReadOnlyCard]::Open("\\.\PhysicalDrive$DiskNumber")
    [LilSmokeReadOnlyCard]::Verify($handle,$DiskNumber,$ExpectedDiskBytes)
    $stream=[IO.FileStream]::new($handle,[IO.FileAccess]::Read,1048576,$false)
    $destination=[IO.File]::Open([IO.Path]::GetFullPath($Output),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    $state.state='reading'; Save-State
    $buffer=[byte[]]::new(1048576)
    $hash=[Security.Cryptography.SHA256]::Create()
    $last=0
    while ($state.bytes_read -lt $ReadBytes) {
        $count=$stream.Read($buffer,0,[int][Math]::Min($ReadBytes-$state.bytes_read,$buffer.Length))
        if ($count -le 0) { throw 'Physical read ended early.' }
        $destination.Write($buffer,0,$count)
        [void]$hash.TransformBlock($buffer,0,$count,$buffer,0)
        $state.bytes_read+=$count
        if ($state.bytes_read-$last -ge 67108864) { Save-State; $last=$state.bytes_read }
    }
    [void]$hash.TransformFinalBlock([byte[]]::new(0),0,0)
    $destination.Flush($true)
    $state.sha256=[BitConverter]::ToString($hash.Hash).Replace('-','').ToLowerInvariant()
    $state.output=[IO.Path]::GetFullPath($Output)
    $state.state='complete'; Save-State
} catch {
    $state.state='failed'; $state.error=$_.Exception.Message; Save-State
    exit 1
} finally {
    if ($destination) { $destination.Dispose() }
    if ($stream) { $stream.Dispose() } elseif ($handle) { $handle.Dispose() }
}
