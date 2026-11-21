-- A4.3 경매 시험 전 정리(서버가 꺼져 있을 때만 실행 — 서비스의 메모리 목록과 어긋나지 않게). run_auction_tests.sh가 묶음마다 부른다.
-- 시험 계정(Test1~Test4)의 경매 흔적만 지운다: 판매 중인 등록은 취소 상태로, 우편 삭제, 경매 보관·수령함 아이템 삭제,
-- 시험이 개발 지급으로 받은 아이템(천 머리 1101·천 상의 2101 — 인벤토리 위치만) 삭제, 물약은 가장 오래된 한 줄(20개)만 남김, 골드 1000.
-- 다른 계정의 등록·우편과 시험 계정의 장착품·그 밖의 인벤토리는 건드리지 않는다.
SET NOCOUNT ON;
SET XACT_ABORT ON;
BEGIN TRAN;

DECLARE @T TABLE (PlayerId BIGINT PRIMARY KEY);
INSERT INTO @T
SELECT P.PlayerId FROM [Player_Info] P INNER JOIN [User_Account] A ON A.AccountId = P.AccountId
WHERE A.LoginId IN (N'Test1', N'Test2', N'Test3', N'Test4');

UPDATE [Auction_Listing] SET State = 4, ClosedAt = SYSDATETIME() WHERE State = 1 AND SellerId IN (SELECT PlayerId FROM @T);
DELETE FROM [Player_Mail] WHERE PlayerId IN (SELECT PlayerId FROM @T);
DELETE FROM [Player_Item] WHERE OwnerId IN (SELECT PlayerId FROM @T) AND Location IN (3, 4);
DELETE FROM [Player_Item] WHERE OwnerId IN (SELECT PlayerId FROM @T) AND Location = 1 AND ItemId IN (1101, 2101);

DELETE I FROM [Player_Item] I
WHERE I.OwnerId IN (SELECT PlayerId FROM @T) AND I.Location = 1 AND I.ItemId = 8001
  AND I.ItemDbId <> (SELECT MIN(J.ItemDbId) FROM [Player_Item] J WHERE J.OwnerId = I.OwnerId AND J.Location = 1 AND J.ItemId = 8001);
UPDATE [Player_Item] SET [Count] = 20 WHERE OwnerId IN (SELECT PlayerId FROM @T) AND Location = 1 AND ItemId = 8001;

UPDATE [Player_Info] SET Gold = 1000 WHERE PlayerId IN (SELECT PlayerId FROM @T);

COMMIT TRAN;
SELECT COUNT(*) AS TestPlayers FROM @T;
