# SPDX-License-Identifier: GPL-3.0-or-later
# Runs inside the PC's logged-in desktop session: starts the game through Steam and presses
# Enter on the configuration utility until the game process itself appears.
param([string]$AppId = '13530')
Add-Type @"
using System; using System.Runtime.InteropServices;
public class PopPadKeys {
  [StructLayout(LayoutKind.Sequential)] public struct KI { public ushort vk; public ushort scan; public uint flags; public uint time; public IntPtr extra; public uint pad1; public uint pad2; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public KI ki; }
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] i, int size);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  public static void Tap(ushort scan) {
    INPUT[] a = new INPUT[1]; a[0].type = 1; a[0].ki.scan = scan; a[0].ki.flags = 8u;
    SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
    System.Threading.Thread.Sleep(90);
    a[0].ki.flags |= 2u;
    SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@
if (Get-Process POP3 -ErrorAction SilentlyContinue) { "already running"; return }
Start-Process "steam://rungameid/$AppId"
for ($i = 0; $i -lt 60; $i++) {
  Start-Sleep -Milliseconds 1000
  if (Get-Process POP3 -ErrorAction SilentlyContinue) { "game started after $i s"; return }
  $cfg = Get-Process PrinceOfPersia -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowTitle -like '*Configuration*' } | Select-Object -First 1
  if ($cfg) { [PopPadKeys]::SetForegroundWindow($cfg.MainWindowHandle) | Out-Null; [PopPadKeys]::Tap(0x1C) }
}
"game did not start"
