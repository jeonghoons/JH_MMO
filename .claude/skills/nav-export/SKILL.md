---
name: nav-export
description: UE 맵 지오메트리를 다시 내보내고 서버 내비메시(.nav)를 다시 만들 때, 또는 내비메시 위의 위치·경로를 질의로 점검할 때 사용. MapExport.bat, NavGenerator 실행법과 출력, --query 검증 모드.
---

# 맵 지오메트리 내보내기·내비메시 생성

UE에서 맵 지오메트리를 다시 export할 때마다 NavGenerator를 다시 실행해야 한다. 서버 이동 검증이 이 내비메시를 쓰므로 기준이 어긋나면 지나갈 수 있는 곳에서 플레이어가 끌려간다(규칙은 `Server/CLAUDE.md` "빌드 & 실행"의 NavGenerator 항목).

## 실행

- 한 번에: `Server/MapResource/MapExport.bat <맵>`(UE 커맨드렛 `MapExportCommandlet` → NavGenerator). 이 환경에서는 bat을 전체 경로로 실행한다.
- NavGenerator만: 빌드는 `msbuild Server.sln /p:Configuration=Debug /p:Platform=x64 /t:NavGenerator`.

사용법: `NavGenerator.exe <GeoBinPath> <NavOutputPath>` (인자를 생략하면 기본 맵 `ParagonSample`을 사용해 `../MapResource/Export/<맵>/Geometry/<맵>_Geo.bin`을 읽어 `../Server/Resource/Map/<맵>_Geo.nav`로 출력). 

가장 넓은 연결 영역만 걷기 가능으로 남기고 나머지 섬(능선 꼭대기, 바위·건물 윗면)은 폴리곤 flags를 0으로 꺼서 서버 질의에서 빠지게 합니다(2026-11-17, 출력의 `[Prune]` 줄). 폴리곤 메시 정점이 단일 타일 상한(65,534)을 넘으면 실패로 끝납니다. 검증용으로 `NavMesh_Output.obj`와 무작위 표본 `NavMesh_Samples.csv`(UE 좌표)를 작업 폴더에 씁니다. 

**검증용 질의 모드**(L2e, `NavQuery.cpp`): `NavGenerator.exe --query <nav 파일> <질의 파일>` — 서버 `NavmeshManager`와 같은 질의(탐색 범위·노드 2,048개·경로 폴리곤 256개)를 `.nav`에 대고 실행해 CSV로 출력합니다(UE 좌표, 한 줄에 질의 하나: `near`(가장 가까운 걷는 점·높이) / `path`(경로가 끝까지 닿는지·폴리곤 수·부분 결과·노드 부족) / `route`(path + 꺾임점) / `move`(`CanMove`와 같은 판정)). 맵을 다시 내보낸 뒤 마커가 걷는 영역 위인지, 먼 경로가 상한 안에서 끝나는지 이것으로 점검합니다.

## 함께 고칠 것

- `NavmeshManager::CanMove`를 고치면 `NavGenerator/NavQuery.cpp`의 `move`도 같이 고친다(두 프로젝트가 코드를 공유하지 않음).
- 문으로 끊긴 영역은 로직 JSON의 `PlayerStart`·같은 레벨 `PortalTrigger` 마커가 기준점이 되어 남는다(`LoadKeepSeeds`, 출력 `[Prune] 기준점 …`).
