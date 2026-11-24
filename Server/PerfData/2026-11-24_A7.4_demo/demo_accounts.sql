-- A7.4 시연 촬영용 시험 계정 상태(D-311). 서버가 꺼져 있을 때만 실행: sqlcmd -S .\SQLEXPRESS -d GameServer -E -I -b -f 65001 -i demo_accounts.sql
-- 먼저 Q1 restore_accounts.sql로 고정 상태(Test1 Lv30·Test2 Lv5, 퀘스트·우편·경매 등록·액티브 칸 없음)를 만든 뒤 이 파일을 돌린다(prepare_accounts.sh).
-- 촬영 뒤 되돌리기 = restore_accounts.sql 한 번(이 파일이 바꾼 행은 모두 그 고정 상태로 돌아감). 촬영 전 상태는 before_demo_state.txt.
--   Test2(주인공, 장면 1~5): Lv5 경험치 2300 — 1001(경험치 100)을 2마리 잡으면 화면에서 레벨 업. 가방에 강철 한손검·활 + 강철 양손검 추가.
--   Test1(거래 상대·파티장, 장면 6~8): Lv30, 근위(Epic) 투구~장화·한손검·방패 착용 — 던전 우두머리용. 가방에 근위 활·양손검, 큰 HP 물약 10(거래·경매 물건).
SET NOCOUNT ON;
SET XACT_ABORT ON;
BEGIN TRAN;

DECLARE @T1 BIGINT = (SELECT I.PlayerId FROM [Player_Info] I JOIN [User_Account] A ON A.AccountId = I.AccountId WHERE A.LoginId = N'Test1');
DECLARE @T2 BIGINT = (SELECT I.PlayerId FROM [Player_Info] I JOIN [User_Account] A ON A.AccountId = I.AccountId WHERE A.LoginId = N'Test2');
IF @T1 IS NULL OR @T2 IS NULL BEGIN ROLLBACK TRAN; RAISERROR(N'Test1·Test2가 없습니다', 16, 1); RETURN; END

-- Test2: 레벨 업 직전
UPDATE [Player_Info] SET Exp = 2300 WHERE PlayerId = @T2;
IF NOT EXISTS (SELECT 1 FROM [Player_Item] WHERE OwnerId = @T2 AND ItemId = 6303)
    INSERT INTO [Player_Item] (OwnerId, ItemId, [Count], Location, EquipSlot, BagSlot) VALUES (@T2, 6303, 1, 1, 0, 11);
-- 물약 장면(C5)용 MP 물약 5개 — 스킬을 쓰면 MP가 줄어 HP 물약(가득 찬 HP면 서버가 거부)보다 사용이 화면에 잘 보인다
IF NOT EXISTS (SELECT 1 FROM [Player_Item] WHERE OwnerId = @T2 AND ItemId = 8002)
    INSERT INTO [Player_Item] (OwnerId, ItemId, [Count], Location, EquipSlot, BagSlot) VALUES (@T2, 8002, 5, 1, 0, 12);

-- Test1: 장착 칸 1~6을 근위로 바꾸고(고정 상태의 행 id를 그대로 씀) 방패 칸 7을 넣음
UPDATE [Player_Item] SET ItemId = CASE EquipSlot WHEN 1 THEN 1401 WHEN 2 THEN 2401 WHEN 3 THEN 3401 WHEN 4 THEN 4401 WHEN 5 THEN 5401 WHEN 6 THEN 6401 END
WHERE OwnerId = @T1 AND Location = 2 AND EquipSlot BETWEEN 1 AND 6;
IF NOT EXISTS (SELECT 1 FROM [Player_Item] WHERE OwnerId = @T1 AND Location = 2 AND EquipSlot = 7)
    INSERT INTO [Player_Item] (OwnerId, ItemId, [Count], Location, EquipSlot, BagSlot) VALUES (@T1, 7401, 1, 2, 7, -1);
IF NOT EXISTS (SELECT 1 FROM [Player_Item] WHERE OwnerId = @T1 AND ItemId = 6402)
    INSERT INTO [Player_Item] (OwnerId, ItemId, [Count], Location, EquipSlot, BagSlot) VALUES (@T1, 6402, 1, 1, 0, 14);
IF NOT EXISTS (SELECT 1 FROM [Player_Item] WHERE OwnerId = @T1 AND ItemId = 6403)
    INSERT INTO [Player_Item] (OwnerId, ItemId, [Count], Location, EquipSlot, BagSlot) VALUES (@T1, 6403, 1, 1, 0, 15);
IF NOT EXISTS (SELECT 1 FROM [Player_Item] WHERE OwnerId = @T1 AND ItemId = 8003)
    INSERT INTO [Player_Item] (OwnerId, ItemId, [Count], Location, EquipSlot, BagSlot) VALUES (@T1, 8003, 10, 1, 0, 16);

COMMIT TRAN;

SELECT A.LoginId, I.[Level], I.Exp, I.Gold,
       (SELECT COUNT(*) FROM [Player_Item] P WHERE P.OwnerId = I.PlayerId AND P.Location = 2) AS equipped,
       (SELECT COUNT(*) FROM [Player_Item] P WHERE P.OwnerId = I.PlayerId AND P.Location = 1) AS bag
FROM [Player_Info] I JOIN [User_Account] A ON A.AccountId = I.AccountId WHERE A.LoginId IN (N'Test1', N'Test2') ORDER BY A.LoginId;
