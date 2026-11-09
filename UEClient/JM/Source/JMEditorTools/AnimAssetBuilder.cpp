#include "AnimAssetBuilder.h"

#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "Engine/MemberReference.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimPose.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimLayerInterface.h"
#include "Animation/AnimMontage.h"
#include "../JM/Animation/AnimNotify_SkillHit.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Factories/BlendSpaceFactory1D.h"
#include "Animation/Skeleton.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AnimationStateGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AssetToolsModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "EditorAssetLibrary.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Factories/AnimMontageFactory.h"
#include "IAssetTools.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogAnimAssetBuilder, Log, All);

namespace
{
	FString Fail(const FString& Msg)
	{
		UE_LOG(LogAnimAssetBuilder, Error, TEXT("%s"), *Msg);
		return TEXT("ERROR: ") + Msg;
	}

	// "/Game/A/B/Name" → ("/Game/A/B", "Name")
	bool SplitPackagePath(const FString& PackagePath, FString& OutFolder, FString& OutName)
	{
		int32 Slash = INDEX_NONE;
		if (!PackagePath.FindLastChar(TEXT('/'), Slash) || Slash <= 0 || Slash >= PackagePath.Len() - 1) return false;
		OutFolder = PackagePath.Left(Slash);
		OutName = PackagePath.Mid(Slash + 1);
		return true;
	}

	IAssetTools& AssetTools()
	{
		return FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	}

	// 컴파일하고 메시지(경고·오류)를 돌려준다. 오류면 "COMPILE ERROR"로 시작.
	FString CompileBlueprint(UBlueprint* BP)
	{
		FCompilerResultsLog Log;
		FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Log);
		FString Out;
		for (const TSharedRef<FTokenizedMessage>& M : Log.Messages)
		{
			if (M->GetSeverity() <= EMessageSeverity::Warning)
				Out += FString::Printf(TEXT("  [%s] %s\n"), M->GetSeverity() == EMessageSeverity::Error ? TEXT("error") : TEXT("warning"), *M->ToText().ToString());
		}
		if (BP->Status == BS_Error) return TEXT("COMPILE ERROR\n") + Out;
		return Out;
	}

	bool SaveAsset(UObject* Asset)
	{
		return UEditorAssetLibrary::SaveLoadedAsset(Asset, false);
	}

	// 포즈 핀(FPoseLink)을 찾는다. 이름을 주면 그 이름을 먼저 본다.
	UEdGraphPin* FindPosePin(UEdGraphNode* Node, EEdGraphPinDirection Dir, FName PreferredName = NAME_None)
	{
		if (Node == nullptr) return nullptr;
		if (PreferredName != NAME_None)
		{
			if (UEdGraphPin* P = Node->FindPin(PreferredName, Dir)) return P;
		}
		for (UEdGraphPin* P : Node->Pins)
		{
			if (P->Direction == Dir && UAnimationGraphSchema::IsPosePin(P->PinType)) return P;
		}
		return nullptr;
	}

	bool Connect(UEdGraph* Graph, UEdGraphPin* Out, UEdGraphPin* In, FString& Err)
	{
		if (Out == nullptr || In == nullptr) { Err = TEXT("connect: pin missing"); return false; }
		if (!Graph->GetSchema()->TryCreateConnection(Out, In))
		{
			Err = FString::Printf(TEXT("connect failed: %s:%s -> %s:%s"), *Out->GetOwningNode()->GetName(), *Out->PinName.ToString(), *In->GetOwningNode()->GetName(), *In->PinName.ToString());
			return false;
		}
		return true;
	}

	template <class T>
	TArray<T*> NodesOf(UEdGraph* Graph)
	{
		TArray<T*> Out;
		if (Graph) Graph->GetNodesOfClass<T>(Out);
		return Out;
	}

	UEdGraph* FindFunctionGraph(UBlueprint* BP, const FString& Name)
	{
		for (UEdGraph* G : BP->FunctionGraphs)
		{
			if (G && G->GetName() == Name) return G;
		}
		return nullptr;
	}

	UEdGraph* FindAnimGraph(UAnimBlueprint* BP)
	{
		return FindFunctionGraph(BP, UEdGraphSchema_K2::GN_AnimGraph.ToString());
	}

	// Anim 노드의 FAnimNode_* 구조체를 텍스트로
	FString ExportAnimNodeStruct(UEdGraphNode* Node)
	{
		FStructProperty* SP = FindFProperty<FStructProperty>(Node->GetClass(), TEXT("Node"));
		if (SP == nullptr) return FString();
		FString Txt;
		SP->ExportTextItem_Direct(Txt, SP->ContainerPtrToValuePtr<void>(Node), nullptr, Node, PPF_None);
		if (Txt.Len() > 700) Txt = Txt.Left(700) + TEXT("...");
		return Txt;
	}

	void DumpGraph(UEdGraph* Graph, int32 Depth, FString& Out, TSet<UEdGraph*>& Seen)
	{
		if (Graph == nullptr || Seen.Contains(Graph)) return;
		Seen.Add(Graph);
		const FString Ind = FString::ChrN(Depth * 2, TEXT(' '));
		Out += FString::Printf(TEXT("%sGRAPH %s (%s) nodes=%d\n"), *Ind, *Graph->GetName(), *Graph->GetClass()->GetName(), Graph->Nodes.Num());
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node == nullptr) continue;
			Out += FString::Printf(TEXT("%s  NODE %s [%s] '%s' pos(%d,%d)\n"), *Ind, *Node->GetName(), *Node->GetClass()->GetName(),
				*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(), Node->NodePosX, Node->NodePosY);
			if (UAnimStateNode* S = Cast<UAnimStateNode>(Node))
				Out += FString::Printf(TEXT("%s    State=%s\n"), *Ind, *S->GetStateName());
			if (UAnimStateTransitionNode* T = Cast<UAnimStateTransitionNode>(Node))
			{
				Out += FString::Printf(TEXT("%s    Transition %s -> %s auto=%d priority=%d crossfade=%.2f\n"), *Ind,
					T->GetPreviousState() ? *T->GetPreviousState()->GetStateName() : TEXT("?"), T->GetNextState() ? *T->GetNextState()->GetStateName() : TEXT("?"),
					T->bAutomaticRuleBasedOnSequencePlayerInState ? 1 : 0, T->PriorityOrder, T->CrossfadeDuration);
			}
			if (Node->IsA<UAnimGraphNode_Base>())
			{
				const FString Txt = ExportAnimNodeStruct(Node);
				if (!Txt.IsEmpty()) Out += FString::Printf(TEXT("%s    Node=%s\n"), *Ind, *Txt);
			}
			for (UEdGraphPin* Pin : Node->Pins)
			{
				FString Links;
				for (UEdGraphPin* L : Pin->LinkedTo)
					Links += FString::Printf(TEXT(" %s:%s"), L->GetOwningNode() ? *L->GetOwningNode()->GetName() : TEXT("?"), *L->PinName.ToString());
				FString Default = Pin->DefaultObject ? Pin->DefaultObject->GetPathName() : Pin->DefaultValue;
				if (Pin->bHidden && Links.IsEmpty()) continue;
				Out += FString::Printf(TEXT("%s    PIN %s %s : %s%s%s%s\n"), *Ind, Pin->Direction == EGPD_Input ? TEXT("in ") : TEXT("out"), *Pin->PinName.ToString(),
					*Pin->PinType.PinCategory.ToString(), Pin->PinType.PinSubCategoryObject.IsValid() ? *FString::Printf(TEXT("(%s)"), *Pin->PinType.PinSubCategoryObject->GetName()) : TEXT(""),
					Default.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" default=%s"), *Default), Links.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" ->%s"), *Links));
			}
			for (UEdGraph* Sub : Node->GetSubGraphs())
				DumpGraph(Sub, Depth + 2, Out, Seen);
		}
	}
}

FString UAnimAssetBuilder::DumpAnimBlueprint(const FString& BlueprintPath)
{
	UAnimBlueprint* BP = LoadObject<UAnimBlueprint>(nullptr, *BlueprintPath);
	if (BP == nullptr) return Fail(TEXT("AnimBlueprint load failed: ") + BlueprintPath);

	FString Out;
	Out += FString::Printf(TEXT("BLUEPRINT %s parent=%s skeleton=%s type=%d status=%d\n"), *BP->GetPathName(),
		BP->ParentClass ? *BP->ParentClass->GetPathName() : TEXT("?"), BP->TargetSkeleton ? *BP->TargetSkeleton->GetPathName() : TEXT("?"), (int32)BP->BlueprintType, (int32)BP->Status);
	for (const FBPInterfaceDescription& I : BP->ImplementedInterfaces)
		Out += FString::Printf(TEXT("  INTERFACE %s graphs=%d\n"), I.Interface ? *I.Interface->GetPathName() : TEXT("?"), I.Graphs.Num());

	TSet<UEdGraph*> Seen;
	Out += TEXT("-- FunctionGraphs --\n");
	for (UEdGraph* G : BP->FunctionGraphs) DumpGraph(G, 1, Out, Seen);
	Out += TEXT("-- UbergraphPages --\n");
	for (UEdGraph* G : BP->UbergraphPages) DumpGraph(G, 1, Out, Seen);
	for (const FBPInterfaceDescription& I : BP->ImplementedInterfaces)
	{
		Out += FString::Printf(TEXT("-- Interface graphs %s --\n"), I.Interface ? *I.Interface->GetName() : TEXT("?"));
		for (UEdGraph* G : I.Graphs) DumpGraph(G, 1, Out, Seen);
	}
	return Out;
}

FString UAnimAssetBuilder::CreateLayerInterface(const FString& PackagePath, const TArray<FString>& LayerNames, const TArray<bool>& LayerHasInputPose)
{
	FString Folder, Name;
	if (!SplitPackagePath(PackagePath, Folder, Name)) return Fail(TEXT("bad package path: ") + PackagePath);
	if (UEditorAssetLibrary::DoesAssetExist(PackagePath)) return Fail(TEXT("asset already exists: ") + PackagePath);
	if (LayerNames.Num() == 0 || LayerNames.Num() != LayerHasInputPose.Num()) return Fail(TEXT("LayerNames/LayerHasInputPose size mismatch"));

	UAnimLayerInterfaceFactory* Factory = NewObject<UAnimLayerInterfaceFactory>();
	UAnimBlueprint* BP = Cast<UAnimBlueprint>(AssetTools().CreateAsset(Name, Folder, UAnimBlueprint::StaticClass(), Factory));
	if (BP == nullptr) return Fail(TEXT("CreateAsset failed (AnimLayerInterface)"));

	FString Out = FString::Printf(TEXT("created %s\n"), *BP->GetPathName());
	for (int32 i = 0; i < LayerNames.Num(); ++i)
	{
		const FName LayerName(*LayerNames[i]);
		// 에디터 "애니메이션 레이어 추가"와 같은 경로(AddDomainSpecificGraph = 결과 노드 생성).
		// AddFunctionGraph는 결과 노드 없이 AutoArrangeInterfaceGraph를 불러 check(RootNodes.Num() == 1)로 에디터가 죽는다.
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(BP, LayerName, UAnimationGraph::StaticClass(), UAnimationGraphSchema::StaticClass());
		FBlueprintEditorUtils::AddDomainSpecificGraph(BP, Graph);
		if (NodesOf<UAnimGraphNode_Root>(Graph).Num() != 1) return Fail(TEXT("layer graph has no single root: ") + LayerNames[i]);

		TArray<UAnimGraphNode_LinkedInputPose*> Inputs = NodesOf<UAnimGraphNode_LinkedInputPose>(Graph);
		if (LayerHasInputPose[i])
		{
			if (Inputs.Num() == 0)
			{
				FGraphNodeCreator<UAnimGraphNode_LinkedInputPose> Creator(*Graph);
				UAnimGraphNode_LinkedInputPose* In = Creator.CreateNode();
				In->Node.Name = TEXT("InPose");
				In->NodePosX = -400;
				Creator.Finalize();
				Inputs.Add(In);
			}
			// 기본 구현 = 통과(입력 포즈 → 결과)
			TArray<UAnimGraphNode_Root*> Roots = NodesOf<UAnimGraphNode_Root>(Graph);
			FString Err;
			if (Roots.Num() > 0 && !Connect(Graph, FindPosePin(Inputs[0], EGPD_Output), FindPosePin(Roots[0], EGPD_Input), Err))
				Out += TEXT("  warning: ") + Err + TEXT("\n");
		}
		else
		{
			for (UAnimGraphNode_LinkedInputPose* In : Inputs) FBlueprintEditorUtils::RemoveNode(BP, In, true);
		}
		Out += FString::Printf(TEXT("  layer %s inputPose=%d\n"), *LayerNames[i], LayerHasInputPose[i] ? 1 : 0);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	const FString Compile = CompileBlueprint(BP);
	if (Compile.StartsWith(TEXT("COMPILE ERROR"))) return Fail(Compile);
	Out += Compile;
	if (!SaveAsset(BP)) return Fail(TEXT("save failed: ") + BP->GetPathName());
	return Out + TEXT("saved\n");
}

FString UAnimAssetBuilder::CreateLayerAnimBlueprint(const FString& PackagePath, const FString& InterfacePath, const FString& SkeletonPath, const FString& ParentClassPath)
{
	FString Folder, Name;
	if (!SplitPackagePath(PackagePath, Folder, Name)) return Fail(TEXT("bad package path: ") + PackagePath);
	if (UEditorAssetLibrary::DoesAssetExist(PackagePath)) return Fail(TEXT("asset already exists: ") + PackagePath);

	UAnimBlueprint* Interface = LoadObject<UAnimBlueprint>(nullptr, *InterfacePath);
	if (Interface == nullptr || Interface->GeneratedClass == nullptr) return Fail(TEXT("interface load failed (or not compiled): ") + InterfacePath);
	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (Skeleton == nullptr) return Fail(TEXT("skeleton load failed: ") + SkeletonPath);
	UClass* Parent = LoadObject<UClass>(nullptr, *ParentClassPath);
	if (Parent == nullptr || !Parent->IsChildOf(UAnimInstance::StaticClass())) return Fail(TEXT("parent class load failed: ") + ParentClassPath);

	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->TargetSkeleton = Skeleton;
	Factory->ParentClass = Parent;
	Factory->BlueprintType = BPTYPE_Normal;
	UAnimBlueprint* BP = Cast<UAnimBlueprint>(AssetTools().CreateAsset(Name, Folder, UAnimBlueprint::StaticClass(), Factory));
	if (BP == nullptr) return Fail(TEXT("CreateAsset failed (AnimBlueprint)"));

	if (!FBlueprintEditorUtils::ImplementNewInterface(BP, Interface->GeneratedClass->GetClassPathName()))
		return Fail(TEXT("ImplementNewInterface failed: ") + InterfacePath);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	const FString Compile = CompileBlueprint(BP);
	if (Compile.StartsWith(TEXT("COMPILE ERROR"))) return Fail(Compile);

	FString Out = FString::Printf(TEXT("created %s parent=%s\n"), *BP->GetPathName(), *Parent->GetName());
	for (const FBPInterfaceDescription& I : BP->ImplementedInterfaces)
		for (UEdGraph* G : I.Graphs) Out += FString::Printf(TEXT("  layer graph %s nodes=%d\n"), *G->GetName(), G->Nodes.Num());
	for (UEdGraph* G : BP->FunctionGraphs) Out += FString::Printf(TEXT("  function graph %s nodes=%d\n"), *G->GetName(), G->Nodes.Num());
	Out += Compile;
	if (!SaveAsset(BP)) return Fail(TEXT("save failed: ") + BP->GetPathName());
	return Out + TEXT("saved\n");
}

namespace
{
	// 레이어 그래프(인터페이스 구현 그래프 또는 FunctionGraphs)를 이름으로 찾는다
	UEdGraph* FindLayerGraph(UAnimBlueprint* BP, const FString& LayerName)
	{
		for (const FBPInterfaceDescription& I : BP->ImplementedInterfaces)
			for (UEdGraph* G : I.Graphs)
				if (G && G->GetName() == LayerName) return G;
		return FindFunctionGraph(BP, LayerName);
	}

	// 결과·입력 포즈 노드만 남기고 지운다
	void ClearLayerGraph(UBlueprint* BP, UEdGraph* Graph)
	{
		TArray<UEdGraphNode*> ToRemove;
		for (UEdGraphNode* N : Graph->Nodes)
			if (N && !N->IsA<UAnimGraphNode_Root>() && !N->IsA<UAnimGraphNode_LinkedInputPose>()) ToRemove.Add(N);
		for (UEdGraphNode* N : ToRemove) FBlueprintEditorUtils::RemoveNode(BP, N, true);
	}

	// 엔진 UAnimGraphNode_LinkedAnimLayer::SetupFromLayerId(protected·미노출)와 같은 설정. Finalize(핀 생성) 전에 부른다.
	// 호스트 BP가 Interface를 구현하고 있어야 레이어 함수(=핀)를 찾는다.
	bool SetupLinkedLayer(UAnimBlueprint* BP, UAnimGraphNode_LinkedAnimLayer* L, TSubclassOf<UAnimLayerInterface> Interface, FName Layer, FString& Err)
	{
		L->Node.Layer = Layer;
		L->Node.Interface = Interface;
		L->Node.InstanceClass = nullptr;
		bool bFound = false;
		for (const FBPInterfaceDescription& D : BP->ImplementedInterfaces)
		{
			if (D.Interface.Get() != Interface.Get()) continue;
			for (UEdGraph* G : D.Graphs)
				if (G && G->GetFName() == Layer) { L->InterfaceGuid = G->InterfaceGuid; bFound = true; }
		}
		if (!bFound) { Err = TEXT("layer not in implemented interface: ") + Layer.ToString(); return false; }

		FGuid FunctionGuid;
		FBlueprintEditorUtils::GetFunctionGuidFromClassByFieldName(FBlueprintEditorUtils::GetMostUpToDateClass(Interface.Get()), Layer, FunctionGuid);
		FStructProperty* P = FindFProperty<FStructProperty>(L->GetClass(), TEXT("FunctionReference"));
		if (P == nullptr || P->Struct != FMemberReference::StaticStruct()) { Err = TEXT("FunctionReference property not found"); return false; }
		P->ContainerPtrToValuePtr<FMemberReference>(L)->SetExternalMember(Layer, Interface.Get(), FunctionGuid);
		return true;
	}

	UK2Node_VariableGet* MakeVariableGet(UEdGraph* Graph, const FString& VarName, int32 PosX, int32 PosY)
	{
		FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
		UK2Node_VariableGet* V = Creator.CreateNode();
		V->VariableReference.SetSelfMember(FName(*VarName));
		V->NodePosX = PosX;
		V->NodePosY = PosY;
		Creator.Finalize();
		return V;
	}
}

FString UAnimAssetBuilder::SetLayerBlendSpace(const FString& BlueprintPath, const FString& LayerName, const FString& BlendSpacePath, const FString& XVariable, const FString& YVariable)
{
	UAnimBlueprint* BP = LoadObject<UAnimBlueprint>(nullptr, *BlueprintPath);
	if (BP == nullptr) return Fail(TEXT("AnimBlueprint load failed: ") + BlueprintPath);
	UEdGraph* Graph = FindLayerGraph(BP, LayerName);
	if (Graph == nullptr) return Fail(TEXT("layer graph not found: ") + LayerName);
	UBlendSpace* BS = LoadObject<UBlendSpace>(nullptr, *BlendSpacePath);
	if (BS == nullptr) return Fail(TEXT("blendspace load failed: ") + BlendSpacePath);
	TArray<UAnimGraphNode_Root*> Roots = NodesOf<UAnimGraphNode_Root>(Graph);
	if (Roots.Num() == 0) return Fail(TEXT("layer graph has no result node: ") + LayerName);

	ClearLayerGraph(BP, Graph);

	FGraphNodeCreator<UAnimGraphNode_BlendSpacePlayer> Creator(*Graph);
	UAnimGraphNode_BlendSpacePlayer* Player = Creator.CreateNode();
	Player->SetAnimationAsset(BS);
	Player->NodePosX = -350;
	Creator.Finalize();

	FString Err, Out = FString::Printf(TEXT("layer %s: BlendSpacePlayer(%s)\n"), *LayerName, *BS->GetName());
	if (!Connect(Graph, FindPosePin(Player, EGPD_Output), FindPosePin(Roots[0], EGPD_Input), Err)) return Fail(Err);
	if (!XVariable.IsEmpty())
	{
		UK2Node_VariableGet* V = MakeVariableGet(Graph, XVariable, -650, -40);
		if (!Connect(Graph, V->GetValuePin(), Player->FindPin(TEXT("X"), EGPD_Input), Err)) return Fail(Err);
		Out += FString::Printf(TEXT("  X <- %s\n"), *XVariable);
	}
	if (!YVariable.IsEmpty())
	{
		UK2Node_VariableGet* V = MakeVariableGet(Graph, YVariable, -650, 60);
		if (!Connect(Graph, V->GetValuePin(), Player->FindPin(TEXT("Y"), EGPD_Input), Err)) return Fail(Err);
		Out += FString::Printf(TEXT("  Y <- %s\n"), *YVariable);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	const FString Compile = CompileBlueprint(BP);
	if (Compile.StartsWith(TEXT("COMPILE ERROR"))) return Fail(Compile);
	Out += Compile;
	if (!SaveAsset(BP)) return Fail(TEXT("save failed: ") + BP->GetPathName());
	return Out + TEXT("saved\n");
}

FString UAnimAssetBuilder::SetLayerPassThrough(const FString& BlueprintPath, const FString& LayerName)
{
	UAnimBlueprint* BP = LoadObject<UAnimBlueprint>(nullptr, *BlueprintPath);
	if (BP == nullptr) return Fail(TEXT("AnimBlueprint load failed: ") + BlueprintPath);
	UEdGraph* Graph = FindLayerGraph(BP, LayerName);
	if (Graph == nullptr) return Fail(TEXT("layer graph not found: ") + LayerName);
	TArray<UAnimGraphNode_Root*> Roots = NodesOf<UAnimGraphNode_Root>(Graph);
	TArray<UAnimGraphNode_LinkedInputPose*> Inputs = NodesOf<UAnimGraphNode_LinkedInputPose>(Graph);
	if (Roots.Num() == 0 || Inputs.Num() == 0) return Fail(FString::Printf(TEXT("layer %s needs result(%d) and input pose(%d)"), *LayerName, Roots.Num(), Inputs.Num()));

	ClearLayerGraph(BP, Graph);
	FString Err;
	if (!Connect(Graph, FindPosePin(Inputs[0], EGPD_Output), FindPosePin(Roots[0], EGPD_Input), Err)) return Fail(Err);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	const FString Compile = CompileBlueprint(BP);
	if (Compile.StartsWith(TEXT("COMPILE ERROR"))) return Fail(Compile);
	if (!SaveAsset(BP)) return Fail(TEXT("save failed: ") + BP->GetPathName());
	return FString::Printf(TEXT("layer %s: pass-through\n%ssaved\n"), *LayerName, *Compile);
}

FString UAnimAssetBuilder::CreateBlendSpace1D(const FString& PackagePath, const FString& SkeletonPath, const FString& AxisName, float AxisMin, float AxisMax,
	const TArray<FString>& SequencePaths, const TArray<float>& SampleValues, const TArray<float>& RateScales)
{
	FString Folder, Name;
	if (!SplitPackagePath(PackagePath, Folder, Name)) return Fail(TEXT("bad package path: ") + PackagePath);
	if (UEditorAssetLibrary::DoesAssetExist(PackagePath)) return Fail(TEXT("asset already exists: ") + PackagePath);
	if (SequencePaths.Num() == 0 || SequencePaths.Num() != SampleValues.Num()) return Fail(TEXT("SequencePaths and SampleValues must have the same non-zero count"));
	if (RateScales.Num() != 0 && RateScales.Num() != SequencePaths.Num()) return Fail(TEXT("RateScales must be empty or match SequencePaths"));
	if (AxisMax <= AxisMin) return Fail(TEXT("AxisMax must be greater than AxisMin"));
	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (Skeleton == nullptr) return Fail(TEXT("skeleton load failed: ") + SkeletonPath);

	TArray<UAnimSequence*> Seqs;
	for (const FString& P : SequencePaths)
	{
		UAnimSequence* S = LoadObject<UAnimSequence>(nullptr, *P);
		if (S == nullptr) return Fail(TEXT("sequence load failed: ") + P);
		if (S->GetSkeleton() != Skeleton) return Fail(TEXT("sequence skeleton mismatch: ") + P);
		Seqs.Add(S);
	}

	UBlendSpaceFactory1D* Factory = NewObject<UBlendSpaceFactory1D>();
	Factory->TargetSkeleton = Skeleton;
	UBlendSpace1D* BS = Cast<UBlendSpace1D>(AssetTools().CreateAsset(Name, Folder, UBlendSpace1D::StaticClass(), Factory));
	if (BS == nullptr) return Fail(TEXT("CreateAsset failed (BlendSpace1D)"));

	// BlendParameters는 protected라 리플렉션으로 쓴다(에디터 디테일 패널과 같은 값)
	FProperty* Prop = FindFProperty<FProperty>(UBlendSpace::StaticClass(), TEXT("BlendParameters"));
	if (Prop == nullptr) return Fail(TEXT("BlendParameters property not found"));
	FBlendParameter* Axis = Prop->ContainerPtrToValuePtr<FBlendParameter>(BS, 0);
	Axis->DisplayName = AxisName;
	Axis->Min = AxisMin;
	Axis->Max = AxisMax;
	Axis->bSnapToGrid = false;

	FString Out = FString::Printf(TEXT("created %s axis %s [%.1f, %.1f]\n"), *BS->GetPathName(), *AxisName, AxisMin, AxisMax);
	for (int32 i = 0; i < Seqs.Num(); ++i)
	{
		const int32 Index = BS->AddSample(Seqs[i], FVector(SampleValues[i], 0.f, 0.f));
		if (Index == INDEX_NONE) return Fail(FString::Printf(TEXT("AddSample failed: %s at %.1f"), *Seqs[i]->GetName(), SampleValues[i]));
		const float Rate = RateScales.Num() ? RateScales[i] : 1.f;
		const_cast<FBlendSample&>(BS->GetBlendSample(Index)).RateScale = Rate;
		Out += FString::Printf(TEXT("  sample %s at %.1f rate %.3f\n"), *Seqs[i]->GetName(), SampleValues[i], Rate);
	}
	BS->ValidateSampleData();
	BS->ResampleData();
	BS->PostEditChange();
	BS->MarkPackageDirty();
	if (!SaveAsset(BS)) return Fail(TEXT("save failed: ") + BS->GetPathName());
	return Out + TEXT("saved\n");
}

FString UAnimAssetBuilder::SetLayerBlendSpaceWithOverride(const FString& BlueprintPath, const FString& LayerName, const FString& BlendSpacePath, const FString& XVariable,
	const FString& OverlayBlendSpacePath, const FString& OverlayXVariable, const FString& OverlayYVariable, const FString& BranchBone, const FString& AlphaVariable)
{
	UAnimBlueprint* BP = LoadObject<UAnimBlueprint>(nullptr, *BlueprintPath);
	if (BP == nullptr) return Fail(TEXT("AnimBlueprint load failed: ") + BlueprintPath);
	UEdGraph* Graph = FindLayerGraph(BP, LayerName);
	if (Graph == nullptr) return Fail(TEXT("layer graph not found: ") + LayerName);
	UBlendSpace* BS = LoadObject<UBlendSpace>(nullptr, *BlendSpacePath);
	if (BS == nullptr) return Fail(TEXT("blendspace load failed: ") + BlendSpacePath);
	UBlendSpace* Overlay = LoadObject<UBlendSpace>(nullptr, *OverlayBlendSpacePath);
	if (Overlay == nullptr) return Fail(TEXT("overlay blendspace load failed: ") + OverlayBlendSpacePath);
	if (BranchBone.IsEmpty() || AlphaVariable.IsEmpty()) return Fail(TEXT("BranchBone and AlphaVariable are required"));
	if (BP->TargetSkeleton && BP->TargetSkeleton->GetReferenceSkeleton().FindBoneIndex(FName(*BranchBone)) == INDEX_NONE)
		return Fail(TEXT("bone not in skeleton: ") + BranchBone);
	TArray<UAnimGraphNode_Root*> Roots = NodesOf<UAnimGraphNode_Root>(Graph);
	if (Roots.Num() == 0) return Fail(TEXT("layer graph has no result node: ") + LayerName);

	ClearLayerGraph(BP, Graph);

	FGraphNodeCreator<UAnimGraphNode_BlendSpacePlayer> C1(*Graph);
	UAnimGraphNode_BlendSpacePlayer* Base = C1.CreateNode();
	Base->SetAnimationAsset(BS);
	Base->NodePosX = -700; Base->NodePosY = -120;
	C1.Finalize();

	FGraphNodeCreator<UAnimGraphNode_BlendSpacePlayer> C2(*Graph);
	UAnimGraphNode_BlendSpacePlayer* Over = C2.CreateNode();
	Over->SetAnimationAsset(Overlay);
	Over->NodePosX = -700; Over->NodePosY = 120;
	C2.Finalize();

	FGraphNodeCreator<UAnimGraphNode_LayeredBoneBlend> C3(*Graph);
	UAnimGraphNode_LayeredBoneBlend* Blend = C3.CreateNode();
	Blend->NodePosX = -300;
	C3.Finalize();
	if (Blend->Node.LayerSetup.Num() < 1) return Fail(TEXT("LayeredBoneBlend has no blend pose"));
	FBranchFilter Filter;
	Filter.BoneName = FName(*BranchBone);
	Filter.BlendDepth = 0;
	Blend->Node.LayerSetup[0].BranchFilters.Reset();
	Blend->Node.LayerSetup[0].BranchFilters.Add(Filter);
	Blend->Node.bMeshSpaceRotationBlend = true;

	FString Err;
	if (!Connect(Graph, FindPosePin(Base, EGPD_Output), FindPosePin(Blend, EGPD_Input, TEXT("BasePose")), Err)) return Fail(Err);
	if (!Connect(Graph, FindPosePin(Over, EGPD_Output), FindPosePin(Blend, EGPD_Input, TEXT("BlendPoses_0")), Err)) return Fail(Err);
	if (!Connect(Graph, FindPosePin(Blend, EGPD_Output), FindPosePin(Roots[0], EGPD_Input), Err)) return Fail(Err);

	FString Out = FString::Printf(TEXT("layer %s: BlendSpacePlayer(%s) | BlendSpacePlayer(%s) below %s -> LayeredBlend(mesh-space rotation)\n"),
		*LayerName, *BS->GetName(), *Overlay->GetName(), *BranchBone);
	struct FLink { const FString* Var; UEdGraphNode* Node; const TCHAR* Pin; int32 X; int32 Y; };
	const FLink Links[] = {
		{ &XVariable, Base, TEXT("X"), -1000, -120 },
		{ &OverlayXVariable, Over, TEXT("X"), -1000, 100 },
		{ &OverlayYVariable, Over, TEXT("Y"), -1000, 180 },
		{ &AlphaVariable, Blend, TEXT("BlendWeights_0"), -600, 320 },
	};
	for (const FLink& L : Links)
	{
		if (L.Var->IsEmpty()) continue;
		UEdGraphPin* Pin = L.Node->FindPin(L.Pin, EGPD_Input);
		if (Pin == nullptr) return Fail(FString::Printf(TEXT("pin not found: %s:%s"), *L.Node->GetName(), L.Pin));
		UK2Node_VariableGet* V = MakeVariableGet(Graph, *L.Var, L.X, L.Y);
		if (!Connect(Graph, V->GetValuePin(), Pin, Err)) return Fail(Err);
		Out += FString::Printf(TEXT("  %s:%s <- %s\n"), *L.Node->GetName(), L.Pin, **L.Var);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	const FString Compile = CompileBlueprint(BP);
	if (Compile.StartsWith(TEXT("COMPILE ERROR"))) return Fail(Compile);
	Out += Compile;
	if (!SaveAsset(BP)) return Fail(TEXT("save failed: ") + BP->GetPathName());
	return Out + TEXT("saved\n");
}

FString UAnimAssetBuilder::CreateMontage(const FString& SequencePath, const FString& PackagePath, const FString& SlotName, bool bAutoBlendOut, float SkillHitTimeSec)
{
	FString Folder, Name;
	if (!SplitPackagePath(PackagePath, Folder, Name)) return Fail(TEXT("bad package path: ") + PackagePath);
	if (UEditorAssetLibrary::DoesAssetExist(PackagePath)) return Fail(TEXT("asset already exists: ") + PackagePath);
	UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *SequencePath);
	if (Seq == nullptr || Seq->GetSkeleton() == nullptr) return Fail(TEXT("sequence load failed: ") + SequencePath);

	UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
	Factory->SourceAnimation = Seq;
	Factory->TargetSkeleton = Seq->GetSkeleton();
	UAnimMontage* Montage = Cast<UAnimMontage>(AssetTools().CreateAsset(Name, Folder, UAnimMontage::StaticClass(), Factory));
	if (Montage == nullptr) return Fail(TEXT("CreateAsset failed (AnimMontage)"));

	if (Montage->SlotAnimTracks.Num() == 0) Montage->SlotAnimTracks.AddDefaulted();
	const FName Slot(*SlotName);
	Montage->SlotAnimTracks[0].SlotName = Slot;
	Seq->GetSkeleton()->RegisterSlotNode(Slot);   // 스켈레톤 슬롯 목록에 없으면 등록(AnimGraph Slot 노드 드롭다운)
	Montage->bEnableAutoBlendOut = bAutoBlendOut;

	FString Out = FString::Printf(TEXT("created %s from %s length=%.3f slot=%s autoBlendOut=%d\n"), *Montage->GetPathName(), *Seq->GetName(), Montage->GetPlayLength(), *SlotName, bAutoBlendOut ? 1 : 0);
	if (SkillHitTimeSec >= 0.f)
	{
		const float Time = FMath::Clamp(SkillHitTimeSec, 0.f, Montage->GetPlayLength());
		if (Montage->AnimNotifyTracks.Num() == 0) Montage->AnimNotifyTracks.AddDefaulted();
		const int32 Index = Montage->Notifies.AddDefaulted();
		FAnimNotifyEvent& Event = Montage->Notifies[Index];
		Event.Notify = NewObject<UAnimNotify_SkillHit>(Montage, UAnimNotify_SkillHit::StaticClass(), NAME_None, RF_Transactional);
		Event.NotifyName = FName(*Event.Notify->GetNotifyName());
		Event.Link(Montage, Time);
		Event.TriggerTimeOffset = GetTriggerTimeOffsetForType(Montage->CalculateOffsetForNotify(Time));
		Event.TrackIndex = 0;
		Montage->RefreshCacheData();
		Out += FString::Printf(TEXT("  notify SkillHit at %.3f\n"), Time);
	}
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	if (!SaveAsset(Montage)) return Fail(TEXT("save failed: ") + Montage->GetPathName());
	return Out + TEXT("saved\n");
}

FString UAnimAssetBuilder::MeasureBoneReach(const FString& SequencePath, const FString& BoneName, int32 Samples)
{
	UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *SequencePath);
	if (Seq == nullptr) return Fail(TEXT("sequence load failed: ") + SequencePath);
	Samples = FMath::Clamp(Samples, 2, 400);
	const float Length = Seq->GetPlayLength();
	FAnimPoseEvaluationOptions Options;
	Options.bExtractRootMotion = false;

	FString Out = FString::Printf(TEXT("%s bone=%s length=%.3f\ntime\tX(side)\tY(fwd)\tZ(up)\tdistXY\n"), *Seq->GetName(), *BoneName, Length);
	for (int32 i = 0; i < Samples; ++i)
	{
		const float T = Length * i / (Samples - 1);
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtTime(Seq, T, Options, Pose);
		const FVector L = UAnimPoseExtensions::GetBonePose(Pose, FName(*BoneName), EAnimPoseSpaces::World).GetLocation();
		Out += FString::Printf(TEXT("%.3f\t%.1f\t%.1f\t%.1f\t%.1f\n"), T, L.X, L.Y, L.Z, L.Size2D());
	}
	return Out;
}

FString UAnimAssetBuilder::RewirePlayerBase(const FString& BlueprintPath, const FString& InterfacePath, const FString& LocomotionLayer, const FString& UpperBodyLayer,
	const FString& LocomotionStateName, const FString& LandStateName, bool bSave)
{
	UAnimBlueprint* BP = LoadObject<UAnimBlueprint>(nullptr, *BlueprintPath);
	if (BP == nullptr) return Fail(TEXT("AnimBlueprint load failed: ") + BlueprintPath);
	UAnimBlueprint* InterfaceBP = LoadObject<UAnimBlueprint>(nullptr, *InterfacePath);
	if (InterfaceBP == nullptr || InterfaceBP->GeneratedClass == nullptr) return Fail(TEXT("interface load failed: ") + InterfacePath);
	TSubclassOf<UAnimLayerInterface> Interface(InterfaceBP->GeneratedClass);
	if (Interface.Get() == nullptr) return Fail(TEXT("interface class is not an AnimLayerInterface: ") + InterfacePath);

	UEdGraph* AnimGraph = FindAnimGraph(BP);
	if (AnimGraph == nullptr) return Fail(TEXT("AnimGraph not found"));
	TArray<UAnimGraphNode_Root*> Roots = NodesOf<UAnimGraphNode_Root>(AnimGraph);
	if (Roots.Num() != 1) return Fail(FString::Printf(TEXT("AnimGraph root count %d"), Roots.Num()));
	// 상태머신이 여럿일 수 있다(예전 ABP_PlayerBase = "Locomotion" 머신 → 캐시 포즈 → "Main States"). LocomotionStateName 스테이트가 있는 머신을 고른다.
	UAnimationStateMachineGraph* Machine = nullptr;
	UAnimStateNode* LocomotionState = nullptr;
	UAnimStateNode* LandState = nullptr;
	for (UAnimGraphNode_StateMachine* M : NodesOf<UAnimGraphNode_StateMachine>(AnimGraph))
	{
		if (M->EditorStateMachineGraph == nullptr) continue;
		for (UAnimStateNode* S : NodesOf<UAnimStateNode>(M->EditorStateMachineGraph))
		{
			if (S->GetStateName() == LocomotionStateName)
			{
				if (Machine != nullptr) return Fail(TEXT("state found in more than one state machine: ") + LocomotionStateName);
				Machine = M->EditorStateMachineGraph;
				LocomotionState = S;
			}
		}
	}
	if (Machine == nullptr || LocomotionState == nullptr || LocomotionState->BoundGraph == nullptr) return Fail(TEXT("state not found: ") + LocomotionStateName);
	if (!LandStateName.IsEmpty())
	{
		for (UAnimStateNode* S : NodesOf<UAnimStateNode>(Machine))
			if (S->GetStateName() == LandStateName) LandState = S;
	}

	FString Out, Err;

	// 0) 호스트가 인터페이스를 구현해야 Linked Anim Layer 노드가 레이어 함수·핀을 찾는다(에디터 Class Settings → Implemented Interfaces와 같음).
	//    새로 구현하면 기본 구현(링크된 레이어가 없을 때)을 채운다: 입력 포즈가 있는 레이어 = 통과, 없는 레이어 = 비워 둠(참조 포즈 — 장비 컴포넌트가 항상 링크).
	{
		bool bImplemented = false;
		for (const FBPInterfaceDescription& D : BP->ImplementedInterfaces)
			if (D.Interface.Get() == Interface.Get()) bImplemented = true;
		if (!bImplemented)
		{
			if (!FBlueprintEditorUtils::ImplementNewInterface(BP, Interface->GetClassPathName())) return Fail(TEXT("ImplementNewInterface failed: ") + InterfacePath);
			Out += FString::Printf(TEXT("implemented interface %s\n"), *Interface->GetName());
			for (const FBPInterfaceDescription& D : BP->ImplementedInterfaces)
			{
				if (D.Interface.Get() != Interface.Get()) continue;
				for (UEdGraph* G : D.Graphs)
				{
					TArray<UAnimGraphNode_Root*> R = NodesOf<UAnimGraphNode_Root>(G);
					TArray<UAnimGraphNode_LinkedInputPose*> I = NodesOf<UAnimGraphNode_LinkedInputPose>(G);
					if (R.Num() == 1 && I.Num() >= 1)
					{
						if (!Connect(G, FindPosePin(I[0], EGPD_Output), FindPosePin(R[0], EGPD_Input), Err)) return Fail(Err);
						Out += FString::Printf(TEXT("  default %s: pass-through\n"), *G->GetName());
					}
					else
					{
						Out += FString::Printf(TEXT("  default %s: empty (ref pose)\n"), *G->GetName());
					}
				}
			}
		}
	}

	// 1) Locomotion 스테이트 = Linked Anim Layer(Locomotion)
	{
		UEdGraph* StateGraph = LocomotionState->BoundGraph;
		TArray<UAnimGraphNode_StateResult*> Results = NodesOf<UAnimGraphNode_StateResult>(StateGraph);
		if (Results.Num() != 1) return Fail(FString::Printf(TEXT("state %s result count %d"), *LocomotionStateName, Results.Num()));

		TArray<UEdGraphNode*> ToRemove;
		for (UEdGraphNode* N : StateGraph->Nodes)
			if (N && N != Results[0]) ToRemove.Add(N);
		for (UEdGraphNode* N : ToRemove) FBlueprintEditorUtils::RemoveNode(BP, N, true);

		FGraphNodeCreator<UAnimGraphNode_LinkedAnimLayer> Creator(*StateGraph);
		UAnimGraphNode_LinkedAnimLayer* Layer = Creator.CreateNode();
		if (!SetupLinkedLayer(BP, Layer, Interface, FName(*LocomotionLayer), Err)) return Fail(Err);
		Layer->NodePosX = Results[0]->NodePosX - 350;
		Layer->NodePosY = Results[0]->NodePosY;
		Creator.Finalize();
		if (!Connect(StateGraph, FindPosePin(Layer, EGPD_Output), FindPosePin(Results[0], EGPD_Input), Err)) return Fail(Err);
		Out += FString::Printf(TEXT("state %s: removed %d nodes, LinkedAnimLayer(%s)\n"), *LocomotionStateName, ToRemove.Num(), *LocomotionLayer);
	}

	// 2) 상태머신 출력 → UpperBody 레이어 → 캐시 → [기본 포즈 | Slot UpperBody] 레이어 블렌드(spine_01 위) → Slot DefaultSlot → (기존 뒷단, 예 Control Rig) → 결과
	//    UpperBody 슬롯은 이동 중 시전용(하체는 이동 유지), DefaultSlot은 전신(사망 등). 이미 UpperBody 레이어나 슬롯이 있으면 건너뜀.
	{
		bool bHasUpper = false, bHasSlotUpper = false;
		UAnimGraphNode_Slot* SlotDefault = nullptr;
		for (UAnimGraphNode_LinkedAnimLayer* L : NodesOf<UAnimGraphNode_LinkedAnimLayer>(AnimGraph))
			if (L->Node.Layer == FName(*UpperBodyLayer)) bHasUpper = true;
		for (UAnimGraphNode_Slot* S : NodesOf<UAnimGraphNode_Slot>(AnimGraph))
		{
			if (S->Node.SlotName == TEXT("UpperBody")) bHasSlotUpper = true;
			if (S->Node.SlotName == TEXT("DefaultSlot"))
			{
				if (SlotDefault != nullptr) return Fail(TEXT("more than one DefaultSlot node"));
				SlotDefault = S;
			}
		}
		if (bHasUpper || bHasSlotUpper)
		{
			Out += FString::Printf(TEXT("AnimGraph: skipped (upper=%d slotUpper=%d)\n"), bHasUpper, bHasSlotUpper);
		}
		else
		{
			// 끼워 넣을 자리: DefaultSlot이 있으면 그 입력 앞, 없으면 결과 앞(DefaultSlot도 새로 만든다)
			const int32 X = SlotDefault ? SlotDefault->NodePosX : Roots[0]->NodePosX;
			const int32 Y = SlotDefault ? SlotDefault->NodePosY : Roots[0]->NodePosY;
			UEdGraphPin* Downstream = SlotDefault ? FindPosePin(SlotDefault, EGPD_Input, TEXT("Source")) : FindPosePin(Roots[0], EGPD_Input);
			if (Downstream == nullptr || Downstream->LinkedTo.Num() != 1) return Fail(TEXT("insert point pin has no single link"));
			UEdGraphPin* Source = Downstream->LinkedTo[0];
			Downstream->BreakAllPinLinks();

			// 뒤쪽 노드를 오른쪽으로 밀어 자리를 만든다
			const int32 Shift = 1400;
			for (UEdGraphNode* N : AnimGraph->Nodes)
				if (N && N->NodePosX >= X) N->NodePosX += Shift;
			const int32 BaseX = X;

			FGraphNodeCreator<UAnimGraphNode_LinkedAnimLayer> C1(*AnimGraph);
			UAnimGraphNode_LinkedAnimLayer* Upper = C1.CreateNode();
			if (!SetupLinkedLayer(BP, Upper, Interface, FName(*UpperBodyLayer), Err)) return Fail(Err);
			Upper->NodePosX = BaseX; Upper->NodePosY = Y;
			C1.Finalize();

			FGraphNodeCreator<UAnimGraphNode_SaveCachedPose> C2(*AnimGraph);
			UAnimGraphNode_SaveCachedPose* Save = C2.CreateNode();
			Save->CacheName = TEXT("UpperBodyIn");
			Save->NodePosX = BaseX + 300; Save->NodePosY = Y;
			C2.Finalize();

			auto MakeUse = [&](int32 PosY)
			{
				FGraphNodeCreator<UAnimGraphNode_UseCachedPose> C(*AnimGraph);
				UAnimGraphNode_UseCachedPose* Use = C.CreateNode();
				Use->SaveCachedPoseNode = Save;
				Use->NodePosX = BaseX + 550; Use->NodePosY = PosY;
				C.Finalize();
				return Use;
			};
			UAnimGraphNode_UseCachedPose* UseBase = MakeUse(Y);
			UAnimGraphNode_UseCachedPose* UseSlot = MakeUse(Y + 160);

			FGraphNodeCreator<UAnimGraphNode_Slot> C3(*AnimGraph);
			UAnimGraphNode_Slot* SlotUpper = C3.CreateNode();
			SlotUpper->Node.SlotName = TEXT("UpperBody");
			SlotUpper->NodePosX = BaseX + 800; SlotUpper->NodePosY = Y + 160;
			C3.Finalize();

			FGraphNodeCreator<UAnimGraphNode_LayeredBoneBlend> C4(*AnimGraph);
			UAnimGraphNode_LayeredBoneBlend* Blend = C4.CreateNode();
			Blend->NodePosX = BaseX + 1100; Blend->NodePosY = Y;
			C4.Finalize();
			if (Blend->Node.LayerSetup.Num() < 1) return Fail(TEXT("LayeredBoneBlend has no blend pose"));
			FBranchFilter Filter;
			Filter.BoneName = TEXT("spine_01");
			Filter.BlendDepth = 0;
			Blend->Node.LayerSetup[0].BranchFilters.Reset();
			Blend->Node.LayerSetup[0].BranchFilters.Add(Filter);
			Blend->Node.bMeshSpaceRotationBlend = true;

			if (SlotDefault == nullptr)
			{
				FGraphNodeCreator<UAnimGraphNode_Slot> C5(*AnimGraph);
				SlotDefault = C5.CreateNode();
				SlotDefault->Node.SlotName = TEXT("DefaultSlot");
				SlotDefault->NodePosX = BaseX + Shift; SlotDefault->NodePosY = Y;
				C5.Finalize();
				if (!Connect(AnimGraph, FindPosePin(SlotDefault, EGPD_Output), Downstream, Err)) return Fail(Err);
				Downstream = FindPosePin(SlotDefault, EGPD_Input, TEXT("Source"));
			}
			if (BP->TargetSkeleton)
			{
				BP->TargetSkeleton->RegisterSlotNode(TEXT("UpperBody"));
				BP->TargetSkeleton->RegisterSlotNode(TEXT("DefaultSlot"));
			}

			if (!Connect(AnimGraph, Source, FindPosePin(Upper, EGPD_Input), Err)) return Fail(Err);
			if (!Connect(AnimGraph, FindPosePin(Upper, EGPD_Output), FindPosePin(Save, EGPD_Input), Err)) return Fail(Err);
			if (!Connect(AnimGraph, FindPosePin(UseBase, EGPD_Output), FindPosePin(Blend, EGPD_Input, TEXT("BasePose")), Err)) return Fail(Err);
			if (!Connect(AnimGraph, FindPosePin(UseSlot, EGPD_Output), FindPosePin(SlotUpper, EGPD_Input, TEXT("Source")), Err)) return Fail(Err);
			if (!Connect(AnimGraph, FindPosePin(SlotUpper, EGPD_Output), FindPosePin(Blend, EGPD_Input, TEXT("BlendPoses_0")), Err)) return Fail(Err);
			if (!Connect(AnimGraph, FindPosePin(Blend, EGPD_Output), Downstream, Err)) return Fail(Err);
			Out += FString::Printf(TEXT("AnimGraph: %s -> LinkedAnimLayer(%s) -> cache UpperBodyIn -> LayeredBlend(base | Slot UpperBody, spine_01) -> Slot DefaultSlot\n"),
				*Source->GetOwningNode()->GetName(), *UpperBodyLayer);
		}
	}

	// 2b) 아무도 쓰지 않는 캐시 포즈와 그 앞 상태머신을 지운다(예전 Locomotion 머신 — 스테이트가 레이어로 바뀌어 고아가 됨)
	{
		TArray<UAnimGraphNode_UseCachedPose*> Uses;
		FBlueprintEditorUtils::GetAllNodesOfClass<UAnimGraphNode_UseCachedPose>(BP, Uses);
		for (UAnimGraphNode_SaveCachedPose* Save : NodesOf<UAnimGraphNode_SaveCachedPose>(AnimGraph))
		{
			bool bUsed = false;
			for (UAnimGraphNode_UseCachedPose* U : Uses)
				if (U->SaveCachedPoseNode.Get() == Save) bUsed = true;
			if (bUsed) continue;

			TArray<UEdGraphNode*> Feeders;
			if (UEdGraphPin* In = FindPosePin(Save, EGPD_Input))
				for (UEdGraphPin* L : In->LinkedTo)
				{
					UEdGraphNode* N = L->GetOwningNode();
					UEdGraphPin* NOut = FindPosePin(N, EGPD_Output);
					if (N->IsA<UAnimGraphNode_StateMachine>() && NOut && NOut->LinkedTo.Num() == 1) Feeders.Add(N);
				}
			Out += FString::Printf(TEXT("removed unused cache %s (+%d state machine)\n"), *Save->CacheName, Feeders.Num());
			for (UEdGraphNode* N : Feeders) FBlueprintEditorUtils::RemoveNode(BP, N, true);
			FBlueprintEditorUtils::RemoveNode(BP, Save, true);
		}
	}

	// 3) Land → Locomotion 전이 = 시퀀스 재생 끝나면 자동
	if (LandState != nullptr)
	{
		int32 Changed = 0;
		for (UAnimStateTransitionNode* T : NodesOf<UAnimStateTransitionNode>(Machine))
		{
			if (T->GetPreviousState() == LandState && T->GetNextState() == LocomotionState && !T->bAutomaticRuleBasedOnSequencePlayerInState)
			{
				T->Modify();
				T->bAutomaticRuleBasedOnSequencePlayerInState = true;
				++Changed;
			}
		}
		Out += FString::Printf(TEXT("transition %s -> %s: automatic rule set on %d\n"), *LandStateName, *LocomotionStateName, Changed);
	}
	else if (!LandStateName.IsEmpty())
	{
		Out += FString::Printf(TEXT("state %s not found: skipped\n"), *LandStateName);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	const FString Compile = CompileBlueprint(BP);
	Out += Compile;
	if (Compile.StartsWith(TEXT("COMPILE ERROR"))) return Fail(Out);
	if (!bSave) return Out + TEXT("not saved (preview)\n");
	if (!SaveAsset(BP)) return Fail(TEXT("save failed: ") + BP->GetPathName());
	return Out + TEXT("saved\n");
}
