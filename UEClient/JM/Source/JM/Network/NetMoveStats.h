#pragma once

#include "CoreMinimal.h"

// 원격 플레이어 이동 표시 측정(MC4.1b, 개발용). 콘솔 "NetMoveStats"가 결과를 로그에 찍고 초기화한다.
// - 화면 속도 비율 = 프레임 이동량 ÷ dt ÷ 이동 스탯. 움직인 프레임만 모은다(순간이동 보정 프레임 제외).
// - 패킷 오차 = 새 위치 패킷이 도착한 순간 화면 위치와 받은 위치의 2D 거리.
struct FNetMoveStats
{
	static FNetMoveStats& Get();

	void AddFrame(float SpeedRatio);
	void AddPacketError(float ErrorCm);
	void AddSnap() { ++Snaps; }
	void AddSend() { if (StartTime == 0.0) StartTime = FPlatformTime::Seconds(); ++Sends; }   // 내 이동 패킷 송신 수(A 수정의 비용 확인)
	// 결과 문자열(로그용). bReset이면 초기화
	FString Report(bool bReset);

private:
	TArray<float> Ratios;
	int64 OverCount = 0;       // 1.2배 초과 프레임
	double ErrorSum = 0.0;
	float ErrorMax = 0.f;
	int64 ErrorCount = 0;
	int64 Snaps = 0;
	int64 Sends = 0;
	double StartTime = 0.0;
};
