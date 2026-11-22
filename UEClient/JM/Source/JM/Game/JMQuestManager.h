#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Protocol/Protocol.pb.h"
#include "JMQuestManager.generated.h"

// 내 퀘스트 한 개. 서버가 보낸 값(QuestInfo) 그대로다. 받지 않은 퀘스트는 목록에 없다(= NONE).
struct FJMQuest
{
	int32 QuestId = 0;
	Protocol::QuestState State = Protocol::QUEST_STATE_NONE;
	int32 Progress = 0;   // 처치형의 처치 수. 수집형은 서버가 0을 보낸다(인벤토리 수량으로 본다)
};

DECLARE_MULTICAST_DELEGATE(FOnJMQuestChanged);

// 퀘스트의 클라이언트 사본과 퀘스트 창 상태 (Q1, D-261·D-264·D-265).
// 서버가 보낸 내 퀘스트(SC_QUEST_LIST = 첫 입장 때 전부, SC_QUEST_UPDATE = 한 개)를 그대로 담고, 수락·완료는 요청(CS_QUEST_REQUEST)만 보낸다.
// 어느 쪽인지·보상은 서버가 정하고, 창·HUD·NPC 표식은 서버 패킷이 와야 바뀐다. 거부(SC_QUEST_RESULT)는 시스템 메시지, 수락·목표 달성·완료는 사라지는 알림 줄 + 채팅 한 줄(D-266).
// 서버는 퀘스트 목록을 첫 입장 때만 보내므로 Room 이동·레벨 전환에는 비우지 않고 로그인 화면으로 돌아갈 때만 비운다(파티 사본과 같음).
//
// 창(UQuestWidget)을 여는 방법 두 가지(D-265):
//   - 퀘스트 NPC 앞 F 키(OpenForNpc) = 대화 모드: 그 NPC의 퀘스트 전부 + [수락]/[완료] 버튼. 멀어지거나 죽으면 닫는다.
//   - L 키(OpenLog) = 목록 모드: 받은 퀘스트(진행 중·완료)만 보기. 버튼 없음.
UCLASS()
class JM_API UJMQuestManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void HandleList(const Protocol::SC_QUEST_LIST_PACKET& Pkt);
	void HandleUpdate(const Protocol::QuestInfo& Info);
	void HandleResult(int32 QuestId, Protocol::QuestResult Result);
	// 로그인 화면으로 돌아갈 때(사본까지 비운다)
	void Clear();
	// 레벨이 바뀔 때: 창 위젯이 레벨과 함께 사라지므로 열림 상태만 내린다(사본은 남긴다)
	void ResetWindow();

	// NpcObjectId = 퀘스트 NPC의 오브젝트 id. 그 NPC의 첫 퀘스트(완료할 수 있는 것 > 받을 수 있는 것 > 진행 중 > 완료)를 골라 둔다.
	void OpenForNpc(int32 NpcObjectId);
	void OpenLog();
	void Close();
	void SelectQuest(int32 QuestId);
	// 고른 퀘스트로 요청을 보낸다(대화 모드에서만 — 거리·상태 검증은 서버)
	void RequestSelected();
	// 개발용 콘솔(Quest): 지금 창의 NPC(없으면 NpcObjectId)로 그 퀘스트를 요청한다
	void Request(int32 NpcObjectId, int32 QuestId);
	// 내 위치·상태로 창을 계속 열어 둘 수 있는지 본다(AJMMyPlayer가 0.1초마다 부른다)
	void UpdateFromPlayer(const FVector& PlayerLocation, bool bDead);

	bool IsOpen() const { return bOpen; }
	bool IsAtNpc() const { return bOpen && NpcObjectId != 0; }
	int32 GetNpcObjectId() const { return NpcObjectId; }
	int32 GetSelectedQuestId() const { return SelectedQuestId; }

	Protocol::QuestState GetState(int32 QuestId) const;
	// 화면에 보이는 진행 수: 처치형 = 서버 값, 수집형 = 인벤토리 칸의 수량(목표 수를 넘으면 목표 수)
	int32 GetProgress(int32 QuestId) const;
	// 진행 중이고 목표를 채웠다(완료 요청이 통과할 것으로 보이는 상태 — 최종 판정은 서버)
	bool IsReadyToComplete(int32 QuestId) const;
	// 창에 보일 퀘스트 ID들: 대화 모드 = 그 NPC의 퀘스트 전부, 목록 모드 = 받은 것(진행 중 먼저, 그다음 완료)
	TArray<int32> GetWindowQuestIds() const;
	// 진행 중인 퀘스트 ID들(ID 순) — HUD 추적 줄
	TArray<int32> GetActiveQuestIds() const;
	// NPC 머리 위 표식: 완료할 수 있는 퀘스트가 있으면 "?", 받을 수 있는 퀘스트가 있으면 "!", 없으면 빈 글
	FString GetNpcMarker(int32 NpcTemplateId) const;

	// 퀘스트 상태·진행·창(열림·고른 퀘스트)이 바뀔 때, 그리고 수집형 진행에 쓰는 인벤토리가 바뀔 때마다
	FOnJMQuestChanged OnQuestChanged;

private:
	int32 GetNpcTemplateId() const;
	FString QuestName(int32 QuestId) const;
	void SetWindowVisible(bool bVisible);
	void ShowMessage(const FText& Message);
	// 수락·목표 달성·완료 안내: 채팅 한 줄(Message) + 사라지는 알림 줄(Notice, D-266)
	void Announce(const FString& Message, const FString& Notice);
	void OnInventoryChanged();

	TMap<int32, FJMQuest> Quests;

	bool bOpen = false;
	int32 NpcObjectId = 0;
	int32 SelectedQuestId = 0;

	FDelegateHandle InventoryChangedHandle;
};
