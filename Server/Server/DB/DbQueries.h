#pragma once
#include <string>
#include <vector>
#include <format>

// GameServer가 실행하는 SQL을 모두 여기에 둔다(A2.5, DECISIONS 2026-11-16). DatabaseWorker는 바인딩·실행·결과 전달만 한다.
// 규칙:
//   - SQL 문자열은 이 파일에만 쓴다. 값은 ? 파라미터로 넘기고, 쿼리에 직접 넣는 것은 서버가 검증한 정수뿐이다.
//   - 테이블·열을 바꾸면 Resource/DB/migrations/NNNN_<날짜>_<내용>.sql 추가 + Resource/DB/schema.sql 갱신 +
//     아래 DB_SCHEMA_VERSION 증가를 한 커밋에서 함께 한다. (실행: Resource/DB/migrate.ps1)
namespace DbQueries
{
	// 이 서버 코드가 기대하는 스키마 버전 = 마지막 마이그레이션 순번. DB의 Schema_Version과 다르면 서버가 시작하지 않는다.
	constexpr int DB_SCHEMA_VERSION = 3;

	// 결과: 열 1 = 적용된 마지막 마이그레이션 순번(행이 없으면 0). 테이블이 없으면 실행 실패.
	inline const wchar_t* const SelectSchemaVersion = L"SELECT ISNULL(MAX([Version]), 0) FROM [Schema_Version]";

	// 로그인. 파라미터: 1 LoginId
	// 결과(장착 아이템 수만큼 행, 없으면 1행): LoginPassword, PlayerId, AccountId, Name, PlayerType, Level, Exp, Hp, Gold, EquipSlot, ItemId,
	//   NeverSaved(1 = 진행 상황을 저장한 적이 없는 캐릭터 = UpdatedAt NULL)
	inline const wchar_t* const SelectLogin = L" \
        SELECT A.LoginPassword, P.PlayerId, P.AccountId, P.Name, P.PlayerType, \
               P.[Level], P.Exp, P.Hp, P.Gold, \
               ISNULL(E.EquipSlot, 0) AS EquipSlot, ISNULL(E.ItemId, 0) AS ItemId, \
               CASE WHEN P.UpdatedAt IS NULL THEN 1 ELSE 0 END AS NeverSaved \
        FROM [User_Account] A \
        INNER JOIN [Player_Info] P ON A.AccountId = P.AccountId \
        LEFT JOIN [Player_Equipped] E ON P.PlayerId = E.PlayerId \
        WHERE A.LoginId = ?";

	// 가입: 계정 + 계정 정보 + 캐릭터 + 기본 장비를 한 트랜잭션으로. 파라미터: 1 LoginId, 2 LoginPassword, 3 Name, 4 PlayerType, 5 Level, 6 Hp
	// starterItems = (EquipSlot, ItemId). 로더가 검증한 정수(ItemTable.Starter)라 쿼리에 그대로 넣는다. ItemDbId는 A3 전까지 0.
	inline std::wstring BuildSignUp(const std::vector<std::pair<int, int>>& starterItems)
	{
		std::wstring starterInsert;
		for (const auto& [slot, itemId] : starterItems)
		{
			starterInsert += std::format(L"INSERT INTO [Player_Equipped] (PlayerId, EquipSlot, ItemDbId, ItemId) VALUES (@PlayerID, {}, 0, {}); ",
				slot, itemId);
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

	// 장비 스냅숏 저장: 전부 지우고 빈 칸(0)을 뺀 나머지를 다시 넣는다(한 트랜잭션). 파라미터: 1 PlayerId
	// items[i] = 슬롯 i + 1(Protocol::EquipPart)의 itemId. Room이 검증한 값이라 쿼리에 그대로 넣는다. ItemDbId는 A3 전까지 0.
	inline std::wstring BuildSaveEquipSnapshot(const std::vector<int32_t>& items)
	{
		std::wstring inserts;
		for (size_t i = 0; i < items.size(); ++i)
		{
			if (items[i] == 0) continue;
			inserts += std::format(L"INSERT INTO [Player_Equipped] (PlayerId, EquipSlot, ItemDbId, ItemId) VALUES (@P, {}, 0, {}); ", i + 1, items[i]);
		}
		return L"SET NOCOUNT ON; SET XACT_ABORT ON; DECLARE @P BIGINT = ?; BEGIN TRAN; "
			L"DELETE FROM [Player_Equipped] WHERE PlayerId = @P; " + inserts + L"COMMIT TRAN;";
	}

	// 진행 상황 저장(A2.5). 파라미터: 1 Level, 2 Exp, 3 Hp, 4 Gold, 5 PlayerId
	// 필드가 늘면(A2.3b 등) DB_PlayerProgress(ServerData.h)와 이 쿼리, DatabaseWorker::SavePlayerProgress의 바인딩을 함께 고친다.
	inline const wchar_t* const UpdatePlayerProgress =
		L"UPDATE [Player_Info] SET [Level] = ?, Exp = ?, Hp = ?, Gold = ?, UpdatedAt = SYSDATETIME() WHERE PlayerId = ?";
}
