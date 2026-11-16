-- 2026-11-17 A4.2 거래 로그 (Docs/DESIGN_ITEMS_TRADE.md "1:1 거래", D-158)
--   Trade_Log: 성사된 거래 1건 = 1행. 아이템 이동·골드 증감과 같은 트랜잭션에서 넣는다(DbQueries::BuildItemTrade).
--   ItemsA·ItemsB = 각자 내놓은 아이템 "ItemDbId:ItemId:Count" 목록(쉼표로 구분, 없으면 빈 문자열).
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

IF OBJECT_ID(N'dbo.Trade_Log', N'U') IS NULL
BEGIN
    CREATE TABLE [dbo].[Trade_Log](
        [TradeId] [bigint] IDENTITY(1,1) NOT NULL,
        [PlayerA] [bigint] NOT NULL,
        [PlayerB] [bigint] NOT NULL,
        [GoldA] [bigint] NOT NULL,
        [GoldB] [bigint] NOT NULL,
        [ItemsA] [nvarchar](600) NOT NULL,
        [ItemsB] [nvarchar](600) NOT NULL,
        [CreatedAt] [datetime2](0) NOT NULL CONSTRAINT [DF_Trade_Log_CreatedAt] DEFAULT (sysdatetime()),
        CONSTRAINT [PK_Trade_Log] PRIMARY KEY CLUSTERED ([TradeId] ASC)
    );
END

INSERT INTO [dbo].[Schema_Version] ([Version], [Name])
SELECT 7, N'0007_2026-11-17_a42_trade_log'
WHERE NOT EXISTS (SELECT 1 FROM [dbo].[Schema_Version] WHERE [Version] = 7);

COMMIT TRAN;
