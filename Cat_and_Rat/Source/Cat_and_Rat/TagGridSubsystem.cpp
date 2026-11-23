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
	const int32 NumColumns = NumX * NumY;

	NodePos.Reset();
	NodeColumn.Reset();
	ColumnStart.SetNum(NumColumns + 1);
	MaxFloorsInColumn = 0;

	// 1) 기둥마다 아래에서 위로 1m 간격으로 NavMesh를 찾아서, 걸을 수 있는 바닥 높이를 전부 모음
	//    (같은 바닥을 여러 번 찾으면 하나로 합침)
	const float StepZ = 100.f;
	const FVector Extent(CellSize * 0.25f, CellSize * 0.25f, StepZ * 0.6f);
	TArray<FVector> Found;

	for (int32 Y = 0; Y < NumY; ++Y)
	{
		for (int32 X = 0; X < NumX; ++X)
		{
			const int32 Col = Y * NumX + X;
			ColumnStart[Col] = NodePos.Num();
			const float Cx = Origin.X + (X + 0.5f) * CellSize;
			const float Cy = Origin.Y + (Y + 0.5f) * CellSize;

			Found.Reset();
			for (float Z = Bounds.Min.Z; Z <= Bounds.Max.Z; Z += StepZ)
			{
				FNavLocation NavLoc;
				if (!NavSys->ProjectPointToNavigation(FVector(Cx, Cy, Z), NavLoc, Extent)) continue;

				bool bSameFloor = false;
				for (const FVector& F : Found)
				{
					if (FMath::Abs(F.Z - NavLoc.Location.Z) < 60.f) { bSameFloor = true; break; }
				}
				if (!bSameFloor) Found.Add(NavLoc.Location);
			}

			Found.Sort([](const FVector& A, const FVector& B) { return A.Z < B.Z; });
			for (const FVector& F : Found)
			{
				NodePos.Add(F);
				NodeColumn.Add(Col);
			}
			MaxFloorsInColumn = FMath::Max(MaxFloorsInColumn, Found.Num());
		}
	}
	ColumnStart[NumColumns] = NodePos.Num();

	const int32 N = NodePos.Num();
	BaseCost.Init(1.f, N);

	// 2) 간선 만들기: 상하좌우 기둥에서 높이 차이가 MaxStepZ 이하인 칸 중 가장 가까운 높이의 칸.
	//    그리고 NavMesh 위에서 두 점 사이를 직선으로 쏴서 막히지 않을 때만 연결
	//    (얇은 벽을 사이에 둔 두 칸이 연결되는 것을 막음)
	Neighbors.Init(INDEX_NONE, N * 4);
	NumLinks = 0;
	const int32 DX[4] = { -1, 1, 0, 0 };
	const int32 DY[4] = { 0, 0, -1, 1 };

	for (int32 Node = 0; Node < N; ++Node)
	{
		const int32 Col = NodeColumn[Node];
		const int32 X = Col % NumX;
		const int32 Y = Col / NumX;

		for (int32 D = 0; D < 4; ++D)
		{
			const int32 Nx = X + DX[D];
			const int32 Ny = Y + DY[D];
			if (Nx < 0 || Nx >= NumX || Ny < 0 || Ny >= NumY) continue;

			const int32 NCol = Ny * NumX + Nx;
			int32 Best = INDEX_NONE;
			float BestDz = MaxStepZ;
			for (int32 M = ColumnStart[NCol]; M < ColumnStart[NCol + 1]; ++M)
			{
				const float Dz = FMath::Abs(NodePos[M].Z - NodePos[Node].Z);
				if (Dz <= BestDz) { BestDz = Dz; Best = M; }
			}
			if (Best == INDEX_NONE) continue;

			FVector HitLocation;
			const bool bBlocked = UNavigationSystemV1::NavigationRaycast(World, NodePos[Node], NodePos[Best], HitLocation);
			if (bBlocked) continue;

			Neighbors[Node * 4 + D] = Best;
			++NumLinks;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[TagGrid] Built %d x %d columns, %d cells, %d links, max %d floors in one column, cell size %.0f"),
		NumX, NumY, N, NumLinks, MaxFloorsInColumn, CellSize);

	// 게임 시작 직후 NavMesh가 아직 준비 안 됐을 수 있어서, 0칸이면 0.5초 뒤 다시 시도 (최대 10번)
	if (N == 0 && RetryCount < 10)
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

	// 그 기둥의 칸 중 높이가 가장 가까운 칸. 캐릭터 위치는 몸 가운데라 바닥보다 1m쯤 높아서 2.5m까지 허용
	const int32 Col = Y * NumX + X;
	int32 Best = INDEX_NONE;
	float BestDz = 250.f;
	for (int32 M = ColumnStart[Col]; M < ColumnStart[Col + 1]; ++M)
	{
		const float Dz = FMath::Abs(NodePos[M].Z - WorldPos.Z);
		if (Dz <= BestDz) { BestDz = Dz; Best = M; }
	}
	return Best;
}

FVector UTagGridSubsystem::CellToWorld(int32 Cell) const
{
	return IsValidCell(Cell) ? NodePos[Cell] : FVector::ZeroVector;
}

int32 UTagGridSubsystem::GetNeighborsFixed(int32 Cell, int32 OutNb[4]) const
{
	int32 Count = 0;
	for (int32 D = 0; D < 4; ++D)
	{
		const int32 Nb = Neighbors[Cell * 4 + D];
		if (Nb != INDEX_NONE) OutNb[Count++] = Nb;
	}
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
	const int32 N = NodePos.Num();
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

void UTagGridSubsystem::BFSMulti(const TArray<int32>& Starts, TArray<int32>& OutDist) const
{
	// 출발점 여러 개를 처음부터 큐에 거리 0으로 넣고 BFS를 한 번 돌림.
	// 결과 = 각 칸에서 "가장 가까운 출발점"까지의 칸 수. 출발점이 몇 개든 O(n)
	const int32 N = NodePos.Num();
	OutDist.Init(-1, N);

	TArray<int32> Queue;
	Queue.Reserve(N);
	for (int32 S : Starts)
	{
		if (IsWalkable(S) && OutDist[S] < 0)
		{
			OutDist[S] = 0;
			Queue.Add(S);
		}
	}

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

int32 UTagGridSubsystem::FindNearestWalkableCell(const FVector& Pos) const
{
	// 캐릭터가 벽에 붙어 있으면 그 기둥에 칸이 없을 수 있음.
	// 그럴 때 주변 2칸 안 기둥들에서, 높이 차이 2.5m 이내인 칸 중 가장 가까운 칸을 씀
	const int32 Center = WorldToCell(Pos);
	if (Center != INDEX_NONE) return Center;
	if (NumX <= 0 || NumY <= 0) return INDEX_NONE;

	const int32 X0 = FMath::FloorToInt((Pos.X - Origin.X) / CellSize);
	const int32 Y0 = FMath::FloorToInt((Pos.Y - Origin.Y) / CellSize);

	int32 Best = INDEX_NONE;
	float BestD2 = TNumericLimits<float>::Max();
	for (int32 Dy = -2; Dy <= 2; ++Dy)
	{
		for (int32 Dx = -2; Dx <= 2; ++Dx)
		{
			const int32 X = X0 + Dx, Y = Y0 + Dy;
			if (X < 0 || X >= NumX || Y < 0 || Y >= NumY) continue;
			const int32 Col = Y * NumX + X;
			for (int32 M = ColumnStart[Col]; M < ColumnStart[Col + 1]; ++M)
			{
				if (FMath::Abs(NodePos[M].Z - Pos.Z) > 250.f) continue;
				const float D2 = FVector::DistSquared(NodePos[M], Pos);
				if (D2 < BestD2) { BestD2 = D2; Best = M; }
			}
		}
	}
	return Best;
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
	const int32 N = NodePos.Num();
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

	// 칸 = 초록 네모. 높이가 달라지는 연결(계단·경사로) = 노란 선
	const float Half = CellSize * 0.5f;
	for (int32 Cell = 0; Cell < NodePos.Num(); ++Cell)
	{
		DrawDebugBox(World, NodePos[Cell] + FVector(0, 0, 5.f), FVector(Half * 0.9f, Half * 0.9f, 2.f),
			FColor::Green, false, Duration);

		int32 Nb[4];
		const int32 Count = GetNeighborsFixed(Cell, Nb);
		for (int32 i = 0; i < Count; ++i)
		{
			if (Nb[i] > Cell && FMath::Abs(NodePos[Nb[i]].Z - NodePos[Cell].Z) > 20.f)
			{
				DrawDebugLine(World, NodePos[Cell] + FVector(0, 0, 10.f), NodePos[Nb[i]] + FVector(0, 0, 10.f),
					FColor::Yellow, false, Duration, 0, 4.f);
			}
		}
	}
}

void UTagGridSubsystem::DrawDistances(const TArray<int32>& Dist, float Duration) const
{
	UWorld* World = GetWorld();
	if (!World || Dist.Num() != NodePos.Num()) return;

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
//   Tag.DrawGrid    : 걸을 수 있는 칸(초록 네모)과 층이 바뀌는 연결(노란 선)을 10초 동안 표시
//   Tag.DrawBFS     : 플레이어(Play 중) 위치에서 BFS 거리를 색으로 10초 동안 표시 + 계산 시간 출력
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

		const int32 Start = Grid->FindNearestWalkableCell(Pawn->GetActorLocation());
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
