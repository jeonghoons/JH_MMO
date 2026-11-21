-- 2026-11-21 A4.3 경매장 (Docs/DESIGN_ITEMS_TRADE.md "경매장", D-159·D-234·D-235)
--   Auction_Listing: 등록 1건 = 1행. State 1 판매 중 · 2 팔림 · 3 만료 · 4 취소. 끝난 행도 지우지 않는다(이력).
--     올린 아이템은 Player_Item.Location 3(경매 보관)으로 옮겨 두고 ItemDbId로 가리킨다. ExpireAt은 UTC다.
--   Player_Mail: 수령함 1건 = 1행. Kind 1 판매 대금(Gold) · 2 구매품 · 3 만료 반환 · 4 취소 반환(ItemDbId — Player_Item.Location 4).
--     받으면 행을 지운다(DbQueries::MailClaim).
-- 다시 실행해도 안전하다(이미 있는 것은 건너뜀). USE 문이 없다 — migrate.ps1이 대상 DB를 정해 실행한다.
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

IF OBJECT_ID(N'dbo.Auction_Listing', N'U') IS NULL
BEGIN
    CREATE TABLE [dbo].[Auction_Listing](
        [ListingId] [bigint] IDENTITY(1,1) NOT NULL,
        [SellerId] [bigint] NOT NULL,
        [ItemDbId] [bigint] NOT NULL,
        [ItemId] [int] NOT NULL,
        [Count] [int] NOT NULL,
        [Price] [bigint] NOT NULL,
        [State] [tinyint] NOT NULL CONSTRAINT [DF_Auction_Listing_State] DEFAULT ((1)),
        [BuyerId] [bigint] NULL,
        [CreatedAt] [datetime2](0) NOT NULL CONSTRAINT [DF_Auction_Listing_CreatedAt] DEFAULT (sysdatetime()),
        [ExpireAt] [datetime2](0) NOT NULL,
        [ClosedAt] [datetime2](0) NULL,
        CONSTRAINT [PK_Auction_Listing] PRIMARY KEY CLUSTERED ([ListingId] ASC),
        CONSTRAINT [FK_Auction_Listing_Player_Info] FOREIGN KEY ([SellerId]) REFERENCES [dbo].[Player_Info] ([PlayerId]),
        CONSTRAINT [CK_Auction_Listing_State] CHECK ([State] >= 1 AND [State] <= 4),
        CONSTRAINT [CK_Auction_Listing_Count] CHECK ([Count] >= 1),
        CONSTRAINT [CK_Auction_Listing_Price] CHECK ([Price] >= 1)
    );
    CREATE NONCLUSTERED INDEX [IX_Auction_Listing_State] ON [dbo].[Auction_Listing] ([State] ASC, [ExpireAt] ASC);
    CREATE NONCLUSTERED INDEX [IX_Auction_Listing_Seller] ON [dbo].[Auction_Listing] ([SellerId] ASC, [State] ASC);
END

IF OBJECT_ID(N'dbo.Player_Mail', N'U') IS NULL
BEGIN
    CREATE TABLE [dbo].[Player_Mail](
        [MailId] [bigint] IDENTITY(1,1) NOT NULL,
        [PlayerId] [bigint] NOT NULL,
        [Kind] [tinyint] NOT NULL,
        [Gold] [bigint] NOT NULL CONSTRAINT [DF_Player_Mail_Gold] DEFAULT ((0)),
        [ItemDbId] [bigint] NULL,
        [ListingId] [bigint] NOT NULL,
        [CreatedAt] [datetime2](0) NOT NULL CONSTRAINT [DF_Player_Mail_CreatedAt] DEFAULT (sysdatetime()),
        CONSTRAINT [PK_Player_Mail] PRIMARY KEY CLUSTERED ([MailId] ASC),
        CONSTRAINT [FK_Player_Mail_Player_Info] FOREIGN KEY ([PlayerId]) REFERENCES [dbo].[Player_Info] ([PlayerId]),
        CONSTRAINT [CK_Player_Mail_Kind] CHECK ([Kind] >= 1 AND [Kind] <= 4),
        CONSTRAINT [CK_Player_Mail_Gold] CHECK ([Gold] >= 0)
    );
    CREATE NONCLUSTERED INDEX [IX_Player_Mail_Player] ON [dbo].[Player_Mail] ([PlayerId] ASC, [MailId] ASC);
END

INSERT INTO [dbo].[Schema_Version] ([Version], [Name])
SELECT 8, N'0008_2026-11-21_a43_auction'
WHERE NOT EXISTS (SELECT 1 FROM [dbo].[Schema_Version] WHERE [Version] = 8);

COMMIT TRAN;
