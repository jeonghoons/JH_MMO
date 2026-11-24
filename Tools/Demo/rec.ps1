# A7.4 시연 클립 녹화(D-311): 모니터 전체를 화면 복제 캡처(ddagrab, GPU) + NVENC로 정해진 초만큼 녹화한다. CPU 부담이 거의 없어 부하 장면에서도 쓴다.
# 사용: powershell -NoProfile -ExecutionPolicy Bypass -File Tools\Demo\rec.ps1 -Name C03 -Seconds 25 [-OutDir C:\Users\kjhkjh\Videos\JH_MMO_demo]
#   → <OutDir>\<Name>.mkv(녹화 중 끊겨도 살아남음) → 끝나면 <Name>.mp4로 옮겨 담음(다시 인코딩 없음). 같은 이름이 있으면 덮어씀(클립 재촬영).
#   ddagrab이 실패하면(원격 데스크톱 등) gdigrab + NVENC로 다시 시도한다.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][double]$Seconds,
    [string]$OutDir = "C:\Users\kjhkjh\Videos\JH_MMO_demo",
    [int]$Fps = 60
)
$ErrorActionPreference = "Continue"
$ff = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $ff) {
    $ff = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter ffmpeg.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $ff) { Write-Host "ffmpeg를 찾지 못함"; exit 1 }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$mkv = Join-Path $OutDir "$Name.mkv"
$mp4 = Join-Path $OutDir "$Name.mp4"
Remove-Item $mkv, $mp4 -ErrorAction SilentlyContinue

$t0 = Get-Date
& $ff -hide_banner -loglevel error -y -filter_complex "ddagrab=0:framerate=$Fps" -c:v h264_nvenc -cq 18 -t $Seconds $mkv
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $mkv)) {
    Write-Host "ddagrab 실패 — gdigrab으로 다시"
    & $ff -hide_banner -loglevel error -y -f gdigrab -framerate $Fps -i desktop -c:v h264_nvenc -cq 18 -pix_fmt yuv420p -t $Seconds $mkv
}
& $ff -hide_banner -loglevel error -y -i $mkv -c copy -movflags +faststart $mp4
if (Test-Path $mp4) { Remove-Item $mkv -ErrorAction SilentlyContinue }
"{0} start={1:HH:mm:ss.fff} sec={2} -> {3}" -f $Name, $t0, $Seconds, $(if (Test-Path $mp4) { $mp4 } else { $mkv })
