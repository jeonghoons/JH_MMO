-- 2026-11-07 MC3.3 장비 저장 (Docs/DECISIONS.md 2026-11-04 "장비 획득·저장", 2026-11-07 MC3.1 "바로 전환")
--   1) Player_Info.PlayerType: 체형이 아닌 값(0, 옛 MONSTER 3, 옛 WARRIOR 4, ARCHER 5 — dummy 봇 계정 포함) → MALE(1)
--   2) Player_Equipped: 새 번호 규칙(itemId 첫째 자리 = EquipSlot 1~6)에 맞지 않는 행(옛 EquipPart 번호·옛 itemId)을 지우고,
--      장비 행이 하나도 없는 캐릭터에 기본 장비를 지급한다. (2026-11-07 실행 시점: 665명, Player_Equipped 0행)
--   다시 실행하면 그때 장비를 전부 벗은 캐릭터에도 기본 장비가 지급된다 — 마이그레이션 용도로만 실행한다.
-- 기본 장비 목록은 Common/DataTable/ItemTable.csv 의 Starter = 1 행과 같아야 한다(SQL은 CSV를 읽지 못함).
--   EquipSlot = Protocol::EquipPart (HEAD 1, TOP 2, BOTTOM 3, GLOVES 4, BOOTS 5, WEAPON 6), ItemDbId는 A3 전까지 0.
-- 실행 전 BACKUP DATABASE.
USE [GameServer];
GO
SET XACT_ABORT ON;
SET NOCOUNT ON;
BEGIN TRAN;

UPDATE [dbo].[Player_Info] SET PlayerType = 1 WHERE PlayerType NOT IN (1, 2);

DECLARE @Starter TABLE (EquipSlot TINYINT PRIMARY KEY, ItemId INT NOT NULL);
INSERT INTO @Starter (EquipSlot, ItemId) VALUES
    (1, 1101),   -- 천 두건
    (2, 2101),   -- 천 상의
    (3, 3101),   -- 천 하의
    (4, 4101),   -- 천 장갑
    (5, 5101),   -- 천 장화
    (6, 6101);   -- 천 한손검

DELETE FROM [dbo].[Player_Equipped]
WHERE EquipSlot NOT BETWEEN 1 AND 6 OR ItemId NOT BETWEEN 1000 AND 9999 OR ItemId / 1000 <> EquipSlot;

INSERT INTO [dbo].[Player_Equipped] (PlayerId, EquipSlot, ItemDbId, ItemId)
SELECT P.PlayerId, S.EquipSlot, 0, S.ItemId
FROM [dbo].[Player_Info] P CROSS JOIN @Starter S
WHERE NOT EXISTS (SELECT 1 FROM [dbo].[Player_Equipped] E WHERE E.PlayerId = P.PlayerId);

COMMIT TRAN;

-- 확인
SELECT PlayerType, COUNT(*) AS Players FROM [dbo].[Player_Info] GROUP BY PlayerType;
SELECT COUNT(*) AS PlayersWithout6Rows FROM [dbo].[Player_Info] P
WHERE (SELECT COUNT(*) FROM [dbo].[Player_Equipped] E WHERE E.PlayerId = P.PlayerId) <> 6;
GO
