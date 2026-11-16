# DB 스키마 마이그레이션 실행기 (A2.5, 2026-11-16)
#
#   migrations\NNNN_<날짜>_<내용>.sql 중 아직 적용하지 않은 것을 순번대로 실행한다.
#   적용 여부는 DB의 Schema_Version 테이블(Version = 파일 순번)에 남는다. 각 마이그레이션 파일이 자기 행을 직접 넣는다.
#   서버(GameServer)는 시작할 때 MAX(Version)이 코드의 DB_SCHEMA_VERSION과 다르면 시작하지 않는다.
#
# 사용법 (Server\Resource\DB 에서):
#   .\migrate.ps1 -Status          적용 상태만 본다(바꾸지 않음)
#   .\migrate.ps1                  백업 후 남은 마이그레이션을 적용한다
#   .\migrate.ps1 -SkipBackup      백업 없이 적용한다(시험용 DB)
#   .\migrate.ps1 -Server <인스턴스> -Database <DB 이름>
#
# 새 DB는 schema.sql로 만든다(이미 최신 버전 행까지 들어 있어 이 스크립트를 돌릴 필요가 없다).
# Windows 통합 인증(-E)을 쓴다. sqlcmd가 PATH에 있어야 한다.
param(
    [string]$Server = '.\SQLEXPRESS',
    [string]$Database = 'GameServer',
    [switch]$Status,
    [switch]$SkipBackup
)

$ErrorActionPreference = 'Stop'

# Schema_Version 테이블이 생기기 전에(0003 이전) 이미 적용돼 있던 마이그레이션 수.
# Schema_Version은 없는데 게임 테이블은 있는 DB는 이 버전으로 본다.
$BaselineVersion = 2

if (-not (Get-Command sqlcmd -ErrorAction SilentlyContinue)) {
    Write-Host '[migrate] sqlcmd를 찾을 수 없습니다. SQL Server 명령줄 도구를 설치하거나 PATH에 추가하세요.'
    exit 1
}

function Invoke-Scalar([string]$query) {
    $out = & sqlcmd -S $Server -d $Database -E -b -h -1 -W -Q "SET NOCOUNT ON; $query"
    if ($LASTEXITCODE -ne 0) { throw "sqlcmd 실패: $out" }
    return ($out | Where-Object { $_ -ne '' } | Select-Object -First 1)
}

function Get-CurrentVersion {
    # -1 = 게임 테이블이 없는 빈 DB
    $query = "IF OBJECT_ID(N'dbo.Schema_Version', N'U') IS NOT NULL SELECT ISNULL(MAX([Version]), 0) FROM dbo.Schema_Version; " +
             "ELSE IF OBJECT_ID(N'dbo.Player_Info', N'U') IS NOT NULL SELECT $BaselineVersion; ELSE SELECT -1;"
    return [int](Invoke-Scalar $query)
}

# 마이그레이션 파일 목록 (순번 = 파일 이름 앞 4자리)
$migrationDir = Join-Path $PSScriptRoot 'migrations'
$migrations = @()
foreach ($file in (Get-ChildItem -Path $migrationDir -Filter '*.sql' | Sort-Object Name)) {
    if ($file.Name -notmatch '^(\d{4})_') {
        Write-Host "[migrate] 이름 규칙(NNNN_<날짜>_<내용>.sql)에 맞지 않는 파일: $($file.Name)"
        exit 1
    }
    $migrations += [pscustomobject]@{ Version = [int]$Matches[1]; Name = $file.Name; Path = $file.FullName }
}

# 순번은 1부터 빈틈·중복 없이 이어져야 한다.
for ($i = 0; $i -lt $migrations.Count; $i++) {
    if ($migrations[$i].Version -ne $i + 1) {
        Write-Host "[migrate] 순번이 이어지지 않습니다: $($migrations[$i].Name) (기대한 순번 $($i + 1))"
        exit 1
    }
}

$current = Get-CurrentVersion
if ($current -lt 0) {
    Write-Host "[migrate] $Server / $Database 에 게임 테이블이 없습니다. 새 DB는 schema.sql로 만드세요."
    exit 1
}

$latest = 0
if ($migrations.Count -gt 0) { $latest = $migrations[-1].Version }
Write-Host "[migrate] 대상: $Server / $Database   현재 버전: $current   최신 버전: $latest"

if ($current -gt $latest) {
    Write-Host '[migrate] DB 버전이 저장소보다 높습니다. 저장소를 최신으로 받았는지 확인하세요.'
    exit 1
}

foreach ($m in $migrations) {
    $state = '적용됨'
    if ($m.Version -gt $current) { $state = '대기' }
    Write-Host ("  {0:D4}  {1,-6}  {2}" -f $m.Version, $state, $m.Name)
}

$pending = @($migrations | Where-Object { $_.Version -gt $current })
if ($Status -or $pending.Count -eq 0) {
    if ($pending.Count -eq 0) { Write-Host '[migrate] 적용할 마이그레이션이 없습니다.' }
    exit 0
}

if (-not $SkipBackup) {
    # 파일 이름만 주면 SQL Server의 기본 백업 폴더에 저장된다.
    $backupName = '{0}_before_{1:D4}_{2}.bak' -f $Database, $pending[0].Version, (Get-Date -Format 'yyyyMMdd_HHmmss')
    Write-Host "[migrate] 백업: $backupName"
    & sqlcmd -S $Server -d master -E -b -Q "BACKUP DATABASE [$Database] TO DISK = N'$backupName' WITH INIT;"
    if ($LASTEXITCODE -ne 0) {
        Write-Host '[migrate] 백업 실패. 마이그레이션을 실행하지 않았습니다.'
        exit 1
    }
}

foreach ($m in $pending) {
    Write-Host "[migrate] 적용: $($m.Name)"
    # -b: 오류가 나면 중단하고 종료 코드를 돌려준다. -I: QUOTED_IDENTIFIER ON. -f 65001: 파일을 UTF-8로 읽는다.
    & sqlcmd -S $Server -d $Database -E -b -I -f 65001 -i $m.Path
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[migrate] 실패: $($m.Name). 여기서 멈춥니다(각 파일은 한 트랜잭션이라 이 파일의 변경은 되돌려졌습니다)."
        exit 1
    }
    $after = Get-CurrentVersion
    if ($after -ne $m.Version) {
        Write-Host "[migrate] $($m.Name) 실행 후 버전이 $after 입니다(기대 $($m.Version)). 파일 끝에서 Schema_Version 행을 넣는지 확인하세요."
        exit 1
    }
}

Write-Host "[migrate] 완료. 현재 버전: $(Get-CurrentVersion)"
exit 0
