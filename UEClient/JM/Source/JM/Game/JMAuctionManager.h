#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Protocol/Protocol.pb.h"
#include "JMAuctionManager.generated.h"

// 경매 등록 한 건(검색 결과·내 등록). 서버가 보낸 값이고, 검색 결과의 줄은 구매 응답으로만 고친다(AU2, D-281).
struct FJMAuctionListing
{
	int64 ListingId = 0;
	int32 ItemId = 0;
	int64 Count = 0;       // 남은 수량
	int64 UnitPrice = 0;   // 1개당 가격
	int32 RemainSec = 0;   // 받은 시점에 만료까지 남은 초
	bool bMine = false;
	int64 CursorKey = 0;   // 이 줄의 정렬 값(SC_AUCTION_LIST.cursor_keys) — 다음 묶음을 받을 때 커서로 보낸다
	bool bClosed = false;  // 구매 응답으로 끝난 것을 알게 된 줄(다 팔림·만료·취소) — 회색 "판매 완료"로 남긴다

	int64 TotalPrice() const { return Count * UnitPrice; }
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

// 경매 창의 클라이언트 상태 (A6.3b D-234 → AU2b D-281, DESIGN_ITEMS_TRADE "경매장").
// 서버가 보낸 검색 결과·내 등록·수령함을 담아 창(UAuctionWidget)이 그리게 하고, 조작은 요청만 보낸다.
// 골드·아이템은 UJMInventoryManager가 맡는다.
//
// 검색(AU2): 서버는 한 번에 여러 쪽 분량(50줄)을 주고, 받은 줄은 모두 쌓아 둔다(줄 캐시). 쪽은 캐시 안에서 PageRows줄씩 나눈 것이고
//   캐시 끝을 넘는 쪽으로 가면 마지막 줄의 커서로 다음 묶음(MORE)을 받는다. 보고 있는 목록은 갱신하지 않으며 구매 응답으로 그 줄만 고친다.
// 요청 간격: 검색·새로고침(SEARCH)은 SearchInterval, 정렬·다음 묶음·구매·등록·취소·수령은 ActionInterval을 함께 쓰고,
//   내 목록은 같은 길이의 간격을 따로 갖는다(자동으로 나가는 내 목록 요청이 사용자의 등록·구매를 막지 않게). 서버가 검사하고 값은 SC_AUCTION_LIST로 온다.
//   간격 안에는 보내지 않는다(창이 버튼을 꺼 둔다). 내 목록 요청만 버리지 않고 간격이 풀릴 때 보낸다.
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

	// NpcId = 경매 NPC의 오브젝트 id(0 = 조회 모드). 창을 띄운다. 검색 간격이 지났으면 지난 검색어로 다시 찾고, 아니면 이전 결과를 그대로 보여 준다.
	void Open(int32 InNpcId);
	void Close();

	// 검색·새로고침(던전 밖 어디서나). 검색어는 앞뒤 공백을 떼고 20자까지. InSort < 0이면 지금 정렬 그대로. 간격 안이면 보내지 않고 false
	bool Search(const FString& InKeyword, int32 InSort = -1, bool bInDesc = false);
	// 정렬 변경(서버가 기억한 검색어로 다시 찾음). ToggleSort: 같은 열이면 방향 반대, 다른 열이면 오름차순
	bool Sort(int32 InSort, bool bInDesc);
	bool ToggleSort(int32 InSort);
	// 쪽 이동(1부터). 캐시에 있으면 바로 바뀌고, 없으면 다음 묶음을 요청해 응답이 오면 바뀐다
	void SetPage(int32 Page);
	void RequestMy();
	// 구매(어디서나)·등록·취소·수령(NPC 모드에서만 — 서버가 거리를 본다)
	void Buy(int64 ListingId, int32 Count);
	void Register(int64 ItemDbId, int32 Count, int64 UnitPrice);
	void Cancel(int64 ListingId);
	void Claim(int64 MailId);
	// NPC 모드에서 인벤토리 아이템을 우클릭하면 등록 후보로만 고른다([등록] 버튼으로 확정한다)
	void SetRegisterCandidate(int64 ItemDbId);
	// 내 위치·상태로 창을 계속 열어 둘 수 있는지 본다
	void UpdateFromPlayer(const FVector& PlayerLocation, bool bDead);

	bool IsOpen() const { return bOpen; }
	bool IsAtNpc() const { return bOpen && NpcId != 0; }
	int32 GetNpcId() const { return NpcId; }

	// ---- 검색 결과 ----
	const FString& GetKeyword() const { return Keyword; }
	int32 GetSort() const { return SortKind; }
	bool IsSortDesc() const { return bSortDesc; }
	int32 GetPage() const { return CurrentPage; }
	int32 GetPageRows() const { return PageRows; }
	int32 GetPageCount() const;
	int32 GetSearchTotal() const { return SearchTotal; }
	// 받은 줄 전부(캐시)와 지금 쪽의 범위 [First, First + Num)
	const TArray<FJMAuctionListing>& GetListings() const { return Listings; }
	void GetPageRange(int32& OutFirst, int32& OutNum) const;
	const FJMAuctionListing* FindListing(int64 ListingId) const;
	bool HasSearchResult() const { return bHasResult; }
	bool IsWaitingMore() const { return bWaitingMore; }
	// 다음 쪽으로 갈 수 있다(캐시에 있거나, 다음 묶음을 지금 요청할 수 있다)
	bool CanNextPage() const;

	// ---- 요청 간격 ----
	bool CanSearch() const;
	bool CanAct() const;
	// 검색을 다시 보낼 수 있을 때까지 남은 초(0 = 가능)
	float SearchWaitSec() const;

	const TArray<FJMAuctionListing>& GetMyListings() const { return MyListings; }
	const TArray<FJMAuctionMail>& GetMails() const { return Mails; }
	bool HasMoreMail() const { return bMailMore; }
	// 서버가 SC_AUCTION_MY로 알려 준 값(받기 전에는 0): 1인 등록 수 상한·1개당 가격 범위
	int32 GetMaxListings() const { return MaxListings; }
	int64 GetMinPrice() const { return MinPrice; }
	int64 GetMaxPrice() const { return MaxPrice; }
	int64 GetRegisterCandidate() const { return RegisterCandidate; }
	// 창 아래에 보이는 한 줄(마지막 성공 안내). 거부·실패는 시스템 메시지 창으로 띄운다.
	const FString& GetStatusText() const { return StatusText; }

	// 열림·닫힘·목록·쪽·등록 후보·상태 글자가 바뀔 때마다
	FOnJMAuctionChanged OnAuctionChanged;

private:
	void SetWindowVisible(bool bVisible);
	void ShowMessage(const FText& Message);
	bool SendSearch(Protocol::AuctionQuery Query, int64 CursorKey, int64 CursorId);
	// 내 목록 요청을 보낸다. 요청 간격 안이면 간격이 풀리는 때에 맞춰 타이머로 다시 부른다
	void FlushMy();
	void MarkAction();
	FJMAuctionListing* FindListingMutable(int64 ListingId);

	bool bOpen = false;
	int32 NpcId = 0;

	// 검색 조건(창을 닫아도 남는다)과 줄 캐시
	FString Keyword;
	int32 SortKind = 0;      // Protocol::AuctionSort
	bool bSortDesc = false;
	int32 CurrentPage = 1;
	int32 SearchTotal = 0;
	int32 PageRows = 10;
	bool bHasMore = false;
	bool bHasResult = false;
	TArray<FJMAuctionListing> Listings;

	// 마지막으로 보낸 검색 요청(늦게 온 다른 요청의 응답을 버리는 데 쓴다)과 다음 묶음을 기다리는 쪽
	int32 RequestedQuery = 0;
	int32 RequestedSort = 0;
	bool bRequestedDesc = false;
	FString RequestedKeyword;
	bool bWaitingMore = false;
	int32 PendingPage = 0;

	// 요청 간격(초, 서버 값)과 다음에 보낼 수 있는 시각(FPlatformTime::Seconds)
	double SearchIntervalSec = 5.0;
	double ActionIntervalSec = 1.0;
	double NextSearchTime = 0.0;
	double NextActionTime = 0.0;
	double NextMyTime = 0.0;
	bool bMyPending = false;
	FTimerHandle MyTimer;

	TArray<FJMAuctionListing> MyListings;
	TArray<FJMAuctionMail> Mails;
	bool bMailMore = false;
	int32 MaxListings = 0;
	int64 MinPrice = 0;
	int64 MaxPrice = 0;

	int64 RegisterCandidate = 0;
	int64 LastBuyListingId = 0;
	FString StatusText;
	bool bHasMailNotice = false;
};
