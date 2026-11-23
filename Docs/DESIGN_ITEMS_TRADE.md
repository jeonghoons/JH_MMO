# 아이템·인벤토리·거래 설계

목표 구조만 기록합니다. 구현 순서는 [ROADMAP.md](ROADMAP.md) 마일스톤 1(1-3·1-4)을 따르고, 이 문서의 테이블·패킷은 해당 항목을 구현할 때 만듭니다.
(2026-11-16 A3.1 작성, 결정 D-152~D-159. 장비 외형·슬롯은 [DESIGN_CHARACTER_EQUIPMENT.md](DESIGN_CHARACTER_EQUIPMENT.md), CSV 규약은 [DESIGN_DATA.md](DESIGN_DATA.md), 동시성 주의점은 [AUDIT_2026-11-06_CONCURRENCY.md](AUDIT_2026-11-06_CONCURRENCY.md))

## 현재 상태 (2026-11-21)
- A6.3b 구현(2026-11-21, Claude 2인 PIE 통과 — 사용자 PIE 확인 대기): 경매 창(`UJMAuctionManager`·`UAuctionWidget`/`WBP_Auction`, K 키 = 조회 모드·경매 NPC 앞 F 키 = NPC 모드). 구현 내용은 아래 "A6.3b 구현 메모".
- A4.3 완료(2026-11-21, Claude 테스트): 경매장 서버 — 전역 `AuctionService`, `Auction_Listing`·`Player_Mail`(마이그레이션 0008), 패킷 CS 1024~1029·SC 2024~2026, 경매 NPC(`NpcTable.Auction`). 쓰는 곳과 혼잡 기준이 설계에서 바뀌었습니다(D-234·D-235). 구현 내용은 아래 "A4.3 구현 메모".
- L4 완료(2026-11-19, Claude 테스트 + 사용자 PIE 확인): 상점 NPC(구매·판매, D-201·D-202). 설계와 구현 메모는 아래 "상점 NPC (L4)". A3.1 때는 범위 밖이었으나 D-167로 마일스톤 1에 들어왔습니다.
- A6.3a 완료(2026-11-17, 사용자 PIE 확인): 거래 UI(`UJMTradeManager`·`UTradeWidget`/`WBP_Trade`, T 키). 구현 내용은 아래 "A6.3a 구현 메모".
- A4.1·A4.2 완료(2026-11-17): 1:1 거래 서버(상태기계·패킷·한 트랜잭션 교환·`Trade_Log`, 마이그레이션 0007). 구현 내용은 아래 "A4.1·A4.2 구현 메모". 거래 UI(A6.3a)와 경매 코드는 아직 없습니다.
- A3.6 완료(2026-11-17, 사용자 PIE 확인): 소비 아이템 사용 `CS_USE_ITEM`·`SC_USE_ITEM`, 액티브 칸 배치 `CS_SET_QUICKSLOT` + `Player_QuickSlot`(마이그레이션 0006). 구현 내용은 아래 "A3.6 구현 메모".
- A3.5 완료(2026-11-17): 줍기 `CS_PICKUP_ITEM`·버리기 `CS_DROP_ITEM`(서버). 구현 내용은 아래 "A3.5 구현 메모". 클라이언트 표시·입력(A6.2)도 완료(사용자 PIE 확인)했습니다.
- A3.4 완료(2026-11-16): 몬스터 처치 시 드랍 롤 → 바닥 드랍(`DropItem`, `OBJECT_TYPE_ITEM`) 생성·소유권 기록·드랍 전용 시야·소멸·방당 상한. 구현 내용은 아래 "A3.4 구현 메모".
- A3.3 완료(2026-11-16): `Player_Item`(마이그레이션 0004, `Player_Equipped` 이전·삭제), 서버 `Inventory`·아이템 작업 공통 경로(`itemOp`), 장착을 `item_db_id` 기준·DB 성공 뒤 반영으로 전환, 개발 지급 `CS_DEV_GIVE`, `SC_INVENTORY`·`SC_INVENTORY_UPDATE`·`SC_ITEM_RESULT`, 클라이언트 `UJMInventoryManager`(데이터만 — 창은 A6.1)와 콘솔 `Give`. 구현 내용은 아래 "A3.3 구현 메모".
- 골드는 진행 상황 스냅숏에서 빠졌고 아이템 트랜잭션에서만 증감합니다(지금 들어오는 경로는 개발 지급뿐, 골드 드랍은 A3.4).
- A2.7 완료(2026-11-16): 같은 캐릭터의 중복 접속은 기존 접속을 끊고 새 로그인을 받습니다(아래 "중복 접속 차단"). 인벤토리·아이템 작업을 추가할 때는 `Room::NotifyClosedIfDone`의 조건과 호출 지점에 함께 넣어야 합니다.
- A3.2 완료(2026-11-16): `DropTable.csv`·`EconomyTable.csv`와 로더(`DataManager::GetDrops`·`RollDrops`·`GetEconomy`), `ItemTable` 새 열과 8001 HP 물약, UE `FItemTableRow`·`DT_ItemTable`이 들어갔습니다. 드랍 롤은 A3.4에서 `RollDrops`를 호출합니다. 열별 허용 범위와 현재 행은 [DESIGN_DATA.md](DESIGN_DATA.md).

## 범위와 하지 않는 것
- **범위**: DropTable, 아이템 인스턴스와 DB, 인벤토리, 드랍·줍기·버리기, 장착 검사 확장, 소비 아이템(HP 물약 1종)과 액티브 칸 저장, 골드, 중복 접속 차단(A2.7), 1:1 거래, 경매장(즉시 구매), NPC 상점(구매·판매 — L4, D-167), 패킷, 시험 항목.
- **하지 않는 것**: 경매 입찰, 수수료, 분배 규칙(주사위·순번 — 파티 중 드랍은 파티원 누구나 줍는 공유만, D-217), 피해 기여 소유권, 인벤토리 안에서 스택을 나누거나 합치는 조작, 인벤토리 정렬·칸 이동, 강화·옵션, 상점 되사기·상점 재고·이용 기록 테이블, 귀속(거래 불가) 아이템, 다른 Room 플레이어와의 1:1 거래, 바닥 아이템의 DB 저장, 골드 버리기.

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
- **`SellPrice`(L4, D-201, `UseCooldownMs` 뒤, 서버·클라)**: 상점에 팔 때 1개당 받는 골드. 0 = 팔 수 없음, 0 ~ `EconomyTable.GoldMax`. 기본 장비(`Starter` 1)도 값을 줍니다. 클라이언트는 표시에만 쓰고 금액은 서버가 계산합니다. 초안: 천 장비 20, 판금 장비 60, HP 물약 5.
- UE: `FItemTableRow`에 같은 이름의 필드를 추가하고(`UseEffect`는 새 UENUM `EItemUseEffect`) `DT_ItemTable`을 다시 임포트합니다. 물약 아이콘 1개가 필요합니다(콘텐츠 동결 중이라 임시 아이콘 허용). 외형 열을 만드는 `jm_kit.item_table.export`는 `items.json`에 없는 행이 있으면 실패하므로 **메시가 없는 행(`Slot` `None`)은 건너뛰도록** 함께 고칩니다. `Slot`·`Tier`의 `None`은 UE enum에서 숨김(`UMETA(Hidden)`) 값이지만 CSV 임포트가 됩니다(A3.2에서 확인 — 8001 행 `0 Problems`). 물약 초기값: `MaxStack` 20(AU2에서 100 — D-281), `UseValue` 200, `UseCooldownMs` 5000, 아이콘 `Content/UI/Icons/Item/T_Icon_Potion_Hp`(임시).

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
| `InventorySlots` | 80 | 인벤토리 칸 수(장착품 제외 — D-285, 창은 8열 × 10줄 스크롤) |
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
| `ShopRangeCm` | 400 | 상점 NPC 이용 거리(L4, D-202). 클라이언트에는 `SC_INVENTORY.shop_range_cm` |
| `PartyMaxMembers` | 4 | 파티 정원(DG2, D-224 — 2~100) |
| `PartyInviteTimeoutSec` | 30 | 파티 초대 응답 대기(1~600). 클라이언트에는 `SC_PARTY_INVITE.timeout_sec` |

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
- 얻는 곳: 몬스터 골드 드랍(DropTable `ItemId` 0), 거래, 경매 판매 대금, 상점 판매(L4), 개발 지급 명령. 쓰는 곳: 상점 구매(L4). 경험치·레벨 보상은 A2.3b에 남습니다.

### 트랜잭션 범위
| 동작 | 한 트랜잭션에 넣는 것 |
|---|---|
| 줍기 | 아이템: 같은 종류 스택에 `Count` 더하기 또는 행 INSERT / 골드: 증액 |
| 버리기 | 행 DELETE(그 뒤 메모리에서 바닥 드랍 생성) |
| 장착·해제 | 관련 행(새 장비·옛 장비·자동 해제되는 방패, 최대 3행)의 `Location`·`EquipSlot` |
| 물약 사용 | `Count` − 1(0이면 DELETE) |
| 상점 구매 | 같은 종류 스택에 `Count` 더하기 또는 행 INSERT + 골드 차감(`Gold >= 총액` 조건) |
| 상점 판매 | 행 DELETE(전량) 또는 `Count` 감소(일부, 기대 수량을 조건으로) + 골드 증액(상한 조건) |
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

### A3.6 구현 메모 (2026-11-17)
- **사용** `Room::PlayerUseItem`: 검증 순서는 방 안 → 더미 봇 아님 → 쓸 행 찾기(`NO_ITEM`) → `UseEffect`(`NOT_USABLE`) → `RequiredLevel`(`LEVEL_TOO_LOW`) → 생존(`DEAD`) → HP 최대(`HP_FULL`) → 재사용 시간(`COOLDOWN`) → 요청 간격·진행 중인 작업. `item_db_id` 0이면 그 종류의 인벤토리 행 중 수량이 가장 적은 것(같으면 `ItemDbId`가 작은 것). `ItemOpKind::Use`(고정 쿼리 `ItemUse`: 수량 − 1, 1개였으면 DELETE).
- **사용 결과**(`Room::OnItemOpDone`): 성공이고 방에 있으면 수량 반영 → `OnHealed(UseValue)`(최대치까지, 그 사이 죽었으면 회복 없음) → 재사용 시각 기록(`Player::itemCooldownUntilMs`, 결과 적용 시점부터 `UseCooldownMs`) → `SC_USE_ITEM`(AOI, 본인 포함) + `SC_INVENTORY_UPDATE`. 실패는 `DB_ERROR`.
- **액티브 칸** `Room::PlayerSetQuickSlot`: 칸 1~5(`QUICK_SLOT_COUNT`), 종류는 ItemTable에 있고 `UseEffect`가 있어야 합니다. 지금 무기의 스킬이 있는 칸은 `BAD_SLOT`. `itemOp`를 쓰지 않고 메모리(`Player::quickSlots`)에 먼저 반영한 뒤 저장합니다. 요청 간격은 아이템 요청과 같은 것을 씁니다.
- **구현하면서 정한 것(설계에 없던 세부 — 바꾸려면 알려 주세요)**: ① 배치 응답은 새 패킷 대신 `SC_INVENTORY_UPDATE`에 `quick_slots`(4번, 바뀌었을 때만 5칸 전체)를 실어 보냅니다. ② 한 종류는 한 칸에만 둡니다(다른 칸에 올리면 옮겨짐). ③ 올릴 때 보유 여부는 보지 않습니다(다 써도 칸은 남고 수량 0으로 흐리게 보임).
- **저장**: `Room::RequestQuickSlotSave` → `DatabaseWorker::SaveQuickSlots`(MERGE 한 문장으로 5칸을 맞춤) → `Room::OnQuickSlotsSaved`. `Player::quickSlotSave`(`SaveState`)로 플레이어당 동시 1건, 진행 중 변경은 끝난 뒤 최신 값으로 한 번 더, DB 값(`lastSavedQuickSlots`)과 같으면 보내지 않습니다. 퇴장 때도 부르고 `NotifyClosedIfDone`이 이 저장이 끝나기를 기다립니다. 로그인 때 쓸 수 없는 종류가 든 칸은 메모리에서 비우고 입장 직후 DB에서도 지웁니다.
- **로그인**: `SelectLogin`에 칸 1~5 열(Q1~Q5)을 더해 `DB_PlayerData.quickSlots`로 읽고, `SC_INVENTORY.quick_slots`에 5칸을 싣습니다.
- **클라이언트**: `UJMInventoryManager`가 배치를 담고(`GetQuickSlotItem`·`GetBagItemCount`), `AJMMyPlayer::RebuildQuickSlots`가 스킬 우선 → 아이템 → 빈 칸으로 채웁니다. HUD는 인벤토리가 바뀔 때마다 칸을 다시 그립니다(수량, 0이면 흐리게). 숫자키 = `CS_USE_ITEM(0, 종류)`, 인벤토리의 소비 아이템 우클릭 = `CS_USE_ITEM(그 행)`. 배치는 인벤토리 창을 연 상태에서 아이템을 칸에 끌어다 놓기(`USkillSlotWidget::NativeOnDrop`), 칸 우클릭 = 비우기. 재사용 표시는 `SC_USE_ITEM.cooldown_ms`(서버 값)로 그 종류가 든 칸에 합니다.
- **알아둘 간극**: 사용 요청의 DB 결과가 오기 전에 접속이 끊기면 수량은 줄고 회복은 적용되지 않습니다(DB 성공 뒤 메모리 반영 원칙 — HP는 메모리 값이라 방에 없으면 반영할 곳이 없음). 재사용 시간은 저장하지 않아 재접속하면 초기화됩니다.
- **시험(TESTING 2026-11-17 A3.6)**: 사망 중 거부(`DEAD`)는 원시 소켓으로 확인하지 못했습니다(사용자 PIE에서 확인).

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

### A4.1·A4.2 구현 메모 (2026-11-17)
- **상태**: `Room::_trades`(거래 id → `TradeSession`: 단계 `TradePhase`, 양쪽 `TradeSide` = 플레이어·제시·잠금·확정, 요청·수락 시각), `Player::tradeId`. 단계는 `REQUESTING` → `OFFERING`(잠금·확정 여부는 제시마다) → `CONFIRMING`(DB 결과 대기). 끝나면 상태를 지웁니다(`Room::EndTrade` — 두 사람에게 `SC_TRADE_END`).
- **패킷**: `SC_TRADE_STATE`는 두 사람에게 같은 내용(`offers[0]` = 요청한 쪽, `offers[1]` = 받은 쪽, 각 `object_id`·아이템·골드·`locked`·`confirmed`)을 바뀔 때마다 보냅니다. 받은 쪽에는 추가로 `SC_TRADE_REQUEST`(요청자 id·이름·응답 제한 시간). 거부는 본인에게 `SC_TRADE_RESULT`(요청 패킷 id + 사유).
- **구현하면서 정한 것(설계에 없던 세부 — 바꾸려면 알려 주세요)**: ① **누가 제시를 바꾸면 양쪽의 잠금·확정을 모두 풉니다**(설계는 "잠근 쪽의 제시 변경 거부"와 "잠금을 풀면 양쪽이 풀림"만 적었는데, 한쪽이 잠근 뒤 상대가 제시를 빼는 경우를 막으려고 더 엄격하게 함). ② 지금 단계에서 받을 수 없는 요청(수락 전 제시, 한쪽만 잠근 상태의 확정 등)은 `NOT_ALLOWED`. ③ 요청 도배는 아이템 요청 간격(`ItemMinIntervalMs`)으로 막고 `BUSY`로 답합니다. ④ 확정 중의 취소는 `BUSY`.
- **올린 행 잠금**: `Room::IsItemInTrade` — 장착(`EQUIP_RESULT_IN_TRADE`)·버리기·사용(`ITEM_RESULT_IN_TRADE`)을 거부하고, 종류로 쓰는 사용(액티브 칸)은 올린 스택을 건너뜁니다. 칸 옮기기와 줍기(스택에 더해짐)는 허용합니다 — 넘어가는 것은 제시한 수량뿐입니다.
- **자동 취소**: 퇴장(`PlayerLeaveRoom` → `DISCONNECTED`), 사망(`Player::OnDead` → `DEAD`), 이동으로 거리 초과(`PlayerMove` 끝 → `TOO_FAR`), 시간 초과(`Room::UpdateTrades`, 1초마다 — 요청 `TradeRequestTimeoutSec`, 수락 뒤 `TradeTimeoutSec`). 확정 중에는 어느 것도 거래를 끝내지 않고 DB 결과를 기다립니다.
- **확정** `Room::TryExecuteTrade`: 연결·생존·거리·양쪽 `itemOp` 없음·아이템(아직 인벤토리에 있고 수량 ≥ 제시 수량)·골드·받는 쪽 칸(지금 빈 칸 + 받는 쪽이 통째로 내놓아 비게 될 칸)·골드 상한을 다시 봅니다. 실패하면 양쪽 잠금·확정을 풀고 두 사람에게 `SC_TRADE_RESULT(CS_TRADE_CONFIRM, 사유)`. 통과하면 `ItemOpKind::Trade`를 요청한 쪽(A)의 `itemOp`로 보내고 받은 쪽(B)의 `itemOp.inFlight`도 켭니다.
- **트랜잭션** `DbQueries::BuildItemTrade`(조립식): ① 통째로 넘기는 행은 **UPDATE 한 문장**으로 `OwnerId`·`BagSlot` 변경(받는 쪽이 방금 내놓은 칸을 다시 써도 칸 유니크 인덱스를 어기지 않음) ② 일부만 넘기는 행은 원래 행 `Count` 감소 + 받는 쪽 새 행 ③ 골드 양쪽 증감(잔액은 결과 Kind 2 = A, 3 = B) ④ `Trade_Log` 1행. 기대 상태(`OwnerId`·`Location`·`ItemId`·`Count`)가 다르면 롤백.
- **결과** `Room::OnTradeDone`: 성공이면 방에 남아 있는 쪽의 메모리에만 반영(`SC_INVENTORY_UPDATE`) 후 `SC_TRADE_END(SUCCESS)`, 롤백이면 `DB_ERROR`, 결과 불명이면 두 사람 모두 접속을 끊습니다. B의 `itemOp`를 끄고 `NotifyClosedIfDone`을 양쪽에 부릅니다.
- **시험(TESTING 2026-11-17 A4.1·A4.2)**: 100회 반복에서 합계 보존·`Trade_Log` 행 수 일치. 거래 중 사망 취소는 확인하지 못했습니다.

## 경매장 (A4.3, D-159 → AU2 재기획 D-281)
- **AU2 재기획(D-281, 2026-11-23)**: 아래 등록·구매·조회 항목은 AU2 기준입니다. A4.3 구현 메모는 당시 기록이고, AU2a 구현 메모가 바뀐 부분을 적습니다.
- **즉시 구매만**, 수수료 없음. 팔린 대금·산 아이템·만료·취소 반환은 모두 **수령함**에 쌓이고 경매 창에서 직접 받습니다.
- **쓰는 곳(D-234 — 처음 설계의 "어디서나 키로"를 바꿈)**: **검색·구매·내 등록/수령함 보기**는 던전 밖 어디서나(K 키), **등록·취소·수령**은 마을의 **경매 NPC**("경매 관리인", `NpcTable.Auction` 1) 근처(`EconomyTable.ShopRangeCm`)에서만(F 키). 인벤토리에 아이템·골드가 들고 나는 일은 모두 NPC 앞에서만 일어납니다. 사망 중과 던전 안에서는 어느 요청도 받지 않습니다(`DEAD`·`IN_DUNGEON`). NPC에서만 열게 하면 NPC 주변에 사람이 몰리므로 가장 잦은 조회·구매는 어디서나 둡니다. 서버에 "창을 연 상태"는 없고 요청마다 검증합니다(상점과 같음).
- **혼잡 기준(D-235)**: 경매 종류의 DB 작업(등록·구매·취소·수령·수령함 읽기·만료 묶음)이 `ServerConfig` `[Database] AuctionDbMaxInFlight`(제안값 16, 0 = 끔)건 넘게 진행 중이면 새 요청을 `TRY_LATER`("잠시 뒤에 다시 요청")로 거부합니다 — 경매가 DB 작업 스레드를 독차지해 줍기·장착이 늦어지지 않게. 값은 PL1·A5.4 측정으로 조정합니다. 부하 테스트의 경매 프로필은 조회 비율을 높게 잡습니다(더미 봇도 검색은 됨).
- **`AuctionService`**(`GAuction`): `AuthLobby`처럼 자체 JobQueue를 가진 전역 서비스이고 수명은 프로세스와 같습니다. 시작 때 판매 중인 등록을 전부 읽어 메모리 목록을 만듭니다(권위는 DB, 목록은 검색·중복 구매 차단용).
- **요청 경로(등록·구매·취소)**: 패킷 → 요청자의 Room 잡(검증, `itemOp` 켬) → `GAuction` 잡 → `DatabaseWorker` → `GAuction` 잡(목록 갱신) → 요청자의 Room 잡(메모리·패킷 반영, `itemOp` 끔). Room과 서비스는 서로의 상태를 직접 만지지 않고 잡으로만 주고받습니다. 결과가 돌아왔을 때 요청자가 방에 없으면 반영만 건너뜁니다. `itemOp`가 켜져 있는 동안에는 Room 이동이 기다리므로(DG3b) 결과가 옛 Room으로 돌아오는 일이 없습니다.
- **조회(검색·내 목록)는 `itemOp`를 쓰지 않습니다**: Room 잡은 검증만 하고 서비스가 세션에 직접 답합니다(파티와 같은 방식). 읽기 전용이라 Room 이동·퇴장 정리와 무관합니다.
- **등록** `CS_AUCTION_REGISTER(npc_id, item_db_id, count, unit_price)`: Room에서 인벤토리 위치·거래 중 아님·수량(1 ~ 그 행의 `Count`)·**1개당 가격**(`AuctionMinPrice`~`AuctionMaxPrice`)·총액(단가 × 수량 ≤ `GoldMax`) 확인, 서비스에서 1인 등록 수 확인. 성공하면 그 수량이 인벤토리에서 빠져 에스크로(`Location` 3)로 갑니다. 한 등록 = 인벤토리 칸 하나(스택 상한은 아이템별 `MaxStack`, 물약·전리품 100). 에스크로 행의 `Count`는 늘 등록의 남은 수량과 같습니다.
- **처리 중 표시와 수량 예약(D-281)**: 취소·만료는 그 등록에 **처리 중**(closing) 표시를 한 뒤 DB로 보냅니다. 구매는 표시 대신 **수량 예약**을 합니다: 서비스가 `남은 수량 − 예약`으로 판정해 예약을 잡고 DB로 보내므로 같은 등록의 구매 여러 건이 함께 DB에 갈 수 있고, DB는 `Count >= @N` 조건부 갱신으로 이중 판매를 막습니다. 성공하면 남은 수량을 줄이고(0이면 목록에서 뺌), 실패하면 예약을 돌려줍니다. 진행 중인 구매가 있는 등록의 취소는 `LISTING_BUSY`, 만료 검사는 건너뜁니다. 결과를 알 수 없는 구매가 있었으면 진행 중인 구매가 모두 끝난 뒤 DB의 `State`·`Count`를 다시 읽어 맞춥니다.
- **구매** `CS_AUCTION_BUY(listing_id, count)` — **부분 구매(D-281)**: 서비스가 목록에서 판매 중·본인 물품 아님·끝나는 중 아님·남은 수량(예약 제외)·골드(수량 × 단가)를 확인합니다. 다 팔렸으면 `SOLD`("이미 판매 완료되었습니다."), 남은 수량이 모자라면 `NOT_ENOUGH_COUNT` + 남은 수량("남은 수량은 N개입니다."), 만료·취소로 끝났으면 `ALREADY_CLOSED`("판매가 끝난 물품입니다.") — 서비스는 최근 1시간에 끝난 등록의 사유를 기억합니다. 구매자는 골드만 줄고 산 수량은 수령함으로 갑니다(다 사면 에스크로 행이 그대로, 일부면 에스크로 행을 나눠 새 행). 판매 대금은 **팔릴 때마다 한 통** 수령함으로 가고 판매 이력은 `Auction_Sale`에 남습니다. **판매자가 접속해 있으면 알림만 보냅니다**(`SC_MAIL_NOTICE` 2045, D-262 — 만료 반환도 같은 패킷, 로그인 때 수령함이 비어 있지 않으면 종류 NONE). 알림은 표시용이고 골드·아이템이 들어오는 것은 여전히 경매 NPC 앞의 수령뿐입니다. 클라이언트는 시스템 메시지 + 채팅 한 줄 + 수령함이 빌 때까지 HUD 표시(D-265).
- **취소** `CS_AUCTION_CANCEL(npc_id, listing_id)`: 본인의 판매 중 등록만. 아이템은 수령함으로.
- **만료**: 서비스가 `AuctionExpireCheckSec`마다 기간이 지난 등록을 만료 처리하고 수령함으로 돌려줍니다. 서버가 꺼져 있던 동안 지난 것은 시작 후 첫 검사에서 처리합니다. 한 번에 50건까지 한 트랜잭션으로 묶고, 밀린 것이 남으면 1초 뒤 다시 합니다.
- **조회(D-281)**: `CS_AUCTION_SEARCH(slot, query, keyword, sort, desc, cursor_key, cursor_id)`. `query` = `SEARCH`(검색·새로고침 — 검색어를 서버가 기억, 간격 `AuctionSearchIntervalMs` 5초) · `SORT`(열 머리 — 기억한 검색어로 처음부터) · `MORE`(다음 묶음 — 기억한 검색어로 커서 다음부터), 뒤의 둘과 내 목록·구매·등록·취소·수령은 `AuctionActionIntervalMs` 1초(경매 전용 간격 — 아이템 공용 간격과 따로). 검색어는 아이템 이름 부분 일치(서버가 ItemTable `DisplayName`을 공백 제거·영문 소문자로 맞춰 비교, 최대 20자), 빈칸 = 전체. 정렬 = 1개당 가격·가격(남은 수량 × 단가)·남은 시간(만료 시각), 오름/내림, 같은 값이면 `ListingId`. 서비스는 정렬마다 전체용·아이템별 정렬 색인(`std::set`)을 두고, 검색어가 있으면 맞는 아이템들의 색인을 커서부터 병합해 `AuctionFetchRows`(50)줄을 `SC_AUCTION_LIST`로 보냅니다(총 건수·더 있음·쪽 줄 수 `AuctionPageRows` 10·간격 값 포함 — 클라이언트에는 EconomyTable이 없음). 커서는 마지막 줄의 (정렬 값, `ListingId`)라 사이에 팔리거나 새로 올라와도 줄이 겹치거나 빠지지 않습니다. **보고 있는 목록은 갱신하지 않습니다** — 구매 응답으로만 알립니다. 필터(`slot`)는 나중(지금은 0만). `CS_AUCTION_MY` → 내 등록(서비스 목록) + 수령함 `SC_AUCTION_MY`. 수령함은 메모리에 두지 않고 요청 때 DB에서 오래된 것부터 `AuctionMailPageSize`개를 읽습니다(Room → 서비스 → DB → 서비스 → Room). 수령하면 다음 것이 보입니다.
- **수령** `CS_MAIL_CLAIM(npc_id, mail_id)`: 한 건씩. 서비스를 거치지 않고 Room → DB → Room. 아이템은 빈 칸, 골드는 상한을 확인합니다.

### A4.3 구현 메모 (2026-11-21, D-234·D-235)
- **서비스** `AuctionService`(`Game/Auction/AuctionService`, 전역 `GAuction`): 판매 중 목록(`_listings`), 판매자별 등록(`_bySeller` — 1인 등록 수·내 등록), 필터별 가격순 색인(`_index[9]`, 등록·제거 때 제자리에 넣고 뺌). 시작 때 `DatabaseWorker` 생성자가 DB 스레드를 만들기 전에 판매 중 등록을 읽어 두고(실패하면 시작 중단) `AuctionService::Init`이 넘겨받습니다. 남은 시간은 DB가 `DATEDIFF`로 초 단위로 주고 서비스가 자기 시계로 바꿉니다(`ExpireAt`은 UTC). **만료 시각 판단은 서비스가 하고 쿼리는 `State`만 조건으로 봅니다.**
- **Room** `Room::CheckAuctionRequest`(방 안 → 더미 봇(검색만 허용) → 생존 → 던전 아님 → 요청 간격 → [바꾸는 요청] `CheckItemRequest`(Room 이동 중·진행 중인 작업 `BUSY`) → [등록·취소·수령] 경매 NPC·거리 `TOO_FAR`) → `PlayerAuctionSearch`·`My`·`Register`·`Buy`·`Cancel`·`PlayerMailClaim`. 등록·구매·취소는 `Room::StartItemOp`가 DB 대신 `AuctionService::BeginOp`로 보내고, 결과는 다른 아이템 작업처럼 `Room::OnItemOpDone`으로 돌아옵니다(`ItemOpKind::AuctionRegister`·`AuctionBuy`·`AuctionCancel`·`MailClaim`). 조회의 요청 간격은 `Player::auctionQuery`(아이템 요청 간격과 따로).
- **서비스의 거부도 Room으로 돌아옵니다**: `BeginOp`가 거부하면(등록 수 초과 `LISTING_LIMIT`, 없는 등록 `NO_LISTING`, 처리 중 `SOLD`, 본인 물품 `OWN_ITEM`, 기간 지남 `NO_LISTING`, 골드 부족 `NOT_ENOUGH_GOLD`) 결과에 사유(`DB_ItemOpResult::auctionResult`)를 적어 `OnItemOpDone`으로 보냅니다 — 빠지면 그 플레이어의 `itemOp.inFlight`가 남아 다음 로그인·Room 이동·던전 인스턴스 닫기가 막힙니다. 서비스는 `Player` 필드를 만지지 않으므로 구매의 골드 비교는 Room이 `DB_ItemOp::auctionGold`에 실어 보낸 값으로 합니다(DB의 `Gold >= 가격` 조건이 두 번째 방어선).
- **실패한 구매·취소·만료 뒤**: 롤백이든 결과 불명이든 서비스가 그 등록의 DB `State`를 다시 읽어(`VerifyListing`) 판매 중이면 처리 중 표시를 풀고 아니면 목록에서 뺍니다. 그동안 그 등록에 온 요청은 `SOLD`입니다. 결과를 알 수 없는 등록은 그 판매자의 판매 중 등록을 다시 읽어 목록에 없는 것을 넣습니다(`ResyncSeller`).
- **쿼리**(`DbQueries.h`, 모두 아이템 트랜잭션 틀): `AuctionRegister`(통째면 `Location` 1 → 3·`BagSlot` -1, 일부면 원래 행 수량 감소 + 경매 보관 새 행), `AuctionBuy`, `AuctionCancel`, `BuildMailClaim`(우편 내용은 메모리에 없어 쿼리가 읽어 판단 — 받을 수 없는 사유는 실패 행 `Kind` 9로 `NO_MAIL`·`INVENTORY_FULL`·`GOLD_FULL`을 돌려줌), `BuildAuctionExpire`(묶음 — 실제로 만료한 id를 `Kind` 8로), 읽기 `SelectActiveListings`·`SelectSellerListings`·`SelectListingState`·`BuildSelectMail`. 결과 `Kind` 4 = 새 `ListingId`, 5·6·7 = 수령한 아이템.
- **구현하면서 정한 것(설계에 없던 세부 — 바꾸려면 알려 주세요)**: ① 더미 봇도 검색은 됩니다(DB 행을 건드리지 않음 — 부하 시험의 조회 부하용). ② `SC_AUCTION_RESULT`는 성공도 알립니다(`id` = 등록 성공이면 새 등록, 수령이면 우편). ③ 기간이 지났지만 아직 만료 처리 전인 등록은 검색에 남고(남은 시간 0) 구매하면 `NO_LISTING`입니다. ④ 거래 중에도 경매를 쓸 수 있고 거래에 올린 행의 등록만 `IN_TRADE`입니다. ⑤ 수령은 빈 칸이 없으면 칸 -1을 보내 골드 우편은 받고 아이템 우편은 `INVENTORY_FULL`로 거부됩니다. ⑥ 검색 필터 8 = 슬롯 없는 아이템(소비 등). ⑦ `SC_AUCTION_MY`에 1인 등록 수 상한·가격 범위를 싣습니다(클라이언트 입력 안내용).
- **지표** `[metrics] auction(now listings/dbInFlight search/s total reg/buy/cancel/claim/expire reject/tryLater)` — 접속이 모두 끝난 뒤 `dbInFlight`가 0이 아니면 `TryBeginDbWork`·`EndDbWork` 짝이 빠진 경로가 있는 것이고, `listings`는 DB의 판매 중 등록 수와 같아야 합니다.
- **시험(TESTING 2026-11-21 A4.3)**: 원시 소켓 14개 시나리오(`Server/PerfData/2026-11-21_A4.3/`). 사망 중 거부(`DEAD`)와 결과 불명(연결이 끊긴 COMMIT) 뒤의 `ResyncSeller`는 확인하지 못했습니다.

### AU2a 구현 메모 (2026-11-23, D-281)
- **서비스** `AuctionService`: 등록마다 `count`(DB에 확정된 남은 수량)·`unitPrice`·`reserved`(진행 중 구매가 예약한 수량)·`inFlightBuys`·`closing`(취소·만료가 DB에 감)·`needVerify`/`verifying`. 색인은 정렬 3종(1개당·총액 = 남은 수량 × 단가·만료 시각)마다 전체용 1벌 + 아이템 종류별 1벌의 `std::set<(정렬 값, ListingId)>` — 부분 판매로 총액이 바뀌면 빼고 다시 넣습니다. 검색어가 있으면 Room이 정리한 아이템 id들의 색인을 커서부터 k-way 병합해 `AuctionFetchRows`줄을 꺼내고, 총 건수는 그 종류들의 등록 수 합입니다. 내림차순은 역순 순회(같은 값이면 ListingId도 역순).
- **구매 판정 순서**(`BeginOp`): 없는 등록 → 최근 1시간 안에 끝났으면 그 사유(`SOLD`·`ALREADY_CLOSED`), 아니면 `NO_LISTING` → 본인 `OWN_ITEM` → 끝내는 중·기간 지남 `ALREADY_CLOSED` → 다시 읽는 중 `LISTING_BUSY` → 가용(남은 − 예약) 0 `SOLD` → 수량 초과 `NOT_ENOUGH_COUNT`(+ 가용 수량) → 골드(수량 × 단가) `NOT_ENOUGH_GOLD` → 예약. 결과가 오면 예약을 돌려주고, 성공이면 남은 수량 = min(메모리 − 수량, DB가 알려 준 남은 수량)(결과가 커밋 순서와 다르게 와도 맞게), 실패(롤백·불명)면 `needVerify` → 진행 중 구매가 0이 되면 `State`·`Count`를 다시 읽습니다.
- **쿼리** `AuctionBuy`(파라미터 4 = 수량 `@Qty` — `@N`은 틀의 `@n`과 같은 변수라 쓰지 않음): 등록 UPDATE(`Count >= @Qty`, 바뀐 뒤 값은 `OUTPUT`으로) → 골드 → 다 팔리면 보관 행을 구매자 수령함으로 / 일부면 보관 행 감소 + 새 행 → 우편 2통 + `Auction_Sale` → 결과 Kind 2 잔액·Kind 10 남은 수량. 기대 행 수는 `@W`(통째 3·일부 4).
- **DB**(마이그레이션 0010): `Auction_Listing.UnitPrice`·`InitialCount`, `Count` = 남은 수량(팔림이면 0, 취소·만료면 반환한 수량), `Price`·`BuyerId` 삭제, `Auction_Sale(ListingId, BuyerId, Count, Gold)`. 옛 팔린 등록은 지우기 전의 `BuyerId`·`Price`로 판매 행을 만들었습니다.
- **Room**: 요청 간격 `Player::auctionSearch`(SEARCH)·`auctionAction`(SORT·MORE·구매·등록·취소·수령 — 아이템 공용 간격과 따로)·`auctionMy`(내 목록 — 같은 길이를 따로 잼, AU2b에서 분리). SEARCH 때 검색어를 정리해 `auctionKeyword`·`auctionSearchAll`·`auctionItemIds`로 기억하고 SORT·MORE는 이 값으로 찾습니다. 검색어는 `PacketHandler::SanitizeChat` 뒤 20자(`AUCTION_KEYWORD_MAX`).
- **지표**: `auction(... partial/conflict)` — 부분 구매 성공 수, 수량 예약에 걸린 거부 수.
- **구현하면서 정한 것(바꾸려면 알려 주세요)**: ① 검색 목록에 보이는 수량은 DB에 확정된 남은 수량(예약 중인 수량을 빼지 않음) ② 필터 값(slot)이 0이 아니면 `NOT_ALLOWED` ③ 정렬 변경·다음 묶음도 요청 간격 1초에 들고 구매·등록·취소·수령과 같은 간격을 씀(내 목록은 같은 1초를 따로 — AU2b 구현 메모) ④ 취소된 등록을 다시 취소하면 `NO_LISTING`(구매는 `ALREADY_CLOSED`).

### AU2b 구현 메모 (2026-11-23, D-281)
- **매니저** `UJMAuctionManager`: 받은 줄을 모두 쌓는 줄 캐시(`Listings`) — SEARCH·SORT 응답은 캐시를 갈아 끼우고 1쪽으로, MORE 응답은 뒤에 붙입니다(그 사이 검색·정렬을 새로 했으면 버림). 쪽(`CurrentPage`, 1부터)은 클라이언트가 갖고 캐시를 `page_rows`줄씩 나눈 구간(`GetPageRange`)입니다. 캐시 끝을 넘는 쪽으로 가면 마지막 줄의 커서(`CursorKey` = `cursor_keys[i]`, `ListingId`)로 MORE를 보내고 응답이 오면 넘어갑니다. 더 받을 것이 없으면(`has_more` 거짓) 총 건수 = 받은 줄 수로 맞춥니다(그 사이 팔려 빈 쪽이 생기지 않게).
- **요청 간격**: `NextSearchTime`(검색·새로고침)·`NextActionTime`(정렬·다음 묶음·구매·등록·취소·수령)·`NextMyTime`(내 목록) — 서버가 준 값 + 여유 0.1초. 간격 안에는 보내지 않고 창이 버튼을 꺼 둡니다(`CanSearch`·`CanAct`). 내 목록 요청만 버리지 않고 간격이 풀리는 때에 타이머로 보냅니다(`FlushMy`). 서버가 그래도 `TOO_FREQUENT`를 주면 조용히 넘기고 그 간격만 다시 겁니다.
- **구매 응답**(목록은 다시 받지 않음): 성공 = 그 줄 수량을 `remain`으로(0이면 끝난 줄), `SOLD` = 끝난 줄 + "이미 판매 완료되었습니다.", `NOT_ENOUGH_COUNT` = 수량을 `remain`으로 + "남은 수량은 N개입니다.", `ALREADY_CLOSED`·`NO_LISTING` = 끝난 줄 + "판매가 끝난 물품입니다.", `LISTING_BUSY` = "처리 중인 물품입니다. 잠시 뒤에 다시 시도해 주세요." 끝난 줄(`bClosed`)은 지우지 않고 회색 "판매 완료"로 남기며 고를 수 없습니다.
- **창을 다시 열 때**: 검색어·정렬·캐시를 유지합니다. 검색 간격이 지났으면 지난 검색어로 다시 찾고, 아니면 이전 결과를 그대로 보여 줍니다. 레벨 전환 때는 비웁니다(`Clear`).
- **창** `UAuctionWidget`/`WBP_Auction`: 검색칸 `EditSearch`(20자, 엔터 = 검색)·`BtnSearch`(글자 `TxtSearchBtn` — 간격이 남으면 "검색 · 3초")·`BtnRefresh`(검색과 같은 동작), 열 머리 `BtnSortUnit`·`BtnSortTotal`·`BtnSortRemain`(글자에 ▲▼, 같은 열 = 방향 반대·다른 열 = 오름차순), 4열 줄(`UAuctionRowButton::SetColumns` — 이름은 `MaxStack > 1`인 아이템만 `이름(남은 수량)`), `TxtPage` "n / m쪽 · 총 k건", 구매 줄 `BuyCountBox`(`EditBuyCount`·`BtnBuyMax` — 스택 아이템을 골랐을 때만), 등록 탭 "1개당 가격"과 합계. 열 폭은 `ColUnitWidth`·`ColTotalWidth`·`ColRemainWidth` 한 곳이고 `NativeConstruct`가 열 머리(`Col*Box`)에도 입힙니다. `FilterBox`는 숨깁니다(필터는 나중).
- **구매 수량 칸**: 기본 1, 숫자만 남기고 남은 수량보다 크면 남은 수량으로 덮어씁니다(입력할 때 + 구매 응답으로 그 줄 수량이 줄었을 때). 빈칸·0은 입력 중에는 두고 [구매]를 끄며 칸을 벗어나면 1로 채웁니다.
- **금액 표시**: 세 자리마다 쉼표(`JMUIStyle::GoldText` — 줄·합계·내 골드·수령함·등록 안내·가격 범위·판매 알림). 입력칸은 숫자만.
- **서버 변경(PIE에서 확인)**: 내 목록 요청의 간격을 `Player::auctionMy`로 따로 뗐습니다 — 등록·취소·수령 성공 뒤 자동으로 나가는 내 목록 요청이 같은 간격을 쓰면 사용자가 누른 다음 등록·구매가 막힙니다(연속 등록 5건 중 2건이 버려짐).
- **시험 도구**: `world_step` `ui_pie`에 `type`(입력칸 글자 + 바뀜 이벤트)·`owner`(창 클래스 이름 앞부분 — 같은 이름의 위젯이 다른 창에도 있을 때, 2인 PIE)를 넣었고, 꺼진 버튼은 누르지 않습니다. 개발 명령 `AuctionSearch [검색어] [정렬] [내림]`·`AuctionSort`·`AuctionPage`·`AuctionBuy <id> [수량]`·`AuctionRegister <itemId> <수량> <1개당>`.

## 상점 NPC (L4, D-201·D-202)
마을의 상점 NPC에게서 골드로 아이템을 사고, 가진 아이템을 골드로 팝니다. 다른 아이템 작업과 같은 `itemOp` 경로(검증 → DB 한 트랜잭션 → 성공 뒤 반영)를 탑니다.

### 데이터
- **`ShopTable.csv`**(서버 전용): 한 행 = 상점 하나가 파는 품목 하나. `Name`(행 ID 1001~), `ShopId`(1 이상), `ItemId`(ItemTable ID), `BuyPrice`(1개 값, 1 ~ `GoldMax`). (`ShopId`, `ItemId`) 중복·없는 `ItemId`는 시작 중단. **`BuyPrice`는 그 아이템의 `SellPrice` 이상**이어야 합니다(사서 되팔아 골드를 불리는 경로 차단 — 어기면 시작 중단). 품목을 늘릴 때는 행만 추가합니다. 초안(`ShopId` 1): HP 물약 30, 천 장비 8종(1101·2101·3101·4101·5101·6101·6102·7101) 각 100.
- **`NpcTable.csv`**(서버 전용): 한 행 = NPC 종류 하나. `Name`(= 마커의 `npcTemplateId` = `ObjectInfo.template_id`, 1001~), `DisplayName`, `ShopId`(0 = 상점 없음, 아니면 ShopTable에 있는 값), `Auction`(1 = 경매 관리인, A4.3·D-234 — 상점과 겸하지 않음. 클라이언트에는 `ObjectInfo.npc_kind`), `Body`(`Male`/`Female`), 장착 외형 7열 `Head`·`Top`·`Bottom`·`Gloves`·`Boots`·`Weapon`·`Offhand`(ItemTable ID 또는 0, 슬롯이 맞아야 함). 외형은 매니퀸 + 기존 장비입니다(D-170).
- `ItemTable.SellPrice`, `EconomyTable.ShopRangeCm`은 위 "데이터" 절.
- 열별 허용 범위와 현재 행은 [DESIGN_DATA.md](DESIGN_DATA.md).

### NPC 객체
- 서버 객체입니다(`OBJECT_TYPE_NPC`). 레벨의 `ALevelMarker`(타입 `Npc`, `npcTemplateId`) → `MapExport` Logic JSON → `GameMap::GetNpcSpawns()` → `Room::InitRoom`이 `NpcTable` 행과 함께 배치합니다. 마커의 종류가 `NpcTable`에 없으면 그 마커는 배치하지 않고 ERROR를 남깁니다(몬스터 스폰과 같은 규칙).
- 드랍처럼 `Room`의 전용 목록(`_npcs`)에 두고 `_objects`(캐릭터 전용)·틱 순회에는 넣지 않습니다. id는 플레이어·몬스터·드랍과 겹치지 않는 전용 대역입니다.
- **시야**: 움직이지 않고 수가 적으므로 색인 없이 거리로 판정합니다(들어옴 ≤ R, 나감 > R+H — 드랍과 같은 반경). 맞추는 곳은 `UpdateDropView`와 같은 세 곳(입장·`PlayerMove`·`PlayerRevive`)이고 `SC_ADD_OBJECT`/`SC_REMOVE_OBJECT`를 본인에게 보냅니다. `ObjectInfo`에는 `template_id`·`name`·`player_type`(체형)·`equip_items`(외형)를 싣습니다.
- NPC는 대상이 되지 않습니다(공격·거래 요청 불가 — `_objects`에 없으므로 기존 검사에서 "대상 없음").

### 요청
- 공통 검증 순서: 방 안 → 더미 봇 아님(`NOT_ALLOWED`) → 생존(`DEAD`) → 요청 간격·진행 중인 작업(`TOO_FREQUENT`·`BUSY`) → **1:1 거래 중이 아님**(`tradeId` ≠ 0이면 `IN_TRADE` — 올린 아이템만이 아니라 상점 전체, D-201) → NPC 존재·상점 있음(`NO_ITEM`) → 수평 거리 ≤ `ShopRangeCm`(`TOO_FAR`).
- **열기** `CS_SHOP_OPEN(npc_id)` → `SC_SHOP_OPEN(npc_id, items[item_id, buy_price])`. DB를 쓰지 않습니다. 구매가는 이 패킷으로만 알립니다(ShopTable은 서버 전용). 열기는 편의 기능이고 **구매·판매 요청마다 위 검증을 다시 합니다**(서버에 "상점을 연 상태"를 두지 않음).
- **구매** `CS_SHOP_BUY(npc_id, item_id, count)`: 그 상점의 품목(`NO_ITEM`) → 수량 1 이상이고 `MaxStack × InventorySlots` 이하(`INVENTORY_FULL`) → 총액(`BuyPrice` × 수량) ≤ 가진 골드(`NOT_ENOUGH_GOLD`) → `Inventory::PlanAdd`(덜 찬 스택 먼저, 자리가 없으면 통째로 `INVENTORY_FULL`) → `ItemOpKind::ShopBuy`. 성공하면 `SC_INVENTORY_UPDATE`(바뀐 행 + 골드).
- **판매** `CS_SHOP_SELL(npc_id, item_db_id, count)`: 내 인벤토리 위치의 행(`NO_ITEM` — 장착 중인 것은 팔 수 없음) → 수량 1 ~ 그 행의 `Count`(`NO_ITEM`) → `SellPrice` > 0(`NOT_SELLABLE`) → 골드 상한(`GOLD_FULL`) → `ItemOpKind::ShopSell`. 스택은 일부만 팔 수 있습니다. 성공하면 `SC_INVENTORY_UPDATE`(바뀐 행 또는 지워진 `ItemDbId` + 골드). 판 아이템은 사라집니다(되사기 없음).
- 거부는 `SC_ITEM_RESULT(요청 패킷 id, ItemResult)`, DB 롤백은 `DB_ERROR`, 결과 불명은 공통 경로가 접속을 끊습니다.
- 쿼리는 고정 쿼리(D-165)입니다. 조건(골드 잔액·기대 수량·칸)이 맞지 않으면 롤백합니다.

### 클라이언트
- NPC 액터: `UJMObjectManager::HandleSpawn`의 `OBJECT_TYPE_NPC` 분기가 플레이어 액터 클래스(`BP_Player`)로 만들어 `UEquipmentComponent::SetLoadout`으로 외형을 입히고 `Npcs`에 따로 둡니다(새 클래스를 만들지 않음 — 구현 메모). 기존 `ANpcCharacter`/`BP_Npc`는 이름과 달리 몬스터입니다.
- 상호작용은 줍기와 같은 **F 키**입니다: `ShopRangeCm`(`SC_INVENTORY.shop_range_cm`) 안의 가장 가까운 상점 NPC와 줍기 거리 안의 가장 가까운 드랍 중 가까운 쪽에 안내("[F] 상점: 이름")를 띄우고 `CS_SHOP_OPEN`을 보냅니다.
- `UJMShopManager`(GameInstanceSubsystem) + `UShopWidget`/`WBP_Shop`: `SC_SHOP_OPEN`을 받으면 상점 창과 인벤토리 창을 함께 엽니다. 구매 = 품목 선택 → 수량 → [구매]. 판매 = 상점이 열린 동안 인벤토리 아이템 우클릭으로 **선택만** 하고 상점 창의 [판매]로 확정합니다(되사기가 없어 잘못 누르는 것을 막음). NPC에서 멀어지거나 거래가 시작되거나 죽으면 창을 닫습니다.

### L4 구현 메모 (2026-11-19)
- **데이터**: `ShopTable.csv` 9행(`ShopId` 1), `NpcTable.csv` 1행(1001 "잡화 상인"), `ItemTable.SellPrice`, `EconomyTable.ShopRangeCm` 400. 로더(`DataManager::Init`, Spawn 뒤)는 `GetShopItems`·`FindShopItem`·`GetNpcData`를 제공하고, `BuyPrice` < `SellPrice`를 포함한 오류에서 시작을 멈춥니다.
- **NPC**: `TownNpc`(`Game/Object/TownNpc.h`, `StaticObject`) — `Room::SpawnTownNpcs`(InitRoom)가 마커 위치를 내비메시 위로 맞춰 `Room::_npcs`에 둡니다. id는 `NPC_ID_BEGIN`(900,000,000)부터 마커 순서. 시야는 `Room::UpdateNpcView`(`Player::npcView`, 거리 판정)이고 **`UpdateDropView`를 부르는 세 곳에서 함께 부릅니다.** 이름이 비슷한 `Room::NpcEnterRoom`·`NPCMove`는 몬스터용입니다.
- **요청**: `Room::CheckShopRequest`(공통 검증) → `PlayerShopOpen`·`PlayerShopBuy`·`PlayerShopSell`. 열기는 진행 중인 작업(`BUSY`)을 보지 않고 요청 간격만 봅니다.
- **쿼리**: 구매(`ItemOpKind::ShopBuy`)는 줍기와 같은 계획·쿼리(`ItemPickup`, 스택 둘 이상·새 행 둘 이상이면 `BuildItemGive`)에 `goldDelta` = -총액을 넣습니다 — `Gold + @Gold BETWEEN 0 AND @GoldMax` 조건이 잔액 부족을 막습니다. 판매(`ItemOpKind::ShopSell`)는 새 고정 쿼리 `ItemShopSell`(전량이면 DELETE, 일부면 수량 감소 + 골드 증액, 잔액 반환).
- **클라이언트**: NPC는 새 액터 클래스 없이 플레이어 액터 클래스(`BP_Player`)로 만들어 서버가 준 체형·장비 외형을 입히고 `UJMObjectManager::Npcs`에 따로 둡니다(`Objects`의 거래·전투 대상 탐색에서 빠짐). 서버 높이는 지면이라 캡슐 반높이만큼 올려 놓습니다. `UJMShopManager`(상태·열고 닫기) + `UShopWidget`/`WBP_Shop`(`BP_GameMode.ShopWidgetClass`, ZOrder 126, 거래 창과 같은 자리). 품목 칸은 `UItemSlotWidget`을 쓰고 칸의 숫자 자리에 1개 값을 보입니다. 안내는 이용 거리보다 30cm 안쪽에서 띄우고, 창은 이용 거리를 벗어나거나 죽거나 거래가 시작되면 닫습니다(`AJMMyPlayer::UpdateInteractPrompt` → `UJMShopManager::UpdateFromPlayer`, 0.1초마다). 열린 상태에서 F를 다시 누르면 닫습니다. 상점 창이 닫히면(닫기 버튼·F·멀어짐·사망·거래 시작) 함께 연 인벤토리 창도 닫습니다(사용자 PIE 의견, 2026-11-19). 같은 기준으로 1:1 거래도 제시 단계까지 갔다가 끝나면(성사·취소·거리 이탈·시간 초과) 인벤토리 창을 함께 닫습니다(`UJMTradeManager::HandleEnd`).
- **거부 문구**: 상점 요청의 `IN_TRADE`는 "거래 중에는 상점을 이용할 수 없습니다.", `NO_ITEM`은 "상점에서 처리할 수 없는 요청입니다."로 따로 띄웁니다(같은 코드가 다른 요청에서는 다른 뜻).
- **개발용 콘솔 명령**(`AJMPlayerController`): `ShopOpen`(시야 안 가장 가까운 NPC), `ShopBuy <itemId> <count>`, `ShopSell <itemId> <count>`, `ShopClose` — 창이 보내는 것과 같은 요청.
- **봇**: `--shop-interval`(구매·판매 번갈아). 봇은 자기 위치를 받은 뒤의 `SC_ADD_OBJECT`를 건너뛰는데, 이 옵션이 켜지면 NPC id를 알아야 해서 계속 읽습니다.
- **시험(TESTING 2026-11-19 L4)**: 사망 중 거부(`DEAD`)는 원시 소켓으로 확인하지 못했습니다.

## 클라이언트 UI (A6.1·A6.2·MC4.4·A6.3a·A6.3b)
- 창은 모두 `UJMUIManager::RegisterWidget`(`bIsWindow`, ZOrder는 크로스헤어 100보다 위)로 등록하고 키로 `ToggleWidget`합니다. 창이 열리면 커서가 보입니다(기존 동작). 키는 초기 제안이며 구현 때 조정합니다.
- **인벤토리 + 장비창**(I 키, `WBP_Inventory` 한 창 — A6.1, D-166): 장착 7칸, 칸 격자(서버 칸 수만큼, 빈 칸 표시, 아이콘·수량 — 칸 위젯 `UItemSlotWidget`/`WBP_ItemSlot`), 골드, 선택한 아이템 이름. 인벤토리의 장비 우클릭 = 장착, 장착 칸 우클릭 = 해제, 끌어다 놓기 = 칸 옮기기·맞바꾸기(인벤토리 ↔ 인벤토리)·장착(인벤토리 → 장착 칸)·해제 또는 교체(장착 칸 → 인벤토리). 창은 클라이언트 사본(`UJMInventoryManager`)을 그리며 여닫을 때 서버에 묻지 않습니다. 선택한 아이템의 버리기(A6.2)·사용·액티브 칸에 올리기(A3.6)는 그때 붙입니다.
- **거래 창**: 양쪽 제시(아이템·수량·골드), 잠금·확정·취소 버튼, 상대의 잠금·확정 표시. 인벤토리에서 아이템을 골라 수량을 적어 올립니다.
- **A6.3a 구현 메모 (2026-11-17)**
  - 상태: `UJMTradeManager`(GameInstanceSubsystem)가 `SC_TRADE_STATE`를 그대로 담습니다(`EJMTradeState` = 받은 요청·응답 대기·제시·확정 중, 내 제시·상대 제시·상대 이름). 조작은 요청만 보내고 화면은 서버 상태가 와야 바뀝니다. 레벨 전환 때 비웁니다.
  - 요청: T 키(`IA_Trade`) → `AJMMyPlayer::OnTradeInput`이 카메라 정면 약 25도 안에서 크로스헤어에 가장 가까운 다른 플레이어를 골라 `CS_TRADE_REQUEST`. 거리·사망·거래 중은 서버가 판정합니다. 대상이 없으면 클라이언트가 안내만 띄웁니다.
  - 창(`WBP_Trade`, ZOrder 125, 화면 왼쪽): 요청 화면(받은 요청 = 수락·거절, 보낸 요청 = 응답 대기·취소)과 거래 화면(양쪽 올린 칸·골드·상태 글자 "제시 중/잠금/확정", 잠금·확정·취소)을 한 위젯에 두고 상태에 따라 하나만 보입니다. 제시 단계에 들어가면 인벤토리 창도 함께 엽니다.
  - 올리기: 제시 단계에서 인벤토리 아이템 우클릭(이때는 장착·사용 대신 올리기) 또는 거래 창으로 끌어다 놓기 = 그 행 전부. 올린 칸을 골라 `EditCount`에 수량을 적고 Enter = 수량 변경(1 ~ 행 수량). 올린 칸 우클릭 = 내리기. 골드는 `EditGold`에 적고 Enter(가진 골드 안으로 맞춤).
  - `CS_TRADE_SET_OFFER`는 전체 교체라, 응답 전에 연달아 고칠 때는 마지막으로 보낸 제시를 바탕으로 다음 요청을 만듭니다(`Pending`). 거부(`SC_TRADE_RESULT`)가 오면 버리고 서버 값으로 돌아갑니다.
  - 안내: `SC_TRADE_RESULT`·`SC_TRADE_END` 사유를 시스템 메시지 창으로. 내가 누른 취소·거절로 끝난 것은 띄우지 않습니다.
  - 창 위젯은 `SelfHitTestInvisible`로 보입니다(`UJMUIManager`) — 화면 전체를 덮는 위젯 자체가 클릭을 받으면 위 창(거래)이 아래 창(인벤토리)을 막기 때문입니다. 창 안의 패널·버튼은 그대로 클릭됩니다.
- **상점 창**(L4, NPC 근처에서 F 키): 위 "상점 NPC (L4)"의 "클라이언트".
- **경매 창**(A6.3b, D-234): K 키 = 조회 모드(검색·구매·내 등록/수령함 보기), 경매 NPC 앞 F 키 = 등록·취소·수령까지. 검색(슬롯 필터, 쪽 넘기기)·구매, 등록(인벤토리에서 골라 수량·가격 입력), 내 등록(취소), 수령함(수령) 탭.
- **A6.3b 구현 메모 (2026-11-21, D-234)**
  - 상태: `UJMAuctionManager`(GameInstanceSubsystem)가 서버가 보낸 검색 결과(`SC_AUCTION_LIST`)·내 등록·수령함(`SC_AUCTION_MY`)을 그대로 담습니다. 조작은 요청만 보내고 화면은 응답이 와야 바뀝니다. 레벨 전환·로그인 화면 복귀 때 비웁니다.
  - 여는 방법 둘: **K 키**(`IA_Auction` → `AJMMyPlayer::OnAuctionInput`) = 조회 모드(`Open(0)` — 검색·구매·내 등록/수령함 보기), **경매 NPC 앞 F 키**(안내 "[F] 경매장: 이름" — `ObjectInfo.npc_kind`로 상점과 구분) = NPC 모드(`Open(NpcId)` — 등록·취소·수령까지, 인벤토리 창도 함께 엶). 조회 모드에서는 등록·취소·수령 버튼이 꺼지고 안내 문구가 보입니다. K로 연 채 NPC 앞에서 F를 누르면 NPC 모드로 바뀝니다.
  - 창(`WBP_Auction`, 부모 `UAuctionWidget`, ZOrder 127, 화면 왼쪽 — 상점과 같은 자리): 탭 네 개(검색·등록·내 등록·수령함)를 한 위젯에 두고 고른 탭의 패널만 보입니다(패널 `Visible`/`Collapsed`). 검색 = 필터 버튼 9개(전체·머리·상의·하의·장갑·신발·무기·보조·기타) + 가격순 목록 + 이전·다음 쪽·새로 고침 + [구매]. 목록 줄과 필터 버튼은 C++가 만듭니다(`UAuctionRowButton` — `UButton`에 줄 번호를 붙인 것, 줄을 눌러 고르고 아래 버튼으로 확정). 아이콘 없이 글자 줄입니다(기능 우선).
  - 등록: NPC 모드에서 인벤토리 아이템 **우클릭 = 등록 후보로 고르기**(`UInventoryWidget::OnSlotRightClicked`의 분기 순서: 장착 칸 해제 → 상점 판매 후보 → 경매 등록 후보 → 거래 제시 → 장착·사용) → 등록 탭이 열리고 수량(기본 = 그 행 전부)·값(묶음 전체)을 적은 뒤 [등록]. 등록 수 상한·가격 범위는 `SC_AUCTION_MY`로 받은 값으로 버튼을 끕니다(검증은 서버).
  - 결과 안내: **성공은 확인 창을 띄우지 않고** 창 아래 한 줄(`TxtStatus`) + 바뀐 목록 다시 요청, **거부·실패는 시스템 메시지 창**(`AuctionResultText`, `BUSY`·`TOO_FREQUENT`는 띄우지 않음). `IN_DUNGEON`·`DEAD`면 창을 닫고, 구매가 `SOLD`·`NO_LISTING`이면 목록을 다시 받습니다.
  - 조회가 서버의 요청 간격(100ms)에 걸려 `TOO_FREQUENT`로 버려지면 0.25초 뒤 다시 보냅니다(`RetryQueries` — 버려진 채 두면 목록이 낡은 채 남음. 예: 탭을 바꾼 직후 등록이 성공해 내 목록을 연달아 요청).
  - 닫히는 때(`UJMAuctionManager::UpdateFromPlayer`, 0.1초마다): 사망, 1:1 거래의 제시 단계 시작, 상점 열림, (NPC 모드) NPC에서 멀어짐·Room 이동. NPC 모드로 닫히면 함께 연 인벤토리 창도 닫습니다. 경매 창을 열면 열려 있던 상점은 닫습니다.
  - 개발용 콘솔 명령(`AJMPlayerController`): `AuctionOpen [0|1]`·`AuctionSearch [slot] [page]`·`AuctionMy`·`AuctionRegister <itemId> <count> <price>`·`AuctionBuy [listingId]`·`AuctionCancel [listingId]`·`MailClaim [mailId]`(0 = 목록의 첫 것)·`AuctionTab <0~3>`·`AuctionShow`(목록을 로그로)·`AuctionClose` — `pie_exec`로 보낼 수 있습니다.
  - **구현하면서 정한 것(바꾸려면 알려 주세요)**: ① 목록은 줄을 고르고 아래 버튼으로 확정(줄마다 버튼을 두지 않음 — 잘못 누르는 것을 막음). ② 다른 사람이 산 뒤에도 내 검색 목록은 [새로 고침]이나 탭 전환 전까지 그대로입니다(서버가 밀어 주지 않음 — 사려고 하면 `SOLD`가 오고 그때 목록을 다시 받음). ③ 수령함에 받을 것이 있다는 표시는 창을 열어야 보입니다(판매 알림과 함께 콘텐츠 다듬기에서).
- 거부·실패·성사 안내는 모두 시스템 메시지 창(D-050)입니다(경매의 성공 안내만 창 안 한 줄 — A6.3b).

## 패킷 (번호 배정)
새 번호는 `Enum.proto`의 마지막 번호 다음부터 이어 씁니다(2026-11-22 기준 CS 1040~, SC 2046~ — 퀘스트가 CS 1038(요청 하나, D-264 — 1039는 사용 안 함)·SC 2042~2045 — 파티가 CS 1035~1037·SC 2033~2035(DG2 구현), Room 이동이 SC 2036·2037(DG3b 구현, 거래 취소 사유 `TRADE_RESULT_MOVED` 17 추가), 2038은 던전 상태(DG3c 구현), `DESIGN_PARTY_DUNGEON.md`, 2039~2041은 성장·MP(A2.3b)). 추가 절차는 [Common/Protocol/CLAUDE.md](../Common/Protocol/CLAUDE.md).

| 항목 | CS | SC |
|---|---|---|
| A3.3 | 1017 `DEV_GIVE` | 2016 `INVENTORY`(본인), 2017 `INVENTORY_UPDATE`(본인), 2018 `ITEM_RESULT`(본인, 거부일 때만: 요청 패킷 id + `ItemResult`) |
| A3.5 | 1013 `PICKUP_ITEM`, 1014 `DROP_ITEM` | (드랍 표시는 기존 `ADD_OBJECT`·`REMOVE_OBJECT`) |
| A3.6 | 1015 `USE_ITEM`, 1016 `SET_QUICKSLOT` | 2019 `USE_ITEM`(AOI). 배치 응답은 `INVENTORY_UPDATE.quick_slots` |
| A4.2 | 1018 `TRADE_REQUEST`, 1019 `TRADE_RESPOND`, 1020 `TRADE_SET_OFFER`, 1021 `TRADE_LOCK`, 1022 `TRADE_CONFIRM`, 1023 `TRADE_CANCEL` | 2020 `TRADE_REQUEST`(상대), 2021 `TRADE_STATE`(두 사람), 2022 `TRADE_END`(두 사람), 2023 `TRADE_RESULT`(본인, 거부) |
| A4.3 | 1024 `AUCTION_SEARCH`, 1025 `AUCTION_REGISTER`, 1026 `AUCTION_BUY`, 1027 `AUCTION_CANCEL`, 1028 `AUCTION_MY`, 1029 `MAIL_CLAIM` (등록·취소·수령에는 `npc_id`) | 2024 `AUCTION_LIST`, 2025 `AUCTION_MY`, 2026 `AUCTION_RESULT`(본인, 성공·거부 모두: 요청 패킷 id + `AuctionResult` + `id`). `ObjectInfo.npc_kind`(13, `NpcKind` — 상점·경매) 추가 |
| A2.7 | — | 2027 `KICK`(끊기 직전 본인, 사유) |
| A6.1 | 1030 `MOVE_ITEM`(칸 옮기기·맞바꾸기) | (성공은 `INVENTORY_UPDATE`, 거부는 `ITEM_RESULT`) |
| L4 | 1031 `SHOP_OPEN`, 1032 `SHOP_BUY`, 1033 `SHOP_SELL` | 2029 `SHOP_OPEN`(본인, 품목·구매가). 구매·판매 성공은 `INVENTORY_UPDATE`, 거부는 `ITEM_RESULT`. `INVENTORY`에 `shop_range_cm` 추가. NPC 표시는 기존 `ADD_OBJECT`·`REMOVE_OBJECT` |
| A2.3b 성장·MP (D-254) | 1017 `DEV_GIVE`에 `exp`(3번, int64 — `[Debug] DevCommands`일 때만, 0보다 크면 경험치만) | 2039 `GAIN_EXP`(본인: 지금 레벨 안의 누적 `exp` + `gained`), 2040 `LEVEL_UP`(AOI 본인 포함: `object_id`·`level`·`StatInfo` — HP·MP 가득), 2041 `CHANGE_MP`(본인: `mp`). HP 자연 회복은 기존 2028 `CHANGE_HP`(AOI). `StatInfo`에 `max_mp`(6)·`mp`(7), `ObjectInfo.exp`(14, 본인에게 보내는 것에만), `AttackResult` `NOT_ENOUGH_MP`(11) |

- 구조체 `ItemInfo`(`item_db_id`, `item_id`, `count`, `equip_part` — 0 = 인벤토리, `slot` — 인벤토리 칸 번호·장착 중이면 -1), `ObjectInfo.item_count`(12번), `CS_CHANGE_EQUIP_PACKET`은 `item_id`(2번)를 `reserved`로 막고 `item_db_id`(3번)를 추가합니다. `item_db_id`·`listing_id`·`mail_id`·골드·가격·수량 필드는 int64입니다.
- 결과 enum은 기능별로 둡니다(D-114 방식). 새 enum은 기존 관례대로 `*_NONE` = 0, `*_SUCCESS` = 1로 시작하고 아래 값이 2부터 이어집니다.
  - `ItemResult`: `NOT_IN_ROOM`, `DEAD`, `BUSY`, `TOO_FREQUENT`, `NOT_ALLOWED`, `NO_ITEM`, `TOO_FAR`, `NOT_OWNER`, `INVENTORY_FULL`, `GOLD_FULL`, `IN_TRADE`, `NOT_USABLE`, `HP_FULL`, `COOLDOWN`, `LEVEL_TOO_LOW`, `DB_ERROR`, `BAD_SLOT`(18, A6.1), `NOT_ENOUGH_GOLD`(19, L4), `NOT_SELLABLE`(20, L4).
  - `EquipResult` 추가: `NOT_OWNED`(9), `LEVEL_TOO_LOW`(10), `INVENTORY_FULL`(11), `BUSY`(12), `IN_TRADE`(13), `DB_ERROR`(14), `NOT_ALLOWED`(15, 더미 봇 — D-161).
  - `TradeResult`: `NO_TARGET`, `TOO_FAR`, `TARGET_BUSY`, `DECLINED`, `TIMEOUT`, `CANCELLED`, `DEAD`, `DISCONNECTED`, `LOCKED`, `INVALID_OFFER`, `INVENTORY_FULL`, `GOLD_FULL`, `BUSY`, `NOT_ALLOWED`, `DB_ERROR`.
  - `AuctionResult`(구현 순서, 2부터): `NOT_IN_ROOM`, `DEAD`, `BUSY`, `TOO_FREQUENT`, `NOT_ALLOWED`, `NO_LISTING`, `SOLD`, `OWN_ITEM`, `NOT_ENOUGH_GOLD`, `LISTING_LIMIT`, `BAD_PRICE`, `NO_ITEM`, `IN_TRADE`, `INVENTORY_FULL`, `GOLD_FULL`, `DB_ERROR`, `TOO_FAR`(18, 경매 NPC 거리 — D-234), `IN_DUNGEON`(19), `TRY_LATER`(20, 혼잡 — D-235), `NO_MAIL`(21). 그 밖에 `MailKind`(판매 대금·구매품·만료 반환·취소 반환).
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
- **L4**: 구매·판매 뒤 골드·아이템이 DB와 일치(반복 뒤 합계 = 계산값 = 재로그인 값), 골드 부족·칸 부족·거리 밖·거래 중·사망·없는 NPC·없는 품목·팔 수 없는 아이템·수량 범위 밖·골드 상한 거부, DB 롤백·지연·결과 도착 전 접속 해제, NPC 시야 진입·이탈 때 ADD·REMOVE, ShopTable·NpcTable 오류(없는 참조, `BuyPrice` < `SellPrice`)에서 시작 중단.
- 결과 도착 전 접속 해제 시험은 A3.3·A4.2·A4.3 각각에서 합니다(A4.4는 병목이 없으면 하지 않을 수 있는 항목이라 여기에만 두지 않음).
- **A4.4**: DB 응답을 늦춘 상태에서 줍기·거래·경매 부하 중 틱 지연 p99 ≤ 20ms, 결과 도착 전 접속 해제 시 서버 오류 없이 DB와 다음 로그인 값 일치.
- **봇(A5.2)**: 개별 계정 봇 + `DevCommands`. 줍기 = 받은 ITEM 오브젝트로 이동 후 요청, 거래 = 이웃 번호끼리 짝(짝수가 요청, 홀수가 자동 수락·잠금·확정), 경매 = 등록·검색·구매·수령 순환. 통계에 요청·성공·거부 수를 추가합니다. 개별 계정 봇이 PlayerStart에서 몬스터에 닿지 못하는 문제(ROADMAP A5.1 메모)를 먼저 풀어야 줍기 프로필이 동작합니다. 봇은 `SC_KICK`·`LOGIN_RESULT_BUSY`를 받으면 실패로 세고 다시 접속하지 않습니다.

## 알아둘 간극
- 기본 장비를 거래할 수 있어 새 계정을 만들어 아이템을 늘릴 수 있습니다(사용자 결정, 경제 균형은 범위 밖).
- 거래·경매에는 수수료가 없습니다. 골드가 회수되는 곳은 상점 구매뿐입니다(L4 — 판매가가 구매가보다 낮아 사고팔면 줄어듦).
- 줍기·장착·물약이 모두 DB 왕복을 기다립니다. 부하 단계(A5.4)에서 병목이면 골드 줍기 묶음 처리 등을 그때 검토합니다.
- 버리기로 거래 절차 없이 아이템을 넘길 수 있습니다(소유권 없는 드랍). 도배는 요청 간격과 `MaxDropsPerRoom`으로만 막습니다.
- 인벤토리에서 스택을 합칠 수 없어, 거래·수령으로 받은 물약은 덜 찬 스택이 여러 칸으로 남을 수 있습니다(사용·액티브 칸 수량은 종류 합계라 쓰는 데는 지장 없음).
- 팔렸다는 알림 패킷이 없습니다. 판매자는 경매 창을 다시 열어야 수령함에서 대금을 봅니다 — 콘텐츠 다듬기에서 시스템 메시지 + UI 표시로 넣을 예정입니다(D-234, 붙일 자리는 `AuctionService::OnOpDone`의 구매 성공 분기).
- 수령함에는 상한이 없습니다(받지 않으면 계속 쌓임). 경매 검색은 정렬 색인(전체용·아이템별)에서 꺼내므로 요청마다 정렬하지 않습니다(AU2, D-281). 만료 검사는 아직 목록 전체를 훑으므로 등록이 아주 많아지면 A5.4에서 만료 시각순 색인을 검토합니다.
- 로그인 보정이나 거래로 인벤토리가 칸 상한을 넘은 상태가 생길 수 있고, 그동안은 새로 들어오는 것만 막습니다.
- 레벨업이 A2.3b라 `RequiredLevel` 거부는 CSV 값을 올린 원시 소켓 시험으로만 확인합니다.
