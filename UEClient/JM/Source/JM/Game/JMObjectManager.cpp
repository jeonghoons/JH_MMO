#include "Game/JMObjectManager.h"
#include "JMGameInstance.h"
#include "Network/NetworkManager.h"
#include "Player/JMMyPlayer.h"
#include "Player/JMPlayer.h"
#include "Player/NpcCharaceter.h"
#include "Engine/World.h"
#include "Player/JMPlayerController.h"
#include "Kismet/GameplayStatics.h"

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

	AJMCharacterBase* SpawnedActor = nullptr;

	if (ObjectId == NetManager->NetworkId)
	{
		if (GameInst->MyPlayerClass)
		{
			AJMMyPlayer* MyCharacter = World->SpawnActor<AJMMyPlayer>(GameInst->MyPlayerClass, SpawnLoc, SpawnRot);
			MyPlayer = MyCharacter;
			SpawnedActor = MyCharacter;

			if (AJMPlayerController* PC = Cast<AJMPlayerController>(UGameplayStatics::GetPlayerController(this, 0)))
			{
				PC->Possess(MyCharacter);
			}
		}
	}
	else if (ObjInfo.player_type() == Protocol::PLAYER_TYPE_MONSTER)
	{
		if (GameInst->NpcCharacterClass)
		{
			SpawnedActor = World->SpawnActor<ANpcCharaceter>(GameInst->NpcCharacterClass, SpawnLoc, SpawnRot);
		}
	}
	else
	{
		if (GameInst->PlayerClass)
		{
			SpawnedActor = World->SpawnActor<AJMPlayer>(GameInst->PlayerClass, SpawnLoc, SpawnRot);
		}
	}

	if (SpawnedActor)
	{
		SpawnedActor->SetPlayerData(ObjInfo);
		Objects.Add(ObjectId, SpawnedActor);
	}
}

void UJMObjectManager::HandleDespawn(int32 ObjectId)
{
	if (TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(ObjectId))
	{
		if (*FoundActor != nullptr)
		{
			(*FoundActor)->Destroy();
		}
		Objects.Remove(ObjectId);
	}
}

void UJMObjectManager::HandleMove(const Protocol::ObjectInfo& ObjInfo)
{
	int ObjectId = ObjInfo.id();
	if (TObjectPtr<AJMCharacterBase>* FoundActor = Objects.Find(ObjectId))
	{
		if (*FoundActor == nullptr) return;
		if (*FoundActor == MyPlayer) return; 

		(*FoundActor)->SetDestInfo(ObjInfo.position());
	}
}
