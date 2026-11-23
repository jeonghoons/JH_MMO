-- A5.2 봇 계정 DB 판정(서버를 끈 뒤 실행). 한 줄: gold item_qty item_rows broken active_listings sales trades mails
-- gold = 봇의 보유 골드 + 수령함 골드, item_qty = 봇 아이템 수량 합(인벤토리·장착·경매 보관·수령함). 거래·경매만 한 실행이면 실행 전과 같아야 한다(수수료 없음, D-159).
-- broken = 경매 데이터가 어긋난 행 수(A4.3 auction_test.py db_broken과 같은 검사, 0이어야 함).
SET NOCOUNT ON;
DECLARE @P TABLE (PlayerId BIGINT PRIMARY KEY);
INSERT INTO @P SELECT PlayerId FROM Player_Info WHERE Name LIKE 'bot[_]%';
SELECT
  (SELECT ISNULL(SUM(Gold), 0) FROM Player_Info WHERE PlayerId IN (SELECT PlayerId FROM @P))
    + (SELECT ISNULL(SUM(Gold), 0) FROM Player_Mail WHERE PlayerId IN (SELECT PlayerId FROM @P)) AS gold,
  (SELECT ISNULL(SUM(CAST([Count] AS BIGINT)), 0) FROM Player_Item WHERE OwnerId IN (SELECT PlayerId FROM @P)) AS item_qty,
  (SELECT COUNT(*) FROM Player_Item WHERE OwnerId IN (SELECT PlayerId FROM @P)) AS item_rows,
  (SELECT COUNT(*) FROM Auction_Listing L WHERE L.State = 1 AND NOT EXISTS (SELECT 1 FROM Player_Item I WHERE I.ItemDbId = L.ItemDbId AND I.Location = 3 AND I.OwnerId = L.SellerId AND I.[Count] = L.[Count]))
    + (SELECT COUNT(*) FROM Player_Item I WHERE I.Location = 3 AND NOT EXISTS (SELECT 1 FROM Auction_Listing L WHERE L.ItemDbId = I.ItemDbId AND L.State = 1))
    + (SELECT COUNT(*) FROM Player_Mail M WHERE M.ItemDbId IS NOT NULL AND NOT EXISTS (SELECT 1 FROM Player_Item I WHERE I.ItemDbId = M.ItemDbId AND I.Location = 4 AND I.OwnerId = M.PlayerId))
    + (SELECT COUNT(*) FROM Player_Item I WHERE I.Location = 4 AND NOT EXISTS (SELECT 1 FROM Player_Mail M WHERE M.ItemDbId = I.ItemDbId))
    + (SELECT COUNT(*) FROM Player_Item I WHERE I.Location IN (3, 4) AND I.BagSlot <> -1)
    + (SELECT COUNT(*) FROM Auction_Listing L WHERE L.InitialCount <> L.[Count] + ISNULL((SELECT SUM(S.[Count]) FROM Auction_Sale S WHERE S.ListingId = L.ListingId), 0))
    + (SELECT COUNT(*) FROM Auction_Sale S INNER JOIN Auction_Listing L ON L.ListingId = S.ListingId WHERE S.Gold <> S.[Count] * L.UnitPrice)
    + (SELECT COUNT(*) FROM Auction_Listing L WHERE L.State = 2 AND L.[Count] <> 0) AS broken,
  (SELECT COUNT(*) FROM Auction_Listing WHERE State = 1 AND SellerId IN (SELECT PlayerId FROM @P)) AS active_listings,
  (SELECT COUNT(*) FROM Auction_Sale WHERE BuyerId IN (SELECT PlayerId FROM @P)) AS sales,
  (SELECT COUNT(*) FROM Trade_Log WHERE PlayerA IN (SELECT PlayerId FROM @P)) AS trades,
  (SELECT COUNT(*) FROM Player_Mail WHERE PlayerId IN (SELECT PlayerId FROM @P)) AS mails;
