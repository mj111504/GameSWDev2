#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "TagCatAIController.generated.h"

/**
 * 고양이(술래) AI. 고양이가 여러 마리여도 각자 이 컨트롤러로 움직임
 *   쫓을 쥐 고르기: 살아 있는 쥐 중 BFS로 걸어서 가장 가까운 쥐 (그리디)
 *   버전 1: 그 쥐의 지금 위치로 감
 *   버전 2: 그 쥐가 PredictSeconds 뒤에 있을 위치(위치 + 속도 x 시간)로 감
 *   한 마리를 잡으면 멈추지 않고 다음 쥐를 쫓음
 * 버전은 콘솔 변수 Tag.CatVersion 으로 바꿈 (1 또는 2)
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
	void Think();                                        // 0.25초마다 쫓을 쥐와 목표 위치를 다시 정함
	APawn* ChooseTarget(const TArray<APawn*>& Rats);     // 걸어서 가장 가까운 쥐

	FTimerHandle ThinkHandle;
	TWeakObjectPtr<APawn> CurrentTarget;
	TArray<int32> MyDist;                                // BFS 결과 (매번 새로 할당하지 않으려고 멤버로 둠)
};
