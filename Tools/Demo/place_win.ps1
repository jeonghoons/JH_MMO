# A7.4 시연 녹화용 창 배치(D-311): 제목에 -Title 글자가 든 창을 화면 좌표(클라이언트 영역 기준)로 옮기고 맨 위에 둔다.
# 사용: powershell -NoProfile -ExecutionPolicy Bypass -File Tools\Demo\place_win.ps1 -Title "JM Preview" -X 0 -Y 0 -W 1920 -H 1080 [-Index 0] [-NoTop]
#   -HideTop = 창이 클라이언트 영역 안에 그리는 제목 줄 높이(언리얼 PIE 창 32) — 그만큼 화면 위로 올려 숨긴다.
#   -Index = 같은 제목의 창이 여럿일 때(2인 PIE) 몇 번째인지(창 핸들 순). 클라이언트 영역이 정확히 X,Y,W,H가 되도록 테두리·제목 줄만큼 바깥 창을 키운다.
param([string]$Title, [int]$X, [int]$Y, [int]$W, [int]$H, [int]$Index = 0, [int]$HideTop = 0, [switch]$NoTop)
Add-Type @"
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class PW {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  public static List<IntPtr> Find(string t) { var o = new List<IntPtr>(); EnumWindows((h, l) => { if (IsWindowVisible(h)) { var s = new StringBuilder(512); GetWindowText(h, s, 512); if (s.ToString().Contains(t)) o.Add(h); } return true; }, IntPtr.Zero); return o; }
}
"@
$hs = [PW]::Find($Title) | Sort-Object { $_.ToInt64() }
if ($hs.Count -le $Index) { "창 없음: '$Title' (찾은 수 $($hs.Count))"; exit 1 }
$hwnd = $hs[$Index]
[PW]::ShowWindow($hwnd, 9) | Out-Null
$wr = New-Object PW+RECT; [PW]::GetWindowRect($hwnd, [ref]$wr) | Out-Null
$cr = New-Object PW+RECT; [PW]::GetClientRect($hwnd, [ref]$cr) | Out-Null
$p = New-Object PW+POINT; [PW]::ClientToScreen($hwnd, [ref]$p) | Out-Null
$offL = $p.X - $wr.L; $offT = $p.Y - $wr.T
$extraW = ($wr.R - $wr.L) - $cr.R; $extraH = ($wr.B - $wr.T) - $cr.B
$after = if ($NoTop) { [IntPtr]::Zero } else { [IntPtr](-1) }
[PW]::SetWindowPos($hwnd, $after, $X - $offL, $Y - $offT - $HideTop, $W + $extraW, $H + $extraH + $HideTop, 0x0040) | Out-Null
"placed '$Title'#$Index client=($X,$Y,$W x $H) frame=(+$offL,+$offT,+$extraW,+$extraH)"
