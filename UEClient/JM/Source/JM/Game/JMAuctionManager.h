#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Protocol/Protocol.pb.h"
#include "JMAuctionManager.generated.h"

// 경매 등록 한 건(검색 결과·내 등록). 서버가 보낸 값 그대로다.
struct FJMAuctionListing
{
	int64 ListingId = 0;
	int32 ItemId = 0;
	int64 Count = 0;
	int64 Price = 0;       // 묶음 전체의 값
	int32 RemainSec = 0;   // 받은 시점에 만료까지 남은 초
	bool bMine = false;
};

// 수령함 한 건. Kind = Protocol::MailKind(1 판매 대금 · 2 구매품 · 3 만료 반환 · 4 취소 반환)
struct FJMAuctionMail
{
	int64 MailId = 0;
	int32 Kind = 0;
	int64 Gold = 0;
	int32 ItemId = 0;
	int64 Count = 0;
	int64 ListingId = 0;
};

DECLARE_MULTICAST_DELEGATE(FOnJMAuctionChanged);

// 경매 창의 클라이언트 상태 (A6.3b, D-159·D-234, DESIGN_ITEMS_TRADE "경매장").
// 서버가 보낸 검색 결과·내 등록·수령함을 그대로 담아 창(UAuctionWidget)이 그리게 하고, 조작은 요청만 보낸다.
// 화면은 서버 응답(SC_AUCTION_LIST·SC_AUCTION_MY·SC_AUCTION_RESULT)이 와야 바뀐다. 골드·아이템은 UJMInventoryManager가 맡는다.
//
// 여는 방법 두 가지(D-234):
//   - K 키(Open(0)) = 조회 모드: 검색·구매·내 등록/수령함 보기. 던전 밖 어디서나.
//   - 경매 NPC 앞 F 키(Open(NpcId)) = NPC 모드: 위에 더해 등록·취소·수령. 인벤토리 창도 함께 연다.
// 서버에는 "창을 연 상태"가 없고 요청마다 검증한다. NPC에서 멀어지거나 죽거나 거래·상점이 열리면 창을 닫는다
// (AJMMyPlayer가 0.1초마다 UpdateFromPlayer를 부른다).
UCLASS()
class JM_API UJMAuctionManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	void HandleList(const Protocol::SC_AUCTION_LIST_PACKET& Pkt);
	void HandleMy(const Protocol::SC_AUCTION_MY_PACKET& Pkt);
	void HandleResult(const Protocol::SC_AUCTION_RESULT_PACKET& Pkt);
	// 수령함 알림(SC_MAIL_NOTICE, D-262·D-265): 판매·만료면 시스템 메시지 + 채팅 한 줄, 로그인 때(NONE)는 HUD 표시만
	void HandleMailNotice(const Protocol::SC_MAIL_NOTICE_PACKET& Pkt);
	// 레벨 전환·로그인 화면으로 돌아갈 때 비운다(수령함 알림 표시는 남긴다 — 서버가 첫 입장 때만 알려 준다)
	void Clear();
	// 로그인 화면으로 돌아갈 때 수령함 알림 표시를 내린다
	void ClearMailNotice();
	// 수령함에 받을 것이 있다(알림을 받았고, 그 뒤 받은 수령함 목록이 비어 있지 않다). HUD "수령함에 받을 것이 있습니다"
	bool HasMailNotice() const { return bHasMailNotice; }

	// NpcId = 경매 NPC의 오브젝트 id(0 = 조회 모드). 창을 띄우고 지금 필터의 첫 쪽을 요청한다.
	void Open(int32 InNpcId);
	void Close();

	// 조회(던전 밖 어디서나). Page는 0부터
	void RequestSearch(int32 Slot, int32 Page);
	void RequestMy();
	// 구매(어디서나)·등록·취소·수령(NPC 모드에서만 — 서버가 거리를 본다)
	void Buy(int64 ListingId);
	void Register(int64 ItemDbId, int32 Count, int64 Price);
	void Cancel(int64 ListingId);
	void Claim(int64 MailId);
	// NPC 모드에서 인벤토리 아이템을 우클릭하면 등록 후보로만 고른다([등록] 버튼으로 확정한다)
	void SetRegisterCandidate(int64 ItemDbId);
	// 내 위치·상태로 창을 계속 열어 둘 수 있는지 본다
	void UpdateFromPlayer(const FVector& PlayerLocation, bool bDead);

	bool IsOpen() const { return bOpen; }
	bool IsAtNpc() const { return bOpen && NpcId != 0; }
	int32 GetNpcId() const { return NpcId; }

	int32 GetSearchSlot() const { return SearchSlot; }
	int32 GetSearchPage() const { return SearchPage; }
	int32 GetSearchTotal() const { return SearchTotal; }
	int32 GetPageSize() const { return PageSize; }
	const TArray<FJMAuctionListing>& GetListings() const { return Listings; }
	const TArray<FJMAuctionListing>& GetMyListings() const { return MyListings; }
	const TArray<FJMAuctionMail>& GetMails() const { return Mails; }
	bool HasMoreMail() const { return bMailMore; }
	// 서버가 SC_AUCTION_MY로 알려 준 값(받기 전에는 0): 1인 등록 수 상한·가격 범위
	int32 GetMaxListings() const { return MaxListings; }
	int64 GetMinPrice() const { return MinPrice; }
	int64 GetMaxPrice() const { return MaxPrice; }
	int64 GetRegisterCandidate() const { return RegisterCandidate; }
	// 창 아래에 보이는 한 줄(마지막 성공 안내). 거부·실패는 시스템 메시지 창으로 띄운다.
	const FString& GetStatusText() const { return StatusText; }

	// 열림·닫힘·목록·등록 후보·상태 글자가 바뀔 때마다
	FOnJMAuctionChanged OnAuctionChanged;

private:
	void SetWindowVisible(bool bVisible);
	void ShowMessage(const FText& Message);
	// 서버의 요청 간격(TOO_FREQUENT)에 걸려 버려진 조회를 다시 보낸다. 조회가 버려지면 목록이 낡은 채 남기 때문이다
	// (예: 탭을 바꾼 직후 등록이 성공해 내 목록을 연달아 두 번 요청한 경우). 둘 다 걸렸으면 하나씩 보낸다.
	void RetryQueries();
	void ScheduleRetry();

	bool bOpen = false;
	int32 NpcId = 0;

	int32 SearchSlot = 0;
	int32 SearchPage = 0;
	int32 SearchTotal = 0;
	int32 PageSize = 20;
	TArray<FJMAuctionListing> Listings;

	TArray<FJMAuctionListing> MyListings;
	TArray<FJMAuctionMail> Mails;
	bool bMailMore = false;
	int32 MaxListings = 0;
	int64 MinPrice = 0;
	int64 MaxPrice = 0;

	int64 RegisterCandidate = 0;
	FString StatusText;
	bool bHasMailNotice = false;

	// 마지막으로 요청한 검색(다시 보낼 때 쓴다)과 다시 보낼 조회
	int32 RequestedSlot = 0;
	int32 RequestedPage = 0;
	bool bRetrySearch = false;
	bool bRetryMy = false;
	FTimerHandle RetryTimer;
};
