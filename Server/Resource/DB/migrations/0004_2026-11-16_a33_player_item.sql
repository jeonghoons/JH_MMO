-- 2026-11-16 A3.3 인벤토리 (Docs/DESIGN_ITEMS_TRADE.md "아이템 인스턴스와 DB", D-153)
--   1) Player_Item: 아이템 한 개(스택이면 한 묶음) = 한 행. Location 1 인벤토리 · 2 장착 · 3 경매 보관 · 4 수령함.
--      장착 중이면 EquipSlot = Protocol::EquipPart(1~7), 아니면 0. 아이템은 행을 지우지 않고 OwnerId·Location만 바꿔 이동한다.
--   2) Player_Equipped의 행을 Location 2(장착)로 옮기고 Player_Equipped를 삭제한다.
--      가입 트랜잭션(DbQueries::BuildSignUp)도 기본 장비를 Player_Item에 넣는다.
-- 필터 인덱스(UX_Player_Item_Equip)가 있는 테이블은 QUOTED_IDENTIFIER·ANSI_NULLS 등이 켜진 연결에서만 쓸 수 있다.
--   서버(ODBC)와 migrate.ps1(sqlcmd -I)은 켜져 있다. 다른 도구로 직접 고칠 때는 아래 SET을 먼저 실행한다.
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

IF OBJECT_ID(N'dbo.Player_Item', N'U') IS NULL
BEGIN
    CREATE TABLE [dbo].[Player_Item](
        [ItemDbId] [bigint] IDENTITY(1,1) NOT NULL,
        [OwnerId] [bigint] NOT NULL,
        [ItemId] [int] NOT NULL,
        [Count] [int] NOT NULL,
        [Location] [tinyint] NOT NULL,
        [EquipSlot] [tinyint] NOT NULL CONSTRAINT [DF_Player_Item_EquipSlot] DEFAULT ((0)),
        [CreatedAt] [datetime2](0) NOT NULL CONSTRAINT [DF_Player_Item_CreatedAt] DEFAULT (SYSDATETIME()),
        [UpdatedAt] [datetime2](0) NOT NULL CONSTRAINT [DF_Player_Item_UpdatedAt] DEFAULT (SYSDATETIME()),
        CONSTRAINT [PK_Player_Item] PRIMARY KEY CLUSTERED ([ItemDbId] ASC),
        CONSTRAINT [FK_Player_Item_Player_Info] FOREIGN KEY ([OwnerId]) REFERENCES [dbo].[Player_Info] ([PlayerId]),
        CONSTRAINT [CK_Player_Item_Count] CHECK ([Count] >= 1),
        CONSTRAINT [CK_Player_Item_Location] CHECK ([Location] BETWEEN 1 AND 4),
        -- 장착(2)이면 슬롯 1~7, 그 밖에는 0
        CONSTRAINT [CK_Player_Item_EquipSlot] CHECK (([Location] = 2 AND [EquipSlot] BETWEEN 1 AND 7) OR ([Location] <> 2 AND [EquipSlot] = 0))
    );

    CREATE NONCLUSTERED INDEX [IX_Player_Item_Owner] ON [dbo].[Player_Item] ([OwnerId] ASC, [Location] ASC);
    -- 한 캐릭터의 같은 장착 슬롯에 두 행이 들어가지 못하게 한다(교체는 옛 장비를 먼저 내리고 새 장비를 올린다).
    CREATE UNIQUE NONCLUSTERED INDEX [UX_Player_Item_Equip] ON [dbo].[Player_Item] ([OwnerId] ASC, [EquipSlot] ASC) WHERE [Location] = 2;
END

IF OBJECT_ID(N'dbo.Player_Equipped', N'U') IS NOT NULL
BEGIN
    -- 옮길 수 없는 행이 있으면 조용히 버리지 않고 멈춘다(값을 어떻게 바로잡을지는 사람이 정한다).
    IF EXISTS (SELECT 1 FROM [dbo].[Player_Equipped] WHERE [EquipSlot] NOT BETWEEN 1 AND 7)
        THROW 50002, N'Player_Equipped에 EquipSlot이 1~7이 아닌 행이 있습니다. 값을 바로잡은 뒤 다시 실행하세요.', 1;

    DECLARE @before INT = (SELECT COUNT(*) FROM [dbo].[Player_Equipped]);

    INSERT INTO [dbo].[Player_Item] ([OwnerId], [ItemId], [Count], [Location], [EquipSlot])
    SELECT E.[PlayerId], E.[ItemId], 1, 2, E.[EquipSlot]
    FROM [dbo].[Player_Equipped] E
    WHERE NOT EXISTS (SELECT 1 FROM [dbo].[Player_Item] I WHERE I.[OwnerId] = E.[PlayerId] AND I.[Location] = 2 AND I.[EquipSlot] = E.[EquipSlot])
    ORDER BY E.[PlayerId], E.[EquipSlot];

    DECLARE @after INT = (SELECT COUNT(*) FROM [dbo].[Player_Item] I
                          WHERE I.[Location] = 2 AND EXISTS (SELECT 1 FROM [dbo].[Player_Equipped] E
                                                             WHERE E.[PlayerId] = I.[OwnerId] AND E.[EquipSlot] = I.[EquipSlot] AND E.[ItemId] = I.[ItemId]));
    IF @before <> @after
        THROW 50003, N'Player_Equipped → Player_Item 이전 행 수가 맞지 않습니다. 변경을 되돌립니다.', 1;

    DROP TABLE [dbo].[Player_Equipped];
END

INSERT INTO [dbo].[Schema_Version] ([Version], [Name])
SELECT 4, N'0004_2026-11-16_a33_player_item'
WHERE NOT EXISTS (SELECT 1 FROM [dbo].[Schema_Version] WHERE [Version] = 4);

COMMIT TRAN;
