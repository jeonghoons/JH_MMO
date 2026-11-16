# 아이템·인벤토리·거래 설계

목표 구조만 기록합니다. 구현 순서는 [ROADMAP.md](ROADMAP.md) 마일스톤 1(1-3·1-4)을 따르고, 이 문서의 테이블·패킷은 해당 항목을 구현할 때 만듭니다.
(2026-11-16 A3.1 작성, 결정 D-152~D-159. 장비 외형·슬롯은 [DESIGN_CHARACTER_EQUIPMENT.md](DESIGN_CHARACTER_EQUIPMENT.md), CSV 규약은 [DESIGN_DATA.md](DESIGN_DATA.md), 동시성 주의점은 [AUDIT_2026-11-06_CONCURRENCY.md](AUDIT_2026-11-06_CONCURRENCY.md))

## 현재 상태 (2026-11-17)
- A3.5 완료(2026-11-17): 줍기 `CS_PICKUP_ITEM`·버리기 `CS_DROP_ITEM`(서버). 구현 내용은 아래 "A3.5 구현 메모". 클라이언트 표시·입력(A6.2)도 완료(사용자 PIE 확인)했습니다. 소비 아이템·거래·경매 코드는 아직 없습니다.
- A3.4 완료(2026-11-16): 몬스터 처치 시 드랍 롤 → 바닥 드랍(`DropItem`, `OBJECT_TYPE_ITEM`) 생성·소유권 기록·드랍 전용 시야·소멸·방당 상한. 구현 내용은 아래 "A3.4 구현 메모".
- A3.3 완료(2026-11-16): `Player_Item`(마이그레이션 0004, `Player_Equipped` 이전·삭제), 서버 `Inventory`·아이템 작업 공통 경로(`itemOp`), 장착을 `item_db_id` 기준·DB 성공 뒤 반영으로 전환, 개발 지급 `CS_DEV_GIVE`, `SC_INVENTORY`·`SC_INVENTORY_UPDATE`·`SC_ITEM_RESULT`, 클라이언트 `UJMInventoryManager`(데이터만 — 창은 A6.1)와 콘솔 `Give`. 구현 내용은 아래 "A3.3 구현 메모".
- 골드는 진행 상황 스냅숏에서 빠졌고 아이템 트랜잭션에서만 증감합니다(지금 들어오는 경로는 개발 지급뿐, 골드 드랍은 A3.4).
- A2.7 완료(2026-11-16): 같은 캐릭터의 중복 접속은 기존 접속을 끊고 새 로그인을 받습니다(아래 "중복 접속 차단"). 인벤토리·아이템 작업을 추가할 때는 `Room::NotifyClosedIfDone`의 조건과 호출 지점에 함께 넣어야 합니다.
- A3.2 완료(2026-11-16): `DropTable.csv`·`EconomyTable.csv`와 로더(`DataManager::GetDrops`·`RollDrops`·`GetEconomy`), `ItemTable` 새 열과 8001 HP 물약, UE `FItemTableRow`·`DT_ItemTable`이 들어갔습니다. 드랍 롤은 A3.4에서 `RollDrops`를 호출합니다. 열별 허용 범위와 현재 행은 [DESIGN_DATA.md](DESIGN_DATA.md).

## 범위와 하지 않는 것
- **범위**: DropTable, 아이템 인스턴스와 DB, 인벤토리, 드랍·줍기·버리기, 장착 검사 확장, 소비 아이템(HP 물약 1종)과 액티브 칸 저장, 골드, 중복 접속 차단(A2.7), 1:1 거래, 경매장(즉시 구매), 패킷, 시험 항목.
- **하지 않는 것**: 경매 입찰, 수수료, 파티·분배, 피해 기여 소유권, 인벤토리 안에서 스택을 나누거나 합치는 조작, 경매 등록의 일부만 사는 구매, 인벤토리 정렬·칸 이동, 강화·옵션, NPC 상점, 귀속(거래 불가) 아이템, 다른 Room 플레이어와의 1:1 거래, 바닥 아이템의 DB 저장, 골드 버리기.

## 공통 원칙
- **DB가 권위, DB 성공 뒤 메모리 반영 (D-153)**: 아이템·골드의 소유가 바뀌는 모든 동작은 ① Room 잡에서 검증 ② `DatabaseWorker`에서 한 트랜잭션 ③ 결과를 Room 잡으로 돌려받아 메모리·패킷 반영 순서로 합니다. 주기 저장·퇴장 저장에 기대지 않으므로 서버가 강제 종료돼도 손실·복제가 없습니다(감사 F6).
- **플레이어당 아이템 작업은 동시 1건**: `Player::itemOp.inFlight`가 켜져 있는 동안 그 플레이어의 새 아이템·골드 요청은 `BUSY`로 거부합니다(쌓아 두지 않음). 거래 확정은 두 사람 모두, 경매는 요청자에게 겁니다.
- **DB 결과가 왔을 때 플레이어가 방에 없으면** 메모리·패킷 반영만 건너뜁니다. DB에는 이미 반영됐고 다음 로그인 때 읽습니다. 월드에 미치는 효과(드랍 제거·생성)는 그대로 적용합니다.
- 모든 요청 핸들러·Room 잡 첫머리에서 세션 연결과 대상을 다시 확인합니다(감사 F1·F2). DB 스레드에서 직접 `Send`하지 않습니다.
- 소유·거래·경매·소유권은 오브젝트 id(세션 번호, 재접속 때 바뀜)가 아니라 DB `PlayerId`로 묶습니다.
- 더미 봇(`isDummy`, 전원이 DB 한 행)은 아이템·골드·거래·경매 요청을 모두 `NOT_ALLOWED`로 거부하고 드랍 소유권도 받지 않습니다. 아이템 시험은 개별 계정 봇으로 합니다.
- 수치는 모두 CSV(`EconomyTable`·`ItemTable`·`DropTable`)에 둡니다. 클라이언트가 알아야 하는 값은 패킷으로 받습니다(`SC_DEAD.revive_delay_ms` 선례).

## 데이터

### ItemTable 추가 열 (A3.2, D-154·D-156)
| 열 | 값 | 읽는 쪽 | 설명 |
|---|---|---|---|
| `RequiredLevel` | 1 이상 | 서버·클라 | 장착(소비 아이템은 사용)에 필요한 레벨. 지금은 전부 1. "티어 제한"은 높은 티어에 높은 값을 주는 것으로 표현 |
| `MaxStack` | 1~9999 | 서버·클라 | 한 칸에 겹치는 최대 수량. 장비는 1 |
| `UseEffect` | `None`/`HealHp` | 서버·클라 | 사용 효과. 장비는 `None` |
| `UseValue` | 0 이상 | 서버 | `HealHp`의 회복량 |
| `UseCooldownMs` | 0~600000 | 서버·클라 | 같은 아이템 종류의 재사용 시간 |

- **장착할 수 없는 아이템**: `Slot` = `None`, `Tier` = `None`(서버 `ItemSlot`·`ItemTier`에 `None` 추가, UE enum에는 이미 있음). itemId 첫째 자리 8 = 소비, 둘째 자리 0 = 티어 없음. 첫 행 `8001` HP 물약(`UseEffect` `HealHp`).
- 추가 검증(어기면 서버 시작 중단): 장비는 `MaxStack` 1·`UseEffect` `None`, `Slot` `None`은 ID 첫째 자리 8·`WeaponType` `None`·보너스 0·`Starter` 0·`UseEffect`가 `None`이 아님. 구현하면서 더한 것(A3.2): `Slot` `None`과 `Tier` `None`은 항상 함께, `HealHp`는 `UseValue` > 0.
- 기본 장비도 거래·경매·버리기를 허용합니다(거래 불가 열 없음, D-155).
- UE: `FItemTableRow`에 같은 이름의 필드를 추가하고(`UseEffect`는 새 UENUM `EItemUseEffect`) `DT_ItemTable`을 다시 임포트합니다. 물약 아이콘 1개가 필요합니다(콘텐츠 동결 중이라 임시 아이콘 허용). 외형 열을 만드는 `jm_kit.item_table.export`는 `items.json`에 없는 행이 있으면 실패하므로 **메시가 없는 행(`Slot` `None`)은 건너뛰도록** 함께 고칩니다. `Slot`·`Tier`의 `None`은 UE enum에서 숨김(`UMETA(Hidden)`) 값이지만 CSV 임포트가 됩니다(A3.2에서 확인 — 8001 행 `0 Problems`). 물약 초기값: `MaxStack` 20, `UseValue` 200, `UseCooldownMs` 5000, 아이콘 `Content/UI/Icons/Item/T_Icon_Potion_Hp`(임시).

### DropTable (서버 전용, A3.2, D-157)
한 행 = 몬스터 한 종류가 떨어뜨릴 수 있는 것 하나. 처치 때 그 몬스터의 모든 행을 **각각 독립으로** 굴립니다.

| 열 | 값 | 설명 |
|---|---|---|
| `Name` | 1001~ | 행 ID |
| `MonsterId` | MonsterTable ID | 없는 ID면 시작 중단 |
| `ItemId` | ItemTable ID 또는 0 | 0 = 골드 |
| `DropRate` | 0~10000 | 만분율(10000 = 100%) |
| `MinCount`·`MaxCount` | 1 이상, Min ≤ Max | 아이템이면 `MaxStack` 이하(장비는 1·1), 골드면 금액 범위(`EconomyTable.GoldMax` 이하) |

- (`MonsterId`, `ItemId`) 중복은 시작 중단. UE에는 임포트하지 않습니다.
- 몬스터 10종·아이템 20종 규모까지 이 구조를 씁니다. 여러 몬스터가 같은 묶음을 공유해야 하면 그때 그룹 열을 추가합니다.

### EconomyTable (서버 전용, A3.2)
행 `1001` 하나. 범위를 벗어나면 시작 중단. 값은 초기 제안값입니다.

| 열 | 초기값 | 설명 |
|---|---|---|
| `InventorySlots` | 30 | 인벤토리 칸 수(장착품 제외) |
| `ItemMinIntervalMs` | 100 | 같은 플레이어의 아이템 요청 최소 간격 |
| `GoldMax` | 2000000000 | 보유 골드 상한(넘기는 획득·거래·수령은 거부). CSV 로더가 int32만 읽으므로(`CsvFile::GetInt`) 그 범위 안. DB 열과 패킷의 골드·가격 필드는 모두 int64 |
| `DropOwnerSec` | 30 | 막타 소유 시간 |
| `DropLifeSec` | 60 | 드랍이 사라지기까지(`DropOwnerSec` 이상) |
| `DropScatterCm` | 80 | 사망 지점에서 흩뿌리는 반경 |
| `PickupRangeCm` | 300 | 줍기 거리 |
| `MaxDropsPerRoom` | 500 | 방 하나의 드랍 상한(넘으면 가장 오래된 것부터 제거) |
| `TradeRangeCm` | 500 | 거래 요청·유지 거리 |
| `TradeRequestTimeoutSec` | 15 | 요청 응답 대기 |
| `TradeTimeoutSec` | 180 | 수락부터 완료까지 |
| `TradeMaxItems` | 8 | 한 사람이 올리는 아이템 수 |
| `AuctionDurationSec` | 86400 | 등록 기간 |
| `AuctionMaxListings` | 10 | 1인 등록 수(수수료가 없어 도배 방지는 이것으로) |
| `AuctionMinPrice`·`AuctionMaxPrice` | 1 · 100000000 | 가격 범위 |
| `AuctionPageSize` | 20 | 목록 한 쪽 |
| `AuctionExpireCheckSec` | 60 | 만료 검사 주기 |

## 아이템 인스턴스와 DB (D-153)

### 테이블
- **`Player_Item`** (A3.3, 마이그레이션 0004) — 아이템 한 개(스택이면 한 묶음) = 한 행.
  - `ItemDbId` bigint IDENTITY PK, `OwnerId`(→ `Player_Info.PlayerId`), `ItemId`, `Count`(≥ 1), `Location`, `EquipSlot`(장착 중이면 `EquipPart` 1~7, 아니면 0), `CreatedAt`, `UpdatedAt`.
  - `Location`: 1 인벤토리 · 2 장착 · 3 경매 보관(에스크로) · 4 수령함. 아이템은 **행을 지우지 않고 `OwnerId`·`Location`만 바꿔** 이동합니다. 예외: 버리기·소비로 0개가 되면 DELETE, 스택의 일부만 거래·경매에 올리면 원래 행의 `Count`를 줄이고 옮기는 수량으로 새 행을 INSERT(새 `ItemDbId`).
  - 인덱스 (`OwnerId`, `Location`), 장착 슬롯 중복 방지용 필터 유니크 인덱스 (`OwnerId`, `EquipSlot`) WHERE `Location` = 2.
  - `BagSlot`(A6.1, 마이그레이션 0005, D-166): 인벤토리(`Location` 1) 행의 칸 번호(0부터), 그 밖의 위치는 -1. 필터 유니크 인덱스 (`OwnerId`, `BagSlot`) WHERE `Location` = 1로 한 칸에 두 행이 들어가지 못합니다. 장착 교체·칸 맞바꿈은 **UPDATE 한 문장**으로 합니다 — 두 유니크 인덱스는 문장이 끝날 때 검사하므로 중간 상태가 없고, 두 문장으로 나누면 어느 순서든 한쪽 인덱스를 어깁니다. 아이템을 인벤토리 밖(경매 보관·수령함·거래 상대)으로 옮기는 작업은 `BagSlot`을 -1로, 들여오는 작업은 받는 쪽의 빈 칸 번호로 함께 바꿔야 합니다(A3.5·A4.2·A4.3).
  - 마이그레이션: `Player_Equipped` 행을 `Location` 2로 옮긴 뒤 `Player_Equipped`를 삭제합니다. 가입 트랜잭션(`BuildSignUp`)의 기본 장비도 이 테이블에 넣습니다. `ItemTable.Starter` 목록과 마이그레이션 SQL이 같아야 한다는 규칙(D-118)은 유지합니다.
- **`Player_QuickSlot`** (A3.6) — (`PlayerId`, `SlotIndex` 1~5) PK, `ItemId`. 액티브 칸에 올린 아이템 종류. 5칸 스냅숏으로 저장합니다.
- **`Trade_Log`** (A4.2) — `TradeId` IDENTITY, `PlayerA`·`PlayerB`, `GoldA`·`GoldB`, `ItemsA`·`ItemsB`(`ItemDbId:ItemId:Count` 목록 문자열), `CreatedAt`.
- **`Auction_Listing`** (A4.3) — `ListingId` IDENTITY, `SellerId`, `ItemDbId`, `ItemId`, `Count`, `Price`, `State`(1 판매 중 · 2 팔림 · 3 만료 · 4 취소), `BuyerId`, `CreatedAt`, `ExpireAt`, `ClosedAt`. 인덱스 (`State`, `ExpireAt`).
- **`Player_Mail`** (A4.3, 수령함) — `MailId` IDENTITY, `PlayerId`, `Kind`(1 판매 대금 · 2 구매품 · 3 만료 반환 · 4 취소 반환), `Gold`, `ItemDbId`(없으면 NULL), `ListingId`, `CreatedAt`.
- 모두 D-150 규칙(마이그레이션 + `schema.sql` + `DB_SCHEMA_VERSION`, SQL은 `DbQueries.h`)을 따르고, 테이블은 그 항목을 구현할 때 추가합니다.

### 골드 (D-152)
- 값은 `Player_Info.Gold` 그대로입니다. 바꿀 때는 `UPDATE … SET Gold = Gold + ?`(차감은 `AND Gold >= ?` 조건)로 **증감**하고, 바뀐 잔액을 결과로 돌려받아 메모리에 씁니다. 메모리 값으로 덮어쓰지 않습니다.
- 진행 상황 스냅숏(`DB_PlayerProgress`, D-151)에서 골드를 뺍니다. 레벨·경험치·HP는 그대로 주기·퇴장 저장입니다.
- 얻는 곳: 몬스터 골드 드랍(DropTable `ItemId` 0), 거래, 경매 판매 대금, 개발 지급 명령. 경험치·레벨 보상은 A2.3b에 남습니다.

### 트랜잭션 범위
| 동작 | 한 트랜잭션에 넣는 것 |
|---|---|
| 줍기 | 아이템: 같은 종류 스택에 `Count` 더하기 또는 행 INSERT / 골드: 증액 |
| 버리기 | 행 DELETE(그 뒤 메모리에서 바닥 드랍 생성) |
| 장착·해제 | 관련 행(새 장비·옛 장비·자동 해제되는 방패, 최대 3행)의 `Location`·`EquipSlot` |
| 물약 사용 | `Count` − 1(0이면 DELETE) |
| 거래 확정 | 양쪽 아이템 이동(통째면 `OwnerId` 변경, 일부면 원래 행 `Count` 감소 + 상대 소유 새 행 — 어느 쪽이든 `Count >= 제시 수량` 조건) + 양쪽 골드 증감 + `Trade_Log` 1행 |
| 경매 등록 | 아이템을 에스크로로(통째면 `Location` 1 → 3, 일부면 원래 행 `Count` 감소 + `Location` 3 새 행) + `Auction_Listing` INSERT |
| 경매 구매 | 등록 `State` 1 → 2 + 구매자 골드 차감 + 아이템 `OwnerId` = 구매자·`Location` 4 + `Player_Mail` 2행(구매품, 판매 대금) |
| 경매 취소·만료 | 등록 `State` 변경 + 아이템 `Location` 3 → 4 + `Player_Mail` 1행 |
| 수령 | 골드 증액 또는 아이템 `Location` 4 → 1 + `Player_Mail` DELETE |

- 모든 UPDATE는 기대 상태를 WHERE에 적고(`OwnerId`·`Location`·`State`) 바뀐 행 수가 기대와 다르면 롤백합니다. 메모리 검증이 놓친 경우의 두 번째 방어선입니다.

## 인벤토리 (A3.3, D-155)
- 서버 `Inventory`는 `Player`가 소유하는 컴포넌트입니다. 로그인 때 `Location` 1·2 행을 읽어 채우고(ItemTable에 없는 `ItemId` 행은 WARN 후 메모리에 올리지 않음, DB 행은 그대로 둠), 장착 7칸은 지금처럼 `ObjectInfo.equip_items`(itemId)에도 반영합니다. 장착 행이 맞지 않으면(슬롯 불일치, 검이 아닌데 방패 — D-119·D-139) 메모리에서 인벤토리로 내리고 DB도 `Location` 1로 고칩니다. 이 때문에 칸 수가 상한을 넘을 수 있으며, 넘은 동안에는 새로 들어오는 것만 거부합니다.
- 칸 수 = 인벤토리 위치의 행 수이고 `InventorySlots`를 넘을 수 없습니다. **장착품은 칸을 차지하지 않습니다.**
- **칸 위치(A6.1, D-166)**: 인벤토리 행은 칸 번호(`BagSlot`, 0 ~ `InventorySlots` - 1)를 갖고 서버가 DB에 저장합니다(`ItemInfo.slot`). 새로 들어오는 행(지급·줍기·장착 해제)은 가장 앞의 빈 칸에 넣고, 빈 칸이 없으면 가득 찬 것입니다. 장착 교체는 옛 장비가 새 장비가 있던 칸으로 내려옵니다(그래서 가득 차 있어도 됨). `CS_MOVE_ITEM(item_db_id, to_slot)`은 대상 칸이 비어 있으면 옮기고 다른 아이템이 있으면 맞바꿉니다 — 소유는 안 바뀌지만 칸도 DB가 권위라 `itemOp` 경로(검증 → DB → 성공 뒤 반영, `SC_INVENTORY_UPDATE`)를 탑니다. 거부: 내 인벤토리 행이 아님 `NO_ITEM`, 범위 밖·쓸 수 없는 칸 `BAD_SLOT`, 같은 칸은 응답 없음. 로그인 보정으로 내려오는 행만 칸 수를 넘는 번호를 받을 수 있고(버리지 않기 위해), 클라이언트는 넘친 칸도 그립니다. ItemTable에 없어 메모리에 올리지 않은 행의 칸은 비워 둡니다(`Inventory::ReserveSlot`).
- 스택: 줍기·지급으로 들어올 때만 같은 종류의 덜 찬 스택에 먼저 채우고 남으면 새 행을 만듭니다. 전부 들어갈 자리가 없으면 통째로 거부합니다(`INVENTORY_FULL`).
- **스택 나누기는 거래와 경매에서만** 합니다(D-158·D-159): 거래 제시와 경매 등록에 수량을 적어 스택의 일부만 올릴 수 있습니다. 인벤토리 안에서 나누거나 합치는 조작은 없고, 버리기는 스택 통째입니다. 거래·수령으로 받은 아이템은 기존 스택에 합치지 않고 한 칸(새 행)을 차지합니다.
- 방 입장 직후 본인에게 `SC_INVENTORY`(전체 + 골드 + 칸 수 + 액티브 칸 + 줍기·거래 거리)를 보내고, 그 뒤로는 `SC_INVENTORY_UPDATE`(바뀐 행·지워진 `ItemDbId`·현재 골드)만 보냅니다.
- **개발 지급 명령**: `CS_DEV_GIVE(item_id, count)`(item_id 0 = 골드). `ServerConfig` `[Debug] DevCommands=true`일 때만 받고 기본은 꺼 둡니다. 다른 아이템 작업과 같은 경로(`itemOp`, DB 성공 뒤 반영)를 탑니다. 클라이언트는 콘솔 `Give <itemId> <count>`.

### A3.3 구현 메모 (2026-11-16, D-160~D-165)
- **흐름**: 요청 → Room 잡 검증(`Room::CheckItemRequest`: 방 안 → 더미 봇 아님 → 요청 간격 → 진행 중인 작업 없음) → `Room::StartItemOp`(`itemOp.inFlight` 켬) → `DatabaseWorker::ExecItemOp`(한 트랜잭션) → `Room::OnItemOpDone`(끔 → 성공이고 방에 있으면 반영 → `NotifyClosedIfDone`). 계획과 결과는 `DB_ItemOp`·`DB_ItemOpResult`(`ServerData.h`)에 담겨 그대로 오갑니다. 새 작업(줍기·버리기·사용)은 `ItemOpKind`를 늘려 같은 경로에 넣습니다.
- **SQL 틀**: 모든 아이템 트랜잭션은 `DbQueries.h`의 `ItemOpHead`/`ItemOpTail`을 씁니다 — TRY 안에서 `BEGIN TRAN`, 문장마다 바뀐 행 수를 `@n`에 더하고, 기대한 수와 다르면 롤백. 결과는 항상 결과 집합 하나(`Ok`, `Kind` 0 상태 · 1 새 `ItemDbId` · 2 골드 잔액, `V`)이고 성공 상태 행을 읽었을 때만 성공입니다.
- **쿼리 모양(D-165)**: 장착 = 고정 파라미터 쿼리(`ItemEquip`), 지급·로그인 보정 = 조립식. 측정(봇 5명): 장착 DB 실행 p50 약 4ms, 조립식 지급은 평균 약 20ms(요청마다 컴파일). **줍기(A3.5)·물약 사용(A3.6)처럼 자주 오는 작업은 고정 쿼리로 만듭니다.**
- **`Player_Item` 제약**: `Count` ≥ 1, `Location` 1~4, 장착(2)이면 `EquipSlot` 1~7·아니면 0(CHECK), `OwnerId` FK, 인덱스 (`OwnerId`, `Location`), 필터 유니크 `UX_Player_Item_Equip`(`OwnerId`, `EquipSlot`) WHERE `Location` = 2. 필터 인덱스 때문에 `QUOTED_IDENTIFIER`·`ANSI_NULLS`가 켜진 연결에서만 쓸 수 있습니다(`sqlcmd`는 `-I`). `Count` 열은 int(스택 상한 9999), 패킷의 수량은 int64입니다.
- **장착 요청의 조용한 종료**: 이미 그 슬롯에 장착한 행을 다시 요청하거나 빈 슬롯을 해제하면 응답 없이 끝냅니다(빈도 제한 시간은 갱신). 검증 순서는 형식(슬롯·보유·아이템 슬롯·방패는 한손검) → 상태(사망·시전·레벨) → 빈도 → 진행 중인 작업(`BUSY`) → 빈 칸입니다.
- **로그인 보정**: 대상은 슬롯이 맞지 않는 장착 행, 검이 아닌데 낀 방패, 그리고 ItemTable에 없는 종류의 장착 행(메모리에는 올리지 않지만 DB의 장착 슬롯을 계속 차지하지 않게 `Location` 1로 내림)입니다. `Player::InitFromDb`가 메모리에서 내리고 `pendingEquipFix`에 적으면 `Room::PlayerEnterRoom`이 `SC_INVENTORY`를 보낸 직후 `LoginFix` 작업으로 DB를 고칩니다. 실패하면 ERROR 로그만 남기고 다음 로그인 때 다시 합니다(그 사이 그 행의 장착 요청은 트랜잭션 조건 불일치로 롤백).
- **더미 봇**: 장착은 `EQUIP_RESULT_NOT_ALLOWED`(15), 아이템 요청은 `ITEM_RESULT_NOT_ALLOWED`, `SC_INVENTORY`를 보내지 않습니다(D-161·D-164). 장비 외형은 지금처럼 공용 행에서 읽어 `ObjectInfo.equip_items`로 보입니다.
- **장착 지연(D-163)**: 기준은 200명 램프에서 요청 → 적용 p99 50ms. 2026-11-16 측정에서 p50 3~5ms·p99 75~230ms로 기준을 넘었고, DB 실행은 p99 4~22ms라 나머지는 Room 잡 대기입니다(같은 구간 틱 지연 p99와 같은 값 — 개별 계정 봇 200명이 PlayerStart 한 곳에 몰린 시나리오, 50명이면 p99 14~53ms). 기록만 하고 마일스톤 2(P1b 송신 묶기) 뒤 다시 잽니다. 상세는 `TESTING.md` A3.3.
- **결과를 알 수 없는 실패**: 트랜잭션 결과는 "성공"·"롤백 확인(결과 행 `Ok` 0)"·"불명(실행 오류, 결과 행 없음 — 예: COMMIT 뒤 연결 끊김)" 셋으로 나눕니다(`DB_ItemOpResult.unknown`). 롤백이 확인되면 메모리 = DB이므로 `DB_ERROR`만 알리고, 불명이면 DB에 반영됐을 수 있으므로 **그 플레이어의 접속을 끊어 다음 로그인 때 DB 값을 다시 읽게 합니다.** 줍기(A3.5)에서는 불명일 때 드랍을 바닥에 되돌리지 않습니다(되돌리면 복제 가능).
- **잠금 대기**: 아이템 트랜잭션은 `SET LOCK_TIMEOUT 5000`으로 시작합니다. 넘으면 오류 1222로 CATCH에서 롤백되어 "롤백 확인" 실패가 됩니다(DB 작업 스레드와 `itemOp`가 끝없이 묶이지 않게). 연결에 남는 설정이라 같은 연결의 로그인·저장 쿼리에도 적용됩니다.
- **수량 상한**: 지급 수량은 `MaxStack × InventorySlots`를 넘으면 계획을 세우기 전에 `INVENTORY_FULL`로 거부합니다(`Inventory::PlanAdd`와 `PlayerDevGive` 양쪽 — 큰 값의 계산 넘침 방어, 줍기도 같은 함수를 씀).
- **시험용 설정**: `[Debug] DevCommands`(지급 허용), `DbItemFailEvery`(N번에 한 번 강제 롤백, D-162), `DbSaveDelayMs`(아이템 트랜잭션에도 적용).
- **클라이언트**: `UJMInventoryManager`(GameInstance 서브시스템)가 `SC_INVENTORY`·`SC_INVENTORY_UPDATE`를 그대로 담고 `OnInventoryChanged`를 냅니다. `SC_ITEM_RESULT`·장착 거부는 시스템 메시지(`BUSY`·`TOO_FREQUENT`는 띄우지 않음). 콘솔 `Give <itemId> <count>`, `Equip <part> <itemId>`(인벤토리에서 그 종류의 `item_db_id`를 찾아 보냄).

## 드랍·줍기·버리기 (A3.4·A3.5, D-157)
1. **드랍 롤**: `Monster::OnDead(attacker)`에서 방에 드랍 생성을 요청합니다. DropTable의 그 몬스터 행을 각각 굴려, 걸린 행마다 사망 지점 `DropScatterCm` 안(내비메시 위)에 드랍 오브젝트를 하나씩 만듭니다.
2. **드랍 오브젝트**: `GameObject`를 상속한 `DropItem`(`OBJECT_TYPE_ITEM`). `ObjectInfo.template_id` = itemId(0 = 골드), 새 필드 `item_count`(12번, int64) = 수량(골드는 금액). 오브젝트 id는 전용 생성기(1,000,000,000부터 증가, int32 끝에 닿으면 처음으로 돌아가되 쓰는 중인 id는 건너뜀)로 플레이어(세션 번호)·몬스터(100001~)와 겹치지 않게 합니다. DB에는 없습니다(서버를 재시작하면 사라짐).
3. **소유권**: 막타 플레이어의 `PlayerId`와 만료 시각(`DropOwnerSec`)을 기록합니다. 만료는 타이머가 아니라 줍기 요청 때 시각 비교로 판정합니다. 공격자를 알 수 없거나(투사체가 날아가는 동안 퇴장) 더미 봇이면 소유권 없이 만듭니다.
4. **AOI**: 드랍은 캐릭터용 시야와 **분리**합니다. `GameMap`에 드랍 전용 색인(`AoiIndex` 하나 더)을 두고, 플레이어는 `_viewList`와 별개인 드랍 시야 목록을 갖습니다. 갱신 시점은 플레이어 이동 처리(`PlayerMove`의 `UpdateMap` 뒤), 입장·퇴장, 드랍 생성·제거뿐이고 판정은 같은 반경(R, R+H)입니다. 플레이어가 다가오면 `SC_ADD_OBJECT`, 멀어지거나 드랍이 사라지면 `SC_REMOVE_OBJECT`입니다. 드랍은 움직이지 않습니다. 이렇게 하면 `Room::UpdateView`·`BroadcastAOI`(인자가 `Character`, `_viewList`는 캐릭터 id만)와 몬스터 이동·`ViewAudit`·시야 지표를 건드리지 않습니다. A3.4에서 드랍이 많을 때의 틱 지연을 전후 비교합니다.
5. **소멸**: 생성 때 `ReserveJob(DropLifeSec, RemoveDrop, id)`. 실행 때 id로 다시 찾아 없으면(이미 주움) 끝냅니다. 줍는 중이면 건너뛰고 DB 결과 처리에서 수명을 다시 봅니다. `MaxDropsPerRoom` 초과로 지울 때도 줍는 중인 것은 건너뜁니다.
6. **줍기** `CS_PICKUP_ITEM(object_id)`: 검증(방 안, 생존, 요청 간격, `itemOp` 없음, 드랍 존재, 줍는 중 아님, 거리 ≤ `PickupRangeCm`, 소유자이거나 소유 시간 경과, 칸·골드 상한) → 드랍에 **줍는 중** 표시 + `itemOp` 켬 → DB → 성공이면 드랍 제거(`SC_REMOVE_OBJECT`) + `SC_INVENTORY_UPDATE`, 실패면 표시를 풀고 `DB_ERROR`. 줍는 중인 드랍에 온 다른 요청은 `BUSY`이므로 **경쟁해도 한 명만 얻습니다.**
7. **버리기** `CS_DROP_ITEM(item_db_id)`: 생존 중, 인벤토리 위치의 행만(장착 중·거래에 올린 것 제외) → DB DELETE → 성공 뒤 내 위치(발밑 `DropScatterCm` 안의 내비메시 위)에 소유권 없는 드랍 생성. 바닥에서 다시 주우면 새 `ItemDbId`를 받습니다.
- 클라이언트(A6.2): `UJMObjectManager::HandleSpawn`에 ITEM 분기와 드랍 전용 맵(`Objects`는 캐릭터 전용이라 따로 둠)을 추가합니다. 표시는 공용 메시 하나(아이템·골드 구분만)로 하고, 거리 안에서 가장 가까운 드랍에 줍기 안내를 띄워 **F 키**(`IA_Interact` 신규)로 줍습니다. 소유권은 클라이언트에 알리지 않고 거부 문구(`NOT_OWNER`)로 안내합니다.

### A3.4 구현 메모 (2026-11-16)
- **보관**: 드랍은 `Room::_drops`(id → `DropItem`)에만 두고 `_objects`에는 넣지 않습니다. `_objects`의 대상을 `Character`로 바로 캐스팅하는 전투·시야 코드(`ExecuteSkillHit`·`ApplyDelayedDamage`·`AuditViews` 등)가 있어, 섞으면 드랍 id를 대상으로 한 요청이 위험해집니다. 틱 `Update` 순회에도 들어가지 않습니다.
- **id**: 방마다 `DROP_ID_BEGIN`(1,000,000,000)부터 증가(`Room::NextDropId`), int32 끝에서 처음으로 돌아가며 쓰는 중인 id는 건너뜁니다. 클라이언트는 한 방만 보므로 방 사이에 겹쳐도 됩니다.
- **생성**: `Monster::OnDead` → `Room::SpawnMonsterDrops`(같은 Room 잡 안에서 바로) → 걸린 행마다 `GameMap::GetRandomPosNear(사망 지점, DropScatterCm)` → `Room::CreateDrop`. 소유권은 막타가 개별 계정 플레이어일 때만 `ownerUID`(= `PlayerId`)·`ownerUntilMs`를 적고, 판정은 `DropItem::CanPickup`(줍기 요청 때 시각 비교, A3.5에서 사용)입니다.
- **드랍 시야**: `GameMap::_dropAoi`(드랍 전용 `AoiIndex`) + `Player::dropView`(지금 보이는 드랍 id 집합). 맞추는 곳은 `Room::UpdateDropView`(입장·`PlayerMove`·`PlayerRevive`의 `UpdateMap` 뒤 — 들어옴 ≤ R, 나감 > R+H), `CreateDrop`(R 안 플레이어에게 ADD), `RemoveDrop`(R+H+1m 안에서 `dropView`에 그 id가 있는 플레이어에게 REMOVE). 방에 드랍이 없고 보는 것도 없으면 `UpdateDropView`는 바로 돌아옵니다. **플레이어 위치를 바꾸는 경로를 추가하면 `UpdateMap` 뒤에 `UpdateDropView`도 불러야 합니다.**
- **제거 경로**: 소멸 = `ReserveJob(DropLifeSec)` → `Room::ExpireDrop`(없으면 끝, `expireAtMs`가 아직 멀면 같은 id의 새 드랍이라 끝, `picking`이면 건너뜀). 상한 = `CreateDrop`이 만들기 전에 `_dropOrder`(만든 순서) 앞에서부터 `picking`이 아닌 것을 지움. 줍기(A3.5)도 `Room::RemoveDrop`을 씁니다.
- **지표**: `[metrics] drops(now kills/created/expired/evicted)`, `Log.Level=Debug`에서 `[Drop] 생성 id= item= count= owner= pos=`.
- **부하(TESTING 2026-11-16 A3.4)**: 기본 `DropTable`에서는 드랍이 없을 때와 틱 지연·틱 본문이 같은 수준입니다. 전 행 100%(처치당 12개) + 상한 지속 제거라는 최악 조건에서는 틱 본문 p99가 약 3배(15 → 44ms)로 늘었는데, 드랍마다 ADD·REMOVE 패킷을 시야 안 전원에게 따로 보내기 때문입니다. 송신 묶기(P1b) 뒤에 다시 잽니다.
- **클라이언트**: `UJMObjectManager::HandleSpawn`은 `OBJECT_TYPE_ITEM`을 무시합니다(A6.2에서 표시 액터로 교체). 봇은 ITEM을 몬스터 목록에 넣지 않습니다.

### A3.5 구현 메모 (2026-11-17)
- **줍기** `Room::PlayerPickupItem`: 검증 순서는 방 안 → 더미 봇 아님 → 생존 → 요청 간격 → 진행 중인 작업(`BUSY`) → 드랍 존재(`NO_ITEM`) → 줍는 중 아님(`BUSY`) → 수평 거리 ≤ `PickupRangeCm`(`TOO_FAR`) → `DropItem::CanPickup`(`NOT_OWNER`) → 골드 상한(`GOLD_FULL`)·`Inventory::PlanAdd`(`INVENTORY_FULL`). 통과하면 `drop->picking = true` + `ItemOpKind::Pickup`. 배치 계획은 지급과 같은 필드(`fills`·`newCounts`·`newSlots`·`goldDelta`)에 담습니다.
- **줍기 결과**(`Room::OnItemOpDone`): 성공 → `RemoveDrop`(방을 나갔어도) → 방에 있으면 인벤토리 반영 + `SC_INVENTORY_UPDATE`. 롤백 확인 → `picking`을 풀고 `DB_ERROR`(드랍은 바닥에 남아 다시 주울 수 있음, 그 사이 수명이 다했으면 그때 제거). 결과 불명 → 드랍을 지웁니다(되돌리면 복제 가능, 플레이어는 공통 경로가 끊음).
- **버리기** `Room::PlayerDropItem`: 인벤토리 위치의 내 행만(없거나 장착 중이면 `NO_ITEM`), 스택 통째. `ItemOpKind::Discard`가 `ItemDbId`·칸·수량을 조건으로 DELETE하고, 성공하면 메모리에서 지워 `SC_INVENTORY_UPDATE`(removed)를 보낸 뒤 `CreateDrop`(소유권 없음)을 부릅니다. 위치는 방에 있으면 지금 위치, 나갔으면 요청 때 위치의 `GetRandomPosNear(…, DropScatterCm)`입니다. 실패·불명이면 드랍을 만들지 않습니다(DB에 행이 남았을 수 있음).
- **쿼리(D-165)**: 줍기는 고정 쿼리 `DbQueries::ItemPickup`(덜 찬 스택 하나 + 새 행 하나 + 골드 중 해당하는 것만 실행)입니다. 스택을 둘 이상 채우거나 새 행이 둘 이상인 드문 경우만 지급의 조립식 `BuildItemGive`를 씁니다. 버리기는 고정 쿼리 `ItemDiscard`. 측정: 줍기 DB 실행 avg 4.1ms·p99 7.9ms.
- **경쟁**: Room 잡은 한 번에 하나라 먼저 처리된 요청이 `picking`을 켜고, 뒤 요청은 DB 결과가 올 때까지 `BUSY`, 그 뒤에는 `NO_ITEM`입니다. `BUSY`는 클라이언트가 문구를 띄우지 않으므로 진 쪽에는 드랍이 사라지는 것만 보입니다.
- **지표**: `[metrics] pickup(picked/discarded)`, Debug 로그 `[Item] 줍기`·`줍기 거부`·`버리기 거부`.
- **시험(TESTING 2026-11-17 A3.5)**: 거부 사유, 버리기 → 타인 줍기, 3인 동시 요청 60라운드, DB 롤백·지연·결과 전 접속 해제, 소유권 거부·개방, 골드 상한. 봇의 줍기 프로필은 A5.2입니다.

### A6.2 구현 메모 (2026-11-17)
- **표시**: `ADropItemActor`(`Game/DropItemActor`) — 메시 컴포넌트 하나, 충돌 없음, 제자리에서 돎. 아이템·골드의 메시·머티리얼·크기는 `BP_DropItem`에서 지정합니다(지금은 엔진 기본 도형: 아이템 = 파란 상자, 골드 = 금색 원판 — `MI_Drop_Item`·`MI_Drop_Gold`). 서버 z(내비메시 높이) 둘레에서 바닥을 찾아 `HoverHeight`만큼 띄웁니다. 클래스는 `BP_JMGameInstance.DropItemClass`.
- **관리**: `UJMObjectManager::Drops`(id → 액터, `Objects`와 별개). `HandleSpawn`의 ITEM 분기가 만들고 `HandleDespawn`이 `Drops`를 먼저 찾아 지웁니다. `FindNearestDrop(위치, 거리)`는 수평 거리(서버와 같은 기준).
- **줍기**: `AJMMyPlayer`가 0.1초마다 `PickupRangeCm`(`SC_INVENTORY` 값) 안의 가장 가까운 드랍을 골라 HUD `TxtInteract`에 "[F] 줍기: 이름"을 띄우고, F 키(`IA_Interact` → `OnInteractInput`)로 `CS_PICKUP_ITEM`을 보냅니다. 결과는 서버 패킷(`SC_REMOVE_OBJECT`·`SC_INVENTORY_UPDATE`)으로만 반영하고 거부는 시스템 메시지입니다(`BUSY`는 띄우지 않으므로 경쟁에서 진 쪽은 드랍이 사라지는 것만 봄). 사망 중에는 안내·요청을 하지 않습니다.
- **버리기**: `WBP_Inventory`의 `BtnDrop`("버리기") — 인벤토리 칸의 아이템을 선택했을 때만 켜지고 `CS_DROP_ITEM(선택한 item_db_id)`을 보냅니다. 확인 창은 없습니다(다시 주울 수 있음).

## 장착 검사 확장 (A3.3·MC4.4, D-156)
- `CS_CHANGE_EQUIP`은 `item_id` 대신 **`item_db_id`**(0 = 해제)를 받습니다. 방송 `SC_CHANGE_EQUIP`과 `ObjectInfo.equip_items`는 지금처럼 itemId입니다.
- 추가 검증: 내 인벤토리 위치의 행인지(`NOT_OWNED`), `RequiredLevel` ≤ 레벨(`LEVEL_TOO_LOW`), 거래에 올린 아이템이 아닌지(`IN_TRADE`), `itemOp` 없음(`BUSY`), 칸이 늘어나는 경우 빈 칸이 충분한지(`INVENTORY_FULL` — 해제 1칸, 무기 교체로 방패 자동 해제 1칸, 무기 해제와 방패 자동 해제가 겹치면 2칸). 같은 슬롯 교체는 칸 수가 그대로라 항상 가능합니다.
- 순서가 바뀝니다: 지금은 메모리 반영 → 스냅숏 저장이지만, 이후에는 **DB 성공 → 메모리 반영·`RecalcStat`·스왑 잠금·방송**입니다. 장착 스냅숏 저장(`RequestEquipSave`, `Player::equipSave`, D-120)은 없어집니다. DB 왕복만큼 장착이 늦어지므로 A3.3에서 지연을 측정합니다. DB 결과를 적용할 때 사망·시전 여부는 다시 보지 않습니다(이미 DB에 반영됨). 대신 **무기 슬롯 장착·해제가 DB에 가 있는 동안에는 그 플레이어의 시전 요청을 받지 않습니다**(`itemOp.weaponSwapPending`, `Room::CastSkill`) — 받으면 시전 도중 무기가 바뀌고 결과 적용 때 거는 스왑 잠금이 시전의 행동 잠금을 덮어써 짧아집니다(2026-11-16 구현 후 리뷰). 방어구 교체 중의 시전은 그대로 받습니다.
- 기존 규칙과의 관계: 요청 빈도는 지금처럼 `ServerEquipMinIntervalMs`를 **요청 시점**에 적용하고(`ItemMinIntervalMs`는 장착에 쓰지 않음), 무기 스왑 잠금은 **결과 적용 시점**부터 겁니다. 방패 자동 해제는 한 트랜잭션으로 저장하고, 방송 순서(보조손 해제 먼저 → 무기 변경, D-139)는 그대로입니다. 거부 패킷 `SC_CHANGE_EQUIP_RESULT`에 `item_db_id`를 추가합니다.
- **장비창(MC4.4)**: 목록 출처는 내 인벤토리의 장비입니다. 장착 7칸 + 인벤토리 장비를 보여 주고 클릭으로 장착·해제합니다. 콘솔 `Equip`은 개발용으로 남기고(D-160, A3.3부터 인벤토리에서 그 종류의 `item_db_id`를 찾아 보냄) 시험용 아이템은 `Give`로 넣습니다.

## 소비 아이템과 액티브 칸 (A3.6, D-154)
- `CS_USE_ITEM(item_db_id, item_id)`: `item_db_id`가 0이면 서버가 그 종류(`item_id`) 중 거래에 올리지 않은 스택을 수량이 적은 것부터 고릅니다(액티브 칸은 이 방식). 검증(보유·인벤토리 위치, `UseEffect` ≠ `None`, `RequiredLevel`, 생존, HP가 최대치 미만 `HP_FULL`, 재사용 시간 `COOLDOWN`, `itemOp` 없음) → DB `Count` − 1 → 성공이면 HP 회복(최대치까지), `SC_USE_ITEM(object_id, item_id, hp, cooldown_ms)`를 AOI(본인 포함)에 방송 + `SC_INVENTORY_UPDATE`. 재사용 표시는 이 `cooldown_ms`로 합니다(서버 값 사용, D-029).
- 재사용 시간은 아이템 종류별로 다음 사용 가능 시각을 메모리에 두고 비교합니다. 저장하지 않습니다(재접속하면 초기화).
- **액티브 칸**: 칸에는 아이템 **종류(itemId)**를 올립니다. 사용하면 그 종류의 스택 하나에서 1개를 쓰고, 칸에는 보유 합계를 표시합니다. 칸 N에 현재 무기의 스킬(슬롯 N)이 있으면 스킬이 우선이고, 없을 때 올려 둔 아이템이 보입니다(`AJMMyPlayer::RebuildQuickSlots` 확장, `EQuickSlotKind::Item`). 지금 검·활은 칸 1만 스킬이라 칸 2~5가 아이템 자리입니다. 현재 무기에 스킬이 있는 칸에 올리려는 요청은 거부합니다.
- 배치는 `CS_SET_QUICKSLOT(slot, item_id)`(0 = 비우기)로 서버에 저장합니다. 소유가 바뀌지 않는 설정값이라 `itemOp` 없이 메모리에 먼저 반영하고, 저장은 기존 `Player::SaveState` 방식(플레이어당 동시 1건, 진행 중 변경은 합쳐 끝난 뒤 최신 5칸을 한 번 더, 실패는 다음 변경·퇴장 때 재시도)으로 합니다. DB 작업 스레드가 여러 개라 따로따로 보내면 순서가 뒤바뀔 수 있기 때문입니다.

## 중복 접속 차단 (A2.7, D-158)
- **기존 접속을 끊고 새 로그인을 받습니다.** `AuthLobby`가 접속 중 캐릭터 목록(`PlayerId` → 세션 id·상태 `Online`/`Closing`)을 갖고, 로비 잡 안에서만 읽고 씁니다(감사 F3).
- 로그인 DB 조회가 성공했는데 목록에 있으면: 기존 세션에 `SC_KICK`을 보내고 **소켓을 닫아** 끊은 뒤 상태를 `Closing`으로 바꾸고(지금 `Session::Disconnect`는 소켓을 닫지 않아 가만히 있는 클라이언트는 끊기지 않으므로, 감사 F4의 소켓 닫기를 A2.7에서 먼저 넣음 — 클라이언트도 `SC_KICK`을 받으면 스스로 닫고 로그인 화면으로 돌아감), 새 로그인은 대기시킵니다(캐릭터당 대기 1건, 이미 대기가 있으면 먼저 온 쪽을 `LOGIN_RESULT_BUSY`로 거부). 방금 읽은 DB 값은 낡았을 수 있으므로 버립니다.
- **목록에서 빼는 시점 = 마지막 저장이 끝난 뒤**: 끊긴 플레이어의 "퇴장 처리 끝 + 진행 중인 DB 작업 0건"이 되는 순간 로비에 `OnPlayerClosed(PlayerId, 세션 id)` 잡을 넣습니다. 로비는 **세션 id가 목록 항목과 같을 때만** 빼고(같은 끊김에서 신호가 두 번 오거나 늦게 와도 새 접속의 항목을 지우지 않음), 대기 중인 로그인이 있으면 DB 조회부터 다시 실행합니다.
- 신호를 내는 지점(빠지면 영영 `Closing`이므로 전부 적음): ① 방에 들어가기 전에 끊김(`GameSession::OnDisconnected`의 방 없음) ② 입장 잡이 세션 끊김을 보고 취소 ③ `PlayerLeaveRoom`의 "방에 없음" 조기 종료 ④ 퇴장했는데 보낼 저장이 없음(진행 상황이 DB 값과 같음) ⑤ 퇴장 뒤 마지막 저장 결과 도착(성공 또는 재시도 포기) ⑥ 퇴장 뒤 진행 중이던 아이템 작업·액티브 칸 저장 결과 도착. 경매 서비스에 가 있는 작업도 요청자 Room으로 돌아와 ⑥에서 처리합니다.
- 안전장치: `Closing`이 `ServerConfig` `LoginCloseForceSec`(30)를 넘으면 ERROR 로그를 남기고 강제로 뺍니다.
- 정상 종료도 같은 경로라, 빠른 재접속은 이전 저장이 끝날 때까지 기다립니다. 대기가 `ServerConfig` `LoginWaitTimeoutSec`(10)를 넘으면 `LOGIN_RESULT_BUSY`로 응답하고 목록은 그대로 둡니다(시각 비교, 예약 잡은 실행 때 다시 확인).
- 더미 봇은 제외합니다(A5.1에서 개별 계정으로 바뀌면 자동으로 적용).
- **구현 메모(2026-11-16)**: 신호는 `Room::NotifyClosedIfDone` 한 곳에서 판단합니다(방에 없음 + 세션 끊김 + DB에 가 있는 작업 0건, 플레이어당 한 번) — 위 ②~⑥의 지점은 모두 이 함수를 부르고, ①만 `GameSession::HandlePlayerGone`이 직접 보냅니다. 로그인 처리 도중에 끊긴 경우는 로비가 `_currPlayer`를 넣은 뒤 연결을 다시 확인해 같은 함수를 부릅니다. 끊기는 `Session::ForceClose`(`shutdown` + 걸린 수신 취소, 핸들은 소멸자에서 닫음)이고, `SC_KICK`은 그 직전에 보내므로 다른 전송이 진행 중이면 도착하지 않을 수 있습니다 — 클라이언트는 게임 중 연결이 끊기면 `SC_KICK` 없이도 로그인 화면으로 돌아갑니다. 설정 키는 `[Network] LoginWaitTimeoutSec`·`LoginCloseForceSec`, 시험용 `[Debug] DbSaveDelayMs`.

## 1:1 거래 (A4.1·A4.2, D-158)
같은 Room의 두 플레이어만 거래하고, 모든 전이는 그 Room의 JobQueue에서 일어납니다. 거래 상태(`TradeSession`: 두 사람의 `PlayerId`·오브젝트 id, 상태, 제시 목록, 잠금·확정 표시, 시작·마지막 동작 시각)는 Room이 갖고 `Player`에는 거래 id만 둡니다.

| 상태 | 들어오는 조건 | 받는 요청 |
|---|---|---|
| 요청 중 | `CS_TRADE_REQUEST(target_id)` — 자기 자신이 아님, 둘 다 생존·거래 중 아님·거리 안 | 상대의 `CS_TRADE_RESPOND`, 양쪽 `CS_TRADE_CANCEL` |
| 제시 | 상대가 수락 | `CS_TRADE_SET_OFFER`(내 제시 전체를 교체: (`item_db_id`, 수량) 목록 + 골드), `CS_TRADE_LOCK(true)`, 취소 |
| 잠금 | 한쪽 이상이 잠금 | 잠근 쪽의 제시 변경은 `LOCKED`로 거부, `CS_TRADE_LOCK(false)`, 양쪽 모두 잠근 뒤에만 `CS_TRADE_CONFIRM` |
| 확정 중 | 양쪽 확정 | 없음(DB 결과 대기, 모든 요청 거부) |
| 끝 | 성사 또는 취소 | — |

- **제시**: 인벤토리 위치의 아이템(장착 중 제외, `TradeMaxItems` 이하)과 골드. 아이템마다 수량(1 ~ 그 행의 `Count`)을 적어 스택의 일부만 올릴 수 있고, 같은 행을 두 번 올릴 수 없습니다. 올린 행은 일부만 올렸더라도 거래가 끝날 때까지 장착·버리기·경매 등록·사용을 거부합니다(`IN_TRADE`). 내 아이템 작업이 진행 중(`itemOp`)일 때는 제시 변경도 `BUSY`로 거부하므로, 다른 경로로 빠져나가는 중인 아이템을 올릴 수 없습니다. 줍기로 올린 스택의 수량이 늘어도 넘어가는 것은 제시한 수량뿐입니다.
- **잠금 규칙**: 누가 잠금을 풀면 **양쪽의 잠금과 확정이 모두 풀립니다.** 바꾸려면 풀고 고친 뒤 다시 잠가야 하므로 잠근 뒤 몰래 바꿀 수 없습니다. 확정을 두 번 보내면 두 번째는 무시합니다.
- **자동 취소**: 접속 해제·방 퇴장, 한쪽 사망, 두 사람 거리가 `TradeRangeCm` 초과(이동 처리와 틱에서 확인 — 거리 안 이동·피격은 허용), 요청 응답 시간 초과, 전체 시간 초과(틱에서 시각 비교). 취소는 메모리 상태만 버리면 되고 DB는 건드린 적이 없으므로 **롤백할 것이 없습니다.**
- **확정**: 양쪽이 확정하면 다시 검증합니다(연결, 생존, 거리, 아이템이 여전히 인벤토리에 있고 수량이 제시 수량 이상인지, 골드 충분, 받은 뒤 양쪽 칸 수·골드 상한, 양쪽 `itemOp` 없음). 검증에 실패하면 거래를 끝내지 않고 **양쪽 잠금·확정을 풀어 제시 상태로 되돌리고** 사유를 `SC_TRADE_RESULT`로 두 사람에게 알립니다. 통과하면 양쪽 `itemOp`를 켜고 한 트랜잭션(위 표)을 실행합니다. 성공이면 양쪽 메모리와 `SC_INVENTORY_UPDATE`, `SC_TRADE_END(SUCCESS)`. 실패면 아무것도 바뀌지 않고 `SC_TRADE_END(DB_ERROR)`. 확정 중 한쪽이 끊겨도 DB 결과가 권위이고, 남은 쪽만 메모리에 반영합니다.
- 상태가 바뀔 때마다 두 사람에게 `SC_TRADE_STATE`(상태, 양쪽 제시, 잠금·확정 표시) 전체를 보냅니다. 요청 거부는 본인에게 `SC_TRADE_RESULT`입니다.
- 클라이언트(A6.3a): 크로스헤어가 가리키는 플레이어에게 거래 키로 요청, 받은 쪽은 수락·거절 창. 결과·거부는 시스템 메시지 창.

## 경매장 (A4.3, D-159)
- **즉시 구매만**, 수수료 없음, 어디서나 키로 엽니다(사망 중 불가). 팔린 대금·산 아이템·만료·취소 반환은 모두 **수령함**에 쌓이고 경매 창에서 직접 받습니다.
- **`AuctionService`**(`GAuction`): `AuthLobby`처럼 자체 JobQueue를 가진 전역 서비스이고 수명은 프로세스와 같습니다. 시작 때 판매 중인 등록을 전부 읽어 메모리 목록을 만듭니다(권위는 DB, 목록은 검색·중복 구매 차단용).
- **요청 경로**: 패킷 → 요청자의 Room 잡(검증, `itemOp` 켬) → `GAuction` 잡 → `DatabaseWorker` → `GAuction` 잡(목록 갱신) → 요청자의 Room 잡(메모리·패킷 반영, `itemOp` 끔). Room과 서비스는 서로의 상태를 직접 만지지 않고 잡으로만 주고받습니다. 결과가 돌아왔을 때 요청자가 방에 없으면 반영만 건너뜁니다.
- **등록** `CS_AUCTION_REGISTER(item_db_id, count, price)`: Room에서 인벤토리 위치·거래 중 아님·수량(1 ~ 그 행의 `Count`)·가격 범위 확인, 서비스에서 1인 등록 수 확인. 성공하면 그 수량이 인벤토리에서 빠져 에스크로(`Location` 3)로 갑니다. `price`는 등록 묶음 전체의 가격이고, 구매는 등록 단위 통째로만 합니다.
- **처리 중 표시**: 구매·취소·만료는 모두 서비스 목록의 그 등록에 **처리 중** 표시를 한 뒤 DB로 보냅니다(DB 작업 스레드가 여러 개라 같은 등록의 두 트랜잭션이 동시에 돌 수 있음). 표시된 등록에 온 다른 요청은 거부하고 만료 검사는 건너뜁니다. DB 결과가 성공이면 목록에서 빼고, **실패·조건 불일치(바뀐 행 0)면 반드시 표시를 풉니다.**
- **구매** `CS_AUCTION_BUY(listing_id)`: 서비스가 목록에서 판매 중·본인 물품 아님·처리 중 아님을 확인합니다. 같은 등록에 온 다른 구매는 `SOLD`로 거부하므로 한 명만 삽니다. 골드 부족·만료는 트랜잭션 조건에서 걸러 롤백합니다. 구매자는 골드만 줄고 아이템은 수령함으로 갑니다. **판매자의 Room에는 아무것도 보내지 않습니다**(오프라인·다른 Room이어도 같음).
- **취소** `CS_AUCTION_CANCEL(listing_id)`: 본인의 판매 중 등록만. 아이템은 수령함으로.
- **만료**: 서비스가 `AuctionExpireCheckSec`마다 기간이 지난 등록을 만료 처리하고 수령함으로 돌려줍니다. 서버가 꺼져 있던 동안 지난 것은 시작 후 첫 검사에서 처리합니다.
- **조회**: `CS_AUCTION_SEARCH(slot, page)` → 서비스 목록에서 슬롯 필터(0 = 전체), 가격 오름차순, `AuctionPageSize`개씩 `SC_AUCTION_LIST`(패킷 상한 8192바이트 안). `CS_AUCTION_MY` → 내 등록(서비스 목록) + 수령함 `SC_AUCTION_MY`. 수령함은 메모리에 두지 않고 요청 때 DB에서 오래된 것부터 `AuctionPageSize`개를 읽습니다(Room → 서비스 → DB → 서비스 → Room). 수령하면 다음 것이 보입니다.
- **수령** `CS_MAIL_CLAIM(mail_id)`: 한 건씩. 서비스를 거치지 않고 Room → DB → Room. 아이템은 빈 칸, 골드는 상한을 확인합니다.

## 클라이언트 UI (A6.1·A6.2·MC4.4·A6.3a·A6.3b)
- 창은 모두 `UJMUIManager::RegisterWidget`(`bIsWindow`, ZOrder는 크로스헤어 100보다 위)로 등록하고 키로 `ToggleWidget`합니다. 창이 열리면 커서가 보입니다(기존 동작). 키는 초기 제안이며 구현 때 조정합니다.
- **인벤토리 + 장비창**(I 키, `WBP_Inventory` 한 창 — A6.1, D-166): 장착 7칸, 칸 격자(서버 칸 수만큼, 빈 칸 표시, 아이콘·수량 — 칸 위젯 `UItemSlotWidget`/`WBP_ItemSlot`), 골드, 선택한 아이템 이름. 인벤토리의 장비 우클릭 = 장착, 장착 칸 우클릭 = 해제, 끌어다 놓기 = 칸 옮기기·맞바꾸기(인벤토리 ↔ 인벤토리)·장착(인벤토리 → 장착 칸)·해제 또는 교체(장착 칸 → 인벤토리). 창은 클라이언트 사본(`UJMInventoryManager`)을 그리며 여닫을 때 서버에 묻지 않습니다. 선택한 아이템의 버리기(A6.2)·사용·액티브 칸에 올리기(A3.6)는 그때 붙입니다.
- **거래 창**: 양쪽 제시(아이템·수량·골드), 잠금·확정·취소 버튼, 상대의 잠금·확정 표시. 인벤토리에서 아이템을 골라 수량을 적어 올립니다.
- **경매 창**(K 키): 검색(슬롯 필터, 쪽 넘기기)·구매, 등록(인벤토리에서 골라 수량·가격 입력), 내 등록(취소), 수령함(수령) 탭.
- 거부·실패·성사 안내는 모두 시스템 메시지 창(D-050)입니다.

## 패킷 (번호 배정)
새 번호는 CS 1013~, SC 2016~로 이어 씁니다. 추가 절차는 [Common/Protocol/CLAUDE.md](../Common/Protocol/CLAUDE.md).

| 항목 | CS | SC |
|---|---|---|
| A3.3 | 1017 `DEV_GIVE` | 2016 `INVENTORY`(본인), 2017 `INVENTORY_UPDATE`(본인), 2018 `ITEM_RESULT`(본인, 거부일 때만: 요청 패킷 id + `ItemResult`) |
| A3.5 | 1013 `PICKUP_ITEM`, 1014 `DROP_ITEM` | (드랍 표시는 기존 `ADD_OBJECT`·`REMOVE_OBJECT`) |
| A3.6 | 1015 `USE_ITEM`, 1016 `SET_QUICKSLOT` | 2019 `USE_ITEM`(AOI) |
| A4.2 | 1018 `TRADE_REQUEST`, 1019 `TRADE_RESPOND`, 1020 `TRADE_SET_OFFER`, 1021 `TRADE_LOCK`, 1022 `TRADE_CONFIRM`, 1023 `TRADE_CANCEL` | 2020 `TRADE_REQUEST`(상대), 2021 `TRADE_STATE`(두 사람), 2022 `TRADE_END`(두 사람), 2023 `TRADE_RESULT`(본인, 거부) |
| A4.3 | 1024 `AUCTION_SEARCH`, 1025 `AUCTION_REGISTER`, 1026 `AUCTION_BUY`, 1027 `AUCTION_CANCEL`, 1028 `AUCTION_MY`, 1029 `MAIL_CLAIM` | 2024 `AUCTION_LIST`, 2025 `AUCTION_MY`, 2026 `AUCTION_RESULT`(본인: 요청 패킷 id + `AuctionResult` + `listing_id`) |
| A2.7 | — | 2027 `KICK`(끊기 직전 본인, 사유) |
| A6.1 | 1030 `MOVE_ITEM`(칸 옮기기·맞바꾸기) | (성공은 `INVENTORY_UPDATE`, 거부는 `ITEM_RESULT`) |

- 구조체 `ItemInfo`(`item_db_id`, `item_id`, `count`, `equip_part` — 0 = 인벤토리, `slot` — 인벤토리 칸 번호·장착 중이면 -1), `ObjectInfo.item_count`(12번), `CS_CHANGE_EQUIP_PACKET`은 `item_id`(2번)를 `reserved`로 막고 `item_db_id`(3번)를 추가합니다. `item_db_id`·`listing_id`·`mail_id`·골드·가격·수량 필드는 int64입니다.
- 결과 enum은 기능별로 둡니다(D-114 방식). 새 enum은 기존 관례대로 `*_NONE` = 0, `*_SUCCESS` = 1로 시작하고 아래 값이 2부터 이어집니다.
  - `ItemResult`: `NOT_IN_ROOM`, `DEAD`, `BUSY`, `TOO_FREQUENT`, `NOT_ALLOWED`, `NO_ITEM`, `TOO_FAR`, `NOT_OWNER`, `INVENTORY_FULL`, `GOLD_FULL`, `IN_TRADE`, `NOT_USABLE`, `HP_FULL`, `COOLDOWN`, `LEVEL_TOO_LOW`, `DB_ERROR`, `BAD_SLOT`(18, A6.1).
  - `EquipResult` 추가: `NOT_OWNED`(9), `LEVEL_TOO_LOW`(10), `INVENTORY_FULL`(11), `BUSY`(12), `IN_TRADE`(13), `DB_ERROR`(14), `NOT_ALLOWED`(15, 더미 봇 — D-161).
  - `TradeResult`: `NO_TARGET`, `TOO_FAR`, `TARGET_BUSY`, `DECLINED`, `TIMEOUT`, `CANCELLED`, `DEAD`, `DISCONNECTED`, `LOCKED`, `INVALID_OFFER`, `INVENTORY_FULL`, `GOLD_FULL`, `BUSY`, `NOT_ALLOWED`, `DB_ERROR`.
  - `AuctionResult`: `NO_LISTING`, `SOLD`, `OWN_ITEM`, `NOT_ENOUGH_GOLD`, `LISTING_LIMIT`, `BAD_PRICE`, `NO_ITEM`, `IN_TRADE`, `INVENTORY_FULL`, `GOLD_FULL`, `BUSY`, `DEAD`, `NOT_ALLOWED`, `DB_ERROR`.
  - `LoginResult` 추가: `BUSY`(6, 이전 접속 정리 중).
- 클라이언트는 결과 코드별 문구를 `ShowSystemMessage`로 띄웁니다(`BUSY`·`TOO_FREQUENT`는 띄우지 않음).

## 동시성·실패 처리 (감사 문서 대응)
| 주의점 | 이 설계의 대응 |
|---|---|
| 끊긴 세션의 남은 패킷(F1·F2) | 핸들러·Room 잡·서비스 잡 첫머리에서 연결과 대상 재확인. 상태 전이는 Room 또는 서비스 잡 한 곳에서만 |
| `_currPlayer` 경쟁(F3) | 접속 목록은 로비 잡에서만 접근 |
| 무응답 클라이언트(F4) | 거래 요청·전체 시간 초과로 풀림. 유휴 세션 자체는 A5.0 |
| 강제 종료 손실(F6) | 아이템·골드는 즉시 트랜잭션. 잃는 것은 바닥 드랍과 메모리 거래 상태뿐 |
| 타이머 강한 참조(F8) | `AuctionService` 수명 = 프로세스. 드랍 소멸은 Room의 `ReserveJob`(Room이 사라지면 버려짐) |
| 예약 취소 API 없음 | 소유권·재사용 시간·거래 시간 초과는 시각 비교, 드랍 소멸은 id 재확인 |
| DB·타이머 스레드의 Send | 결과는 항상 Room·서비스·로비 잡을 거쳐 전송 |
| Room 간 통신 없음 | 경매 결과는 요청자의 Room으로만 돌아가고, 상대에게 줄 것은 수령함(DB)에 둠 |
| 퇴장 저장이 덮어쓰는 문제 | 아이템·골드는 스냅숏 저장 대상에서 빠짐. 접속 목록이 마지막 저장 뒤에 풀려 재접속이 옛 값을 읽지 않음 |
| DB 작업 비동기(A4.4) | 새 DB 경로 전부 `PushDBJob` → 잡 복귀. 지연 주입 시험 대상 |

## 시험 항목 (ROADMAP 완료 기준 대응)
- **A3.2**: DropTable·EconomyTable·ItemTable 새 열의 형식 오류·범위·없는 참조·중복에서 시작 중단, `DropRate`를 바꾸면 드랍 빈도가 바뀜.
- **A2.7**: 같은 계정 2접속 시 먼저 접속한 쪽이 `SC_KICK` 후 끊기고 나중 쪽만 남음, DB 저장을 인위로 늦춘 빠른 재접속 반복에서 레벨·HP·골드·아이템이 되돌아가지 않음, 대기 시간 초과 시 `BUSY`.
- **A3.3**: 재접속·서버 재시작 후 인벤토리·장착·골드 유지, 저장 중 접속 해제·DB 실패(연결 끊기)에서 손실·중복 없음(DB 행 수와 `ItemDbId`로 대조), 보유하지 않은 `item_db_id` 장착 거부, 가득 찬 상태의 해제 거부, 마이그레이션 0004 전후 장착 유지, 장착 지연 측정.
- **A3.4**: 처치 수 대비 드랍 수가 `DropRate`와 맞음, 소유 시간 안 타인 줍기 거부·경과 후 허용, `DropLifeSec` 뒤 제거, AOI 진입·이탈 때 ADD·REMOVE(원시 소켓).
- **A3.5**: 두 클라이언트가 같은 드랍에 동시에 요청해도 한 명만 획득(반복), 거리 밖·가득 참·없는 id 거부, 버리기 → 다른 사람이 줍기.
- **A3.6**: 물약 사용 시 HP·수량 반영과 AOI 방송, 사망·HP 최대·재사용 시간 중 거부, 액티브 칸 배치가 재접속 후 유지.
- **A4.1·A4.2**: 단계별 중간 접속 해제·시간 초과·거리 이탈·사망에서 양쪽 인벤토리 불변, 잠금 후 제시 변경 거부, 중복 확정 무시, 제시한 아이템의 장착·버리기 거부, 반복 거래 후 양쪽 합계(아이템 행 수·골드 합) 보존과 `Trade_Log` 행 수 일치.
- **A4.3**: 다른 Room 두 유저의 등록 → 구매 → 양쪽 수령, 동시 구매 시 한 명만 성공, 구매와 취소·만료가 겹쳐도 한쪽만 성공하고 목록이 DB와 일치, 결과 도착 전 접속 해제 후 DB와 다음 로그인 값 일치, 골드 부족·본인 물품·등록 수 초과 거부, 만료 반환, 서버 재시작 후 등록·수령함 유지와 밀린 만료 처리.
- 결과 도착 전 접속 해제 시험은 A3.3·A4.2·A4.3 각각에서 합니다(A4.4는 병목이 없으면 하지 않을 수 있는 항목이라 여기에만 두지 않음).
- **A4.4**: DB 응답을 늦춘 상태에서 줍기·거래·경매 부하 중 틱 지연 p99 ≤ 20ms, 결과 도착 전 접속 해제 시 서버 오류 없이 DB와 다음 로그인 값 일치.
- **봇(A5.2)**: 개별 계정 봇 + `DevCommands`. 줍기 = 받은 ITEM 오브젝트로 이동 후 요청, 거래 = 이웃 번호끼리 짝(짝수가 요청, 홀수가 자동 수락·잠금·확정), 경매 = 등록·검색·구매·수령 순환. 통계에 요청·성공·거부 수를 추가합니다. 개별 계정 봇이 PlayerStart에서 몬스터에 닿지 못하는 문제(ROADMAP A5.1 메모)를 먼저 풀어야 줍기 프로필이 동작합니다. 봇은 `SC_KICK`·`LOGIN_RESULT_BUSY`를 받으면 실패로 세고 다시 접속하지 않습니다.

## 알아둘 간극
- 기본 장비를 거래할 수 있어 새 계정을 만들어 아이템을 늘릴 수 있습니다(사용자 결정, 경제 균형은 범위 밖).
- 수수료가 없어 골드가 회수되지 않습니다. 골드 드랍만큼 계속 늘어납니다.
- 줍기·장착·물약이 모두 DB 왕복을 기다립니다. 부하 단계(A5.4)에서 병목이면 골드 줍기 묶음 처리 등을 그때 검토합니다.
- 버리기로 거래 절차 없이 아이템을 넘길 수 있습니다(소유권 없는 드랍). 도배는 요청 간격과 `MaxDropsPerRoom`으로만 막습니다.
- 인벤토리에서 스택을 합칠 수 없어, 거래·수령으로 받은 물약은 덜 찬 스택이 여러 칸으로 남을 수 있습니다(사용·액티브 칸 수량은 종류 합계라 쓰는 데는 지장 없음).
- 팔렸다는 알림 패킷이 없습니다. 판매자는 경매 창을 다시 열어야 수령함에서 대금을 봅니다.
- 수령함에는 상한이 없습니다(받지 않으면 계속 쌓임). 경매 검색은 요청마다 목록을 걸러 정렬하므로 등록이 많아지면 A5.4에서 정렬된 색인을 검토합니다.
- 로그인 보정이나 거래로 인벤토리가 칸 상한을 넘은 상태가 생길 수 있고, 그동안은 새로 들어오는 것만 막습니다.
- 레벨업이 A2.3b라 `RequiredLevel` 거부는 CSV 값을 올린 원시 소켓 시험으로만 확인합니다.
