-- A2.3b 시험 뒤 시험 계정(Test1~Test4)의 진행 상황을 시험 전 값(before_accounts.txt)으로 되돌린다. 서버가 꺼져 있을 때만 실행한다
-- (켜져 있으면 퇴장·주기 저장이 다시 덮어쓴다). 실행: sqlcmd -S .\SQLEXPRESS -d GameServer -E -I -i restore_db.sql
-- 레벨을 내리는 게임 안 경로는 없다(DEV 경험치는 0 이상만 받음) — 그래서 SQL로 직접 되돌린다. 아이템·골드는 시험이 바꾸지 않는다(활 장착은 시험 끝에 검으로 되돌림).
SET NOCOUNT ON;
SET XACT_ABORT ON;
BEGIN TRAN;
UPDATE P SET P.Level = V.Level, P.Exp = V.Exp, P.Hp = V.Hp, P.UpdatedAt = V.UpdatedAt
FROM [Player_Info] P
INNER JOIN [User_Account] A ON A.AccountId = P.AccountId
INNER JOIN (VALUES
    (N'Test1', 1, CAST(0 AS BIGINT), 300, CAST('2026-11-21 17:28:15' AS DATETIME2)),
    (N'Test2', 1, CAST(0 AS BIGINT), 650, CAST('2026-11-20 23:53:12' AS DATETIME2)),
    (N'Test3', 1, CAST(0 AS BIGINT), 530, CAST('2026-11-20 17:23:33' AS DATETIME2)),
    (N'Test4', 1, CAST(0 AS BIGINT), 530, CAST('2026-11-21 00:23:07' AS DATETIME2))
) AS V(LoginId, Level, Exp, Hp, UpdatedAt) ON V.LoginId = A.LoginId;
COMMIT TRAN;
SELECT A.LoginId, P.PlayerId, P.Level, P.Exp, P.Hp, P.Gold, P.UpdatedAt FROM [Player_Info] P INNER JOIN [User_Account] A ON A.AccountId = P.AccountId
WHERE A.LoginId IN (N'Test1', N'Test2', N'Test3', N'Test4') ORDER BY A.LoginId;
