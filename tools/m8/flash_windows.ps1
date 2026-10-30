param(
    [Parameter(Mandatory=$true)][string]$Image,
    [Parameter(Mandatory=$true)][ValidatePattern('^[D-Z]$')][string]$DriveLetter,
    [Parameter(Mandatory=$true)][int]$ExpectedDiskNumber,
    [Parameter(Mandatory=$true)][UInt64]$ExpectedDiskBytes,
    [Parameter(Mandatory=$true)][string]$ExpectedVolumeId,
    [Parameter(Mandatory=$true)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$ExpectedSha256,
    [Parameter(Mandatory=$true)][string]$Report
)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules"
Import-Module Storage
Import-Module Microsoft.PowerShell.Utility
$state = @{ state='starting'; drive=$DriveLetter; expected_disk=$ExpectedDiskNumber; bytes_written=0; bytes_verified=0 }
function Save-State {
    $state.updated_utc = [DateTime]::UtcNow.ToString('o')
    [IO.File]::WriteAllText($Report, ($state | ConvertTo-Json -Depth 5))
}
Save-State
$volumes = @()
$physical = $null
$diskStream = $null
$source = $null
try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    if (!([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Raw writes require an elevated administrator token.'
    }
    $item = Get-Item -LiteralPath $Image
    if ($item.PSIsContainer -or $item.Length -lt 512 -or $item.Length % 512 -ne 0) { throw 'Invalid regular image file.' }
    $source = [IO.File]::Open($item.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    $imageHash = [BitConverter]::ToString($algorithm.ComputeHash($source)).Replace('-','').ToLowerInvariant()
    if ($imageHash -ne $ExpectedSha256.ToLowerInvariant()) { throw 'Image checksum mismatch; no disk write attempted.' }
    $source.Position = 0
    $partition = @(Get-Partition -DriveLetter $DriveLetter)
    if ($partition.Count -ne 1 -or $partition[0].DiskNumber -ne $ExpectedDiskNumber) { throw 'Drive letter no longer maps to the authorized disk.' }
    $disk = Get-Disk -Number $ExpectedDiskNumber
    $volume = Get-Volume -DriveLetter $DriveLetter
    if ($disk.Number -eq 0 -or $disk.IsBoot -or $disk.IsSystem -or $disk.BusType -ne 'USB' -or
        $disk.Size -ne $ExpectedDiskBytes -or $disk.IsReadOnly -or $disk.LogicalSectorSize -ne 512 -or
        $volume.UniqueId -ne $ExpectedVolumeId -or $item.Length -gt $disk.Size) {
        throw 'Target identity, safety or geometry check failed; no disk write attempted.'
    }
    $state.image=$item.FullName; $state.image_sha256=$imageHash; $state.image_bytes=$item.Length
    $state.target_disk=$disk.Number; $state.target_capacity=$disk.Size; $state.volume_id=$volume.UniqueId
    $state.original_card_touched=$false

    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class LilSmokeRawDisk {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool DeviceIoControl(SafeFileHandle handle, uint code, IntPtr input, uint inputBytes, byte[] output, uint outputBytes, out uint returned, IntPtr overlapped);
    public static SafeFileHandle Open(string name) {
        SafeFileHandle handle=CreateFile(name, 0xC0000000, 3, IntPtr.Zero, 3, 0x80000000, IntPtr.Zero);
        if(handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
        return handle;
    }
    public static void Control(SafeFileHandle handle, uint code) {
        uint returned;
        if(!DeviceIoControl(handle,code,IntPtr.Zero,0,null,0,out returned,IntPtr.Zero))
            throw new Win32Exception(Marshal.GetLastWin32Error());
    }
    public static uint DeviceNumber(SafeFileHandle handle) {
        byte[] data=new byte[12]; uint returned;
        if(!DeviceIoControl(handle,0x002D1080,IntPtr.Zero,0,data,12,out returned,IntPtr.Zero) || returned<12)
            throw new Win32Exception(Marshal.GetLastWin32Error());
        return BitConverter.ToUInt32(data,4);
    }
    public static long Length(SafeFileHandle handle) {
        byte[] data=new byte[8]; uint returned;
        if(!DeviceIoControl(handle,0x0007405C,IntPtr.Zero,0,data,8,out returned,IntPtr.Zero) || returned<8)
            throw new Win32Exception(Marshal.GetLastWin32Error());
        return BitConverter.ToInt64(data,0);
    }
}
'@
    # Lock every mounted volume on this exact disk, including hidden volumes.
    $paths = @()
    foreach ($part in Get-Partition -DiskNumber $ExpectedDiskNumber) {
        foreach ($path in $part.AccessPaths) {
            if ($path -match '^\\\\\?\\Volume\{') { $paths += $path.TrimEnd([char]92) }
        }
    }
    if (!$paths) { $paths = @("\\.\${DriveLetter}:") }
    foreach ($path in ($paths | Select-Object -Unique)) {
        $handle = [LilSmokeRawDisk]::Open($path)
        if ([LilSmokeRawDisk]::DeviceNumber($handle) -ne $ExpectedDiskNumber) { $handle.Dispose(); throw 'Volume handle belongs to another disk.' }
        try { [LilSmokeRawDisk]::Control($handle, 0x00090018) } catch { $handle.Dispose(); throw }
        $volumes += $handle
        [LilSmokeRawDisk]::Control($handle, 0x00090020)
    }
    $physical = [LilSmokeRawDisk]::Open("\\.\PhysicalDrive$ExpectedDiskNumber")
    if ([LilSmokeRawDisk]::DeviceNumber($physical) -ne $ExpectedDiskNumber -or
        [LilSmokeRawDisk]::Length($physical) -ne $ExpectedDiskBytes) { throw 'Physical handle identity differs; no write attempted.' }
    $diskStream = [IO.FileStream]::new($physical, [IO.FileAccess]::ReadWrite, 1048576, $false)
    $state.state='writing'; Save-State
    $buffer = [byte[]]::new(1048576)
    $last = 0
    while (($count = $source.Read($buffer,0,$buffer.Length)) -gt 0) {
        $diskStream.Write($buffer,0,$count)
        $state.bytes_written += $count
        if ($state.bytes_written - $last -ge 67108864) { Save-State; $last=$state.bytes_written }
    }
    $diskStream.Flush($true)
    $state.state='verifying'; Save-State
    $diskStream.Position = 0
    $readback = [Security.Cryptography.SHA256]::Create()
    $remaining = [Int64]$item.Length
    $last = 0
    while ($remaining -gt 0) {
        $count = $diskStream.Read($buffer,0,[int][Math]::Min($remaining,$buffer.Length))
        if ($count -le 0) { throw 'Read-back ended early.' }
        [void]$readback.TransformBlock($buffer,0,$count,$buffer,0)
        $remaining -= $count; $state.bytes_verified += $count
        if ($state.bytes_verified - $last -ge 67108864) { Save-State; $last=$state.bytes_verified }
    }
    [void]$readback.TransformFinalBlock([byte[]]::new(0),0,0)
    $actual = [BitConverter]::ToString($readback.Hash).Replace('-','').ToLowerInvariant()
    $state.readback_sha256=$actual
    if ($actual -ne $imageHash) { throw 'Read-back checksum mismatch. Do not use the card.' }
    # Refresh Windows' view without formatting/repairing the vendor's encoded MBR.
    try { [LilSmokeRawDisk]::Control($physical, 0x00070140) }
    catch { $state.refresh_warning=$_.Exception.Message }
    $state.state='verified'; Save-State
} catch {
    $state.state='failed'; $state.error=$_.Exception.Message; Save-State
    exit 1
} finally {
    if ($source) { $source.Dispose() }
    if ($diskStream) { $diskStream.Dispose() } elseif ($physical) { $physical.Dispose() }
    foreach ($handle in $volumes) { $handle.Dispose() }
}
