-- 2026-11-16 A6.1 인벤토리 칸 위치 (Docs/DESIGN_ITEMS_TRADE.md "인벤토리", D-166)
--   Player_Item.BagSlot: 인벤토리(Location 1) 행의 칸 번호(0부터). 그 밖의 위치(장착·경매 보관·수령함)는 -1.
--   UX_Player_Item_BagSlot: 한 캐릭터의 같은 칸에 두 행이 들어가지 못하게 한다(칸 수 상한도 서버가 0 ~ 칸 수 - 1만 배정해 지킨다).
--   기존 인벤토리 행은 캐릭터별로 ItemDbId 순서대로 0부터 번호를 매긴다(지금까지 화면에 보이던 순서).
-- 새 열을 같은 배치에서 바로 쓰면 컴파일 오류가 나므로 열을 쓰는 문장은 EXEC로 실행한다.
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

IF COL_LENGTH(N'dbo.Player_Item', N'BagSlot') IS NULL
BEGIN
    ALTER TABLE [dbo].[Player_Item] ADD [BagSlot] [int] NOT NULL CONSTRAINT [DF_Player_Item_BagSlot] DEFAULT ((-1));

    EXEC (N'
        WITH Numbered AS (
            SELECT [BagSlot], ROW_NUMBER() OVER (PARTITION BY [OwnerId] ORDER BY [ItemDbId]) - 1 AS NewSlot
            FROM [dbo].[Player_Item] WHERE [Location] = 1
        )
        UPDATE Numbered SET [BagSlot] = NewSlot;');

    EXEC (N'
        ALTER TABLE [dbo].[Player_Item] ADD CONSTRAINT [CK_Player_Item_BagSlot]
            CHECK (([Location] = 1 AND [BagSlot] >= 0) OR ([Location] <> 1 AND [BagSlot] = -1));
        CREATE UNIQUE NONCLUSTERED INDEX [UX_Player_Item_BagSlot] ON [dbo].[Player_Item] ([OwnerId] ASC, [BagSlot] ASC) WHERE [Location] = 1;');
END

INSERT INTO [dbo].[Schema_Version] ([Version], [Name])
SELECT 5, N'0005_2026-11-16_a61_bag_slot'
WHERE NOT EXISTS (SELECT 1 FROM [dbo].[Schema_Version] WHERE [Version] = 5);

COMMIT TRAN;
