#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "TagRatAIController.generated.h"

class UTagGridSubsystem;

/**
 * 쥐 AI - 도주
 *   방식 0 (기본): BFS 2번(고양이 기준, 쥐 기준)으로 "고양이보다 먼저 도착하는 칸" 중
 *                  고양이에게서 가장 먼 칸으로 감. 주변이 열린 칸에 가산점
 *   방식 1: 고양이 반대 방향으로 단순히 달림 (실험 4 비교용)
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

private:
	void Think();
	APawn* FindCat();
	int32 FindNearestWalkableCell(const UTagGridSubsystem* Grid, const FVector& Pos) const;

	FTimerHandle ThinkHandle;
	TWeakObjectPtr<APawn> Cat;
	bool bCaught = false;

	TArray<int32> RatDist;  // BFS 결과를 매번 새로 할당하지 않으려고 멤버로 둠
	TArray<int32> CatDist;
};
