# A7.4 시연 영상(D-311): 사용자가 Clipchamp로 컷 편집한 mp4에 한국어 자막(SRT)을 입히거나, 장면 경계 확인용 프레임 시트를 뽑는다.
# 사용:
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\Demo\burn.ps1 -Mode sheet -In <편집본.mp4> [-OutDir <폴더>] [-Every 1]
#       → <폴더>\sheet_000.png …(한 장 = 30초, 6×5 칸, 칸마다 경과 시각) — Claude가 장면 경계·자막 시각을 맞출 때 본다
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\Demo\burn.ps1 -Mode burn -In <편집본.mp4> -Srt Docs\demo\subs.srt -Out <최종.mp4>
#       → 맑은 고딕 자막(화면 아래, 테두리)을 입힌 1080p H.264(NVENC, 없으면 libx264) + 소리는 그대로
param(
    [ValidateSet("sheet", "burn")][string]$Mode = "burn",
    [Parameter(Mandatory = $true)][string]$In,
    [string]$Srt = "",
    [string]$Out = "",
    [string]$OutDir = "",
    [int]$Every = 1,
    [int]$FontSize = 13,   # libass 기준 높이 288 → 1080p에서 약 3.75배(13 ≈ 49px)
    # 편집본에 이미 들어간 카드를 고칠 때: "<새 그림.png>|<시작 초>|<끝 초>" — 그 구간에 새 그림을 화면 전체로 덮는다(자막보다 먼저)
    [string]$Overlay = ""
)
$ErrorActionPreference = "Stop"
$ff = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $ff) {
    $ff = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter ffmpeg.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $ff) { throw "ffmpeg를 찾지 못함(winget install Gyan.FFmpeg)" }
$In = (Resolve-Path $In).Path
if ($Overlay) {   # 상대 경로는 지금 폴더 기준(아래에서 자막 폴더로 옮기기 전에 절대 경로로)
    $ov = $Overlay.Split("|"); $ov[0] = (Resolve-Path $ov[0]).Path; $Overlay = $ov -join "|"
}

if ($Mode -eq "sheet") {
    if (-not $OutDir) { $OutDir = Join-Path (Split-Path $In) "sheets" }
    New-Item -ItemType Directory -Force $OutDir | Out-Null
    # 프레임마다 경과 시각(분:초)을 왼쪽 위에 찍고 6×5로 붙인다
    $vf = "fps=1/$Every,scale=320:-2,drawtext=fontfile='C\:/Windows/Fonts/arial.ttf':text='%{pts\:gmtime\:0\:%M\\\:%S}':x=6:y=6:fontsize=20:fontcolor=yellow:box=1:boxcolor=black@0.6,tile=6x5"
    & $ff -hide_banner -loglevel error -y -i $In -vf $vf -fps_mode vfr (Join-Path $OutDir "sheet_%03d.png")
    Get-ChildItem $OutDir -Filter "sheet_*.png" | Select-Object -ExpandProperty FullName
    exit 0
}

if (-not $Srt -or -not $Out) { throw "-Srt와 -Out이 필요함" }
$Srt = (Resolve-Path $Srt).Path
# subtitles 필터는 Windows 경로의 드라이브 콜론·역슬래시를 따로 이스케이프해야 하므로, SRT 폴더로 옮겨 파일 이름만 넘긴다
$srtDir = Split-Path $Srt; $srtName = Split-Path $Srt -Leaf
$style = "FontName=Malgun Gothic,FontSize=$FontSize,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=1,Outline=1.5,Shadow=0,MarginV=14"
$vf = "subtitles=${srtName}:charenc=UTF-8:force_style='$style'"
# .ass(srt2ass.py — 줄마다 위·아래·왼쪽 위 자리를 정한 것)는 파일의 스타일을 그대로 쓴다
if ($srtName -like "*.ass") { $vf = "subtitles=${srtName}" }
$enc = @("-c:v", "h264_nvenc", "-preset", "p6", "-cq", "19", "-b:v", "0")
# NVENC가 되는지 먼저 짧게 시험(5.1에서는 네이티브 stderr가 Stop 오류가 되므로 잠시 Continue, 출력은 버림)
$ErrorActionPreference = "Continue"
& $ff -hide_banner -loglevel quiet -f lavfi -i "nullsrc=s=1280x720:d=0.1" -c:v h264_nvenc -f null - 2>$null
$nvencOk = ($LASTEXITCODE -eq 0)
$ErrorActionPreference = "Stop"
if (-not $nvencOk) { $enc = @("-c:v", "libx264", "-preset", "slow", "-crf", "18") }
$Out = [IO.Path]::GetFullPath($Out)
Push-Location $srtDir
try {
    $ErrorActionPreference = "Continue"   # ffmpeg 경고(stderr)로 멈추지 않게 — 성공 여부는 종료 코드로 봄
    if ($Overlay) {
        $png, $s, $e = $Overlay.Split("|")
        $fc = "[0:v][1:v]overlay=0:0:enable='between(t,$s,$e)':shortest=1,$vf[v]"
        & $ff -hide_banner -loglevel error -y -i $In -loop 1 -i $png -filter_complex $fc -map "[v]" -map "0:a?" @enc -pix_fmt yuv420p -c:a copy -movflags +faststart $Out
    } else {
        & $ff -hide_banner -loglevel error -y -i $In -vf $vf @enc -pix_fmt yuv420p -c:a copy -movflags +faststart $Out
    }
    if ($LASTEXITCODE -ne 0) { throw "ffmpeg 실패($LASTEXITCODE)" }
} finally { Pop-Location }
Write-Host "완성: $Out"
