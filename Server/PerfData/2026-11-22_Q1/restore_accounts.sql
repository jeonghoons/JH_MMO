-- Q1 시험 계정 복원(서버가 꺼져 있을 때만 실행). run_tests.sh가 묶음마다 서버를 띄우기 전에, 그리고 모든 시험이 끝난 뒤에 부른다.
-- 시험 계정(Test1~Test4)을 시험 전(2026-11-22 04:5x, before_db_state.txt)의 상태로 되돌린다:
--   레벨·경험치·HP·골드, 아이템(인벤토리·장착) 행 전부, 액티브 칸(없었음), 퀘스트(없었음), 경매 흔적(판매 중인 등록은 취소 상태로·우편 삭제).
-- 주 줄이 이 계정들을 화면 확인(HUD — Test1 Lv30, Test2 Lv5 exp 265)에 쓰므로, 시험이 준 경험치·골드·아이템·퀘스트 행을 남기지 않는다.
-- 다른 계정은 건드리지 않는다. Player_Item에 필터 유니크 인덱스가 있어 sqlcmd는 -I로 실행한다.
SET NOCOUNT ON;
SET XACT_ABORT ON;
BEGIN TRAN;

DECLARE @P TABLE (LoginId NVARCHAR(50) PRIMARY KEY, [Level] INT, Exp BIGINT, Hp INT, Gold BIGINT, PlayerId BIGINT NULL);
INSERT INTO @P (LoginId, [Level], Exp, Hp, Gold) VALUES
    (N'Test1', 30, 0, 3500, 1000), (N'Test2', 5, 265, 1000, 1000), (N'Test3', 1, 0, 650, 1000), (N'Test4', 5, 100, 650, 1000);
UPDATE T SET PlayerId = I.PlayerId
FROM @P T INNER JOIN [User_Account] A ON A.LoginId = T.LoginId INNER JOIN [Player_Info] I ON I.AccountId = A.AccountId;
IF EXISTS (SELECT 1 FROM @P WHERE PlayerId IS NULL) BEGIN ROLLBACK TRAN; RAISERROR(N'시험 계정 Test1~Test4 가운데 없는 것이 있습니다', 16, 1); RETURN; END

-- 시험 전의 아이템 행(LoginId, ItemDbId, ItemId, Count, Location, EquipSlot, BagSlot)
DECLARE @S TABLE (ItemDbId BIGINT PRIMARY KEY, LoginId NVARCHAR(50), ItemId INT, Cnt INT, Location INT, EquipSlot INT, BagSlot INT);
INSERT INTO @S (LoginId, ItemDbId, ItemId, Cnt, Location, EquipSlot, BagSlot) VALUES
    (N'Test1', 10520, 1101, 1, 2, 1, -1), (N'Test1', 10521, 2101, 1, 2, 2, -1), (N'Test1', 10522, 3101, 1, 2, 3, -1), (N'Test1', 10523, 4101, 1, 2, 4, -1),
    (N'Test1', 10524, 5101, 1, 2, 5, -1), (N'Test1', 13147, 6102, 1, 2, 6, -1), (N'Test1', 13013, 6101, 1, 1, 0, 0), (N'Test1', 13141, 6101, 1, 1, 0, 1),
    (N'Test1', 13142, 1301, 1, 1, 0, 2), (N'Test1', 13143, 2301, 1, 1, 0, 3), (N'Test1', 13144, 3301, 1, 1, 0, 4), (N'Test1', 13145, 4301, 1, 1, 0, 5),
    (N'Test1', 13146, 5301, 1, 1, 0, 6), (N'Test1', 13148, 6301, 1, 1, 0, 8), (N'Test1', 13149, 6302, 1, 1, 0, 9), (N'Test1', 13150, 7101, 1, 1, 0, 10),
    (N'Test1', 13151, 7301, 1, 1, 0, 11), (N'Test1', 13152, 8001, 20, 1, 0, 12), (N'Test1', 13258, 6102, 1, 1, 0, 13),
    (N'Test2', 10526, 1101, 1, 2, 1, -1), (N'Test2', 10527, 2101, 1, 2, 2, -1), (N'Test2', 10528, 3101, 1, 2, 3, -1), (N'Test2', 10529, 4101, 1, 2, 4, -1),
    (N'Test2', 10530, 5101, 1, 2, 5, -1), (N'Test2', 10531, 6101, 1, 2, 6, -1), (N'Test2', 13153, 1301, 1, 1, 0, 0), (N'Test2', 13154, 2301, 1, 1, 0, 1),
    (N'Test2', 13155, 3301, 1, 1, 0, 2), (N'Test2', 13156, 4301, 1, 1, 0, 3), (N'Test2', 13157, 5301, 1, 1, 0, 4), (N'Test2', 13158, 6102, 1, 1, 0, 5),
    (N'Test2', 13159, 6301, 1, 1, 0, 6), (N'Test2', 13160, 6302, 1, 1, 0, 7), (N'Test2', 13161, 7101, 1, 1, 0, 8), (N'Test2', 13162, 7301, 1, 1, 0, 9),
    (N'Test2', 13163, 8001, 20, 1, 0, 10),
    (N'Test3', 10532, 1101, 1, 2, 1, -1), (N'Test3', 10533, 2101, 1, 2, 2, -1), (N'Test3', 10534, 3101, 1, 2, 3, -1), (N'Test3', 10535, 4101, 1, 2, 4, -1),
    (N'Test3', 10536, 5101, 1, 2, 5, -1), (N'Test3', 10537, 6101, 1, 2, 6, -1), (N'Test3', 10785, 7101, 1, 2, 7, -1), (N'Test3', 10784, 8001, 20, 1, 0, 0),
    (N'Test3', 10788, 6102, 1, 1, 0, 2), (N'Test3', 13164, 1301, 1, 1, 0, 3), (N'Test3', 13165, 2301, 1, 1, 0, 4), (N'Test3', 13166, 3301, 1, 1, 0, 5),
    (N'Test3', 13167, 4301, 1, 1, 0, 6), (N'Test3', 13168, 5301, 1, 1, 0, 7), (N'Test3', 13169, 6301, 1, 1, 0, 8), (N'Test3', 13170, 6302, 1, 1, 0, 9),
    (N'Test3', 13171, 7301, 1, 1, 0, 10), (N'Test3', 13259, 6103, 1, 1, 0, 11),
    (N'Test4', 10538, 1101, 1, 2, 1, -1), (N'Test4', 10539, 2101, 1, 2, 2, -1), (N'Test4', 10540, 3101, 1, 2, 3, -1), (N'Test4', 10541, 4101, 1, 2, 4, -1),
    (N'Test4', 10542, 5101, 1, 2, 5, -1), (N'Test4', 10543, 6101, 1, 2, 6, -1), (N'Test4', 13172, 1301, 1, 1, 0, 0), (N'Test4', 13173, 2301, 1, 1, 0, 1),
    (N'Test4', 13174, 3301, 1, 1, 0, 2), (N'Test4', 13175, 4301, 1, 1, 0, 3), (N'Test4', 13176, 5301, 1, 1, 0, 4), (N'Test4', 13177, 6102, 1, 1, 0, 5),
    (N'Test4', 13178, 6301, 1, 1, 0, 6), (N'Test4', 13179, 6302, 1, 1, 0, 7), (N'Test4', 13180, 7101, 1, 1, 0, 8), (N'Test4', 13181, 7301, 1, 1, 0, 9),
    (N'Test4', 13182, 8001, 20, 1, 0, 10);

-- 퀘스트·경매 흔적·액티브 칸
DELETE FROM [Player_Quest] WHERE PlayerId IN (SELECT PlayerId FROM @P);
UPDATE [Auction_Listing] SET State = 4, ClosedAt = SYSDATETIME() WHERE State = 1 AND SellerId IN (SELECT PlayerId FROM @P);
DELETE FROM [Player_Mail] WHERE PlayerId IN (SELECT PlayerId FROM @P);
DELETE FROM [Player_QuickSlot] WHERE PlayerId IN (SELECT PlayerId FROM @P);

-- 아이템: 시험이 만든 행을 지우고, 시험 전의 행은 한 문장으로 되돌린다(유니크 인덱스는 문장 끝에 검사한다). 없어진 행은 같은 ItemDbId로 다시 넣는다.
DELETE FROM [Player_Item] WHERE OwnerId IN (SELECT PlayerId FROM @P) AND ItemDbId NOT IN (SELECT ItemDbId FROM @S);
UPDATE I SET OwnerId = T.PlayerId, ItemId = S.ItemId, [Count] = S.Cnt, Location = S.Location, EquipSlot = S.EquipSlot, BagSlot = S.BagSlot
FROM [Player_Item] I INNER JOIN @S S ON S.ItemDbId = I.ItemDbId INNER JOIN @P T ON T.LoginId = S.LoginId
WHERE I.OwnerId <> T.PlayerId OR I.ItemId <> S.ItemId OR I.[Count] <> S.Cnt OR I.Location <> S.Location OR I.EquipSlot <> S.EquipSlot OR I.BagSlot <> S.BagSlot;
SET IDENTITY_INSERT [Player_Item] ON;
INSERT INTO [Player_Item] (ItemDbId, OwnerId, ItemId, [Count], Location, EquipSlot, BagSlot)
SELECT S.ItemDbId, T.PlayerId, S.ItemId, S.Cnt, S.Location, S.EquipSlot, S.BagSlot
FROM @S S INNER JOIN @P T ON T.LoginId = S.LoginId
WHERE NOT EXISTS (SELECT 1 FROM [Player_Item] I WHERE I.ItemDbId = S.ItemDbId);
SET IDENTITY_INSERT [Player_Item] OFF;

-- 레벨·경험치·HP·골드(UpdatedAt은 그대로 둔다 — NULL이 아니어야 로그인 때 저장된 HP를 쓴다)
UPDATE I SET [Level] = T.[Level], Exp = T.Exp, Hp = T.Hp, Gold = T.Gold
FROM [Player_Info] I INNER JOIN @P T ON T.PlayerId = I.PlayerId;

COMMIT TRAN;
SELECT (SELECT COUNT(*) FROM @P) AS TestPlayers,
       (SELECT COUNT(*) FROM [Player_Item] WHERE OwnerId IN (SELECT PlayerId FROM @P)) AS Items,
       (SELECT COUNT(*) FROM [Player_Quest] WHERE PlayerId IN (SELECT PlayerId FROM @P)) AS Quests;
