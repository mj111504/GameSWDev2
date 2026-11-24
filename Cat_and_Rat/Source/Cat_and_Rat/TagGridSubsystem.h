#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TagGridSubsystem.generated.h"

/**
 * 술래잡기 격자 그래프 (층 지원)
 * - 바닥을 위에서 볼 때 CellSize 크기의 "기둥(column)"으로 나눔
 * - 기둥 하나에 걸을 수 있는 바닥이 여러 층 있으면, 바닥 하나마다 칸 하나를 만듦
 *   예: 1층 바닥 위에 2층 바닥이 겹친 곳 = 칸 2개
 * - 칸 = 정점. 상하좌우 기둥의 칸 중 높이 차이가 MaxStepZ 이하이고,
 *   NavMesh 위에서 직선으로 막힘없이 이어지면 간선으로 연결
 *   (높이가 다른 두 칸은 직선 검사가 실패해도 NavMesh 실제 길이 짧으면 연결)
 *   → 계단·경사로는 한 칸씩 조금씩 높아지므로 자동으로 층끼리 연결됨
 * - NavMesh에서 자동 생성 (하드코딩 없음)
 * - 술래 AI와 쥐 AI가 이 객체 하나를 같이 씀
 */
UCLASS()
class CAT_AND_RAT_API UTagGridSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// NavMesh를 검사해서 격자를 (다시) 만든다
	UFUNCTION(BlueprintCallable, Category = "TagGrid")
	void BuildGrid(float InCellSize = 35.f);

	// 좌표 → 칸. 그 위치 기둥의 칸 중 높이가 가장 가까운 칸. 없으면 INDEX_NONE(-1)
	UFUNCTION(BlueprintPure, Category = "TagGrid")
	int32 WorldToCell(const FVector& WorldPos) const;

	// 칸 → 좌표 (NavMesh 위의 점)
	UFUNCTION(BlueprintPure, Category = "TagGrid")
	FVector CellToWorld(int32 Cell) const;

	bool  IsValidCell(int32 Cell) const { return Cell >= 0 && Cell < NodePos.Num(); }
	bool  IsWalkable(int32 Cell) const { return IsValidCell(Cell); }   // 칸은 전부 걸을 수 있는 바닥에만 만듦
	int32 GetNumCells() const { return NodePos.Num(); }
	int32 GetNumWalkable() const { return NodePos.Num(); }
	int32 GetNumLinks() const { return NumLinks; }
	int32 GetNumX() const { return NumX; }
	int32 GetNumY() const { return NumY; }
	float GetCellSize() const { return CellSize; }
	int32 GetBuildCount() const { return BuildCount; }   // BuildGrid가 끝날 때마다 1씩 늘어남

	// 상하좌우로 연결된 칸 (층이 바뀌는 계단·경사로 연결 포함)
	void  GetNeighbors(int32 Cell, TArray<int32>& OutNeighbors) const;

	// 쥐 도주 때 "열린 칸" 가산점용
	int32 CountWalkableNeighbors(int32 Cell) const;

	// 기본 비용 (지금은 전부 1. 나중에 가구가 놓이면 이 값만 바꿈)
	float GetBaseCost(int32 Cell) const { return IsValidCell(Cell) ? BaseCost[Cell] : 0.f; }
	void  SetBaseCost(int32 Cell, float Cost) { if (IsValidCell(Cell)) { BaseCost[Cell] = Cost; } }

	// BFS: Start에서 모든 칸까지의 칸 수 거리. 못 가는 칸은 -1
	void BFS(int32 Start, TArray<int32>& OutDist) const;

	// 여러 출발점 BFS: 각 칸에서 Starts 중 가장 가까운 칸까지의 칸 수. 못 가는 칸은 -1
	// 고양이가 여러 마리일 때 "가장 가까운 고양이까지 거리"를 BFS 한 번으로 구하려고 씀
	void BFSMulti(const TArray<int32>& Starts, TArray<int32>& OutDist) const;

	// Pos가 있는 칸. 없으면 주변 2칸 안에서 가장 가까운 칸. 그래도 없으면 INDEX_NONE
	int32 FindNearestWalkableCell(const FVector& Pos) const;

	// 다익스트라(힙): 기본 비용 + ExtraCost(위험도 비용). ExtraCost가 비어 있으면 기본 비용만 씀
	// 못 가는 칸은 TNumericLimits<float>::Max()
	void Dijkstra(int32 Start, const TArray<float>& ExtraCost, TArray<float>& OutDist) const;

	// 디버그 그리기
	void DrawGrid(float Duration) const;
	void DrawDistances(const TArray<int32>& Dist, float Duration) const;

protected:
	// 에디터 화면이 아니라 게임 실행(PIE 포함) 때만 만든다
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	// 상하좌우 이웃을 고정 배열에 담고 개수를 돌려줌 (BFS 안에서 메모리 할당 안 하려고)
	int32 GetNeighborsFixed(int32 Cell, int32 OutNb[4]) const;

	float CellSize = 35.f;    // 실제 시작 값은 콘솔 변수 Tag.CellSize (기본 35)
	float MaxStepZ = 120.f;   // 옆 칸과 이 높이 차이까지는 이어진 바닥(계단·경사로)으로 봄. 층 사이 높이보다 작아야 함
	FVector Origin = FVector::ZeroVector; // 격자 왼쪽 아래 모서리 (X, Y 최솟값)
	int32 NumX = 0;
	int32 NumY = 0;
	int32 NumLinks = 0;
	int32 MaxFloorsInColumn = 0;
	int32 BuildCount = 0;

	// 칸 정보. 칸 번호 = 이 배열들의 위치
	TArray<FVector> NodePos;     // NavMesh 위의 점
	TArray<int32>   NodeColumn;  // 어느 기둥에 속하는지 (기둥 번호 = Y * NumX + X)
	TArray<int32>   Neighbors;   // 칸마다 4개 (왼쪽, 오른쪽, 아래, 위). 없으면 -1
	TArray<float>   BaseCost;

	// 기둥 c에 속한 칸 = ColumnStart[c] ~ ColumnStart[c + 1] - 1 (높이 낮은 순)
	TArray<int32>   ColumnStart;

	FTimerHandle RetryHandle;
	int32 RetryCount = 0;
};
