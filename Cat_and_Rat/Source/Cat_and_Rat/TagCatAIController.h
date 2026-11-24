#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "TagCatAIController.generated.h"

class APickupItem;
class UInteractionComponent;
class UTagGridSubsystem;
class UTagAgentSubsystem;

/**
 * 고양이(술래) AI. 고양이가 여러 마리여도 각자 이 컨트롤러로 움직임. 0.25초마다 아래 순서로 판단함
 *
 *   1) 물건을 들고 있으면(회수 중): 원래 위치로 가져가서 내려놓음
 *      - 쥐가 Tag.CatDropForRat 초 안에 닿는 거리로 오면 물건을 내려놓고 쫓음
 *      - 물건을 들고 있는 동안은 쥐를 잡을 수 없음 (팀원 InteractionComponent 규칙)
 *   2) 쫓기 vs 회수 (그리디)
 *      - 물건을 든 쥐가 Tag.CatChaseSeconds 초 안에 닿으면 무조건 쫓음 (반출 막기가 먼저)
 *      - 아니면, 어지러움이 Tag.CatRecoverMinMess % 이상이고 반출된 물건이 있으면
 *        가중치 ÷ (물건까지 시간 + 원래 위치까지 시간)이 가장 큰 물건을 회수하러 감
 *      - 고양이가 2마리 이상이면 최소 1마리는 항상 쫓음
 *   3) 쫓을 쥐 고르기: 살아 있는 쥐 중 BFS로 걸어서 가장 가까운 쥐 (그리디)
 *                  물건을 든 쥐는 Tag.CatCarrierBonus칸만큼 더 가깝다고 보고 우선 쫓음
 *      버전 1: 그 쥐의 지금 위치로 감
 *      버전 2: 그 쥐가 PredictSeconds 뒤에 있을 위치(위치 + 속도 x 시간)로 감
 *   한 마리를 잡으면 멈추지 않고 다음 쥐를 쫓음
 *
 * 고양이 캐릭터에 InteractionComponent가 없으면 게임 시작 때 코드로 붙임 (bIsCat = true)
 * 버전은 콘솔 변수 Tag.CatVersion 으로 바꿈 (1 또는 2). Tag.CatRecover 0 이면 회수 안 함
 */
UCLASS()
class CAT_AND_RAT_API ATagCatAIController : public AAIController
{
	GENERATED_BODY()

public:
	ATagCatAIController();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	void Think();                                        // 0.25초마다 할 일과 목표 위치를 다시 정함
	APawn* ChooseTarget(const TArray<APawn*>& Rats);     // 걸어서 가장 가까운 쥐 (물건 든 쥐 우선)
	void Chase(APawn* Target);
	bool TryRecover(UTagGridSubsystem* Grid, UTagAgentSubsystem* Agents, UInteractionComponent* Interact, int32 NumCats);
	void CarryHome(UTagGridSubsystem* Grid, UTagAgentSubsystem* Agents, UInteractionComponent* Interact, APickupItem* Held);
	float RatSecondsAway(UTagGridSubsystem* Grid, APawn* Rat) const;   // 그 쥐까지 걸어서 몇 초 (못 가면 직선거리)
	UInteractionComponent* GetOrAddInteraction();

	FTimerHandle ThinkHandle;
	TWeakObjectPtr<APawn> CurrentTarget;
	TWeakObjectPtr<APickupItem> RecoverItem;             // 회수하러 가는 물건
	TArray<int32> MyDist;                                // BFS 결과 (매번 새로 할당하지 않으려고 멤버로 둠)
	TArray<int32> ItemDist;

	// Think 한 번 동안 쓰는 값
	int32 MyCell = INDEX_NONE;
	float Cs = 35.f;
	float MySpeed = 1.f;
};
