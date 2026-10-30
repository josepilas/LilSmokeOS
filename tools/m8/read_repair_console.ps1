param([Parameter(Mandatory=$true)][int]$RepairProcessId,[switch]$ConfirmRecoverLost)
$ErrorActionPreference='Stop'
$process=Get-CimInstance Win32_Process -Filter "ProcessId=$RepairProcessId"
if(-not $process -or $process.Name -ne 'powershell.exe' -or
   $process.CommandLine -notlike '*\tools\m8\repair_card14.ps1*'){throw 'Not the owned repair shell'}
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class RepairConsole {
 [StructLayout(LayoutKind.Sequential)] struct Coord {public short X,Y;}
 [StructLayout(LayoutKind.Sequential)] struct Rect {public short Left,Top,Right,Bottom;}
 [StructLayout(LayoutKind.Sequential)] struct Info {public Coord Size,Cursor;public short Attributes;public Rect Window;public Coord Maximum;}
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool AttachConsole(uint pid);
 [DllImport("kernel32.dll")] static extern bool FreeConsole();
 [DllImport("kernel32.dll")] static extern IntPtr GetStdHandle(int n);
 [DllImport("kernel32.dll")] static extern bool SetStdHandle(int n,IntPtr h);
 [DllImport("kernel32.dll")] static extern bool GetConsoleScreenBufferInfo(IntPtr h,out Info i);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern bool ReadConsoleOutputCharacter(IntPtr h,StringBuilder text,uint n,Coord start,out uint read);
 [StructLayout(LayoutKind.Explicit,Size=20)] struct KeyInput {
  [FieldOffset(0)] public ushort Type;
  [FieldOffset(4)] public int Down;
  [FieldOffset(8)] public ushort Repeat;
  [FieldOffset(10)] public ushort Key;
  [FieldOffset(12)] public ushort Scan;
  [FieldOffset(14)] public ushort Character;
  [FieldOffset(16)] public uint State;
 }
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern bool WriteConsoleInput(IntPtr h,KeyInput[] input,uint count,out uint written);
 public static string Read(uint pid,bool confirm) {
  IntPtr a=GetStdHandle(-10),b=GetStdHandle(-11),c=GetStdHandle(-12);
  FreeConsole();
  if(!AttachConsole(pid))throw new Exception("Cannot attach owned CLI console: "+Marshal.GetLastWin32Error());
  try {Info i;IntPtr h=GetStdHandle(-11);if(!GetConsoleScreenBufferInfo(h,out i))throw new Exception("No CLI screen buffer");
   int rows=Math.Min(i.Cursor.Y+1,80);Coord start=new Coord();start.Y=(short)Math.Max(0,i.Cursor.Y-rows+1);
   StringBuilder s=new StringBuilder(i.Size.X*rows);uint n;ReadConsoleOutputCharacter(h,s,(uint)(i.Size.X*rows),start,out n);
   string text=s.ToString();
   if(confirm) {
    string lower=text.ToLowerInvariant();
    if(!lower.Contains("cadeias perdidas")||!text.Contains("(S/N)"))throw new Exception("Not the expected CHKDSK recovery prompt; no CLI input sent");
    KeyInput[] keys=new KeyInput[4];
    for(int j=0;j<4;++j){keys[j].Type=1;keys[j].Down=j%2==0?1:0;keys[j].Repeat=1;keys[j].Key=(ushort)(j<2?83:13);keys[j].Character=(ushort)(j<2?83:13);}
    uint written;if(!WriteConsoleInput(GetStdHandle(-10),keys,4,out written)||written!=4)throw new Exception("Owned CLI stdin write failed");
    text+="\nConfirmed recovery in the isolated CHKDSK console only.\n";
   }
   return text;
  } finally {FreeConsole();SetStdHandle(-10,a);SetStdHandle(-11,b);SetStdHandle(-12,c);}
 }
}
'@
$p=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
[RepairConsole]::Read([uint32]$RepairProcessId,$ConfirmRecoverLost.IsPresent) |
    Set-Content -LiteralPath (Join-Path $p 'test-results\opendingux-native-video-2026-10-09\card-repair-console.txt') -Encoding UTF8
