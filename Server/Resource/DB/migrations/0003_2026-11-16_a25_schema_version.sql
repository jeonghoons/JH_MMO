-- 2026-11-16 A2.5 DB 저장 연결 (Docs/DECISIONS.md 2026-11-16)
--   1) Schema_Version: 적용한 마이그레이션을 DB에 남긴다(마이그레이션 1개 = 행 1개).
--      서버는 시작할 때 MAX(Version)을 코드 상수 DB_SCHEMA_VERSION(Server/DB/DbQueries.h)과 비교해 다르면 시작하지 않는다.
--      이 테이블이 생기기 전에 이미 적용된 0001·0002는 기준선 행으로 넣는다.
--   2) Player_Info.UpdatedAt: 진행 상황(레벨·경험치·HP·골드)을 마지막으로 저장한 시각. 저장한 적이 없으면 NULL.
--   3) Player_Info CHECK 제약: Level >= 1, Exp >= 0, Hp >= 0, Gold >= 0 (서버 버그로 음수가 저장되는 것을 DB가 막는다)
-- 다시 실행해도 안전하다(이미 있는 것은 건너뜀). USE 문이 없다 — migrate.ps1이 대상 DB를 정해 실행한다.
SET XACT_ABORT ON;
SET NOCOUNT ON;
BEGIN TRAN;

IF OBJECT_ID(N'dbo.Schema_Version', N'U') IS NULL
BEGIN
    CREATE TABLE [dbo].[Schema_Version](
        [Version] [int] NOT NULL,
        [Name] [nvarchar](100) NOT NULL,
        [AppliedAt] [datetime2](0) NOT NULL CONSTRAINT [DF_Schema_Version_AppliedAt] DEFAULT (SYSDATETIME()),
        CONSTRAINT [PK_Schema_Version] PRIMARY KEY CLUSTERED ([Version] ASC)
    );
END

IF COL_LENGTH(N'dbo.Player_Info', N'UpdatedAt') IS NULL
    ALTER TABLE [dbo].[Player_Info] ADD [UpdatedAt] [datetime2](0) NULL;

-- 위반 행이 있으면 제약을 걸 수 없다. 조용히 고치지 않고 멈춘다(값을 어떻게 바로잡을지는 사람이 정한다).
IF EXISTS (SELECT 1 FROM [dbo].[Player_Info] WHERE [Level] < 1 OR [Exp] < 0 OR [Hp] < 0 OR [Gold] < 0)
    THROW 50001, N'Player_Info에 Level < 1 또는 Exp/Hp/Gold < 0 인 행이 있습니다. 값을 바로잡은 뒤 다시 실행하세요.', 1;

IF OBJECT_ID(N'dbo.CK_Player_Info_Level', N'C') IS NULL
    ALTER TABLE [dbo].[Player_Info] WITH CHECK ADD CONSTRAINT [CK_Player_Info_Level] CHECK ([Level] >= 1);
IF OBJECT_ID(N'dbo.CK_Player_Info_Exp', N'C') IS NULL
    ALTER TABLE [dbo].[Player_Info] WITH CHECK ADD CONSTRAINT [CK_Player_Info_Exp] CHECK ([Exp] >= 0);
IF OBJECT_ID(N'dbo.CK_Player_Info_Hp', N'C') IS NULL
    ALTER TABLE [dbo].[Player_Info] WITH CHECK ADD CONSTRAINT [CK_Player_Info_Hp] CHECK ([Hp] >= 0);
IF OBJECT_ID(N'dbo.CK_Player_Info_Gold', N'C') IS NULL
    ALTER TABLE [dbo].[Player_Info] WITH CHECK ADD CONSTRAINT [CK_Player_Info_Gold] CHECK ([Gold] >= 0);

INSERT INTO [dbo].[Schema_Version] ([Version], [Name])
SELECT V.[Version], V.[Name]
FROM (VALUES
    (1, N'0001_2026-10-28_drop_unused (기준선)'),
    (2, N'0002_2026-11-07_mc33_equip (기준선)'),
    (3, N'0003_2026-11-16_a25_schema_version')
) AS V([Version], [Name])
WHERE NOT EXISTS (SELECT 1 FROM [dbo].[Schema_Version] S WHERE S.[Version] = V.[Version]);

COMMIT TRAN;
