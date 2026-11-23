#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "TagRatAIController.generated.h"

/**
 * 쥐 AI - 도주. 쥐가 여러 마리여도 각자 이 컨트롤러로 움직임
 *   방식 0 (기본): BFS 2번으로 "모든 고양이보다 먼저 도착하는 칸" 중 고양이에게서 가장 먼 칸으로 감
 *                  - 쥐 기준 BFS 1번
 *                  - 고양이 기준은 모든 고양이를 출발점으로 넣은 여러 출발점 BFS 1번 (고양이 수와 상관없이 1번)
 *                  - 주변이 열린 칸에 가산점
 *   방식 1: 가장 가까운 고양이 반대 방향으로 단순히 달림 (실험 4 비교용)
 * 방식은 콘솔 변수 Tag.RatMode 로 바꿈
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

	FTimerHandle ThinkHandle;
	bool bCaught = false;

	TArray<int32> RatDist;   // BFS 결과를 매번 새로 할당하지 않으려고 멤버로 둠
	TArray<int32> CatDist;
	TArray<int32> CatCells;
};
