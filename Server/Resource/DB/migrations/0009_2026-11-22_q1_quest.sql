-- 2026-11-22 Q1 퀘스트 (D-259·D-261, Docs/DESIGN_BALANCE.md §9)
--   Player_Quest: (PlayerId, QuestId) = 그 캐릭터가 받은 퀘스트 1건. 행 없음 = 받지 않음.
--     State 1 진행 중 · 2 완료. 완료한 행은 지우지 않는다(한 번만 할 수 있는 퀘스트 — 다시 받지 못하게 하는 기록).
--     Progress = 처치형의 처치 수(0 ~ QuestTable.Count). 수집형은 인벤토리 수량으로 판정하므로 0으로 둔다.
--     수락(INSERT)·완료(State 1 → 2 + 회수·보상)는 아이템 트랜잭션, 처치 진행은 따로 저장한다(DbQueries::UpdateQuestProgress — State 1이고 값이 커질 때만).
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

IF OBJECT_ID(N'dbo.Player_Quest', N'U') IS NULL
BEGIN
    CREATE TABLE [dbo].[Player_Quest](
        [PlayerId] [bigint] NOT NULL,
        [QuestId] [int] NOT NULL,
        [State] [tinyint] NOT NULL CONSTRAINT [DF_Player_Quest_State] DEFAULT ((1)),
        [Progress] [int] NOT NULL CONSTRAINT [DF_Player_Quest_Progress] DEFAULT ((0)),
        [AcceptedAt] [datetime2](0) NOT NULL CONSTRAINT [DF_Player_Quest_AcceptedAt] DEFAULT (sysdatetime()),
        [CompletedAt] [datetime2](0) NULL,
        CONSTRAINT [PK_Player_Quest] PRIMARY KEY CLUSTERED ([PlayerId] ASC, [QuestId] ASC),
        CONSTRAINT [FK_Player_Quest_Player_Info] FOREIGN KEY ([PlayerId]) REFERENCES [dbo].[Player_Info] ([PlayerId]),
        CONSTRAINT [CK_Player_Quest_QuestId] CHECK ([QuestId] >= 1),
        CONSTRAINT [CK_Player_Quest_State] CHECK ([State] >= 1 AND [State] <= 2),
        CONSTRAINT [CK_Player_Quest_Progress] CHECK ([Progress] >= 0)
    );
END

INSERT INTO [dbo].[Schema_Version] ([Version], [Name])
SELECT 9, N'0009_2026-11-22_q1_quest'
WHERE NOT EXISTS (SELECT 1 FROM [dbo].[Schema_Version] WHERE [Version] = 9);

COMMIT TRAN;
