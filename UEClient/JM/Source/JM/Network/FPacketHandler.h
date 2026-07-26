#pragma once
#include "CoreMinimal.h"

class NetworkSession;

class FPacketHandler
{
public:
	static void ProcessPacket(const TSharedPtr<NetworkSession>& session, BYTE* buffer, int len);

};

