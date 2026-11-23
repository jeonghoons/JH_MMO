#pragma once
#include <string>
#include <vector>
#include <format>
#include "Data/ServerData.h"

// GameServer가 실행하는 SQL을 모두 여기에 둔다(A2.5, DECISIONS 2026-11-16). DatabaseWorker는 바인딩·실행·결과 전달만 한다.
// 규칙:
//   - SQL 문자열은 이 파일에만 쓴다. 값은 ? 파라미터로 넘기고, 쿼리에 직접 넣는 것은 서버가 검증한 정수뿐이다.
//   - 테이블·열을 바꾸면 Resource/DB/migrations/NNNN_<날짜>_<내용>.sql 추가 + Resource/DB/schema.sql 갱신 +
//     아래 DB_SCHEMA_VERSION 증가를 한 커밋에서 함께 한다. (실행: Resource/DB/migrate.ps1)
namespace DbQueries
{
	// 이 서버 코드가 기대하는 스키마 버전 = 마지막 마이그레이션 순번. DB의 Schema_Version과 다르면 서버가 시작하지 않는다.
	constexpr int DB_SCHEMA_VERSION = 10;

	// 결과: 열 1 = 적용된 마지막 마이그레이션 순번(행이 없으면 0). 테이블이 없으면 실행 실패.
	inline const wchar_t* const SelectSchemaVersion = L"SELECT ISNULL(MAX([Version]), 0) FROM [Schema_Version]";

	// 로그인. 파라미터: 1 LoginId
	// 결과(인벤토리·장착 아이템 수만큼 행, 없으면 1행, ItemDbId 순): LoginPassword, PlayerId, AccountId, Name, PlayerType, Level, Exp, Hp, Gold,
	//   ItemDbId, ItemId, Count, Location, EquipSlot (아이템이 없으면 모두 0), BagSlot(없으면 -1),
	//   NeverSaved(1 = 진행 상황을 저장한 적이 없는 캐릭터 = UpdatedAt NULL),
	//   Q1~Q5(액티브 칸 1~5에 올린 ItemId, 빈 칸 0 — 모든 행에 같은 값, A3.6),
	//   HasMail(1 = 수령함에 받을 것이 있다 — 모든 행에 같은 값, D-262)
	inline const wchar_t* const SelectLogin = L" \
        SELECT A.LoginPassword, P.PlayerId, P.AccountId, P.Name, P.PlayerType, \
               P.[Level], P.Exp, P.Hp, P.Gold, \
               ISNULL(I.ItemDbId, 0) AS ItemDbId, ISNULL(I.ItemId, 0) AS ItemId, ISNULL(I.[Count], 0) AS ItemCount, \
               ISNULL(I.Location, 0) AS Location, ISNULL(I.EquipSlot, 0) AS EquipSlot, ISNULL(I.BagSlot, -1) AS BagSlot, \
               CASE WHEN P.UpdatedAt IS NULL THEN 1 ELSE 0 END AS NeverSaved, \
               ISNULL((SELECT Q.ItemId FROM [Player_QuickSlot] Q WHERE Q.PlayerId = P.PlayerId AND Q.SlotIndex = 1), 0) AS Q1, \
               ISNULL((SELECT Q.ItemId FROM [Player_QuickSlot] Q WHERE Q.PlayerId = P.PlayerId AND Q.SlotIndex = 2), 0) AS Q2, \
               ISNULL((SELECT Q.ItemId FROM [Player_QuickSlot] Q WHERE Q.PlayerId = P.PlayerId AND Q.SlotIndex = 3), 0) AS Q3, \
               ISNULL((SELECT Q.ItemId FROM [Player_QuickSlot] Q WHERE Q.PlayerId = P.PlayerId AND Q.SlotIndex = 4), 0) AS Q4, \
               ISNULL((SELECT Q.ItemId FROM [Player_QuickSlot] Q WHERE Q.PlayerId = P.PlayerId AND Q.SlotIndex = 5), 0) AS Q5, \
               CASE WHEN EXISTS (SELECT 1 FROM [Player_Mail] M WHERE M.PlayerId = P.PlayerId) THEN 1 ELSE 0 END AS HasMail \
        FROM [User_Account] A \
        INNER JOIN [Player_Info] P ON A.AccountId = P.AccountId \
        LEFT JOIN [Player_Item] I ON I.OwnerId = P.PlayerId AND I.Location IN (1, 2) \
        WHERE A.LoginId = ? \
        ORDER BY I.ItemDbId";

	// 로그인의 두 번째 조회(같은 연결, SelectLogin이 성공한 뒤 — Q1): 받은 퀘스트 전부. 파라미터: 1 PlayerId
	// 결과(QuestId 순): QuestId, State(1 진행 중 · 2 완료), Progress
	inline const wchar_t* const SelectPlayerQuests =
		L"SELECT QuestId, CAST(State AS INT), Progress FROM [Player_Quest] WHERE PlayerId = ? ORDER BY QuestId";

	// 가입: 계정 + 계정 정보 + 캐릭터 + 기본 장비를 한 트랜잭션으로. 파라미터: 1 LoginId, 2 LoginPassword, 3 Name, 4 PlayerType, 5 Level, 6 Hp
	// starterItems = (EquipSlot, ItemId). 로더가 검증한 정수(ItemTable.Starter)라 쿼리에 그대로 넣는다. 기본 장비는 장착한 채(Location 2)로 준다.
	inline std::wstring BuildSignUp(const std::vector<std::pair<int, int>>& starterItems)
	{
		std::wstring starterInsert;
		for (const auto& [slot, itemId] : starterItems)
		{
			starterInsert += std::format(L"INSERT INTO [Player_Item] (OwnerId, ItemId, [Count], Location, EquipSlot) VALUES (@PlayerID, {}, 1, 2, {}); ",
				itemId, slot);
		}

		return std::wstring(L" \
        SET NOCOUNT ON; SET XACT_ABORT ON; \
        BEGIN TRAN; \
        DECLARE @AccID BIGINT, @PlayerID BIGINT; \
        INSERT INTO [User_Account] (LoginId, LoginPassword) VALUES (?, ?); \
        SET @AccID = SCOPE_IDENTITY(); \
        INSERT INTO [User_Info] (AccountId) VALUES (@AccID); \
        INSERT INTO [Player_Info] (AccountId, Name, PlayerType, [Level], Exp, Hp, Gold) \
        VALUES (@AccID, ?, ?, ?, 0, ?, 0); \
        SET @PlayerID = SCOPE_IDENTITY(); ") + starterInsert + L"COMMIT TRAN;";
	}

	// 진행 상황 저장(A2.5). 파라미터: 1 Level, 2 Exp, 3 Hp, 4 PlayerId. 골드는 여기서 쓰지 않는다(A3.3, D-152 — 아이템 트랜잭션에서 증감).
	// 필드가 늘면(A2.3b 등) DB_PlayerProgress(ServerData.h)와 이 쿼리, DatabaseWorker::SavePlayerProgress의 바인딩을 함께 고친다.
	inline const wchar_t* const UpdatePlayerProgress =
		L"UPDATE [Player_Info] SET [Level] = ?, Exp = ?, Hp = ?, UpdatedAt = SYSDATETIME() WHERE PlayerId = ?";

	// 퀘스트 처치 진행 저장(Q1, D-261). 파라미터: 1 Progress, 2 PlayerId, 3 QuestId, 4 Progress
	// 진행 중(State 1)이고 값이 커질 때만 바꾼다 — 완료(아이템 트랜잭션)와 순서가 뒤바뀌어도 완료를 덮거나 진행을 되돌리지 않는다.
	// 바뀐 행이 0이어도(이미 완료됨·이미 같은 값) 저장 성공으로 본다: 결과 집합 1행(바뀐 행 수)을 읽었으면 성공
	// (SET NOCOUNT ON이 없으면 0행 UPDATE가 SQL_NO_DATA로 돌아와 실패처럼 보인다).
	inline const wchar_t* const UpdateQuestProgress =
		L"SET NOCOUNT ON; UPDATE [Player_Quest] SET Progress = ? WHERE PlayerId = ? AND QuestId = ? AND State = 1 AND Progress < ?; SELECT @@ROWCOUNT;";

	// 액티브 칸 배치 저장(A3.6, D-154): 5칸을 한 문장으로 맞춘다(빈 칸은 행 삭제, 바뀐 칸은 UPDATE, 새 칸은 INSERT).
	// 한 문장이라 실행 성공 = 저장 성공이고 중간 상태가 없다. 파라미터: 1~5 칸 1~5의 ItemId(빈 칸 0), 6 PlayerId, 7 PlayerId
	inline const wchar_t* const SaveQuickSlots =
		L"MERGE [Player_QuickSlot] AS T "
		L"USING (SELECT V.SlotIndex, V.ItemId FROM (VALUES (1, CAST(? AS INT)), (2, CAST(? AS INT)), (3, CAST(? AS INT)), (4, CAST(? AS INT)), (5, CAST(? AS INT))) V(SlotIndex, ItemId)) AS S "
		L"ON T.PlayerId = ? AND T.SlotIndex = S.SlotIndex "
		L"WHEN MATCHED AND S.ItemId = 0 THEN DELETE "
		L"WHEN MATCHED AND T.ItemId <> S.ItemId THEN UPDATE SET ItemId = S.ItemId "
		L"WHEN NOT MATCHED BY TARGET AND S.ItemId <> 0 THEN INSERT (PlayerId, SlotIndex, ItemId) VALUES (?, S.SlotIndex, S.ItemId);";

	// ---------------------------------------------------------------------------------------------------------------
	// 아이템 트랜잭션(A3.3, D-153·D-165). 모든 쿼리가 같은 틀과 같은 결과 모양을 쓴다(DatabaseWorker::ExecItemOp가 읽는다).
	//   - 틀: TRY 안에서 BEGIN TRAN → 문장들(@n에 바뀐 행 수 누적) → @n이 기대와 같으면 COMMIT, 아니면 ROLLBACK. 오류는 CATCH에서 ROLLBACK.
	//   - 결과: 항상 결과 집합 하나. 열 = Ok(1 성공 / 0 실패), Kind(0 상태 행 · 1 새 ItemDbId · 2 골드 잔액 · 3 거래 상대의 골드 잔액 ·
	//     4 새 ListingId · 5·6·7 수령한 아이템의 ItemDbId·ItemId·Count · 8 만료 처리된 ListingId · 9 실패 사유(Ok 0, AuctionResult) — A4.3 · 10 구매 뒤 등록의 남은 수량 — D-281), V(값).
	//     성공이면 상태 행 1개 + 새 행·골드 행, 실패면 (0, 0, 오류 번호 또는 0) 1행. 행을 하나도 못 읽으면 실패로 본다
	//     (DBConnection::Execute는 배치 첫 문장의 성공만 알려 주므로 결과 행으로 판정한다).
	//   - 모든 UPDATE는 기대 상태(OwnerId·Location·EquipSlot·Count)를 WHERE에 적는다.
	//   - 골드 UPDATE는 UpdatedAt을 건드리지 않는다(UpdatedAt NULL = 진행 상황을 저장한 적 없음, 로그인 HP 판단에 쓴다).
	//   - @Fail = 1이면 끝에서 일부러 롤백한다(시험용 [Debug] DbItemFailEvery).
	//   - 잠금 대기는 ITEM_LOCK_TIMEOUT_MS까지만 한다(넘으면 오류 1222 → CATCH에서 롤백). 누가 행을 잡고 있어도
	//     DB 작업 스레드와 그 플레이어의 itemOp가 끝없이 묶이지 않게 한다. 연결에 남는 설정이라 같은 연결의 다른 쿼리에도 적용된다.
	inline const wchar_t* const ItemOpHead =
		L"SET LOCK_TIMEOUT 5000; DECLARE @n INT = 0; DECLARE @R TABLE (Seq INT IDENTITY(1,1), Kind INT, V BIGINT); "
		L"BEGIN TRY BEGIN TRAN; INSERT INTO @R (Kind, V) VALUES (0, 0); ";

	// want = 바뀌어야 하는 행 수의 합
	inline std::wstring ItemOpTail(const std::wstring& want)
	{
		return L"IF @n <> " + want + L" OR @Fail = 1 BEGIN ROLLBACK TRAN; SELECT 0 AS Ok, 0 AS Kind, CAST(0 AS BIGINT) AS V; END "
			L"ELSE BEGIN COMMIT TRAN; SELECT 1 AS Ok, Kind, V FROM @R ORDER BY Seq; END "
			L"END TRY BEGIN CATCH IF @@TRANCOUNT > 0 ROLLBACK TRAN; SELECT 0 AS Ok, 0 AS Kind, CAST(ERROR_NUMBER() AS BIGINT) AS V; END CATCH";
	}

	// 장착·해제(고정 쿼리 — 실행 계획 재사용).
	// 파라미터: 1 PlayerId, 2 Fail, 3 EquipPart, 4 NewDbId, 5 OldDbId, 6 OffhandDbId (없는 것은 0),
	//           7 NewSlot(올릴 행이 지금 있는 칸), 8 OldSlot(옛 장비가 내려갈 칸), 9 OffSlot(방패가 내려갈 칸)
	// 올리는 행과 내리는 행을 UPDATE 한 문장으로 바꾼다. 유니크 인덱스(장착 슬롯 UX_Player_Item_Equip, 인벤토리 칸 UX_Player_Item_BagSlot)는
	// 문장이 끝날 때 검사하므로, 옛 장비가 새 장비가 있던 칸으로 내려오는 맞바꿈도 중간 상태 없이 된다(두 문장으로 나누면 어느 순서든 한쪽 인덱스를 어긴다).
	inline const std::wstring ItemEquip =
		std::wstring(L"SET NOCOUNT ON; SET XACT_ABORT ON; "
		L"DECLARE @P BIGINT = ?, @Fail INT = ?, @Part INT = ?, @New BIGINT = ?, @Old BIGINT = ?, @Off BIGINT = ?, @NewSlot INT = ?, @OldSlot INT = ?, @OffSlot INT = ?; "
		L"DECLARE @want INT = IIF(@New <> 0, 1, 0) + IIF(@Old <> 0, 1, 0) + IIF(@Off <> 0, 1, 0); ")
		+ ItemOpHead +
		L"UPDATE [Player_Item] SET "
		L"Location = CASE WHEN ItemDbId = @New THEN 2 ELSE 1 END, "
		L"EquipSlot = CASE WHEN ItemDbId = @New THEN @Part ELSE 0 END, "
		L"BagSlot = CASE WHEN ItemDbId = @New THEN -1 WHEN ItemDbId = @Old THEN @OldSlot ELSE @OffSlot END, "
		L"UpdatedAt = SYSDATETIME() "
		L"WHERE ItemDbId IN (@New, @Old, @Off) AND OwnerId = @P AND ("
		L"(ItemDbId = @New AND Location = 1 AND BagSlot = @NewSlot) OR "
		L"(ItemDbId = @Old AND Location = 2 AND EquipSlot = @Part) OR "
		L"(ItemDbId = @Off AND Location = 2 AND EquipSlot = 7)); "
		L"SET @n += @@ROWCOUNT; "
		+ ItemOpTail(L"@want");

	// 인벤토리 칸 옮기기·맞바꾸기(고정 쿼리, A6.1). 파라미터: 1 PlayerId, 2 Fail, 3 DbId, 4 FromSlot, 5 ToSlot, 6 OtherDbId(대상 칸의 행, 빈 칸이면 0)
	// 맞바꿈도 UPDATE 한 문장이라 칸 유니크 인덱스를 중간에 어기지 않는다.
	inline const std::wstring ItemMove =
		std::wstring(L"SET NOCOUNT ON; SET XACT_ABORT ON; "
		L"DECLARE @P BIGINT = ?, @Fail INT = ?, @A BIGINT = ?, @From INT = ?, @To INT = ?, @B BIGINT = ?; "
		L"DECLARE @want INT = 1 + IIF(@B <> 0, 1, 0); ")
		+ ItemOpHead +
		L"UPDATE [Player_Item] SET BagSlot = CASE WHEN ItemDbId = @A THEN @To ELSE @From END, UpdatedAt = SYSDATETIME() "
		L"WHERE ItemDbId IN (@A, @B) AND OwnerId = @P AND Location = 1 AND ("
		L"(ItemDbId = @A AND BagSlot = @From) OR (ItemDbId = @B AND BagSlot = @To)); "
		L"SET @n += @@ROWCOUNT; "
		+ ItemOpTail(L"@want");

	// 줍기(고정 쿼리, A3.5 — 자주 오는 작업이라 실행 계획을 재사용한다, D-165). 덜 찬 스택 하나에 더하기 + 새 행 하나 + 골드 증액 중
	// 해당하는 것만 실행한다(없는 것은 0). 스택을 둘 이상 채우거나 새 행이 둘 이상인 드문 경우는 BuildItemGive를 쓴다.
	// 파라미터: 1 PlayerId, 2 Fail, 3 ItemId, 4 FillDbId, 5 FillExpect(그 스택의 지금 수량), 6 FillAdd, 7 NewCount, 8 NewSlot, 9 Gold, 10 GoldMax
	inline const std::wstring ItemPickup =
		std::wstring(L"SET NOCOUNT ON; SET XACT_ABORT ON; "
		L"DECLARE @P BIGINT = ?, @Fail INT = ?, @Item INT = ?, @Fill BIGINT = ?, @Expect INT = ?, @Add INT = ?, @NewCount INT = ?, @NewSlot INT = ?, @Gold BIGINT = ?, @GoldMax BIGINT = ?; "
		L"DECLARE @want INT = IIF(@Fill <> 0, 1, 0) + IIF(@Gold <> 0, 1, 0); ")
		+ ItemOpHead +
		L"IF @Fill <> 0 BEGIN "
		L"UPDATE [Player_Item] SET [Count] = [Count] + @Add, UpdatedAt = SYSDATETIME() "
		L"WHERE ItemDbId = @Fill AND OwnerId = @P AND Location = 1 AND ItemId = @Item AND [Count] = @Expect; SET @n += @@ROWCOUNT; END "
		L"IF @NewCount > 0 BEGIN "
		L"INSERT INTO [Player_Item] (OwnerId, ItemId, [Count], Location, EquipSlot, BagSlot) VALUES (@P, @Item, @NewCount, 1, 0, @NewSlot); "
		L"INSERT INTO @R (Kind, V) VALUES (1, SCOPE_IDENTITY()); END "
		L"IF @Gold <> 0 BEGIN "
		L"UPDATE [Player_Info] SET Gold = Gold + @Gold WHERE PlayerId = @P AND Gold + @Gold BETWEEN 0 AND @GoldMax; SET @n += @@ROWCOUNT; "
		L"INSERT INTO @R (Kind, V) SELECT 2, Gold FROM [Player_Info] WHERE PlayerId = @P; END "
		+ ItemOpTail(L"@want");

	// 버리기(고정 쿼리, A3.5). 파라미터: 1 PlayerId, 2 Fail, 3 DbId, 4 Slot, 5 Count (칸·수량은 기대 상태)
	inline const std::wstring ItemDiscard =
		std::wstring(L"SET NOCOUNT ON; SET XACT_ABORT ON; "
		L"DECLARE @P BIGINT = ?, @Fail INT = ?, @A BIGINT = ?, @Slot INT = ?, @Count INT = ?; ")
		+ ItemOpHead +
		L"DELETE FROM [Player_Item] WHERE ItemDbId = @A AND OwnerId = @P AND Location = 1 AND BagSlot = @Slot AND [Count] = @Count; "
		L"SET @n += @@ROWCOUNT; "
		+ ItemOpTail(L"1");

	// 소비 아이템 사용(고정 쿼리, A3.6): 수량 1 감소, 1개였으면 행 삭제. 파라미터: 1 PlayerId, 2 Fail, 3 DbId, 4 Count(지금 수량 = 기대 상태)
	inline const std::wstring ItemUse =
		std::wstring(L"SET NOCOUNT ON; SET XACT_ABORT ON; "
		L"DECLARE @P BIGINT = ?, @Fail INT = ?, @A BIGINT = ?, @Count INT = ?; ")
		+ ItemOpHead +
		L"IF @Count > 1 BEGIN "
		L"UPDATE [Player_Item] SET [Count] = [Count] - 1, UpdatedAt = SYSDATETIME() WHERE ItemDbId = @A AND OwnerId = @P AND Location = 1 AND [Count] = @Count; SET @n += @@ROWCOUNT; END "
		L"ELSE BEGIN "
		L"DELETE FROM [Player_Item] WHERE ItemDbId = @A AND OwnerId = @P AND Location = 1 AND [Count] = @Count; SET @n += @@ROWCOUNT; END "
		+ ItemOpTail(L"1");

	// 상점 판매(고정 쿼리, L4·D-202): 전량이면 행 삭제, 일부면 수량 감소 + 골드 증액(상한 안). 잔액은 Kind 2 행으로 돌려준다.
	// 파라미터: 1 PlayerId, 2 Fail, 3 DbId, 4 Expect(그 행의 지금 수량 = 기대 상태), 5 Sell(파는 수량), 6 Gold(받는 골드), 7 GoldMax
	// 상점 구매는 줍기와 같은 모양이라 ItemPickup을 그대로 쓴다(@Gold가 음수 — "Gold + @Gold BETWEEN 0 AND @GoldMax"가 잔액 부족을 막는다).
	inline const std::wstring ItemShopSell =
		std::wstring(L"SET NOCOUNT ON; SET XACT_ABORT ON; "
		L"DECLARE @P BIGINT = ?, @Fail INT = ?, @A BIGINT = ?, @Expect INT = ?, @Sell INT = ?, @Gold BIGINT = ?, @GoldMax BIGINT = ?; ")
		+ ItemOpHead +
		L"IF @Sell >= @Expect BEGIN "
		L"DELETE FROM [Player_Item] WHERE ItemDbId = @A AND OwnerId = @P AND Location = 1 AND [Count] = @Expect; SET @n += @@ROWCOUNT; END "
		L"ELSE BEGIN "
		L"UPDATE [Player_Item] SET [Count] = [Count] - @Sell, UpdatedAt = SYSDATETIME() WHERE ItemDbId = @A AND OwnerId = @P AND Location = 1 AND [Count] = @Expect AND @Sell > 0; SET @n += @@ROWCOUNT; END "
		L"UPDATE [Player_Info] SET Gold = Gold + @Gold WHERE PlayerId = @P AND @Gold > 0 AND Gold + @Gold BETWEEN 0 AND @GoldMax; SET @n += @@ROWCOUNT; "
		L"INSERT INTO @R (Kind, V) SELECT 2, Gold FROM [Player_Info] WHERE PlayerId = @P; "
		+ ItemOpTail(L"2");

	// 지급(조립식 — 채울 스택 수가 요청마다 다르다). 파라미터: 1 PlayerId, 2 Fail. 나머지는 Room이 검증한 정수라 쿼리에 그대로 넣는다.
	// 새 행의 ItemDbId는 Kind 1 행으로(newCounts 순서대로), 골드를 바꿨으면 바뀐 뒤 잔액을 Kind 2 행으로 돌려준다.
	inline std::wstring BuildItemGive(const DB_ItemOp& op)
	{
		std::wstring q = L"SET NOCOUNT ON; SET XACT_ABORT ON; DECLARE @P BIGINT = ?, @Fail INT = ?; ";
		q += ItemOpHead;
		int want = 0;
		for (const DB_ItemOp::Fill& fill : op.fills)
		{
			q += std::format(L"UPDATE [Player_Item] SET [Count] = [Count] + {}, UpdatedAt = SYSDATETIME() "
				L"WHERE ItemDbId = {} AND OwnerId = @P AND Location = 1 AND ItemId = {} AND [Count] = {}; SET @n += @@ROWCOUNT; ",
				fill.add, fill.itemDbId, op.itemId, fill.expectCount);
			++want;
		}
		// 새 행은 Room이 고른 빈 칸에 넣는다(칸이 이미 차 있으면 유니크 인덱스 위반 → CATCH에서 롤백).
		for (size_t i = 0; i < op.newCounts.size(); ++i)
		{
			q += std::format(L"INSERT INTO [Player_Item] (OwnerId, ItemId, [Count], Location, EquipSlot, BagSlot) VALUES (@P, {}, {}, 1, 0, {}); "
				L"INSERT INTO @R (Kind, V) VALUES (1, SCOPE_IDENTITY()); ", op.itemId, op.newCounts[i], op.newSlots[i]);
		}
		if (op.goldDelta != 0)
		{
			q += std::format(L"UPDATE [Player_Info] SET Gold = Gold + ({0}) WHERE PlayerId = @P AND Gold + ({0}) BETWEEN 0 AND {1}; SET @n += @@ROWCOUNT; "
				L"INSERT INTO @R (Kind, V) SELECT 2, Gold FROM [Player_Info] WHERE PlayerId = @P; ", op.goldDelta, op.goldMax);
			++want;
		}
		return q + ItemOpTail(std::to_wstring(want));
	}

	// 1:1 거래 확정(조립식, A4.2·D-158). 파라미터: 1 PlayerId(A = 요청한 쪽), 2 Fail. 나머지는 Room이 검증한 정수라 쿼리에 그대로 넣는다.
	//   ① 통째로 넘기는 행(넘기는 수량 = 행의 수량)은 **UPDATE 한 문장**으로 OwnerId·BagSlot을 바꾼다 — 받는 쪽이 방금 내놓은 칸을
	//      다시 써도 칸 유니크 인덱스를 중간에 어기지 않는다(문장이 끝날 때 검사).
	//   ② 일부만 넘기는 행은 원래 행의 수량을 줄이고 받는 쪽에 새 행을 넣는다(새 ItemDbId는 Kind 1 행으로, tradeMoves 순서대로).
	//   ③ 골드는 양쪽을 각각 증감하고(차감 뒤 0 이상·상한 이하) 잔액을 Kind 2(A)·3(B) 행으로 돌려준다.
	//   ④ Trade_Log 1행. 모든 UPDATE는 기대 상태(OwnerId·Location 1·Count)를 조건으로 하고 바뀐 행 수가 다르면 롤백한다.
	inline std::wstring BuildItemTrade(const DB_ItemOp& op)
	{
		std::wstring q = std::format(L"SET NOCOUNT ON; SET XACT_ABORT ON; DECLARE @P BIGINT = ?, @Fail INT = ?; DECLARE @B BIGINT = {}; ", op.tradeOtherUID);
		q += ItemOpHead;
		int want = 0;
		auto owner = [](int side) { return side == 0 ? L"@P" : L"@B"; };

		// ① 통째로 넘기는 행
		std::wstring ids, ownerCase, slotCase, cond;
		int wholeCount = 0;
		for (const DB_ItemOp::TradeMove& m : op.tradeMoves)
		{
			if (m.count != m.expectCount) continue;
			if (wholeCount++ > 0) { ids += L", "; cond += L" OR "; }
			ids += std::to_wstring(m.itemDbId);
			ownerCase += std::format(L"WHEN {} THEN {} ", m.itemDbId, owner(1 - m.fromSide));
			slotCase += std::format(L"WHEN {} THEN {} ", m.itemDbId, m.toSlot);
			cond += std::format(L"(ItemDbId = {} AND OwnerId = {} AND ItemId = {} AND [Count] = {})", m.itemDbId, owner(m.fromSide), m.itemId, m.expectCount);
		}
		if (wholeCount > 0)
		{
			q += L"UPDATE [Player_Item] SET OwnerId = CASE ItemDbId " + ownerCase + L"END, BagSlot = CASE ItemDbId " + slotCase + L"END, UpdatedAt = SYSDATETIME() "
				L"WHERE ItemDbId IN (" + ids + L") AND Location = 1 AND (" + cond + L"); SET @n += @@ROWCOUNT; ";
			want += wholeCount;
		}

		// ② 일부만 넘기는 행
		for (const DB_ItemOp::TradeMove& m : op.tradeMoves)
		{
			if (m.count == m.expectCount) continue;
			q += std::format(L"UPDATE [Player_Item] SET [Count] = [Count] - {}, UpdatedAt = SYSDATETIME() "
				L"WHERE ItemDbId = {} AND OwnerId = {} AND Location = 1 AND ItemId = {} AND [Count] = {}; SET @n += @@ROWCOUNT; ",
				m.count, m.itemDbId, owner(m.fromSide), m.itemId, m.expectCount);
			q += std::format(L"INSERT INTO [Player_Item] (OwnerId, ItemId, [Count], Location, EquipSlot, BagSlot) VALUES ({}, {}, {}, 1, 0, {}); "
				L"INSERT INTO @R (Kind, V) VALUES (1, SCOPE_IDENTITY()); ", owner(1 - m.fromSide), m.itemId, m.count, m.toSlot);
			++want;
		}

		// ③ 골드
		if (op.tradeGoldA != 0 || op.tradeGoldB != 0)
		{
			q += std::format(L"UPDATE [Player_Info] SET Gold = Gold - ({0}) + ({1}) WHERE PlayerId = @P AND Gold - ({0}) >= 0 AND Gold - ({0}) + ({1}) <= {2}; SET @n += @@ROWCOUNT; "
				L"UPDATE [Player_Info] SET Gold = Gold - ({1}) + ({0}) WHERE PlayerId = @B AND Gold - ({1}) >= 0 AND Gold - ({1}) + ({0}) <= {2}; SET @n += @@ROWCOUNT; "
				L"INSERT INTO @R (Kind, V) SELECT 2, Gold FROM [Player_Info] WHERE PlayerId = @P; "
				L"INSERT INTO @R (Kind, V) SELECT 3, Gold FROM [Player_Info] WHERE PlayerId = @B; ", op.tradeGoldA, op.tradeGoldB, op.goldMax);
			want += 2;
		}

		// ④ 거래 로그
		std::wstring itemsA, itemsB;
		for (const DB_ItemOp::TradeMove& m : op.tradeMoves)
		{
			std::wstring& out = m.fromSide == 0 ? itemsA : itemsB;
			if (!out.empty()) out += L",";
			out += std::format(L"{}:{}:{}", m.itemDbId, m.itemId, m.count);
		}
		q += std::format(L"INSERT INTO [Trade_Log] (PlayerA, PlayerB, GoldA, GoldB, ItemsA, ItemsB) VALUES (@P, @B, {}, {}, N'{}', N'{}'); ",
			op.tradeGoldA, op.tradeGoldB, itemsA, itemsB);

		return q + ItemOpTail(std::to_wstring(want));
	}

	// 로그인 보정: 맞지 않는 장착 행(슬롯 불일치, 검이 아닌데 방패)을 인벤토리로 내린다. 파라미터: 1 PlayerId, 2 Fail
	inline std::wstring BuildItemLoginFix(const DB_ItemOp& op)
	{
		std::wstring q = L"SET NOCOUNT ON; SET XACT_ABORT ON; DECLARE @P BIGINT = ?, @Fail INT = ?; ";
		q += ItemOpHead;
		for (size_t i = 0; i < op.demoteDbIds.size(); ++i)
		{
			q += std::format(L"UPDATE [Player_Item] SET Location = 1, EquipSlot = 0, BagSlot = {}, UpdatedAt = SYSDATETIME() "
				L"WHERE ItemDbId = {} AND OwnerId = @P AND Location = 2; SET @n += @@ROWCOUNT; ", op.demoteSlots[i], op.demoteDbIds[i]);
		}
		return q + ItemOpTail(std::to_wstring(op.demoteDbIds.size()));
	}

	// ---------------------------------------------------------------------------------------------------------------
	// 퀘스트(Q1, D-259·D-261). 아이템 트랜잭션과 같은 틀·같은 결과 모양을 쓴다.
	// ---------------------------------------------------------------------------------------------------------------

	// 수락(고정 쿼리). 파라미터: 1 PlayerId, 2 Fail, 3 QuestId
	// 행이 이미 있으면(진행 중이든 완료든) 0행 → 롤백. 완료한 행은 지우지 않으므로 다시 받을 수 없다.
	inline const std::wstring QuestAccept =
		std::wstring(L"SET NOCOUNT ON; SET XACT_ABORT ON; "
		L"DECLARE @P BIGINT = ?, @Fail INT = ?, @Q INT = ?; ")
		+ ItemOpHead +
		L"INSERT INTO [Player_Quest] (PlayerId, QuestId, State, Progress) "
		L"SELECT @P, @Q, 1, 0 WHERE NOT EXISTS (SELECT 1 FROM [Player_Quest] WHERE PlayerId = @P AND QuestId = @Q); SET @n += @@ROWCOUNT; "
		+ ItemOpTail(L"1");

	// 완료(조립식 — 회수할 스택 수·보상 배치가 요청마다 다르다). 파라미터: 1 PlayerId, 2 Fail. 나머지는 Room이 검증한 정수라 쿼리에 그대로 넣는다.
	//   ① 상태: 진행 중(State 1)일 때만 완료(2)로 바꾸고 최종 처치 수를 함께 적는다(0행이면 롤백 — 두 번 완료되지 않는다).
	//   ② 회수(수집형): 스택을 통째로 가져가면 행 삭제, 일부면 수량 감소. 기대 수량이 다르면 0행 → 롤백.
	//   ③ 보상 아이템: 덜 찬 스택에 더하기 + 새 행(새 ItemDbId는 Kind 1 행으로, newCounts 순서대로).
	//      **회수 → 추가 순서를 지킨다** — 보상 배치 계획은 회수를 적용한 뒤의 인벤토리로 세운 것이라, 회수로 빈 칸을 새 행이 다시 쓸 수 있다
	//      (순서를 바꾸면 칸 유니크 인덱스 UX_Player_Item_BagSlot을 어긴다).
	//   ④ 보상 골드: 상한 안에서 증액하고 잔액을 Kind 2 행으로.
	inline std::wstring BuildQuestComplete(const DB_ItemOp& op)
	{
		std::wstring q = L"SET NOCOUNT ON; SET XACT_ABORT ON; DECLARE @P BIGINT = ?, @Fail INT = ?; ";
		q += ItemOpHead;
		int want = 0;

		q += std::format(L"UPDATE [Player_Quest] SET State = 2, Progress = {}, CompletedAt = SYSDATETIME() "
			L"WHERE PlayerId = @P AND QuestId = {} AND State = 1; SET @n += @@ROWCOUNT; ", op.questProgress, op.questId);
		++want;

		for (const DB_ItemOp::QuestTake& take : op.questTakes)
		{
			if (take.take >= take.expectCount)
				q += std::format(L"DELETE FROM [Player_Item] WHERE ItemDbId = {} AND OwnerId = @P AND Location = 1 AND ItemId = {} AND [Count] = {}; SET @n += @@ROWCOUNT; ",
					take.itemDbId, op.questTakeItemId, take.expectCount);
			else
				q += std::format(L"UPDATE [Player_Item] SET [Count] = [Count] - {}, UpdatedAt = SYSDATETIME() "
					L"WHERE ItemDbId = {} AND OwnerId = @P AND Location = 1 AND ItemId = {} AND [Count] = {}; SET @n += @@ROWCOUNT; ",
					take.take, take.itemDbId, op.questTakeItemId, take.expectCount);
			++want;
		}

		for (const DB_ItemOp::Fill& fill : op.fills)
		{
			q += std::format(L"UPDATE [Player_Item] SET [Count] = [Count] + {}, UpdatedAt = SYSDATETIME() "
				L"WHERE ItemDbId = {} AND OwnerId = @P AND Location = 1 AND ItemId = {} AND [Count] = {}; SET @n += @@ROWCOUNT; ",
				fill.add, fill.itemDbId, op.itemId, fill.expectCount);
			++want;
		}
		for (size_t i = 0; i < op.newCounts.size(); ++i)
		{
			q += std::format(L"INSERT INTO [Player_Item] (OwnerId, ItemId, [Count], Location, EquipSlot, BagSlot) VALUES (@P, {}, {}, 1, 0, {}); "
				L"INSERT INTO @R (Kind, V) VALUES (1, SCOPE_IDENTITY()); ", op.itemId, op.newCounts[i], op.newSlots[i]);
		}

		if (op.goldDelta != 0)
		{
			q += std::format(L"UPDATE [Player_Info] SET Gold = Gold + ({0}) WHERE PlayerId = @P AND Gold + ({0}) BETWEEN 0 AND {1}; SET @n += @@ROWCOUNT; "
				L"INSERT INTO @R (Kind, V) SELECT 2, Gold FROM [Player_Info] WHERE PlayerId = @P; ", op.goldDelta, op.goldMax);
			++want;
		}
		return q + ItemOpTail(std::to_wstring(want));
	}

	// ---------------------------------------------------------------------------------------------------------------
	// 경매장(A4.3, D-159·D-234 — DESIGN_ITEMS_TRADE "경매장"). 아이템 트랜잭션과 같은 틀·같은 결과 모양을 쓴다.
	// 올린 아이템은 Player_Item.Location 3(경매 보관, BagSlot -1)에 두고, 구매·취소·만료 때 받을 사람의 수령함(Location 4)으로 옮긴다.
	// ExpireAt은 UTC다. 만료 시각 판단은 서비스(AuctionService)가 하고, 쿼리는 State만 조건으로 본다(두 시계가 어긋나 롤백이 반복되지 않게).
	// ---------------------------------------------------------------------------------------------------------------

	// 등록(고정 쿼리). 파라미터: 1 PlayerId, 2 Fail, 3 DbId, 4 Expect(그 행의 지금 수량), 5 Count(올리는 수량), 6 Slot(그 행의 칸),
	//                          7 ItemId, 8 UnitPrice(1개당 가격 — D-281), 9 DurationSec
	// 통째(Count = Expect)면 그 행을 경매 보관으로 옮기고, 일부면 원래 행의 수량을 줄이고 경매 보관에 새 행을 만든다. 새 ListingId는 Kind 4 행으로.
	inline const std::wstring AuctionRegister =
		std::wstring(L"SET NOCOUNT ON; SET XACT_ABORT ON; "
		L"DECLARE @P BIGINT = ?, @Fail INT = ?, @A BIGINT = ?, @Expect INT = ?, @Count INT = ?, @Slot INT = ?, @Item INT = ?, @Unit BIGINT = ?, @Dur INT = ?; "
		L"DECLARE @Esc BIGINT = @A; ")
		+ ItemOpHead +
		L"IF @Count >= @Expect BEGIN "
		L"UPDATE [Player_Item] SET Location = 3, BagSlot = -1, UpdatedAt = SYSDATETIME() "
		L"WHERE ItemDbId = @A AND OwnerId = @P AND Location = 1 AND BagSlot = @Slot AND ItemId = @Item AND [Count] = @Expect; SET @n += @@ROWCOUNT; END "
		L"ELSE BEGIN "
		L"UPDATE [Player_Item] SET [Count] = [Count] - @Count, UpdatedAt = SYSDATETIME() "
		L"WHERE ItemDbId = @A AND OwnerId = @P AND Location = 1 AND BagSlot = @Slot AND ItemId = @Item AND [Count] = @Expect AND @Count > 0; SET @n += @@ROWCOUNT; "
		L"IF @n = 1 BEGIN INSERT INTO [Player_Item] (OwnerId, ItemId, [Count], Location, EquipSlot, BagSlot) VALUES (@P, @Item, @Count, 3, 0, -1); SET @Esc = SCOPE_IDENTITY(); END END "
		L"IF @n = 1 BEGIN "
		L"INSERT INTO [Auction_Listing] (SellerId, ItemDbId, ItemId, [Count], InitialCount, UnitPrice, State, ExpireAt) "
		L"VALUES (@P, @Esc, @Item, @Count, @Count, @Unit, 1, DATEADD(SECOND, @Dur, SYSUTCDATETIME())); "
		L"INSERT INTO @R (Kind, V) VALUES (4, SCOPE_IDENTITY()); END "
		+ ItemOpTail(L"1");

	// 즉시 구매 — 부분 구매(고정 쿼리, D-281). 파라미터: 1 PlayerId(구매자), 2 Fail, 3 ListingId, 4 Qty(사는 수량)
	// ① 등록의 남은 수량을 N만큼 줄인다(조건 Count >= N — 동시에 온 구매가 겹쳐도 이중 판매가 없다, 다 팔리면 State 2). 바뀐 뒤 값은 OUTPUT으로 받는다
	//    (UPDATE 안의 "@변수 = 열"은 바뀌기 전·후 어느 값인지 헷갈리므로 쓰지 않는다).
	// ② 구매자 골드를 N × 1개당 가격만큼 줄인다(잔액 부족이면 0행 → 롤백).
	// ③ 다 팔렸으면 경매 보관 행을 통째로 구매자 수령함(Location 4)으로, 아니면 보관 행 수량을 N 줄이고 구매자 수령함에 새 행.
	//    보관 행의 수량은 늘 등록의 남은 수량과 같다 — WHERE에 기대 수량을 적는다.
	// ④ 우편 2행(구매자에게 구매품, 판매자에게 대금) + Auction_Sale 1행. 결과: Kind 2 = 구매자 잔액, Kind 10 = 남은 수량.
	// 기대 행 수 @W: 통째 3(등록·골드·보관 행), 일부 4(+ 새 행 — INSERT는 직접 센다). 등록 UPDATE가 0행이어도 @W는 3이라 롤백된다.
	inline const std::wstring AuctionBuy =
		std::wstring(L"SET NOCOUNT ON; SET XACT_ABORT ON; "
		L"DECLARE @P BIGINT = ?, @Fail INT = ?, @L BIGINT = ?, @Qty INT = ?; "
		L"DECLARE @Seller BIGINT, @Esc BIGINT, @Unit BIGINT, @ItemId INT, @Left INT, @Gold BIGINT, @Buy BIGINT, @W INT = 3; "
		L"DECLARE @U TABLE (SellerId BIGINT, ItemDbId BIGINT, UnitPrice BIGINT, ItemId INT, LeftCount INT); ")
		+ ItemOpHead +
		L"UPDATE [Auction_Listing] SET [Count] = [Count] - @Qty, State = CASE WHEN [Count] = @Qty THEN 2 ELSE 1 END, "
		L"ClosedAt = CASE WHEN [Count] = @Qty THEN SYSDATETIME() ELSE NULL END "
		L"OUTPUT inserted.SellerId, inserted.ItemDbId, inserted.UnitPrice, inserted.ItemId, inserted.[Count] INTO @U "
		L"WHERE ListingId = @L AND State = 1 AND SellerId <> @P AND @Qty >= 1 AND [Count] >= @Qty; SET @n += @@ROWCOUNT; "
		L"IF @n = 1 BEGIN "
		L"SELECT @Seller = SellerId, @Esc = ItemDbId, @Unit = UnitPrice, @ItemId = ItemId, @Left = LeftCount FROM @U; "
		L"SET @Gold = @Unit * @Qty; "
		L"UPDATE [Player_Info] SET Gold = Gold - @Gold WHERE PlayerId = @P AND Gold >= @Gold; SET @n += @@ROWCOUNT; "
		L"IF @n = 2 BEGIN "
		L"IF @Left = 0 BEGIN "
		L"UPDATE [Player_Item] SET OwnerId = @P, Location = 4, UpdatedAt = SYSDATETIME() "
		L"WHERE ItemDbId = @Esc AND OwnerId = @Seller AND Location = 3 AND [Count] = @Qty; SET @n += @@ROWCOUNT; SET @Buy = @Esc; END "
		L"ELSE BEGIN SET @W = 4; "
		L"UPDATE [Player_Item] SET [Count] = [Count] - @Qty, UpdatedAt = SYSDATETIME() "
		L"WHERE ItemDbId = @Esc AND OwnerId = @Seller AND Location = 3 AND [Count] = @Left + @Qty; SET @n += @@ROWCOUNT; "
		L"IF @n = 3 BEGIN INSERT INTO [Player_Item] (OwnerId, ItemId, [Count], Location, EquipSlot, BagSlot) VALUES (@P, @ItemId, @Qty, 4, 0, -1); "
		L"SET @Buy = SCOPE_IDENTITY(); SET @n += 1; END END "
		L"IF @n = @W BEGIN "
		L"INSERT INTO [Player_Mail] (PlayerId, Kind, Gold, ItemDbId, ListingId) VALUES (@P, 2, 0, @Buy, @L), (@Seller, 1, @Gold, NULL, @L); "
		L"INSERT INTO [Auction_Sale] (ListingId, BuyerId, [Count], Gold) VALUES (@L, @P, @Qty, @Gold); "
		L"INSERT INTO @R (Kind, V) SELECT 2, Gold FROM [Player_Info] WHERE PlayerId = @P; "
		L"INSERT INTO @R (Kind, V) VALUES (10, @Left); END END END "
		+ ItemOpTail(L"@W");

	// 취소(고정 쿼리). 파라미터: 1 PlayerId(판매자), 2 Fail, 3 ListingId. 아이템은 판매자의 수령함으로(취소 반환).
	inline const std::wstring AuctionCancel =
		std::wstring(L"SET NOCOUNT ON; SET XACT_ABORT ON; "
		L"DECLARE @P BIGINT = ?, @Fail INT = ?, @L BIGINT = ?; "
		L"DECLARE @Item BIGINT; ")
		+ ItemOpHead +
		L"UPDATE [Auction_Listing] SET State = 4, ClosedAt = SYSDATETIME(), @Item = ItemDbId WHERE ListingId = @L AND State = 1 AND SellerId = @P; SET @n += @@ROWCOUNT; "
		L"IF @n = 1 BEGIN "
		L"UPDATE [Player_Item] SET Location = 4, UpdatedAt = SYSDATETIME() WHERE ItemDbId = @Item AND OwnerId = @P AND Location = 3; SET @n += @@ROWCOUNT; "
		L"INSERT INTO [Player_Mail] (PlayerId, Kind, Gold, ItemDbId, ListingId) VALUES (@P, 4, 0, @Item, @L); END "
		+ ItemOpTail(L"2");

	// 수령(파라미터 고정 — 사유 번호만 넣어 한 번 만든다). 파라미터: 1 PlayerId, 2 Fail, 3 MailId, 4 Slot(아이템이 들어갈 인벤토리 칸, -1 = 빈 칸 없음), 5 GoldMax
	// 우편 내용은 메모리에 없으므로 쿼리가 읽어서 판단한다: 아이템이면 수령함(Location 4) → 인벤토리(그 칸), 골드면 상한 안에서 증액, 그리고 우편 삭제.
	// 받을 수 없는 사유는 실패 행 (Ok 0, Kind 9, AuctionResult)으로 돌려준다 — reasonNoMail·reasonInventoryFull·reasonGoldFull = 그 enum 값.
	// 성공이면 받은 아이템을 Kind 5(ItemDbId)·6(ItemId)·7(Count) 행으로, 골드를 받았으면 잔액을 Kind 2 행으로.
	inline std::wstring BuildMailClaim(int reasonNoMail, int reasonInventoryFull, int reasonGoldFull)
	{
		return std::wstring(L"SET NOCOUNT ON; SET XACT_ABORT ON; "
			L"DECLARE @P BIGINT = ?, @Fail INT = ?, @M BIGINT = ?, @Slot INT = ?, @GoldMax BIGINT = ?; "
			L"DECLARE @reason INT = 0, @want INT = 1, @Gold BIGINT = 0, @Item BIGINT = NULL; ")
			+ ItemOpHead +
			L"SELECT @Gold = Gold, @Item = ItemDbId FROM [Player_Mail] WITH (UPDLOCK, ROWLOCK) WHERE MailId = @M AND PlayerId = @P; "
			+ std::format(L"IF @@ROWCOUNT = 0 SET @reason = {}; ", reasonNoMail) +
			L"IF @reason = 0 AND @Item IS NOT NULL BEGIN "
			+ std::format(L"IF @Slot < 0 SET @reason = {}; ", reasonInventoryFull) +
			L"ELSE BEGIN "
			L"UPDATE [Player_Item] SET Location = 1, BagSlot = @Slot, UpdatedAt = SYSDATETIME() WHERE ItemDbId = @Item AND OwnerId = @P AND Location = 4; SET @n += @@ROWCOUNT; SET @want += 1; "
			L"INSERT INTO @R (Kind, V) SELECT 5, ItemDbId FROM [Player_Item] WHERE ItemDbId = @Item; "
			L"INSERT INTO @R (Kind, V) SELECT 6, ItemId FROM [Player_Item] WHERE ItemDbId = @Item; "
			L"INSERT INTO @R (Kind, V) SELECT 7, [Count] FROM [Player_Item] WHERE ItemDbId = @Item; END END "
			L"IF @reason = 0 AND @Gold > 0 BEGIN "
			L"UPDATE [Player_Info] SET Gold = Gold + @Gold WHERE PlayerId = @P AND Gold + @Gold <= @GoldMax; "
			+ std::format(L"IF @@ROWCOUNT = 0 SET @reason = {}; ELSE BEGIN SET @n += 1; SET @want += 1; END ", reasonGoldFull) +
			L"INSERT INTO @R (Kind, V) SELECT 2, Gold FROM [Player_Info] WHERE PlayerId = @P; END "
			L"IF @reason = 0 BEGIN DELETE FROM [Player_Mail] WHERE MailId = @M AND PlayerId = @P; SET @n += @@ROWCOUNT; END "
			L"IF @reason <> 0 BEGIN ROLLBACK TRAN; SELECT 0 AS Ok, 9 AS Kind, CAST(@reason AS BIGINT) AS V; END ELSE "
			+ ItemOpTail(L"@want");
	}

	// 만료 묶음(조립식 — 서비스가 고른 ListingId들, 검증된 정수). 파라미터: 1 PlayerId(쓰지 않음, 0), 2 Fail
	// 아직 판매 중인 것만 만료로 바꾸고 아이템을 판매자 수령함으로 돌려준다(만료 반환). 실제로 만료 처리한 ListingId를 Kind 8 행으로 돌려준다
	// — 목록에 없는 id는 DB에서 이미 판매 중이 아니었던 것이다(서비스가 목록에서 뺀다).
	inline std::wstring BuildAuctionExpire(const std::vector<int64_t>& listingIds)
	{
		std::wstring ids;
		for (const int64_t id : listingIds)
		{
			if (!ids.empty()) ids += L", ";
			ids += std::to_wstring(id);
		}
		return std::wstring(L"SET NOCOUNT ON; SET XACT_ABORT ON; DECLARE @P BIGINT = ?, @Fail INT = ?; "
			L"DECLARE @E TABLE (ListingId BIGINT, SellerId BIGINT, ItemDbId BIGINT); ")
			+ ItemOpHead +
			L"UPDATE [Auction_Listing] SET State = 3, ClosedAt = SYSDATETIME() "
			L"OUTPUT inserted.ListingId, inserted.SellerId, inserted.ItemDbId INTO @E "
			L"WHERE State = 1 AND ListingId IN (" + ids + L"); "
			L"UPDATE I SET Location = 4, UpdatedAt = SYSDATETIME() FROM [Player_Item] I INNER JOIN @E E ON E.ItemDbId = I.ItemDbId "
			L"WHERE I.Location = 3 AND I.OwnerId = E.SellerId; SET @n += @@ROWCOUNT; "
			L"INSERT INTO [Player_Mail] (PlayerId, Kind, Gold, ItemDbId, ListingId) SELECT SellerId, 3, 0, ItemDbId, ListingId FROM @E; "
			L"INSERT INTO @R (Kind, V) SELECT 8, ListingId FROM @E; "
			+ ItemOpTail(L"(SELECT COUNT(*) FROM @E)");
	}

	// 판매 중인 등록 읽기. 결과: ListingId, SellerId, ItemId, Count(남은 수량), UnitPrice, RemainSec(만료까지 남은 초 — 지났으면 0 이하)
	// 서버 시작 때 전부(SelectActiveListings), 결과를 알 수 없는 등록 뒤 그 판매자 것만(SelectSellerListings — 파라미터: 1 SellerId)
	inline const wchar_t* const SelectActiveListings =
		L"SELECT ListingId, SellerId, ItemId, [Count], UnitPrice, DATEDIFF(SECOND, SYSUTCDATETIME(), ExpireAt) AS RemainSec "
		L"FROM [Auction_Listing] WHERE State = 1 ORDER BY ListingId";
	inline const wchar_t* const SelectSellerListings =
		L"SELECT ListingId, SellerId, ItemId, [Count], UnitPrice, DATEDIFF(SECOND, SYSUTCDATETIME(), ExpireAt) AS RemainSec "
		L"FROM [Auction_Listing] WHERE State = 1 AND SellerId = ? ORDER BY ListingId";

	// 등록 하나의 상태 읽기(실패한 구매·취소 뒤 목록을 DB에 맞출 때). 파라미터: 1 ListingId. 결과: State, Count(남은 수량) (행이 없으면 없는 등록)
	inline const wchar_t* const SelectListingState = L"SELECT CAST(State AS INT), [Count] FROM [Auction_Listing] WHERE ListingId = ?";

	// 수령함 읽기(오래된 것부터). 파라미터: 1 PlayerId. limit = 한 번에 읽을 수(서비스는 보여 줄 수 + 1을 읽어 "더 있음"을 안다).
	// 결과: MailId, Kind, Gold, ItemId(아이템 우편이 아니면 0), Count, ListingId
	inline std::wstring BuildSelectMail(int limit)
	{
		return std::format(L"SELECT TOP ({}) M.MailId, CAST(M.Kind AS INT), M.Gold, ISNULL(I.ItemId, 0), ISNULL(I.[Count], 0), M.ListingId "
			L"FROM [Player_Mail] M LEFT JOIN [Player_Item] I ON I.ItemDbId = M.ItemDbId AND I.Location = 4 "
			L"WHERE M.PlayerId = ? ORDER BY M.MailId", limit);
	}
}
