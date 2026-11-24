# A7.4 시연 녹화(D-311): 작업 표시줄 자동 숨김을 잠깐 켜고 끈다(녹화 화면 아래를 가리지 않게). 사용: taskbar.ps1 -Hide | -Restore
# -Hide는 지금 상태를 %TEMP%\jm_taskbar_state.txt에 적고 자동 숨김을 켠다. -Restore는 적어 둔 상태로 되돌린다(원래 자동 숨김이 아니었으면 끔).
param([switch]$Hide, [switch]$Restore)
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class TB {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct APPBARDATA { public int cbSize; public IntPtr hWnd; public uint uCallbackMessage; public uint uEdge; public RECT rc; public IntPtr lParam; }
  [DllImport("shell32.dll")] public static extern IntPtr SHAppBarMessage(uint msg, ref APPBARDATA d);
  public static int Get() { var d = new APPBARDATA(); d.cbSize = Marshal.SizeOf(d); return (int)SHAppBarMessage(4, ref d); }   // ABM_GETSTATE
  public static void Set(int s) { var d = new APPBARDATA(); d.cbSize = Marshal.SizeOf(d); d.lParam = (IntPtr)s; SHAppBarMessage(10, ref d); } // ABM_SETSTATE
}
"@
$f = Join-Path $env:TEMP "jm_taskbar_state.txt"
if ($Hide) {
    if (-not (Test-Path $f)) { [TB]::Get() | Set-Content $f }
    [TB]::Set(([TB]::Get()) -bor 1); "taskbar autohide on (saved $(Get-Content $f))"
}
if ($Restore -and (Test-Path $f)) {
    $s = [int](Get-Content $f); [TB]::Set($s); Remove-Item $f; "taskbar restored ($s)"
}
