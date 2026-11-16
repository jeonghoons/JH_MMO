# 아이템·인벤토리·거래 설계

목표 구조만 기록합니다. 구현 순서는 [ROADMAP.md](ROADMAP.md) 마일스톤 1(1-3·1-4)을 따르고, 이 문서의 테이블·패킷은 해당 항목을 구현할 때 만듭니다.
(2026-11-16 A3.1 작성, 결정 D-152~D-159. 장비 외형·슬롯은 [DESIGN_CHARACTER_EQUIPMENT.md](DESIGN_CHARACTER_EQUIPMENT.md), CSV 규약은 [DESIGN_DATA.md](DESIGN_DATA.md), 동시성 주의점은 [AUDIT_2026-11-06_CONCURRENCY.md](AUDIT_2026-11-06_CONCURRENCY.md))

## 현재 상태 (2026-11-16)
- 서버·클라이언트·프로토콜에 인벤토리·드랍·줍기·거래·경매 코드가 없습니다. `OBJECT_TYPE_ITEM`(4)은 enum에만 있습니다.
- 장착은 보유 검사 없이 "ItemTable에 있고 슬롯이 맞으면" 허용합니다(`Room::PlayerChangeEquip`). 저장은 7칸 스냅숏(`Player_Equipped`, D-120)이고 `ItemDbId`는 항상 0입니다.
- `Player_Info.Gold` 열과 `Player::GetGold`는 있지만 값을 바꾸는 코드가 없습니다. 골드는 진행 상황 스냅숏(D-151)에 포함돼 저장됩니다.
- 같은 캐릭터의 중복 접속은 막지 않습니다(다른 연결에서 로그인하면 둘 다 들어옴).
- A3.2 완료(2026-11-16): `DropTable.csv`·`EconomyTable.csv`와 로더(`DataManager::GetDrops`·`RollDrops`·`GetEconomy`), `ItemTable` 새 열과 8001 HP 물약, UE `FItemTableRow`·`DT_ItemTable`이 들어갔습니다. 값을 쓰는 코드는 아직 없습니다(드랍 롤은 A3.4에서 `RollDrops`를 호출). 열별 허용 범위와 현재 행은 [DESIGN_DATA.md](DESIGN_DATA.md).

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
  - 인덱스 (`OwnerId`, `Location`), 장착 슬롯 중복 방지용 필터 유니크 인덱스 (`OwnerId`, `EquipSlot`) WHERE `Location` = 2. 교체 트랜잭션은 옛 장비를 먼저 빼고 새 장비를 넣습니다.
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
- 칸 수 = 인벤토리 위치의 행 수이고 `InventorySlots`를 넘을 수 없습니다. **장착품은 칸을 차지하지 않습니다.** 칸 위치(몇 번째 칸)는 저장하지 않고 `ItemDbId` 순서로 보여 줍니다.
- 스택: 줍기·지급으로 들어올 때만 같은 종류의 덜 찬 스택에 먼저 채우고 남으면 새 행을 만듭니다. 전부 들어갈 자리가 없으면 통째로 거부합니다(`INVENTORY_FULL`).
- **스택 나누기는 거래와 경매에서만** 합니다(D-158·D-159): 거래 제시와 경매 등록에 수량을 적어 스택의 일부만 올릴 수 있습니다. 인벤토리 안에서 나누거나 합치는 조작은 없고, 버리기는 스택 통째입니다. 거래·수령으로 받은 아이템은 기존 스택에 합치지 않고 한 칸(새 행)을 차지합니다.
- 방 입장 직후 본인에게 `SC_INVENTORY`(전체 + 골드 + 칸 수 + 액티브 칸 + 줍기·거래 거리)를 보내고, 그 뒤로는 `SC_INVENTORY_UPDATE`(바뀐 행·지워진 `ItemDbId`·현재 골드)만 보냅니다.
- **개발 지급 명령**: `CS_DEV_GIVE(item_id, count)`(item_id 0 = 골드). `ServerConfig` `[Debug] DevCommands=true`일 때만 받고 기본은 꺼 둡니다. 다른 아이템 작업과 같은 경로(`itemOp`, DB 성공 뒤 반영)를 탑니다. 클라이언트는 콘솔 `Give <itemId> <count>`.

## 드랍·줍기·버리기 (A3.4·A3.5, D-157)
1. **드랍 롤**: `Monster::OnDead(attacker)`에서 방에 드랍 생성을 요청합니다. DropTable의 그 몬스터 행을 각각 굴려, 걸린 행마다 사망 지점 `DropScatterCm` 안(내비메시 위)에 드랍 오브젝트를 하나씩 만듭니다.
2. **드랍 오브젝트**: `GameObject`를 상속한 `DropItem`(`OBJECT_TYPE_ITEM`). `ObjectInfo.template_id` = itemId(0 = 골드), 새 필드 `item_count`(12번, int64) = 수량(골드는 금액). 오브젝트 id는 전용 생성기(1,000,000,000부터 증가, int32 끝에 닿으면 처음으로 돌아가되 쓰는 중인 id는 건너뜀)로 플레이어(세션 번호)·몬스터(100001~)와 겹치지 않게 합니다. DB에는 없습니다(서버를 재시작하면 사라짐).
3. **소유권**: 막타 플레이어의 `PlayerId`와 만료 시각(`DropOwnerSec`)을 기록합니다. 만료는 타이머가 아니라 줍기 요청 때 시각 비교로 판정합니다. 공격자를 알 수 없거나(투사체가 날아가는 동안 퇴장) 더미 봇이면 소유권 없이 만듭니다.
4. **AOI**: 드랍은 캐릭터용 시야와 **분리**합니다. `GameMap`에 드랍 전용 색인(`AoiIndex` 하나 더)을 두고, 플레이어는 `_viewList`와 별개인 드랍 시야 목록을 갖습니다. 갱신 시점은 플레이어 이동 처리(`PlayerMove`의 `UpdateMap` 뒤), 입장·퇴장, 드랍 생성·제거뿐이고 판정은 같은 반경(R, R+H)입니다. 플레이어가 다가오면 `SC_ADD_OBJECT`, 멀어지거나 드랍이 사라지면 `SC_REMOVE_OBJECT`입니다. 드랍은 움직이지 않습니다. 이렇게 하면 `Room::UpdateView`·`BroadcastAOI`(인자가 `Character`, `_viewList`는 캐릭터 id만)와 몬스터 이동·`ViewAudit`·시야 지표를 건드리지 않습니다. A3.4에서 드랍이 많을 때의 틱 지연을 전후 비교합니다.
5. **소멸**: 생성 때 `ReserveJob(DropLifeSec, RemoveDrop, id)`. 실행 때 id로 다시 찾아 없으면(이미 주움) 끝냅니다. 줍는 중이면 건너뛰고 DB 결과 처리에서 수명을 다시 봅니다. `MaxDropsPerRoom` 초과로 지울 때도 줍는 중인 것은 건너뜁니다.
6. **줍기** `CS_PICKUP_ITEM(object_id)`: 검증(방 안, 생존, 요청 간격, `itemOp` 없음, 드랍 존재, 줍는 중 아님, 거리 ≤ `PickupRangeCm`, 소유자이거나 소유 시간 경과, 칸·골드 상한) → 드랍에 **줍는 중** 표시 + `itemOp` 켬 → DB → 성공이면 드랍 제거(`SC_REMOVE_OBJECT`) + `SC_INVENTORY_UPDATE`, 실패면 표시를 풀고 `DB_ERROR`. 줍는 중인 드랍에 온 다른 요청은 `BUSY`이므로 **경쟁해도 한 명만 얻습니다.**
7. **버리기** `CS_DROP_ITEM(item_db_id)`: 생존 중, 인벤토리 위치의 행만(장착 중·거래에 올린 것 제외) → DB DELETE → 성공 뒤 내 위치에 소유권 없는 드랍 생성. 바닥에서 다시 주우면 새 `ItemDbId`를 받습니다.
- 클라이언트(A6.2): `UJMObjectManager::HandleSpawn`에 ITEM 분기와 드랍 전용 맵(`Objects`는 캐릭터 전용이라 따로 둠)을 추가합니다. 표시는 공용 메시 하나(아이템·골드 구분만)로 하고, 거리 안에서 가장 가까운 드랍에 줍기 안내를 띄워 **F 키**(`IA_Interact` 신규)로 줍습니다. 소유권은 클라이언트에 알리지 않고 거부 문구(`NOT_OWNER`)로 안내합니다.

## 장착 검사 확장 (A3.3·MC4.4, D-156)
- `CS_CHANGE_EQUIP`은 `item_id` 대신 **`item_db_id`**(0 = 해제)를 받습니다. 방송 `SC_CHANGE_EQUIP`과 `ObjectInfo.equip_items`는 지금처럼 itemId입니다.
- 추가 검증: 내 인벤토리 위치의 행인지(`NOT_OWNED`), `RequiredLevel` ≤ 레벨(`LEVEL_TOO_LOW`), 거래에 올린 아이템이 아닌지(`IN_TRADE`), `itemOp` 없음(`BUSY`), 칸이 늘어나는 경우 빈 칸이 충분한지(`INVENTORY_FULL` — 해제 1칸, 무기 교체로 방패 자동 해제 1칸, 무기 해제와 방패 자동 해제가 겹치면 2칸). 같은 슬롯 교체는 칸 수가 그대로라 항상 가능합니다.
- 순서가 바뀝니다: 지금은 메모리 반영 → 스냅숏 저장이지만, 이후에는 **DB 성공 → 메모리 반영·`RecalcStat`·스왑 잠금·방송**입니다. 장착 스냅숏 저장(`RequestEquipSave`, `Player::equipSave`, D-120)은 없어집니다. DB 왕복만큼 장착이 늦어지므로 A3.3에서 지연을 측정합니다. DB 결과를 적용할 때 사망·시전 여부는 다시 보지 않습니다(이미 DB에 반영됨 — 요청 직후 시전을 시작하면 시전 도중 장비가 바뀔 수 있고, 이는 "시전 중 거부"(D-066)의 알려진 예외로 둠).
- 기존 규칙과의 관계: 요청 빈도는 지금처럼 `ServerEquipMinIntervalMs`를 **요청 시점**에 적용하고(`ItemMinIntervalMs`는 장착에 쓰지 않음), 무기 스왑 잠금은 **결과 적용 시점**부터 겁니다. 방패 자동 해제는 한 트랜잭션으로 저장하고, 방송 순서(보조손 해제 먼저 → 무기 변경, D-139)는 그대로입니다. 거부 패킷 `SC_CHANGE_EQUIP_RESULT`에 `item_db_id`를 추가합니다.
- **장비창(MC4.4)**: 목록 출처는 내 인벤토리의 장비입니다. 장착 7칸 + 인벤토리 장비를 보여 주고 클릭으로 장착·해제합니다. 콘솔 `Equip`은 삭제하고 시험용 아이템은 `Give`로 넣습니다.

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
- **인벤토리**(I 키, `WBP_Inventory`): 칸 격자(아이콘·수량, `USkillSlotWidget::SetItem` 재사용), 골드, 선택한 아이템의 버리기·사용·액티브 칸에 올리기.
- **장비창**(`WBP_Equipment`, 인벤토리와 함께 열림): 장착 7칸. 인벤토리의 장비를 클릭하면 장착, 장착 칸을 클릭하면 해제.
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

- 구조체 `ItemInfo`(`item_db_id`, `item_id`, `count`, `equip_part` — 0 = 인벤토리), `ObjectInfo.item_count`(12번), `CS_CHANGE_EQUIP_PACKET`은 `item_id`(2번)를 `reserved`로 막고 `item_db_id`(3번)를 추가합니다. `item_db_id`·`listing_id`·`mail_id`·골드·가격·수량 필드는 int64입니다.
- 결과 enum은 기능별로 둡니다(D-114 방식). 새 enum은 기존 관례대로 `*_NONE` = 0, `*_SUCCESS` = 1로 시작하고 아래 값이 2부터 이어집니다.
  - `ItemResult`: `NOT_IN_ROOM`, `DEAD`, `BUSY`, `TOO_FREQUENT`, `NOT_ALLOWED`, `NO_ITEM`, `TOO_FAR`, `NOT_OWNER`, `INVENTORY_FULL`, `GOLD_FULL`, `IN_TRADE`, `NOT_USABLE`, `HP_FULL`, `COOLDOWN`, `LEVEL_TOO_LOW`, `DB_ERROR`.
  - `EquipResult` 추가: `NOT_OWNED`(9), `LEVEL_TOO_LOW`(10), `INVENTORY_FULL`(11), `BUSY`(12), `IN_TRADE`(13), `DB_ERROR`(14).
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
