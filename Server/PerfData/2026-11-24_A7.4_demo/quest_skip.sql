-- A7.4 시연 촬영(D-311): 퀘스트 1 "초원 정리"(1001 15마리)의 반복 사냥을 건너뛴다 — Test2의 진행을 15로(상태 1 = 진행 중 유지).
-- Test2가 접속을 끊고 저장이 끝난 뒤 실행하고, 다시 로그인하면 경비대장에게 완료할 수 있다. 서버는 켜 둔 채로 실행한다(접속하지 않은 계정의 행만 고침).
SET NOCOUNT ON;
UPDATE Q SET Progress = 15
FROM [Player_Quest] Q
JOIN [Player_Info] I ON I.PlayerId = Q.PlayerId
JOIN [User_Account] A ON A.AccountId = I.AccountId
WHERE A.LoginId = N'Test2' AND Q.QuestId = 1 AND Q.State = 1;
SELECT A.LoginId, Q.QuestId, Q.State, Q.Progress
FROM [Player_Quest] Q JOIN [Player_Info] I ON I.PlayerId = Q.PlayerId JOIN [User_Account] A ON A.AccountId = I.AccountId
WHERE A.LoginId = N'Test2';
