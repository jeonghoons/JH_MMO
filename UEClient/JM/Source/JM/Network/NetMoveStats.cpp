#include "Network/NetMoveStats.h"
#include "HAL/PlatformTime.h"

FNetMoveStats& FNetMoveStats::Get()
{
	static FNetMoveStats Instance;
	return Instance;
}

void FNetMoveStats::AddFrame(float SpeedRatio)
{
	if (StartTime == 0.0) StartTime = FPlatformTime::Seconds();
	Ratios.Add(SpeedRatio);
	if (SpeedRatio > 1.2f) ++OverCount;
}

void FNetMoveStats::AddPacketError(float ErrorCm)
{
	if (StartTime == 0.0) StartTime = FPlatformTime::Seconds();
	ErrorSum += ErrorCm;
	ErrorMax = FMath::Max(ErrorMax, ErrorCm);
	++ErrorCount;
}

FString FNetMoveStats::Report(bool bReset)
{
	TArray<float> Sorted = Ratios;
	Sorted.Sort();
	auto Pct = [&Sorted](double P) { return Sorted.Num() ? Sorted[FMath::Clamp(int32(P * (Sorted.Num() - 1)), 0, Sorted.Num() - 1)] : 0.f; };

	const double Elapsed = StartTime > 0.0 ? FPlatformTime::Seconds() - StartTime : 0.0;
	const FString Out = FString::Printf(
		TEXT("NetMoveStats %.0fs: frames=%d speedRatio p50/p99/max=%.2f/%.2f/%.2f over1.2=%.2f%% | packetErr avg/max=%.1f/%.1fcm n=%lld | snaps=%lld | mySends=%lld (%.1f/s)"),
		Elapsed, Sorted.Num(), Pct(0.5), Pct(0.99), Sorted.Num() ? Sorted.Last() : 0.f,
		Sorted.Num() ? 100.0 * OverCount / Sorted.Num() : 0.0,
		ErrorCount ? ErrorSum / ErrorCount : 0.0, ErrorMax, ErrorCount, Snaps, Sends, Elapsed > 0.0 ? Sends / Elapsed : 0.0);

	if (bReset) *this = FNetMoveStats();
	return Out;
}
