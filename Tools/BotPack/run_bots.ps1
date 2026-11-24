# A5.4c 노트북 봇 실행기. 서버 PC의 run_stages.sh와 같은 단계 목록을 차례로 돈다.
# 단계마다: 서버 포트가 열릴 때까지 기다림 → LoadTest 실행(봇 CPU를 1초마다 기록) → 결과를 results\<단계>\ 에 남김
#          → 서버가 내려가 포트가 닫힐 때까지 기다림 → TIME_WAIT가 줄 때까지 기다림 → 다음 단계.
# 사용: powershell -ExecutionPolicy Bypass -File run_bots.ps1 [-ServerIp 1.2.3.4] [-Stages smoke,s1000_1,...]
#       IP를 주지 않으면 server_ip.txt에서 읽고, 없으면 물어본다(server_ip.txt는 커밋하지 않음 — .gitignore).
# A5.4d 재측정(D-306): 단계 이름 앞에 "d_"를 붙이면(d_s1000_1 등) 같은 정의로 돌고 결과는 results\d_<단계>\, 환경은 results\env_d.txt
#       — A5.4c 결과를 덮어쓰지 않는다. 사용: powershell -NoProfile -ExecutionPolicy Bypass -Command "& .\run_bots.ps1 -Stages d_smoke,d_s1000_1,..."
#       (-File로 넘기면 쉼표 목록이 배열로 나뉘지 않는다)
param(
    [string]$ServerIp = "",
    [string[]]$Stages = @("smoke", "s1000_1", "s1000_2", "s2500_1", "s2500_2", "s5000_1", "s5000_2"),
    [int]$Port = 8888,
    [int]$WaitOpenMin = 30     # 단계마다 서버 포트가 열리기를 기다리는 최대 시간(분)
)
$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Here

# 단계 정의(서버 PC run_stages.sh와 같아야 함): 봇 수, 초당 접속, 지속 초(램프 + 120초 유지), 계정 시작 번호, 프로필
$Mix = "idle=25,move=15,chat=10,hunt=20,loot=20,trade=5,auction=5"
$Def = @{
    "smoke"   = @{ Bots = 20;   Rate = 20;  Dur = 30;  Start = 0 }
    "s1000_1" = @{ Bots = 1000; Rate = 200; Dur = 125; Start = 0 }
    "s1000_2" = @{ Bots = 1000; Rate = 200; Dur = 125; Start = 0 }
    "s2500_1" = @{ Bots = 2500; Rate = 200; Dur = 135; Start = 0 }
    "s2500_2" = @{ Bots = 2500; Rate = 200; Dur = 135; Start = 0 }
    "s5000_1" = @{ Bots = 2500; Rate = 100; Dur = 145; Start = 2500 }   # 노트북 몫 2,500(계정 2,500~4,999), PC가 나머지 2,500
    "s5000_2" = @{ Bots = 2500; Rate = 100; Dur = 145; Start = 2500 }
    "demo"    = @{ Bots = 3000; Rate = 100; Dur = 900; Start = 2000 }   # A7.4 시연 촬영(D-311): 노트북 3,000(계정 2,000~4,999), PC DummyClient 2,000 — 15분, 결과는 커밋하지 않아도 됨
}

if (-not $ServerIp) {
    if (Test-Path "$Here\server_ip.txt") { $ServerIp = (Get-Content "$Here\server_ip.txt" -TotalCount 1).Trim() }
    else { $ServerIp = (Read-Host "서버 PC IP").Trim(); Set-Content -Path "$Here\server_ip.txt" -Value $ServerIp -Encoding ascii }
}

function Test-ServerPort {
    $c = New-Object System.Net.Sockets.TcpClient
    try {
        $t = $c.ConnectAsync($ServerIp, $Port)
        return ($t.Wait(1500) -and $c.Connected)
    } catch { return $false } finally { $c.Close() }
}

function Get-TimeWaitCount {
    return @(netstat -an | Select-String "TIME_WAIT").Count
}

$Results = Join-Path $Here "results"
New-Item -ItemType Directory -Force $Results | Out-Null

# 노트북 환경(한 번)
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
$bat = Get-CimInstance Win32_Battery -ErrorAction SilentlyContinue | Select-Object -First 1
$plan = (powercfg /getactivescheme) -join " "
$wifi = (netsh wlan show interfaces | Select-String "Signal|신호|Receive rate|수신 속도|Radio type|무선 유형") -join "; "
@(
    "time      = $(Get-Date -Format s)",
    "computer  = laptop",
    "cpu       = $($cpu.Name) cores=$($cpu.NumberOfCores) threads=$($cpu.NumberOfLogicalProcessors) maxMHz=$($cpu.MaxClockSpeed)",
    "memoryGB  = $([math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1))",
    "battery   = status=$($bat.BatteryStatus) charge=$($bat.EstimatedChargeRemaining)",
    "power     = $plan",
    "wifi      = $wifi",
    "stages    = $($Stages -join ',')"
) | Set-Content -Path $(if ($Stages | Where-Object { $_ -like "d_*" }) { "$Results\env_d.txt" } else { "$Results\env.txt" }) -Encoding utf8

$Cores = [int]$cpu.NumberOfLogicalProcessors
foreach ($stage in $Stages) {
    $base = $stage -replace '^d_', ''   # A5.4d 재측정 단계는 같은 정의
    if (-not $Def.ContainsKey($base)) { Write-Host "[bots] 모르는 단계: $stage"; continue }
    $d = $Def[$base]
    $out = Join-Path $Results $stage
    if (Test-Path $out) { Remove-Item -Recurse -Force $out }
    New-Item -ItemType Directory -Force $out | Out-Null

    # 1) 서버 포트가 열릴 때까지(서버 PC가 이 단계의 서버를 띄움)
    Write-Host "[bots] $stage — 서버 $($ServerIp):$Port 열리기를 기다림"
    $deadline = (Get-Date).AddMinutes($WaitOpenMin)
    while (-not (Test-ServerPort)) {
        if ((Get-Date) -gt $deadline) { "서버 포트가 $WaitOpenMin 분 동안 열리지 않음" | Set-Content "$out\error.txt" -Encoding utf8; Write-Host "[bots] 시간 초과 — 멈춤"; exit 2 }
        Start-Sleep -Seconds 3
    }
    Start-Sleep -Seconds 3

    # 2) LoadTest
    # 경로에 공백이 있어도 되게 따옴표로 감싼 한 줄로 넘긴다(PowerShell 5.1의 Start-Process는 배열 원소를 따옴표로 감싸지 않음)
    $ltArgs = "--host $ServerIp --port $Port --bots $($d.Bots) --ramp-rate $($d.Rate) --duration $($d.Dur) " +
        "--threads 8 --move-interval 200 --body male --login account --account-start $($d.Start) --town-wander --mix $Mix " +
        "--nav-dir `"$Here\nav`" --map `"$Here\map\Continent01_Logic.json`" --data-dir `"$Here\data`" " +
        "--server-proc none.exe --out `"$out\loadtest.csv`""
    "$(Get-Date -Format s) $ltArgs" | Set-Content "$out\args.txt" -Encoding utf8
    Write-Host "[bots] $stage — 봇 $($d.Bots)명, 초당 $($d.Rate), $($d.Dur)초"
    $p = Start-Process -FilePath "$Here\LoadTest.exe" -ArgumentList $ltArgs -NoNewWindow -PassThru `
        -RedirectStandardOutput "$out\console.txt" -RedirectStandardError "$out\console_err.txt"
    $null = $p.Handle   # 핸들을 잡아 두어야 끝난 뒤 ExitCode를 읽을 수 있다
    # 봇 CPU(코어 1개 = 100%)를 1초마다
    "epoch_ms,cpu_pct,cpu_pct_of_all,mem_mb" | Set-Content "$out\bot_cpu.csv" -Encoding ascii
    $prevCpu = 0.0; $prevT = Get-Date
    while (-not $p.HasExited) {
        Start-Sleep -Seconds 1
        try {
            $p.Refresh()
            $now = Get-Date; $cpuS = $p.TotalProcessorTime.TotalSeconds
            $pct = ($cpuS - $prevCpu) / (($now - $prevT).TotalSeconds) * 100
            $prevCpu = $cpuS; $prevT = $now
            "{0},{1:F0},{2:F0},{3:F0}" -f [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(), $pct, ($pct / $Cores), ($p.WorkingSet64 / 1MB) | Add-Content "$out\bot_cpu.csv"
        } catch { }
    }
    "exit $($p.ExitCode)" | Add-Content "$out\args.txt"
    # 공개 저장소에 올리므로 결과 파일의 서버 IP를 가린다(A5.4c 노트북 Claude 제안)
    foreach ($f in "args.txt", "console.txt") {
        $fp = Join-Path $out $f
        if (Test-Path $fp) { (Get-Content $fp -Raw) -replace [regex]::Escape($ServerIp), "<SERVER_IP>" | Set-Content $fp -Encoding utf8 -NoNewline }
    }
    Write-Host "[bots] $stage — 끝(exit $($p.ExitCode))"
    Get-Content "$out\console.txt" | Select-String "\[summary\] (attempts|login |enter|ping)" | ForEach-Object { Write-Host "   $_" }

    # 3) 서버가 내려가 포트가 닫힐 때까지(서버 PC가 다음 단계 준비)
    $deadline = (Get-Date).AddMinutes(10)
    while ((Test-ServerPort) -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 5 }
    # 4) 이 노트북의 TIME_WAIT 소켓이 줄 때까지(봇이 먼저 닫아 포트가 약 2분 묶임)
    while ((Get-TimeWaitCount) -gt 3000) { Write-Host "[bots] TIME_WAIT 기다림"; Start-Sleep -Seconds 15 }
}
Write-Host "[bots] 모든 단계 끝 — results 폴더를 커밋·푸시하세요(LAPTOP_TASK.md)"
