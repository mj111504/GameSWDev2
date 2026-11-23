#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TagAgentSubsystem.generated.h"

/**
 * 고양이·쥐 명단 관리자
 * - 고양이 N마리, 쥐 M마리가 있어도 돌아가게, 누가 있는지를 한곳에서 관리함
 * - 각 AI 컨트롤러가 캐릭터를 조종하기 시작할 때 자기를 등록함
 * - 잡힌 쥐는 "살아 있는 쥐" 명단에서 빠짐. 마지막 쥐가 잡히면 총 시간을 출력함
 * - 플레이어가 조종하는 캐릭터는 AI 컨트롤러가 없어서 스스로 등록하지 못함.
 *   그래서 0.5초마다 플레이어 캐릭터를 확인해서 쥐 명단에 넣음 (콘솔 변수 Tag.PlayerIsRat, 기본 1)
 */
UCLASS()
class CAT_AND_RAT_API UTagAgentSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	void RegisterCat(APawn* Cat);
	void UnregisterCat(APawn* Cat);
	void RegisterRat(APawn* Rat);
	void UnregisterRat(APawn* Rat);

	void GetCats(TArray<APawn*>& OutCats) const;
	void GetAliveRats(TArray<APawn*>& OutRats) const;
	bool IsRatAlive(const APawn* Rat) const;

	// 고양이가 쥐를 잡았을 때 호출. 그 쥐가 아직 살아 있었으면 true (같은 쥐를 두 번 잡지 않게)
	bool ReportCaught(APawn* Cat, APawn* Rat);

	float GetElapsedSeconds() const;

	// 잡힌 쥐가 플레이어 캐릭터일 때: 조작을 막고 멈춤
	void HandlePlayerCaught(APawn* Rat);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void RegisterPlayers();      // 0.5초마다 플레이어 캐릭터를 쥐로 등록

	FTimerHandle PlayerScanHandle;
	TArray<TWeakObjectPtr<APawn>> CaughtRats;   // 한 번 잡힌 쥐는 다시 등록하지 않음
	TArray<TWeakObjectPtr<APawn>> Cats;
	TArray<TWeakObjectPtr<APawn>> AliveRats;
	int32 NumRatsTotal = 0;
	int32 NumCaught = 0;
	float StartTime = 0.f;
};
