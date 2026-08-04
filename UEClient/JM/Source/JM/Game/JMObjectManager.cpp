#include "Game/JMObjectManager.h"
#include "JMGameInstance.h"
#include "Network/NetworkManager.h"
#include "Player/JMMyPlayer.h"
#include "Player/JMPlayer.h"
#include "Player/NpcCharaceter.h"
#include "Engine/World.h"

void UJMObjectManager::ClearObjects()
{
	Objects.Empty();
	MyPlayer = nullptr;
}

void UJMObjectManager::HandleSpawn(const Protocol::ObjectInfo& ObjInfo)
{
	UWorld* World = GetWorld();
	if (World == nullptr) return;

	int ObjectId = ObjInfo.id();

	if (Objects.Contains(ObjectId)) return;

	UJMGameInstance* GameInst = Cast<UJMGameInstance>(GetGameInstance());
	UNetworkManager* NetManager = GetGameInstance()->GetSubsystem<UNetworkManager>();
	if (GameInst == nullptr || NetManager == nullptr) return;

	FVector SpawnLoc(ObjInfo.position().x(), ObjInfo.position().y(), ObjInfo.position().z());
	FRotator SpawnRot(0, ObjInfo.position().yaw(), 0);

	AActor* SpawnedActor = nullptr;

	if (ObjectId == NetManager->NetworkId)
	{
		if (GameInst->MyPlayerClass)
		{
			AJMMyPlayer* MyCharacter = World->SpawnActor<AJMMyPlayer>(GameInst->MyPlayerClass, SpawnLoc, SpawnRot);
			MyCharacter->SetPlayerData(ObjInfo);
			MyPlayer = MyCharacter;
			SpawnedActor = MyCharacter;
		}
	}
	else if (ObjInfo.player_type() == Protocol::PLAYER_TYPE_MONSTER)
	{
		if (GameInst->NpcCharacterClass)
		{
			ANpcCharaceter* NpcCharacter = World->SpawnActor<ANpcCharaceter>(GameInst->NpcCharacterClass, SpawnLoc, SpawnRot);
			NpcCharacter->SetPlayerData(ObjInfo);
			SpawnedActor = NpcCharacter;
		}
	}
	else
	{
		if (GameInst->PlayerClass)
		{
			AJMPlayer* OtherCharacter = World->SpawnActor<AJMPlayer>(GameInst->PlayerClass, SpawnLoc, SpawnRot);
			OtherCharacter->SetPlayerData(ObjInfo);
			SpawnedActor = OtherCharacter;
		}
	}

	if (SpawnedActor)
	{
		Objects.Add(ObjectId, SpawnedActor);
	}
}

void UJMObjectManager::HandleDespawn(int32 ObjectId)
{
	if (TObjectPtr<AActor>* FoundActor = Objects.Find(ObjectId))
	{
		if (*FoundActor != nullptr)
		{
			(*FoundActor)->Destroy();
		}
		Objects.Remove(ObjectId);
	}
}
