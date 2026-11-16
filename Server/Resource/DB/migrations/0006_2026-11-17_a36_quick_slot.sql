-- 2026-11-17 A3.6 액티브 칸 배치 저장 (Docs/DESIGN_ITEMS_TRADE.md "소비 아이템과 액티브 칸", D-154)
--   Player_QuickSlot: (PlayerId, SlotIndex 1~5) = 그 칸에 올린 아이템 종류(ItemId). 빈 칸은 행이 없다.
--   서버가 5칸을 한 문장(MERGE)으로 저장한다(DbQueries::SaveQuickSlots).
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

IF OBJECT_ID(N'dbo.Player_QuickSlot', N'U') IS NULL
BEGIN
    CREATE TABLE [dbo].[Player_QuickSlot](
        [PlayerId] [bigint] NOT NULL,
        [SlotIndex] [tinyint] NOT NULL,
        [ItemId] [int] NOT NULL,
        CONSTRAINT [PK_Player_QuickSlot] PRIMARY KEY CLUSTERED ([PlayerId] ASC, [SlotIndex] ASC),
        CONSTRAINT [FK_Player_QuickSlot_Player_Info] FOREIGN KEY ([PlayerId]) REFERENCES [dbo].[Player_Info] ([PlayerId]),
        CONSTRAINT [CK_Player_QuickSlot_SlotIndex] CHECK ([SlotIndex] >= 1 AND [SlotIndex] <= 5),
        CONSTRAINT [CK_Player_QuickSlot_ItemId] CHECK ([ItemId] > 0)
    );
END

INSERT INTO [dbo].[Schema_Version] ([Version], [Name])
SELECT 6, N'0006_2026-11-17_a36_quick_slot'
WHERE NOT EXISTS (SELECT 1 FROM [dbo].[Schema_Version] WHERE [Version] = 6);

COMMIT TRAN;
