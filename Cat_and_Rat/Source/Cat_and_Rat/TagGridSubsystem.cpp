#include "TagGridSubsystem.h"

#include "NavigationSystem.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"

bool UTagGridSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTagGridSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	RetryCount = 0;
	BuildGrid(CellSize);
}

void UTagGridSubsystem::BuildGrid(float InCellSize)
{
	UWorld* World = GetWorld();
	if (!World) return;

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TagGrid] No navigation system."));
		return;
	}

	// Nav Mesh Bounds Volume 범위 = 격자 범위
	const FBox Bounds = NavSys->GetNavigableWorldBounds();
	if (!Bounds.IsValid)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TagGrid] No Nav Mesh Bounds Volume."));
		return;
	}

	CellSize = FMath::Max(InCellSize, 10.f);
	Origin = FVector(Bounds.Min.X, Bounds.Min.Y, 0.f);
	NumX = FMath::Max(1, FMath::FloorToInt(Bounds.GetSize().X / CellSize));
	NumY = FMath::Max(1, FMath::FloorToInt(Bounds.GetSize().Y / CellSize));

	const int32 N = NumX * NumY;
	Walkable.Init(false, N);
	BaseCost.Init(1.f, N);
	CellZ.Init(0.f, N);
	NumWalkable = 0;

	// 칸 중심을 NavMesh에 투영해 보고, 칸 안쪽(반경 1/4칸)에 NavMesh가 있으면 걸을 수 있는 칸
	const float ProbeZ = Bounds.GetCenter().Z;
	const FVector Extent(CellSize * 0.25f, CellSize * 0.25f, Bounds.GetExtent().Z + 100.f);

	for (int32 Y = 0; Y < NumY; ++Y)
	{
		for (int32 X = 0; X < NumX; ++X)
		{
			const int32 Cell = Y * NumX + X;
			const FVector Center(Origin.X + (X + 0.5f) * CellSize, Origin.Y + (Y + 0.5f) * CellSize, ProbeZ);

			FNavLocation NavLoc;
			if (NavSys->ProjectPointToNavigation(Center, NavLoc, Extent))
			{
				Walkable[Cell] = true;
				CellZ[Cell] = NavLoc.Location.Z;
				++NumWalkable;
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[TagGrid] Built %d x %d = %d cells, walkable %d, cell size %.0f"),
		NumX, NumY, N, NumWalkable, CellSize);

	// 게임 시작 직후 NavMesh가 아직 준비 안 됐을 수 있어서, 0칸이면 0.5초 뒤 다시 시도 (최대 10번)
	if (NumWalkable == 0 && RetryCount < 10)
	{
		++RetryCount;
		World->GetTimerManager().SetTimer(RetryHandle,
			FTimerDelegate::CreateUObject(this, &UTagGridSubsystem::BuildGrid, CellSize), 0.5f, false);
	}
}

int32 UTagGridSubsystem::WorldToCell(const FVector& WorldPos) const
{
	if (NumX <= 0 || NumY <= 0) return INDEX_NONE;

	const int32 X = FMath::FloorToInt((WorldPos.X - Origin.X) / CellSize);
	const int32 Y = FMath::FloorToInt((WorldPos.Y - Origin.Y) / CellSize);
	if (X < 0 || X >= NumX || Y < 0 || Y >= NumY) return INDEX_NONE;

	return Y * NumX + X;
}

FVector UTagGridSubsystem::CellToWorld(int32 Cell) const
{
	if (!IsValidCell(Cell)) return FVector::ZeroVector;

	const int32 X = Cell % NumX;
	const int32 Y = Cell / NumX;
	return FVector(Origin.X + (X + 0.5f) * CellSize, Origin.Y + (Y + 0.5f) * CellSize, CellZ[Cell]);
}

int32 UTagGridSubsystem::GetNeighborsFixed(int32 Cell, int32 OutNb[4]) const
{
	const int32 X = Cell % NumX;
	const int32 Y = Cell / NumX;
	int32 Count = 0;

	if (X > 0        && Walkable[Cell - 1])    OutNb[Count++] = Cell - 1;    // 왼쪽
	if (X < NumX - 1 && Walkable[Cell + 1])    OutNb[Count++] = Cell + 1;    // 오른쪽
	if (Y > 0        && Walkable[Cell - NumX]) OutNb[Count++] = Cell - NumX; // 아래
	if (Y < NumY - 1 && Walkable[Cell + NumX]) OutNb[Count++] = Cell + NumX; // 위

	return Count;
}

void UTagGridSubsystem::GetNeighbors(int32 Cell, TArray<int32>& OutNeighbors) const
{
	OutNeighbors.Reset();
	if (!IsWalkable(Cell)) return;

	int32 Nb[4];
	const int32 Count = GetNeighborsFixed(Cell, Nb);
	OutNeighbors.Append(Nb, Count);
}

int32 UTagGridSubsystem::CountWalkableNeighbors(int32 Cell) const
{
	if (!IsWalkable(Cell)) return 0;
	int32 Nb[4];
	return GetNeighborsFixed(Cell, Nb);
}

void UTagGridSubsystem::BFS(int32 Start, TArray<int32>& OutDist) const
{
	const int32 N = Walkable.Num();
	OutDist.Init(-1, N);
	if (!IsWalkable(Start)) return;

	// 큐를 배열 + 앞쪽 인덱스(Head)로 구현. 각 칸은 한 번만 들어가므로 최대 N개
	TArray<int32> Queue;
	Queue.Reserve(N);

	OutDist[Start] = 0;
	Queue.Add(Start);

	int32 Nb[4];
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 Cur = Queue[Head];
		const int32 Count = GetNeighborsFixed(Cur, Nb);
		for (int32 i = 0; i < Count; ++i)
		{
			if (OutDist[Nb[i]] < 0)
			{
				OutDist[Nb[i]] = OutDist[Cur] + 1;
				Queue.Add(Nb[i]);
			}
		}
	}
}

namespace
{
	struct FDijkstraNode
	{
		int32 Cell;
		float Dist;
	};

	// TArray 힙은 이 비교 기준에서 "가장 작은 것"이 맨 위에 오는 최소 힙
	struct FDijkstraLess
	{
		bool operator()(const FDijkstraNode& A, const FDijkstraNode& B) const { return A.Dist < B.Dist; }
	};
}

void UTagGridSubsystem::Dijkstra(int32 Start, const TArray<float>& ExtraCost, TArray<float>& OutDist) const
{
	const int32 N = Walkable.Num();
	const float Inf = TNumericLimits<float>::Max();
	OutDist.Init(Inf, N);
	if (!IsWalkable(Start)) return;

	const bool bUseExtra = (ExtraCost.Num() == N);

	TArray<FDijkstraNode> Heap;
	Heap.Reserve(N);

	OutDist[Start] = 0.f;
	Heap.HeapPush({ Start, 0.f }, FDijkstraLess());

	int32 Nb[4];
	while (Heap.Num() > 0)
	{
		FDijkstraNode Top;
		Heap.HeapPop(Top, FDijkstraLess());

		// 이미 더 짧은 거리로 처리된 칸이면 건너뜀 (지연 삭제 방식)
		if (Top.Dist > OutDist[Top.Cell]) continue;

		const int32 Count = GetNeighborsFixed(Top.Cell, Nb);
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 Next = Nb[i];
			const float StepCost = BaseCost[Next] + (bUseExtra ? ExtraCost[Next] : 0.f);
			const float NewDist = Top.Dist + StepCost;
			if (NewDist < OutDist[Next])
			{
				OutDist[Next] = NewDist;
				Heap.HeapPush({ Next, NewDist }, FDijkstraLess());
			}
		}
	}
}

void UTagGridSubsystem::DrawGrid(float Duration) const
{
	UWorld* World = GetWorld();
	if (!World) return;

	const float Half = CellSize * 0.5f;
	for (int32 Cell = 0; Cell < Walkable.Num(); ++Cell)
	{
		if (!Walkable[Cell]) continue;
		DrawDebugBox(World, CellToWorld(Cell) + FVector(0, 0, 5.f), FVector(Half * 0.9f, Half * 0.9f, 2.f),
			FColor::Green, false, Duration);
	}
}

void UTagGridSubsystem::DrawDistances(const TArray<int32>& Dist, float Duration) const
{
	UWorld* World = GetWorld();
	if (!World || Dist.Num() != Walkable.Num()) return;

	int32 MaxDist = 1;
	for (int32 D : Dist) MaxDist = FMath::Max(MaxDist, D);

	// 가까운 칸 = 초록, 먼 칸 = 빨강
	const float Half = CellSize * 0.5f;
	for (int32 Cell = 0; Cell < Dist.Num(); ++Cell)
	{
		if (Dist[Cell] < 0) continue;
		const float T = static_cast<float>(Dist[Cell]) / MaxDist;
		const FColor Color = FLinearColor::LerpUsingHSV(FLinearColor::Green, FLinearColor::Red, T).ToFColor(true);
		DrawDebugSolidBox(World, CellToWorld(Cell) + FVector(0, 0, 5.f), FVector(Half * 0.9f, Half * 0.9f, 2.f),
			Color, false, Duration);
	}
}

// ---------------------------------------------------------------------------
// 콘솔 명령 (게임 실행 중 ` 키로 콘솔을 열고 입력)
//   Tag.Build 150   : 칸 크기 150으로 격자 다시 만들기
//   Tag.DrawGrid    : 걸을 수 있는 칸을 10초 동안 표시
//   Tag.DrawBFS     : 플레이어 위치에서 BFS 거리를 색으로 10초 동안 표시 + 계산 시간 출력
// ---------------------------------------------------------------------------
namespace
{
	UTagGridSubsystem* GetGrid(UWorld* World)
	{
		UTagGridSubsystem* Grid = World ? World->GetSubsystem<UTagGridSubsystem>() : nullptr;
		if (!Grid)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TagGrid] Run this command while playing (PIE)."));
		}
		return Grid;
	}

	void CmdBuild(const TArray<FString>& Args, UWorld* World)
	{
		if (UTagGridSubsystem* Grid = GetGrid(World))
		{
			const float Size = Args.Num() > 0 ? FCString::Atof(*Args[0]) : Grid->GetCellSize();
			Grid->BuildGrid(Size);
		}
	}

	void CmdDrawGrid(const TArray<FString>& Args, UWorld* World)
	{
		if (UTagGridSubsystem* Grid = GetGrid(World))
		{
			Grid->DrawGrid(10.f);
		}
	}

	void CmdDrawBFS(const TArray<FString>& Args, UWorld* World)
	{
		UTagGridSubsystem* Grid = GetGrid(World);
		APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
		if (!Grid || !Pawn) return;

		const int32 Start = Grid->WorldToCell(Pawn->GetActorLocation());
		if (!Grid->IsWalkable(Start))
		{
			UE_LOG(LogTemp, Warning, TEXT("[TagGrid] Player is not on a walkable cell."));
			return;
		}

		TArray<int32> Dist;
		const double T0 = FPlatformTime::Seconds();
		Grid->BFS(Start, Dist);
		const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;

		int32 Reached = 0;
		for (int32 D : Dist) if (D >= 0) ++Reached;

		UE_LOG(LogTemp, Log, TEXT("[TagGrid] BFS from cell %d: reached %d / %d walkable, %.3f ms"),
			Start, Reached, Grid->GetNumWalkable(), Ms);

		Grid->DrawDistances(Dist, 10.f);
	}

	FAutoConsoleCommandWithWorldAndArgs GCmdTagBuild(
		TEXT("Tag.Build"), TEXT("Rebuild tag grid. Usage: Tag.Build [CellSize]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CmdBuild));

	FAutoConsoleCommandWithWorldAndArgs GCmdTagDrawGrid(
		TEXT("Tag.DrawGrid"), TEXT("Draw walkable cells for 10 seconds."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CmdDrawGrid));

	FAutoConsoleCommandWithWorldAndArgs GCmdTagDrawBFS(
		TEXT("Tag.DrawBFS"), TEXT("Draw BFS distance from the player for 10 seconds."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CmdDrawBFS));
}
