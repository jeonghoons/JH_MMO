-- Q1 회귀(run_regress.sh growth_*) 전용: B2(A2.3b) growth_test.py가 전제하는 시험 계정 상태를 맞춘다(서버가 꺼져 있을 때만).
--   Test1~Test4 = 1레벨·경험치 0, Test1은 HP 300(자연 회복 검사 — HP가 덜 찬 채 들어와야 한다), 나머지는 HP 650.
-- 회귀가 끝나면 restore_accounts.sql이 시험 전 상태(Test1 30레벨 등)로 되돌린다.
SET NOCOUNT ON;
UPDATE P SET [Level] = 1, Exp = 0, Hp = CASE A.LoginId WHEN N'Test1' THEN 300 ELSE 650 END
FROM [Player_Info] P INNER JOIN [User_Account] A ON A.AccountId = P.AccountId
WHERE A.LoginId IN (N'Test1', N'Test2', N'Test3', N'Test4');
SELECT @@ROWCOUNT AS Updated;
