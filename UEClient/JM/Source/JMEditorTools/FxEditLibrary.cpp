#include "FxEditLibrary.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotifyState_Trail.h"
#include "Dom/JsonObject.h"
#include "EditorAssetLibrary.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Particles/Location/ParticleModuleLocationBoneSocket.h"
#include "Particles/Location/ParticleModuleLocationSkelVertSurface.h"
#include "Particles/Material/ParticleModuleMeshMaterial.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/ParticleSystem.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Particles/TypeData/ParticleModuleTypeDataBase.h"
#include "Particles/TypeData/ParticleModuleTypeDataMesh.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectIterator.h"

namespace
{
	FString PathOrNull(const UObject* Obj)
	{
		return Obj ? Obj->GetPathName() : FString();
	}

	TSharedPtr<FJsonObject> DescribeModule(UParticleModule* Module)
	{
		TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
		if (!Module)
		{
			J->SetField(TEXT("class"), MakeShared<FJsonValueNull>());
			return J;
		}
		J->SetStringField(TEXT("class"), Module->GetClass()->GetName());
		J->SetStringField(TEXT("object"), Module->GetName());
		J->SetBoolField(TEXT("enabled"), Module->bEnabled != 0);

		if (const UParticleModuleLocationBoneSocket* BoneSocket = Cast<UParticleModuleLocationBoneSocket>(Module))
		{
			TArray<TSharedPtr<FJsonValue>> Bones;
			for (const FLocationBoneSocketInfo& Info : BoneSocket->SourceLocations)
			{
				Bones.Add(MakeShared<FJsonValueString>(Info.BoneSocketName.ToString()));
			}
			J->SetArrayField(TEXT("bones"), Bones);
			J->SetNumberField(TEXT("source_type"), static_cast<int32>(BoneSocket->SourceType));
		}
		else if (const UParticleModuleLocationSkelVertSurface* VertSurface = Cast<UParticleModuleLocationSkelVertSurface>(Module))
		{
			TArray<TSharedPtr<FJsonValue>> Bones;
			for (const FName& Bone : VertSurface->ValidAssociatedBones)
			{
				Bones.Add(MakeShared<FJsonValueString>(Bone.ToString()));
			}
			J->SetArrayField(TEXT("bones"), Bones);
			J->SetNumberField(TEXT("source_type"), static_cast<int32>(VertSurface->SourceType));
		}
		else if (const UParticleModuleTypeDataMesh* MeshData = Cast<UParticleModuleTypeDataMesh>(Module))
		{
			J->SetStringField(TEXT("mesh"), PathOrNull(MeshData->Mesh));
		}
		else if (const UParticleModuleMeshMaterial* MeshMat = Cast<UParticleModuleMeshMaterial>(Module))
		{
			TArray<TSharedPtr<FJsonValue>> Mats;
			for (const TObjectPtr<UMaterialInterface>& Mat : MeshMat->MeshMaterials)
			{
				Mats.Add(MakeShared<FJsonValueString>(PathOrNull(Mat)));
			}
			J->SetArrayField(TEXT("mesh_materials"), Mats);
		}
		return J;
	}

	TSharedPtr<FJsonObject> DescribeLOD(UParticleLODLevel* LOD)
	{
		TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetNumberField(TEXT("level"), LOD->Level);
		J->SetBoolField(TEXT("enabled"), LOD->bEnabled != 0);
		J->SetStringField(TEXT("type_data"), LOD->TypeDataModule ? LOD->TypeDataModule->GetClass()->GetName() : FString());

		TSharedPtr<FJsonObject> Req = MakeShared<FJsonObject>();
		if (LOD->RequiredModule)
		{
			Req->SetStringField(TEXT("material"), PathOrNull(LOD->RequiredModule->Material));
			Req->SetNumberField(TEXT("delay"), LOD->RequiredModule->EmitterDelay);
			Req->SetNumberField(TEXT("duration"), LOD->RequiredModule->EmitterDuration);
			Req->SetNumberField(TEXT("loops"), LOD->RequiredModule->EmitterLoops);
		}
		J->SetObjectField(TEXT("required"), Req);

		TArray<TSharedPtr<FJsonValue>> Modules;
		if (LOD->TypeDataModule)
		{
			Modules.Add(MakeShared<FJsonValueObject>(DescribeModule(LOD->TypeDataModule)));
		}
		for (UParticleModule* Module : LOD->Modules)
		{
			Modules.Add(MakeShared<FJsonValueObject>(DescribeModule(Module)));
		}
		J->SetArrayField(TEXT("modules"), Modules);
		return J;
	}

	// 남은 이미터들이 쓰는 모듈 전부(Required·Spawn·TypeData 포함)
	void CollectUsedModules(UParticleSystem* System, TSet<UObject*>& Out)
	{
		for (UParticleEmitter* Emitter : System->Emitters)
		{
			if (!Emitter) continue;
			for (UParticleLODLevel* LOD : Emitter->LODLevels)
			{
				if (!LOD) continue;
				Out.Add(LOD->RequiredModule);
				Out.Add(LOD->SpawnModule);
				Out.Add(LOD->TypeDataModule);
				for (UParticleModule* Module : LOD->Modules) Out.Add(Module);
			}
		}
	}
}

int32 UFxEditLibrary::FindEmitter(UParticleSystem* System, const FString& Emitter, FString& OutError)
{
	if (!System)
	{
		OutError = TEXT("실패: ParticleSystem이 없음");
		return INDEX_NONE;
	}
	if (Emitter.StartsWith(TEXT("#")))
	{
		const int32 Index = FCString::Atoi(*Emitter.Mid(1));
		if (Index < 0 || Index >= System->Emitters.Num())
		{
			OutError = FString::Printf(TEXT("실패: 인덱스 %d 범위 밖(이미터 %d개)"), Index, System->Emitters.Num());
			return INDEX_NONE;
		}
		return Index;
	}
	int32 Found = INDEX_NONE, Count = 0;
	for (int32 i = 0; i < System->Emitters.Num(); ++i)
	{
		if (System->Emitters[i] && System->Emitters[i]->EmitterName.ToString() == Emitter)
		{
			Found = i;
			++Count;
		}
	}
	if (Count != 1)
	{
		OutError = FString::Printf(TEXT("실패: 이미터 '%s' 매칭 %d개 — 이름 또는 '#인덱스'로 하나만"), *Emitter, Count);
		return INDEX_NONE;
	}
	return Found;
}

FString UFxEditLibrary::DescribeParticleSystem(UParticleSystem* System)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	if (!System)
	{
		Root->SetStringField(TEXT("error"), TEXT("ParticleSystem이 없음"));
	}
	else
	{
		Root->SetNumberField(TEXT("lod_method"), static_cast<int32>(System->LODMethod));
		TArray<TSharedPtr<FJsonValue>> Emitters;
		for (int32 i = 0; i < System->Emitters.Num(); ++i)
		{
			UParticleEmitter* Emitter = System->Emitters[i];
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetNumberField(TEXT("index"), i);
			if (!Emitter)
			{
				E->SetField(TEXT("name"), MakeShared<FJsonValueNull>());
			}
			else
			{
				E->SetStringField(TEXT("name"), Emitter->EmitterName.ToString());
				E->SetStringField(TEXT("object"), Emitter->GetName());
				TArray<TSharedPtr<FJsonValue>> LODs;
				for (UParticleLODLevel* LOD : Emitter->LODLevels)
				{
					if (LOD) LODs.Add(MakeShared<FJsonValueObject>(DescribeLOD(LOD)));
				}
				E->SetArrayField(TEXT("lods"), LODs);
			}
			Emitters.Add(MakeShared<FJsonValueObject>(E));
		}
		Root->SetArrayField(TEXT("emitters"), Emitters);
	}
	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Out;
}

FString UFxEditLibrary::RemoveEmitter(UParticleSystem* System, const FString& Emitter)
{
	FString Error;
	const int32 Index = FindEmitter(System, Emitter, Error);
	if (Index == INDEX_NONE) return Error;

	UParticleEmitter* Removed = System->Emitters[Index];
	const FString Name = Removed ? Removed->EmitterName.ToString() : TEXT("(null)");

	System->PreEditChange(nullptr);
	System->Modify();
	System->Emitters.RemoveAt(Index);

	// 어느 이미터도 안 쓰는 모듈(모듈의 outer는 시스템)과 뺀 이미터는 임시 패키지로 — 저장·참조 검사에서 빠진다(Paragon 머티리얼 의존도 함께 끊김).
	TSet<UObject*> Used;
	CollectUsedModules(System, Used);
	TArray<UObject*> Children;
	GetObjectsWithOuter(System, Children, EGetObjectsFlags::None);
	int32 Orphans = 0;
	for (UObject* Child : Children)
	{
		if (Child->IsA<UParticleModule>() && !Used.Contains(Child))
		{
			Child->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			Child->MarkAsGarbage();
			++Orphans;
		}
	}
	if (Removed)
	{
		Removed->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		Removed->MarkAsGarbage();
	}

	System->SetupSoloing();
	System->UpdateAllModuleLists();
	System->PostEditChange();
	System->MarkPackageDirty();
	return FString::Printf(TEXT("삭제 %s (남은 이미터 %d, 정리한 모듈 %d)"), *Name, System->Emitters.Num(), Orphans);
}

FString UFxEditLibrary::SetEmitterEnabled(UParticleSystem* System, const FString& Emitter, bool bEnabled)
{
	FString Error;
	const int32 Index = FindEmitter(System, Emitter, Error);
	if (Index == INDEX_NONE) return Error;
	UParticleEmitter* Target = System->Emitters[Index];
	if (!Target) return TEXT("실패: 이미터가 null");

	System->PreEditChange(nullptr);
	System->Modify();
	int32 Count = 0;
	for (UParticleLODLevel* LOD : Target->LODLevels)
	{
		if (!LOD) continue;
		LOD->Modify();
		LOD->bEnabled = bEnabled ? 1 : 0;
		++Count;
	}
	System->SetupSoloing();
	System->PostEditChange();
	System->MarkPackageDirty();
	return FString::Printf(TEXT("%s %s (LOD %d개)"), bEnabled ? TEXT("켬") : TEXT("끔"), *Target->EmitterName.ToString(), Count);
}

TArray<UObject*> UFxEditLibrary::GetEmitterModules(UParticleSystem* System, const FString& Emitter, int32 LOD)
{
	TArray<UObject*> Out;
	FString Error;
	const int32 Index = FindEmitter(System, Emitter, Error);
	if (Index == INDEX_NONE || !System->Emitters[Index]) return Out;

	const TArray<TObjectPtr<UParticleLODLevel>>& LODs = System->Emitters[Index]->LODLevels;
	for (int32 i = 0; i < LODs.Num(); ++i)
	{
		if (LOD >= 0 && i != LOD) continue;
		UParticleLODLevel* Level = LODs[i];
		if (!Level) continue;
		Out.AddUnique(Level->RequiredModule);
		Out.AddUnique(Level->SpawnModule);
		if (Level->TypeDataModule) Out.AddUnique(Level->TypeDataModule);
		for (UParticleModule* Module : Level->Modules)
		{
			if (Module) Out.AddUnique(Module);
		}
	}
	Out.Remove(nullptr);
	return Out;
}

void UFxEditLibrary::FinishEdit(UParticleSystem* System)
{
	if (!System) return;
	System->UpdateAllModuleLists();
	System->PostEditChange();
	System->MarkPackageDirty();
}

FString UFxEditLibrary::EditBones(UParticleSystem* System, const FString& Emitter, const FString& Mode, const TArray<FString>& Names, const TArray<FString>& NewNames)
{
	if (!System) return TEXT("실패: ParticleSystem이 없음");
	if (Mode != TEXT("rename") && Mode != TEXT("drop") && Mode != TEXT("set")) return TEXT("실패: Mode는 rename/drop/set");
	if (Mode == TEXT("rename") && Names.Num() != NewNames.Num()) return TEXT("실패: rename은 Names와 NewNames 개수가 같아야 함");

	TArray<UParticleEmitter*> Targets;
	if (Emitter.IsEmpty())
	{
		for (UParticleEmitter* E : System->Emitters) if (E) Targets.Add(E);
	}
	else
	{
		FString Error;
		const int32 Index = FindEmitter(System, Emitter, Error);
		if (Index == INDEX_NONE) return Error;
		if (System->Emitters[Index]) Targets.Add(System->Emitters[Index]);
	}

	TMap<FName, FName> Rename;
	TSet<FName> Drop;
	TArray<FName> SetTo;
	for (int32 i = 0; i < Names.Num(); ++i)
	{
		if (Mode == TEXT("rename")) Rename.Add(FName(*Names[i]), FName(*NewNames[i]));
		else if (Mode == TEXT("drop")) Drop.Add(FName(*Names[i]));
		else SetTo.Add(FName(*Names[i]));
	}

	auto Apply = [&](TArray<FName>& Bones) -> bool
	{
		TArray<FName> Old = Bones;
		if (Mode == TEXT("set"))
		{
			Bones = SetTo;
		}
		else
		{
			Bones.Reset();
			for (const FName& B : Old)
			{
				if (Mode == TEXT("drop") && Drop.Contains(B)) continue;
				const FName* R = (Mode == TEXT("rename")) ? Rename.Find(B) : nullptr;
				Bones.Add(R ? *R : B);
			}
		}
		return Old != Bones;
	};

	TSet<UParticleModule*> Done;   // 모듈은 이미터·LOD끼리 공유될 수 있어 한 번만
	FString Out;
	System->PreEditChange(nullptr);
	System->Modify();
	for (UParticleEmitter* E : Targets)
	{
		for (UParticleLODLevel* LOD : E->LODLevels)
		{
			if (!LOD) continue;
			for (UParticleModule* Module : LOD->Modules)
			{
				if (!Module || Done.Contains(Module)) continue;
				Done.Add(Module);
				if (UParticleModuleLocationBoneSocket* BoneSocket = Cast<UParticleModuleLocationBoneSocket>(Module))
				{
					TArray<FName> Bones;
					for (const FLocationBoneSocketInfo& Info : BoneSocket->SourceLocations) Bones.Add(Info.BoneSocketName);
					const int32 Before = Bones.Num();
					if (!Apply(Bones)) continue;
					BoneSocket->Modify();
					TArray<FLocationBoneSocketInfo> NewLocs;
					for (int32 i = 0; i < Bones.Num(); ++i)
					{
						// 기존 항목의 Offset은 같은 이름이 있으면 유지
						FLocationBoneSocketInfo Info;
						Info.BoneSocketName = Bones[i];
						for (const FLocationBoneSocketInfo& OldInfo : BoneSocket->SourceLocations)
						{
							if (OldInfo.BoneSocketName == Bones[i] || (Mode == TEXT("rename") && Rename.Contains(OldInfo.BoneSocketName) && Rename[OldInfo.BoneSocketName] == Bones[i]))
							{
								Info.Offset = OldInfo.Offset;
								break;
							}
						}
						NewLocs.Add(Info);
					}
					BoneSocket->SourceLocations = NewLocs;
					BoneSocket->SetSourceIndexMode();
					BoneSocket->PostEditChange();
					Out += FString::Printf(TEXT("%s:LocationBoneSocket %d → %d; "), *E->EmitterName.ToString(), Before, Bones.Num());
				}
				else if (UParticleModuleLocationSkelVertSurface* VertSurface = Cast<UParticleModuleLocationSkelVertSurface>(Module))
				{
					const int32 Before = VertSurface->ValidAssociatedBones.Num();
					TArray<FName> Bones = VertSurface->ValidAssociatedBones;
					if (!Apply(Bones)) continue;
					VertSurface->Modify();
					VertSurface->ValidAssociatedBones = Bones;
					VertSurface->PostEditChange();
					Out += FString::Printf(TEXT("%s:SkelVertSurface %d → %d; "), *E->EmitterName.ToString(), Before, Bones.Num());
				}
			}
		}
	}
	System->UpdateAllModuleLists();
	System->PostEditChange();
	if (Out.IsEmpty()) return TEXT("변경 없음");
	System->MarkPackageDirty();
	return Out;
}

FString UFxEditLibrary::SetTrailNotify(const FString& MontagePath, UParticleSystem* Template, float StartSec, float DurationSec, FName FirstSocket, FName SecondSocket, bool bRemoveOnly)
{
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
	if (!Montage) return TEXT("실패: 몽타주 없음 ") + MontagePath;
	if (!bRemoveOnly && !Template) return TEXT("실패: Template(ParticleSystem)이 없음");

	Montage->Modify();
	int32 Removed = 0;
	for (int32 i = Montage->Notifies.Num() - 1; i >= 0; --i)
	{
		if (Montage->Notifies[i].NotifyStateClass && Montage->Notifies[i].NotifyStateClass->IsA<UAnimNotifyState_Trail>())
		{
			Montage->Notifies.RemoveAt(i);
			++Removed;
		}
	}
	FString Out = FString::Printf(TEXT("기존 Trail 노티파이 %d개 제거"), Removed);
	if (!bRemoveOnly)
	{
		const float Length = Montage->GetPlayLength();
		const float Start = FMath::Clamp(StartSec, 0.f, Length);
		const float Duration = FMath::Clamp(DurationSec, 0.05f, Length - Start);
		if (Montage->AnimNotifyTracks.Num() == 0) Montage->AnimNotifyTracks.AddDefaulted();
		const int32 Index = Montage->Notifies.AddDefaulted();
		FAnimNotifyEvent& Event = Montage->Notifies[Index];
		UAnimNotifyState_Trail* Trail = NewObject<UAnimNotifyState_Trail>(Montage, UAnimNotifyState_Trail::StaticClass(), NAME_None, RF_Transactional);
		Trail->PSTemplate = Template;
		Trail->FirstSocketName = FirstSocket;
		Trail->SecondSocketName = SecondSocket;
		Event.NotifyStateClass = Trail;
		Event.NotifyName = FName(*Trail->GetNotifyName());
		Event.Guid = FGuid::NewGuid();
		Event.Link(Montage, Start);
		Event.TriggerTimeOffset = GetTriggerTimeOffsetForType(Montage->CalculateOffsetForNotify(Start));
		Event.SetDuration(Duration);
		Event.EndTriggerTimeOffset = GetTriggerTimeOffsetForType(Montage->CalculateOffsetForNotify(Start + Duration));
		Event.TrackIndex = 0;
		Out += FString::Printf(TEXT(", Trail %.2f~%.2f초(%s, %s→%s) 추가"), Start, Start + Duration, *Template->GetName(), *FirstSocket.ToString(), *SecondSocket.ToString());
	}
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	if (!UEditorAssetLibrary::SaveLoadedAsset(Montage, false)) return TEXT("실패: 저장 ") + MontagePath;
	return Out + TEXT(", 저장");
}
