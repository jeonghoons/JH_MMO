#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Protocol/Protocol.pb.h"
#include "JMPartyManager.generated.h"

// 파티원 한 명. 다른 Room에 있을 수 있어(화면에 액터가 없음) 이름·레벨·있는 곳을 서버가 보낸 값으로 갖는다.
struct FJMPartyMember
{
	int32 ObjectId = 0;
	FString Name;
	int32 Level = 0;
	Protocol::RoomKind Kind = Protocol::ROOM_KIND_NONE;
};

DECLARE_MULTICAST_DELEGATE(FOnJMPartyChanged);

// 파티의 클라이언트 사본 (DG2, D-217 — DESIGN_PARTY_DUNGEON §5).
// 서버가 보낸 상태(SC_PARTY_STATE)와 받은 초대(SC_PARTY_INVITE)를 그대로 담고, 조작은 요청만 보낸다. 화면(UPartyWidget)은 서버 상태가 와야 바뀐다.
// 거부·초대 결과(SC_PARTY_RESULT)는 시스템 메시지 창으로 알린다.
// 파티는 Room 이동·레벨 전환(던전 DG3c)에도 이어지므로 레벨을 바꿀 때 비우지 않고, 로그인 화면으로 돌아갈 때만 비운다.
UCLASS()
class JM_API UJMPartyManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	void HandleInvite(const Protocol::SC_PARTY_INVITE_PACKET& Pkt);
	void HandleState(const Protocol::SC_PARTY_STATE_PACKET& Pkt);
	void HandleResult(Protocol::PacketId Request, Protocol::PartyResult Result);
	// 로그인 화면으로 돌아갈 때
	void Clear();

	// TargetId = 초대할 플레이어의 오브젝트 id(화면에 보이는 사람). 채널·파티장·정원 검증은 서버가 한다.
	void Invite(int32 TargetId);
	// 캐릭터 이름으로 초대(채팅 "/파티초대 이름" — 다른 Room에 있어도 된다)
	void InviteByName(const FString& Name);
	void Respond(bool bAccept);
	void Leave();

	bool HasInvite() const { return bHasInvite; }
	const FString& GetInviteFromName() const { return InviteFromName; }
	bool IsInParty() const { return Members.Num() > 0; }
	int32 GetLeaderId() const { return LeaderId; }
	const TArray<FJMPartyMember>& GetMembers() const { return Members; }
	// 내 오브젝트 id(내 캐릭터가 아직 없으면 0)
	int32 GetMyId() const;
	bool IsLeader() const { return IsInParty() && LeaderId == GetMyId(); }

	// 파티 구성·있는 곳·받은 초대가 바뀔 때마다
	FOnJMPartyChanged OnPartyChanged;

private:
	void SetWindowVisible(bool bVisible);
	void ShowMessage(const FText& Message);

	int32 PartyId = 0;
	int32 LeaderId = 0;
	TArray<FJMPartyMember> Members;

	bool bHasInvite = false;
	int32 InviteFromId = 0;
	FString InviteFromName;
	// 내가 탈퇴를 보냈다(그 결과로 오는 "파티 없음"은 안내하지 않는다)
	bool bSelfLeft = false;
};
