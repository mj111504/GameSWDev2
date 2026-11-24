#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TagAgentSubsystem.generated.h"

class APickupItem;
class UTagGridSubsystem;

// 이벤트. 점수·어지러움 UI, 승리 판정은 블루프린트에서 이 이벤트에 연결하면 됨
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FTagOnItemExported, APickupItem*, Item, APawn*, Rat, int32, Score);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FTagOnItemRecovered, APickupItem*, Item, APawn*, Cat, int32, Score);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTagOnMessinessChanged, float, Percent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTagOnRatsWin);

// 필드에 있는 물건 하나의 상태
struct FTagItemEntry
{
	TWeakObjectPtr<APickupItem> Item;
	FVector Home = FVector::ZeroVector;   // 처음 본 위치 = 원래 위치 (고양이 회수용)
	bool bHomeSet = false;
	TWeakObjectPtr<APawn> Holder;         // 지금 들고 있는 캐릭터 (없으면 바닥에 있음)
	TWeakObjectPtr<APawn> LastHolder;     // 마지막으로 들었던 캐릭터 (반출 점수를 누구에게 줄지)
	TWeakObjectPtr<APawn> ReservedBy;     // 이 물건을 가지러 가는 중인 쥐 (두 마리가 같은 물건으로 몰리지 않게)
	TWeakObjectPtr<APawn> ReservedByCat;  // 이 물건을 회수하러 가는 중인 고양이
	bool bExported = false;               // 반출됐고 아직 제자리로 안 돌아옴 = 어지러움에 들어가 있음
};

/**
 * 고양이·쥐·물건·반출구역 명단 관리자
 * - 고양이 N마리, 쥐 M마리가 있어도 돌아가게, 누가 있는지를 한곳에서 관리함
 * - 각 AI 컨트롤러가 캐릭터를 조종하기 시작할 때 자기를 등록함
 * - 잡힌 쥐는 "살아 있는 쥐" 명단에서 빠짐. 마지막 쥐가 잡히면 총 시간을 출력함
 * - 플레이어가 조종하는 캐릭터는 AI 컨트롤러가 없어서 스스로 등록하지 못함.
 *   그래서 0.5초마다 플레이어 캐릭터를 확인해서 쥐 명단에 넣음 (콘솔 변수 Tag.PlayerIsRat, 기본 1)
 * - 물건(APickupItem): 0.25초마다 맵을 훑어서 새로 생긴 물건을 명단에 넣음 (게임 시작 때 생성돼도 잡힘)
 *   누가 들고 있는지는 물건이 어느 캐릭터에 붙어 있는지로 판단함
 * - 반출구역: Actor Tag가 "ExportZone"인 액터 (Trigger Box 권장)
 *   들고 있지 않은 물건이 반출구역 안에 있으면 반출 완료 처리 (AI 쥐·플레이어 쥐 공통)
 * - 어지러움 (기획서 5.4): 물건 하나 = 가중치 / 전체 가중치 x 100
 *   반출되면 올라가고, 고양이가 원래 위치(Tag.RecoverRadius 안)에 내려놓아야 내려감.
 *   저장소에서 꺼내기만 하면 그대로. Tag.MessWinPercent(기본 80)%를 넘으면 쥐 팀 승리 이벤트
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

	// 잡힌 쥐가 플레이어 캐릭터일 때: 들고 있던 물건을 떨어뜨리고, 조작을 막고 멈춤
	void HandlePlayerCaught(APawn* Rat);

	// ---------- 물건 ----------
	const TArray<FTagItemEntry>& GetItems() const { return Items; }

	// 쥐가 물건을 찜함. 다른 살아 있는 쥐가 이미 찜했으면 false
	bool ReserveItem(APickupItem* Item, APawn* Rat);
	void ReleaseReservations(APawn* Rat);
	// 물건 넘기기: 찜을 다른 쥐에게 옮김
	void TransferReservation(APickupItem* Item, APawn* NewRat);

	// 고양이 회수용 찜. 다른 고양이가 이미 찜했으면 false
	bool ReserveItemForCat(APickupItem* Item, APawn* Cat);
	void ReleaseCatReservations(APawn* Cat);
	// Except를 뺀, 지금 회수 중(찜했거나 들고 있음)인 고양이 수
	int32 CountRecoveringCats(const APawn* Except) const;
	const FTagItemEntry* FindEntry(const APickupItem* Item) const;

	// 기획서 점수: 소형(1) 5점, 중형(3) 15점, 대형(6) 30점, 초대형(10) 50점
	static int32 GetItemScore(int32 Weight);
	// 팀원 InteractionComponent와 같은 표: 1 → 100%, 3 → 75%, 6 → 50%, 10 → 30%
	static float GetCarrySpeedMultiplier(int32 Weight);

	// 반출됐고 아직 제자리로 안 돌아온 물건의 가중치 합 / 전체 물건 가중치 합 x 100 (기획서 집 어지러움 공식)
	UFUNCTION(BlueprintPure, Category = "TagAI")
	float GetMessinessPercent() const;

	UFUNCTION(BlueprintPure, Category = "TagAI")
	int32 GetExportedScore() const { return ExportedScore; }

	UFUNCTION(BlueprintPure, Category = "TagAI")
	int32 GetRecoveredScore() const { return RecoveredScore; }

	UPROPERTY(BlueprintAssignable, Category = "TagAI")
	FTagOnItemExported OnItemExported;

	UPROPERTY(BlueprintAssignable, Category = "TagAI")
	FTagOnItemRecovered OnItemRecovered;

	UPROPERTY(BlueprintAssignable, Category = "TagAI")
	FTagOnMessinessChanged OnMessinessChanged;

	UPROPERTY(BlueprintAssignable, Category = "TagAI")
	FTagOnRatsWin OnRatsWin;

	// ---------- 반출구역 ----------
	int32 GetNumExportZones() const { return ExportZones.Num(); }
	bool IsInExportZone(const FVector& Pos) const;
	void DrawExportZones(float Duration) const;

	// 반출구역 안의 칸들, 그리고 각 칸에서 가장 가까운 반출구역까지의 칸 수 (여러 출발점 BFS)
	// 격자가 다시 만들어졌을 때만 새로 계산함
	const TArray<int32>& GetExportCells(UTagGridSubsystem* Grid);
	const TArray<int32>& GetExportDist(UTagGridSubsystem* Grid);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void RegisterPlayers();      // 0.5초마다 플레이어 캐릭터를 쥐로 등록
	void UpdateItems();          // 0.25초마다 물건 목록·들고 있는 사람·반출 여부 갱신
	void FindExportZones();
	void ExportItem(FTagItemEntry& Entry);
	void RecoverItem(FTagItemEntry& Entry);
	void CheckRatsWin();
	void EnsureExportMap(UTagGridSubsystem* Grid);

	FTimerHandle PlayerScanHandle;
	FTimerHandle ItemScanHandle;
	TArray<TWeakObjectPtr<APawn>> CaughtRats;   // 한 번 잡힌 쥐는 다시 등록하지 않음
	TArray<TWeakObjectPtr<APawn>> Cats;
	TArray<TWeakObjectPtr<APawn>> AliveRats;
	int32 NumRatsTotal = 0;
	int32 NumCaught = 0;
	float StartTime = 0.f;

	TArray<FTagItemEntry> Items;
	int32 ExportedScore = 0;    // 쥐 팀이 반출로 얻은 점수 합
	int32 RecoveredScore = 0;   // 고양이 팀이 회수로 얻은 점수 합
	int32 MessWeight = 0;       // 지금 어지러움에 들어가 있는 가중치 합
	bool bRatsWon = false;

	TArray<TWeakObjectPtr<AActor>> ExportZones;
	int32 ZoneScanCounter = 0;
	TArray<int32> ExportCells;
	TArray<int32> ExportDist;
	int32 ExportMapBuildCount = -1;   // 이 값이 격자의 BuildCount와 다르면 다시 계산
	int32 ExportMapZoneCount = -1;
};
