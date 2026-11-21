#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TagGridSubsystem.generated.h"

/**
 * 술래잡기 격자 그래프
 * - 바닥을 CellSize 크기 칸으로 나눔. 걸을 수 있는 칸 = 정점, 상하좌우로 붙은 칸 = 간선
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
	void BuildGrid(float InCellSize = 150.f);

	// 좌표 변환. 격자 밖이면 INDEX_NONE(-1)
	UFUNCTION(BlueprintPure, Category = "TagGrid")
	int32 WorldToCell(const FVector& WorldPos) const;

	UFUNCTION(BlueprintPure, Category = "TagGrid")
	FVector CellToWorld(int32 Cell) const;

	bool  IsValidCell(int32 Cell) const { return Cell >= 0 && Cell < Walkable.Num(); }
	bool  IsWalkable(int32 Cell) const { return IsValidCell(Cell) && Walkable[Cell]; }
	int32 GetNumCells() const { return Walkable.Num(); }
	int32 GetNumWalkable() const { return NumWalkable; }
	int32 GetNumX() const { return NumX; }
	int32 GetNumY() const { return NumY; }
	float GetCellSize() const { return CellSize; }

	// 상하좌우 중 걸을 수 있는 이웃 칸
	void  GetNeighbors(int32 Cell, TArray<int32>& OutNeighbors) const;

	// 쥐 도주 때 "열린 칸" 가산점용
	int32 CountWalkableNeighbors(int32 Cell) const;

	// 기본 비용 (지금은 전부 1. 나중에 가구가 놓이면 이 값만 바꿈)
	float GetBaseCost(int32 Cell) const { return IsValidCell(Cell) ? BaseCost[Cell] : 0.f; }
	void  SetBaseCost(int32 Cell, float Cost) { if (IsValidCell(Cell)) { BaseCost[Cell] = Cost; } }

	// BFS: Start에서 모든 칸까지의 칸 수 거리. 못 가는 칸은 -1
	void BFS(int32 Start, TArray<int32>& OutDist) const;

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

	float CellSize = 150.f;
	FVector Origin = FVector::ZeroVector; // 격자 왼쪽 아래 모서리 (X, Y 최솟값)
	int32 NumX = 0;
	int32 NumY = 0;
	int32 NumWalkable = 0;

	TArray<bool>  Walkable;  // 칸 번호 = Y * NumX + X
	TArray<float> BaseCost;
	TArray<float> CellZ;     // NavMesh 위 실제 높이

	FTimerHandle RetryHandle;
	int32 RetryCount = 0;
};
