#include "Game/JMPartyManager.h"
#include "Game/JMObjectManager.h"
#include "Player/JMMyPlayer.h"
#include "Network/NetworkManager.h"
#include "UI/JMUIManager.h"
#include "UI/WidgetNames.h"

namespace
{
	// 거부·초대 결과(SC_PARTY_RESULT) 문구
	FText PartyResultText(Protocol::PartyResult Result)
	{
		switch (Result)
		{
		case Protocol::PARTY_RESULT_NO_TARGET:			return FText::FromString(TEXT("초대할 수 없는 대상입니다."));
		case Protocol::PARTY_RESULT_OTHER_CHANNEL:		return FText::FromString(TEXT("다른 채널에 있는 사람은 초대할 수 없습니다."));
		case Protocol::PARTY_RESULT_TARGET_IN_PARTY:	return FText::FromString(TEXT("상대가 이미 파티에 들어 있습니다."));
		case Protocol::PARTY_RESULT_TARGET_BUSY:		return FText::FromString(TEXT("상대가 다른 초대에 답하는 중입니다."));
		case Protocol::PARTY_RESULT_PARTY_FULL:			return FText::FromString(TEXT("파티가 가득 찼습니다."));
		case Protocol::PARTY_RESULT_NOT_LEADER:			return FText::FromString(TEXT("파티장만 초대할 수 있습니다."));
		case Protocol::PARTY_RESULT_IN_DUNGEON:			return FText::FromString(TEXT("던전 안에서는 초대하거나 초대받을 수 없습니다."));
		case Protocol::PARTY_RESULT_DECLINED:			return FText::FromString(TEXT("상대가 파티 초대를 거절했습니다."));
		case Protocol::PARTY_RESULT_TIMEOUT:			return FText::FromString(TEXT("응답 시간이 지나 파티 초대가 취소되었습니다."));
		case Protocol::PARTY_RESULT_NO_INVITE:			return FText::FromString(TEXT("파티 초대가 취소되었거나 이미 끝났습니다."));
		case Protocol::PARTY_RESULT_NOT_IN_PARTY:		return FText::FromString(TEXT("파티에 들어 있지 않습니다."));
		case Protocol::PARTY_RESULT_TOO_FREQUENT:		return FText::FromString(TEXT("잠시 후 다시 초대해 주세요."));
		default:										return FText::FromString(TEXT("파티 요청을 처리하지 못했습니다."));
		}
	}
}

int32 UJMPartyManager::GetMyId() const
{
	// 내 오브젝트 id = 로그인 때 받은 NetworkId. 내 캐릭터 액터는 레벨 전환 중(던전 입장·퇴장)에 잠깐 없으므로 액터에서 읽지 않는다(DG3c-2).
	const UGameInstance* GameInstance = GetGameInstance();
	const UNetworkManager* NetManager = GameInstance ? GameInstance->GetSubsystem<UNetworkManager>() : nullptr;
	return (NetManager && NetManager->NetworkId > 0) ? NetManager->NetworkId : 0;
}

void UJMPartyManager::HandleInvite(const Protocol::SC_PARTY_INVITE_PACKET& Pkt)
{
	bHasInvite = true;
	InviteFromId = Pkt.from_id();
	InviteFromName = FString(UTF8_TO_TCHAR(Pkt.from_name().c_str()));
	UE_LOG(LogTemp, Log, TEXT("Party invite from %d (%s), timeout %d sec"), InviteFromId, *InviteFromName, Pkt.timeout_sec());

	// 수락·거절을 누를 수 있게 파티 창을 띄운다.
	SetWindowVisible(true);
	OnPartyChanged.Broadcast();
}

void UJMPartyManager::HandleState(const Protocol::SC_PARTY_STATE_PACKET& Pkt)
{
	const bool bWasInParty = IsInParty();

	PartyId = Pkt.members_size() > 0 ? Pkt.party_id() : 0;
	LeaderId = Pkt.members_size() > 0 ? Pkt.leader_id() : 0;
	Members.Reset();
	for (const Protocol::PartyMember& In : Pkt.members())
	{
		FJMPartyMember Member;
		Member.ObjectId = In.id();
		Member.Name = FString(UTF8_TO_TCHAR(In.name().c_str()));
		Member.Level = In.level();
		Member.Kind = In.kind();
		Members.Add(MoveTemp(Member));
	}
	UE_LOG(LogTemp, Log, TEXT("Party state: party=%d leader=%d members=%d"), PartyId, LeaderId, Members.Num());

	const bool bNowInParty = IsInParty();
	if (bNowInParty)
	{
		// 파티에 들면 받아 둔 초대는 서버에서도 지워진다.
		bHasInvite = false;
		bSelfLeft = false;
		// 처음 파티가 맺어졌을 때 한 번 창을 보여 준다(초대한 쪽도 성사를 알 수 있게). 그 뒤에는 P 키로 여닫는다.
		if (!bWasInParty) SetWindowVisible(true);
	}
	OnPartyChanged.Broadcast();

	if (bWasInParty && !bNowInParty)
	{
		if (!bSelfLeft) ShowMessage(FText::FromString(TEXT("파티가 해산되었습니다.")), true);
		bSelfLeft = false;
	}
}

void UJMPartyManager::HandleResult(Protocol::PacketId Request, Protocol::PartyResult Result)
{
	UE_LOG(LogTemp, Log, TEXT("Party result: request=%d result=%d"), static_cast<int32>(Request), static_cast<int32>(Result));

	// 초대받은 쪽에게 온 결과(시간 초과·초대한 사람이 떠남·답할 초대 없음): 초대 화면을 닫는다.
	if (Request == Protocol::CS_PARTY_RESPOND && bHasInvite)
	{
		bHasInvite = false;
		if (!IsInParty()) SetWindowVisible(false);
		OnPartyChanged.Broadcast();
	}
	if (Request == Protocol::CS_PARTY_LEAVE) bSelfLeft = false;
	// 상대의 응답·시간 경과로 끝난 것은 알림 줄, 요청이 막힌 것은 확인 창
	const bool bNotice = Result == Protocol::PARTY_RESULT_DECLINED || Result == Protocol::PARTY_RESULT_TIMEOUT || Result == Protocol::PARTY_RESULT_NO_INVITE;
	ShowMessage(PartyResultText(Result), bNotice);
}

void UJMPartyManager::Clear()
{
	PartyId = 0;
	LeaderId = 0;
	Members.Reset();
	bHasInvite = false;
	InviteFromId = 0;
	InviteFromName.Reset();
	bSelfLeft = false;
	OnPartyChanged.Broadcast();
}

void UJMPartyManager::Invite(int32 TargetId)
{
	if (TargetId <= 0) return;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendPartyInvite(TargetId, FString());
}

void UJMPartyManager::InviteByName(const FString& Name)
{
	const FString Trimmed = Name.TrimStartAndEnd();
	if (Trimmed.IsEmpty()) return;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendPartyInvite(0, Trimmed);
}

void UJMPartyManager::Respond(bool bAccept)
{
	if (!bHasInvite) return;
	// 화면은 바로 닫는다: 수락이면 SC_PARTY_STATE가, 실패면 SC_PARTY_RESULT가 온다. 거절은 응답이 없다.
	bHasInvite = false;
	if (!bAccept && !IsInParty()) SetWindowVisible(false);
	OnPartyChanged.Broadcast();
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendPartyRespond(bAccept);
}

void UJMPartyManager::Leave()
{
	if (!IsInParty()) return;
	bSelfLeft = true;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendPartyLeave();
}

void UJMPartyManager::SetWindowVisible(bool bVisible)
{
	if (UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr)
	{
		UIManager->SetWidgetVisible(WidgetNames::Party, bVisible);
	}
}

void UJMPartyManager::ShowMessage(const FText& Message, bool bNotice)
{
	if (UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr)
	{
		if (bNotice) UIManager->ShowNotice(Message);
		else UIManager->ShowSystemMessage(Message);
	}
}
