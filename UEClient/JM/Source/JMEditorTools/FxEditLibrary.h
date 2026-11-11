#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FxEditLibrary.generated.h"

class UParticleSystem;

// FX 라이브러리 손질용 Cascade 편집 도우미(에디터 전용, Docs/FX_CATALOG.md 4단계).
// UParticleSystem::Emitters·UParticleEmitter::LODLevels·UParticleLODLevel::Modules는 EditAnywhere/BlueprintVisible이 아니라
// 에디터 Python(get_editor_property)에서 읽고 쓸 수 없다. 여기서 구조 읽기·이미터 제거·켬/끔·모듈 꺼내기만 하고,
// 모듈 프로퍼티(본 이름·메시·머티리얼)는 EditAnywhere라 Python(jm_kit.fx_fix)이 직접 고친다. jm_mcp KitTools가 감싼다.
UCLASS()
class JMEDITORTOOLS_API UFxEditLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// 구조를 JSON으로: {lod_method, emitters:[{index, name, object, lods:[{level, enabled, type_data, required:{material, delay, duration, loops},
	// modules:[{class, object, enabled, bones:[...](LocationBoneSocket·SkelVertSurface), source_type, mesh, mesh_materials:[...]}]}]}]}
	UFUNCTION(BlueprintCallable, Category = "JM|FxEdit")
	static FString DescribeParticleSystem(UParticleSystem* System);

	// 이미터를 Emitters 배열에서 빼고(Cascade의 Delete Emitter와 같은 순서), 어느 이미터도 안 쓰는 모듈은 임시 패키지로 옮겨 저장에서 빠지게 한다.
	// Emitter = 이미터 이름 또는 "#인덱스". 결과 문구("실패: ..."면 실패).
	UFUNCTION(BlueprintCallable, Category = "JM|FxEdit")
	static FString RemoveEmitter(UParticleSystem* System, const FString& Emitter);

	// 이미터의 모든 LOD를 켜거나 끈다(되돌리기 쉬운 삭제 대용).
	UFUNCTION(BlueprintCallable, Category = "JM|FxEdit")
	static FString SetEmitterEnabled(UParticleSystem* System, const FString& Emitter, bool bEnabled);

	// 이미터 LOD의 모듈(Required·Spawn·TypeData 포함, 중복 제거). LOD < 0이면 모든 LOD. 없으면 빈 배열.
	UFUNCTION(BlueprintCallable, Category = "JM|FxEdit")
	static TArray<UObject*> GetEmitterModules(UParticleSystem* System, const FString& Emitter, int32 LOD);

	// Python에서 모듈 프로퍼티를 고친 뒤 호출: 모듈 목록 갱신·PostEditChange·패키지 dirty.
	UFUNCTION(BlueprintCallable, Category = "JM|FxEdit")
	static void FinishEdit(UParticleSystem* System);

	// 본 모듈(LocationBoneSocket·SkelVertSurface)의 본 이름 편집. Emitter를 비우면 모든 이미터, 모든 LOD.
	// Mode: "rename"(Names[i] → NewNames[i]) / "drop"(Names 제거) / "set"(목록을 Names로 교체). 결과 문구("변경 없음" 가능).
	// Python 쪽 모듈 래퍼는 Cascade 파생 클래스 프로퍼티를 못 찾아(2026-11-12) 여기서 한다.
	UFUNCTION(BlueprintCallable, Category = "JM|FxEdit")
	static FString EditBones(UParticleSystem* System, const FString& Emitter, const FString& Mode, const TArray<FString>& Names, const TArray<FString>& NewNames);

	// 몽타주의 AnimNotifyState_Trail을 전부 지우고(bRemoveOnly면 여기까지) StartSec~+DurationSec에 하나 단다(PSTemplate·소켓 2개). 저장까지. 결과 문구.
	UFUNCTION(BlueprintCallable, Category = "JM|FxEdit")
	static FString SetTrailNotify(const FString& MontagePath, UParticleSystem* Template, float StartSec, float DurationSec, FName FirstSocket, FName SecondSocket, bool bRemoveOnly);

private:
	static int32 FindEmitter(UParticleSystem* System, const FString& Emitter, FString& OutError);
};
