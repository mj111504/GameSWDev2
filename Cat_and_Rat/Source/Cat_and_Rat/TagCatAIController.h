#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "TagCatAIController.generated.h"

/**
 * 고양이(술래) AI
 *   버전 1: 쥐의 지금 위치로 감
 *   버전 2: 쥐가 PredictSeconds 뒤에 있을 위치(위치 + 속도 x 시간)로 감
 * 버전은 콘솔 변수 Tag.CatVersion 으로 바꿈 (1 또는 2)
 */
UCLASS()
class CAT_AND_RAT_API ATagCatAIController : public AAIController
{
	GENERATED_BODY()

public:
	ATagCatAIController();
	virtual void Tick(float DeltaSeconds) override;

	bool HasCaught() const { return bCaught; }

protected:
	virtual void OnPossess(APawn* InPawn) override;

private:
	void Think();                // 0.25초마다 목표 위치를 다시 정함
	APawn* FindRat();

	FTimerHandle ThinkHandle;
	TWeakObjectPtr<APawn> Rat;
	bool bCaught = false;
	float StartTime = 0.f;
};
