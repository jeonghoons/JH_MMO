-- 2026-11-23 AU2a 경매장 재기획 (D-281, Docs/DESIGN_ITEMS_TRADE.md "경매장")
--   Auction_Listing: 가격을 1개당 가격(UnitPrice)으로, Count = 남은 수량(부분 구매로 줄어듦), InitialCount = 처음 올린 수량.
--     묶음 전체 값(Price)과 구매자(BuyerId) 열은 없앤다 — 부분 구매라 구매자가 여럿이고, 판매 한 건은 Auction_Sale 한 행이다.
--     기존 등록은 UnitPrice = Price / Count(내림, 최소 1)로 옮긴다(나누어 떨어지지 않으면 판매 중인 등록의 값이 조금 줄어든다 — 시험 DB 전용 데이터).
--     State 2(팔림) = 남은 수량 0. 경매 보관(Player_Item.Location 3) 행의 Count는 늘 판매 중 등록의 Count와 같다.
--   Auction_Sale: 판매 한 건(부분 구매 포함) = 1행. Gold = Count × 그때의 UnitPrice.
--     이미 팔린 옛 등록(State 2)은 BuyerId·Price를 지우기 전에 판매 한 행(Gold = 옛 Price)으로 옮기고 남은 수량을 0으로 한다.
-- 다시 실행해도 안전하다(이미 바뀐 것은 건너뜀). USE 문이 없다 — migrate.ps1이 대상 DB를 정해 실행한다.
SET ANSI_NULLS ON;
SET QUOTED_IDENTIFIER ON;
SET ANSI_PADDING ON;
SET ANSI_WARNINGS ON;
SET CONCAT_NULL_YIELDS_NULL ON;
SET ARITHABORT ON;
SET NUMERIC_ROUNDABORT OFF;
SET XACT_ABORT ON;
SET NOCOUNT ON;
BEGIN TRAN;

-- ① 새 열(같은 배치에서 바로 참조할 수 없어 — 컴파일 오류 — 동적 SQL로 나눠 실행한다)
IF COL_LENGTH(N'dbo.Auction_Listing', N'UnitPrice') IS NULL
    EXEC(N'ALTER TABLE [dbo].[Auction_Listing] ADD [UnitPrice] [bigint] NULL, [InitialCount] [int] NULL;');

IF COL_LENGTH(N'dbo.Auction_Listing', N'Price') IS NOT NULL
    EXEC(N'UPDATE [dbo].[Auction_Listing] SET [UnitPrice] = CASE WHEN [Price] / [Count] < 1 THEN 1 ELSE [Price] / [Count] END, [InitialCount] = [Count] WHERE [UnitPrice] IS NULL;');

IF EXISTS (SELECT 1 FROM sys.columns WHERE object_id = OBJECT_ID(N'dbo.Auction_Listing') AND name = N'UnitPrice' AND is_nullable = 1)
BEGIN
    EXEC(N'ALTER TABLE [dbo].[Auction_Listing] ALTER COLUMN [UnitPrice] [bigint] NOT NULL;');
    EXEC(N'ALTER TABLE [dbo].[Auction_Listing] ALTER COLUMN [InitialCount] [int] NOT NULL;');
END

-- ② 판매 이력 테이블
IF OBJECT_ID(N'dbo.Auction_Sale', N'U') IS NULL
BEGIN
    CREATE TABLE [dbo].[Auction_Sale](
        [SaleId] [bigint] IDENTITY(1,1) NOT NULL,
        [ListingId] [bigint] NOT NULL,
        [BuyerId] [bigint] NOT NULL,
        [Count] [int] NOT NULL,
        [Gold] [bigint] NOT NULL,
        [CreatedAt] [datetime2](0) NOT NULL CONSTRAINT [DF_Auction_Sale_CreatedAt] DEFAULT (sysdatetime()),
        CONSTRAINT [PK_Auction_Sale] PRIMARY KEY CLUSTERED ([SaleId] ASC),
        CONSTRAINT [FK_Auction_Sale_Auction_Listing] FOREIGN KEY ([ListingId]) REFERENCES [dbo].[Auction_Listing] ([ListingId]),
        CONSTRAINT [FK_Auction_Sale_Player_Info] FOREIGN KEY ([BuyerId]) REFERENCES [dbo].[Player_Info] ([PlayerId]),
        CONSTRAINT [CK_Auction_Sale_Count] CHECK ([Count] >= 1),
        CONSTRAINT [CK_Auction_Sale_Gold] CHECK ([Gold] >= 1)
    );
    CREATE NONCLUSTERED INDEX [IX_Auction_Sale_Listing] ON [dbo].[Auction_Sale] ([ListingId] ASC);
END

-- ③ 이미 팔린 옛 등록: BuyerId·Price를 지우기 전에 판매 한 행으로 옮긴다(옛 등록은 통째로만 팔렸다)
IF COL_LENGTH(N'dbo.Auction_Listing', N'BuyerId') IS NOT NULL AND COL_LENGTH(N'dbo.Auction_Listing', N'Price') IS NOT NULL
    EXEC(N'INSERT INTO [dbo].[Auction_Sale] (ListingId, BuyerId, [Count], Gold, CreatedAt) '
       + N'SELECT L.ListingId, L.BuyerId, L.[Count], L.Price, ISNULL(L.ClosedAt, SYSDATETIME()) FROM [dbo].[Auction_Listing] L '
       + N'WHERE L.State = 2 AND L.BuyerId IS NOT NULL AND NOT EXISTS (SELECT 1 FROM [dbo].[Auction_Sale] S WHERE S.ListingId = L.ListingId);');

-- ④ 팔림(State 2)은 남은 수량 0이 되므로 Count >= 1 제약을 0 ~ InitialCount로 바꾸고 옛 팔린 등록의 남은 수량을 0으로
IF OBJECT_ID(N'dbo.CK_Auction_Listing_Count', N'C') IS NOT NULL
    ALTER TABLE [dbo].[Auction_Listing] DROP CONSTRAINT [CK_Auction_Listing_Count];
EXEC(N'UPDATE [dbo].[Auction_Listing] SET [Count] = 0 WHERE State = 2 AND [Count] <> 0;');
EXEC(N'ALTER TABLE [dbo].[Auction_Listing] WITH CHECK ADD CONSTRAINT [CK_Auction_Listing_Count] CHECK ([Count] >= 0 AND [Count] <= [InitialCount]);');
IF OBJECT_ID(N'dbo.CK_Auction_Listing_UnitPrice', N'C') IS NULL
    EXEC(N'ALTER TABLE [dbo].[Auction_Listing] WITH CHECK ADD CONSTRAINT [CK_Auction_Listing_UnitPrice] CHECK ([UnitPrice] >= 1);');
IF OBJECT_ID(N'dbo.CK_Auction_Listing_InitialCount', N'C') IS NULL
    EXEC(N'ALTER TABLE [dbo].[Auction_Listing] WITH CHECK ADD CONSTRAINT [CK_Auction_Listing_InitialCount] CHECK ([InitialCount] >= 1);');

-- ⑤ 옛 열 삭제
IF OBJECT_ID(N'dbo.CK_Auction_Listing_Price', N'C') IS NOT NULL
    ALTER TABLE [dbo].[Auction_Listing] DROP CONSTRAINT [CK_Auction_Listing_Price];
IF COL_LENGTH(N'dbo.Auction_Listing', N'Price') IS NOT NULL
    EXEC(N'ALTER TABLE [dbo].[Auction_Listing] DROP COLUMN [Price];');
IF COL_LENGTH(N'dbo.Auction_Listing', N'BuyerId') IS NOT NULL
    EXEC(N'ALTER TABLE [dbo].[Auction_Listing] DROP COLUMN [BuyerId];');

INSERT INTO [dbo].[Schema_Version] ([Version], [Name])
SELECT 10, N'0010_2026-11-23_au2_auction'
WHERE NOT EXISTS (SELECT 1 FROM [dbo].[Schema_Version] WHERE [Version] = 10);

COMMIT TRAN;
