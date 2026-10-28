-- 2026-10-28 저장소 정리 C-2: 코드가 쓰지 않는 테이블·열 삭제 (Docs/DECISIONS.md)
--   테이블: Item_Meta, Player_Inventory, Trade_Log (모두 0행, 코드 참조 없음. 아이템 정의는 CSV가 맡는다)
--   외래 키: Player_Equipped -> Item_Meta
--   열: Player_Info.Mp (MP 개념 없음), User_Account.Status (읽는 코드 없음)
-- 여러 번 실행해도 안전하다(이미 지운 것은 건너뜀). 실행 전 BACKUP DATABASE 권장.
USE [GameServer];
GO
SET XACT_ABORT ON;
BEGIN TRAN;

IF OBJECT_ID(N'dbo.FK_Player_Equipped_Item_Meta', N'F') IS NOT NULL
    ALTER TABLE [dbo].[Player_Equipped] DROP CONSTRAINT [FK_Player_Equipped_Item_Meta];

IF OBJECT_ID(N'dbo.Trade_Log', N'U') IS NOT NULL DROP TABLE [dbo].[Trade_Log];
IF OBJECT_ID(N'dbo.Player_Inventory', N'U') IS NOT NULL DROP TABLE [dbo].[Player_Inventory];
IF OBJECT_ID(N'dbo.Item_Meta', N'U') IS NOT NULL DROP TABLE [dbo].[Item_Meta];

-- 열에 걸린 기본값 제약(이름이 자동 생성된 것 포함)을 먼저 지운다.
DECLARE @sql NVARCHAR(MAX) = N'';
SELECT @sql += N'ALTER TABLE [dbo].[' + t.name + N'] DROP CONSTRAINT [' + dc.name + N'];'
FROM sys.default_constraints dc
JOIN sys.tables t ON t.object_id = dc.parent_object_id
JOIN sys.columns c ON c.object_id = dc.parent_object_id AND c.column_id = dc.parent_column_id
WHERE (t.name = N'Player_Info' AND c.name = N'Mp') OR (t.name = N'User_Account' AND c.name = N'Status');
EXEC sp_executesql @sql;

IF COL_LENGTH(N'dbo.Player_Info', N'Mp') IS NOT NULL ALTER TABLE [dbo].[Player_Info] DROP COLUMN [Mp];
IF COL_LENGTH(N'dbo.User_Account', N'Status') IS NOT NULL ALTER TABLE [dbo].[User_Account] DROP COLUMN [Status];

COMMIT TRAN;
GO
