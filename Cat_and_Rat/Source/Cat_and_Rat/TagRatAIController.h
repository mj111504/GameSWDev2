#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "TagRatAIController.generated.h"

class APickupItem;
class UInteractionComponent;
class UTagGridSubsystem;
class UTagAgentSubsystem;

/**
 * 쥐 AI. 쥐가 여러 마리여도 각자 이 컨트롤러로 움직임. 0.25초마다 아래 순서로 판단함
 *
 *   1) 위험: 가장 가까운 고양이가 Tag.RatDanger 초 안에 올 수 있으면 → 들고 있던 물건을 버리고 도주
 *            (한 번 도주를 시작하면 고양이가 그 1.5배 시간 밖으로 멀어질 때까지 계속 도주. 왔다 갔다 방지)
 *   1-1) 넘기기: 무게 3 이상을 들고 있고 고양이가 Tag.RatDanger x 2 초 안에 오면,
 *            그 자리까지 고양이보다 먼저 올 수 있는 빈손 AI 쥐에게 물건을 넘김 (내려놓고 찜을 그 쥐에게 옮김)
 *   2) 물건을 들고 있음: 반출구역으로 운반. 다익스트라로 "고양이 근처 칸은 비싸게" 계산한 길을 따라감
 *            반출구역 안에 들어가면 물건을 내려놓음 → 명단 관리자가 반출 완료 처리
 *      2-1) 숨겨 두기: 위험 비용 때문에 길 비용이 그냥 거리의 Tag.RatStashRatio 배를 넘으면 (고양이가 길을 막음)
 *            가까운 칸 중 고양이보다 먼저 갈 수 있고 고양이에게서 먼 칸에 물건을 내려놓음.
 *            찜은 유지해서 Tag.RatStashWait 초가 지나고 안전해지면 다시 가지러 감
 *   3) 빈손: 물건 고르기 (그리디). 점수 ÷ (물건까지 가는 시간 + 물건을 들고 반출구역까지 가는 시간)이 가장 큰 물건
 *            - 고양이가 쥐보다 먼저 도착하는 물건, 다른 쥐가 찜한 물건, 초대형(못 듦)은 뺌
 *            - 고를 물건이 없으면 도주 방식으로 안전한 칸에 가 있음
 *            - 자기가 찜한 물건(숨겨 둔 것, 넘겨받은 것)은 값을 2배로 쳐서 우선 가지러 감
 *
 *   도주 방식 0 (기본): BFS 2번으로 "모든 고양이보다 먼저 도착하는 칸" 중 고양이에게서 가장 먼 칸으로 감
 *   도주 방식 1: 가장 가까운 고양이 반대 방향으로 단순히 달림 (비교 실험용. 물건은 고려 안 함)
 *
 * 콘솔 변수: Tag.RatMode (도주 방식), Tag.RatItems (1 = 물건 고려, 0 = 도주만), Tag.RatDanger 등
 * 쥐 캐릭터에 팀원의 InteractionComponent가 붙어 있어야 물건을 들 수 있음. 없으면 도주만 함
 */
UCLASS()
class CAT_AND_RAT_API ATagRatAIController : public AAIController
{
	GENERATED_BODY()

public:
	void OnCaught();

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	void Think();

	// 각 상태의 행동. Think에서 BFS 결과를 채운 뒤 호출함
	void Flee(UTagGridSubsystem* Grid, int32 MyCell);
	bool GoToItem(UTagGridSubsystem* Grid, UTagAgentSubsystem* Agents, UInteractionComponent* Interact);
	void Carry(UTagGridSubsystem* Grid, UTagAgentSubsystem* Agents, UInteractionComponent* Interact, int32 MyCell);
	bool TryHandOff(UTagGridSubsystem* Grid, UTagAgentSubsystem* Agents, UInteractionComponent* Interact, float CatTimeToMe);
	void Stash(UTagGridSubsystem* Grid, UTagAgentSubsystem* Agents, UInteractionComponent* Interact, int32 MyCell);
	void ReleaseMine(UTagAgentSubsystem* Agents);   // 내 찜 풀기 (숨겨 둔 물건은 남김)

	// 고양이가 칸 C까지 오는 데 걸리는 칸 수.
	// 격자에서 고양이가 못 가는 칸이면 직선거리로 대신 셈 (격자가 끊겨도 실제로는 올 수 있으니까)
	float CatCellsTo(UTagGridSubsystem* Grid, int32 C) const;

	FTimerHandle ThinkHandle;
	bool bCaught = false;
	bool bFleeing = false;
	bool bWarnedNoInteract = false;
	bool bStashing = false;                   // 숨길 칸으로 가는 중
	TWeakObjectPtr<APickupItem> StashedItem;  // 방금 숨긴 물건. StashUntil까지는 다시 줍지 않음
	float StashUntil = 0.f;
	TWeakObjectPtr<APickupItem> TargetItem;   // 가지러 가는 물건

	// BFS·다익스트라 결과를 매번 새로 할당하지 않으려고 멤버로 둠
	TArray<int32> RatDist;
	TArray<int32> CatDist;
	TArray<int32> CatCells;
	TArray<FVector> CatPositions;
	TArray<float> RiskCost;
	TArray<float> CarryDist;
	TArray<int32> Nbs;

	// Think 한 번 동안 쓰는 값
	float Cs = 35.f;
	float MySpeed = 1.f;
	float CatSpeed = 1.f;
	bool bDebug = true;
};
