// 실험 2. 거리 계산 알고리즘 비교 벤치마크
//
// 콘솔에서 Tag.Bench2 입력 (Play 안 해도 됨). Tag.Bench2 full 이면 80x80 플로이드-워셜도 직접 잼. 결과는 Output Log + Saved/Experiments/exp2_날짜.csv
//
// 2-A. 한 출발점 -> 모든 칸: BFS / 다익스트라(배열) / 다익스트라(힙)
//      격자 20x20, 40x40, 80x80 x (기본 비용만 / 위험도 비용 추가)
// 2-B. 모든 칸 쌍: BFS를 칸마다 n번 / 플로이드-워셜
//      격자 20x20, 40x40 실측. 80x80 플로이드-워셜은 40x40 결과로 추정(n^3 비율), full 옵션이면 실측도 함
//
// 알고리즘 코드는 UTagGridSubsystem의 BFS, Dijkstra와 같은 방식으로 작성함.
// 게임 맵과 상관없이 장애물 없는 정사각 격자로 측정해서, 격자 크기만 변수로 둠.

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"

namespace TagBench
{
	const float Inf = TNumericLimits<float>::Max();

	struct FGrid
	{
		int32 W = 0;
		TArray<float> StepCost; // 그 칸에 들어갈 때 드는 비용
		int32 Num() const { return W * W; }
	};

	// bWeighted = false : 모든 칸 비용 1 (지금 게임 상태)
	// bWeighted = true  : 기본 비용 1 + 위험도 비용. 술래를 마지막으로 본 위치를 격자 3/4 지점으로 가정하고,
	//                     그 근처일수록 최대 10까지 더함 (쥐 배회 때와 같은 모양)
	FGrid MakeGrid(int32 Size, bool bWeighted)
	{
		FGrid G;
		G.W = Size;
		G.StepCost.Init(1.f, Size * Size);
		if (bWeighted)
		{
			const float Cx = Size * 0.75f;
			const float Cy = Size * 0.75f;
			const float Sigma = Size / 8.f;
			for (int32 Y = 0; Y < Size; ++Y)
			{
				for (int32 X = 0; X < Size; ++X)
				{
					const float D2 = FMath::Square(X - Cx) + FMath::Square(Y - Cy);
					G.StepCost[Y * Size + X] = 1.f + 10.f * FMath::Exp(-D2 / (2.f * Sigma * Sigma));
				}
			}
		}
		return G;
	}

	FORCEINLINE int32 Neighbors(const FGrid& G, int32 C, int32 Out[4])
	{
		const int32 X = C % G.W;
		const int32 Y = C / G.W;
		int32 Count = 0;
		if (X > 0)       Out[Count++] = C - 1;
		if (X < G.W - 1) Out[Count++] = C + 1;
		if (Y > 0)       Out[Count++] = C - G.W;
		if (Y < G.W - 1) Out[Count++] = C + G.W;
		return Count;
	}

	// BFS. Parent는 비용 비교용 (BFS가 고른 경로를 따라가기 위해)
	void BFS(const FGrid& G, int32 Start, TArray<int32>& Dist, TArray<int32>& Queue, TArray<int32>* Parent = nullptr)
	{
		const int32 N = G.Num();
		Dist.Init(-1, N);
		if (Parent) Parent->Init(INDEX_NONE, N);
		Queue.Reset();
		Dist[Start] = 0;
		Queue.Add(Start);

		int32 Nb[4];
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const int32 Cur = Queue[Head];
			const int32 Count = Neighbors(G, Cur, Nb);
			for (int32 i = 0; i < Count; ++i)
			{
				if (Dist[Nb[i]] < 0)
				{
					Dist[Nb[i]] = Dist[Cur] + 1;
					if (Parent) (*Parent)[Nb[i]] = Cur;
					Queue.Add(Nb[i]);
				}
			}
		}
	}

	// 다익스트라(배열): 매번 방문 안 한 칸 중 최솟값을 처음부터 끝까지 훑어서 찾음. O(n^2)
	void DijkstraArray(const FGrid& G, int32 Start, TArray<float>& Dist, TArray<bool>& Visited)
	{
		const int32 N = G.Num();
		Dist.Init(Inf, N);
		Visited.Init(false, N);
		Dist[Start] = 0.f;

		int32 Nb[4];
		for (int32 Iter = 0; Iter < N; ++Iter)
		{
			int32 U = INDEX_NONE;
			float Best = Inf;
			for (int32 C = 0; C < N; ++C)
			{
				if (!Visited[C] && Dist[C] < Best)
				{
					Best = Dist[C];
					U = C;
				}
			}
			if (U == INDEX_NONE) break;
			Visited[U] = true;

			const int32 Count = Neighbors(G, U, Nb);
			for (int32 i = 0; i < Count; ++i)
			{
				const float NewDist = Best + G.StepCost[Nb[i]];
				if (NewDist < Dist[Nb[i]]) Dist[Nb[i]] = NewDist;
			}
		}
	}

	struct FNode { int32 Cell; float Dist; };
	struct FNodeLess { bool operator()(const FNode& A, const FNode& B) const { return A.Dist < B.Dist; } };

	// 다익스트라(힙): TArray 최소 힙. O(m log n). MaxHeap은 메모리 측정용
	void DijkstraHeap(const FGrid& G, int32 Start, TArray<float>& Dist, TArray<FNode>& Heap, int32& MaxHeap)
	{
		const int32 N = G.Num();
		Dist.Init(Inf, N);
		Heap.Reset();
		MaxHeap = 0;
		Dist[Start] = 0.f;
		Heap.HeapPush({ Start, 0.f }, FNodeLess());

		int32 Nb[4];
		while (Heap.Num() > 0)
		{
			MaxHeap = FMath::Max(MaxHeap, Heap.Num());
			FNode Top;
			Heap.HeapPop(Top, FNodeLess());
			if (Top.Dist > Dist[Top.Cell]) continue;

			const int32 Count = Neighbors(G, Top.Cell, Nb);
			for (int32 i = 0; i < Count; ++i)
			{
				const float NewDist = Top.Dist + G.StepCost[Nb[i]];
				if (NewDist < Dist[Nb[i]])
				{
					Dist[Nb[i]] = NewDist;
					Heap.HeapPush({ Nb[i], NewDist }, FNodeLess());
				}
			}
		}
	}

	// 플로이드-워셜: N x N 행렬. O(n^3)
	void FloydWarshall(const FGrid& G, TArray<float>& D)
	{
		const int32 N = G.Num();
		D.Init(Inf, N * N);
		int32 Nb[4];
		for (int32 I = 0; I < N; ++I)
		{
			D[I * N + I] = 0.f;
			const int32 Count = Neighbors(G, I, Nb);
			for (int32 j = 0; j < Count; ++j) D[I * N + Nb[j]] = G.StepCost[Nb[j]];
		}

		float* M = D.GetData();
		for (int32 K = 0; K < N; ++K)
		{
			const float* RowK = M + K * N;
			for (int32 I = 0; I < N; ++I)
			{
				float* RowI = M + I * N;
				const float Dik = RowI[K];
				if (Dik == Inf) continue;
				for (int32 J = 0; J < N; ++J)
				{
					const float Via = Dik + RowK[J];
					if (Via < RowI[J]) RowI[J] = Via;
				}
			}
		}
	}

	// BFS를 칸마다 한 번씩 돌려 모든 쌍 거리 행렬을 채움. O(n^2) (격자라 간선 수가 n에 비례)
	void AllPairsBFS(const FGrid& G, TArray<int32>& D)
	{
		const int32 N = G.Num();
		D.SetNumUninitialized(N * N);
		TArray<int32> Dist, Queue;
		Queue.Reserve(N);
		for (int32 S = 0; S < N; ++S)
		{
			BFS(G, S, Dist, Queue);
			FMemory::Memcpy(D.GetData() + S * N, Dist.GetData(), N * sizeof(int32));
		}
	}

	template <typename F>
	double TimeMs(int32 Reps, F&& Fn, bool bWarmup = true)
	{
		if (bWarmup) Fn(); // 첫 실행은 메모리 할당 등이 섞여서 측정에서 뺌 (오래 걸리는 2-B는 생략)
		const double T0 = FPlatformTime::Seconds();
		for (int32 R = 0; R < Reps; ++R) Fn();
		return (FPlatformTime::Seconds() - T0) * 1000.0 / Reps;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		FString Csv = TEXT("experiment,grid,cells,costType,algorithm,reps,avgMs,memoryBytes,sameAsDijkstra,bfsExtraCostAvgPct,bfsExtraCostMaxPct\n");
		auto Row = [&Csv](const TCHAR* Exp, int32 Size, const TCHAR* CostType, const TCHAR* Algo, int32 Reps,
			double Ms, int64 Mem, const FString& Same, const FString& ExtraAvg, const FString& ExtraMax)
		{
			const FString Line = FString::Printf(TEXT("%s,%dx%d,%d,%s,%s,%d,%.4f,%lld,%s,%s,%s"),
				Exp, Size, Size, Size * Size, CostType, Algo, Reps, Ms, Mem, *Same, *ExtraAvg, *ExtraMax);
			Csv += Line + TEXT("\n");
			UE_LOG(LogTemp, Log, TEXT("[Bench2] %s"), *Line);
		};

		const bool bFull = Args.Num() > 0 && Args[0].Equals(TEXT("full"), ESearchCase::IgnoreCase);
		UE_LOG(LogTemp, Log, TEXT("[Bench2] Start%s. Floyd-Warshall takes a while. The editor will freeze until it ends."),
			bFull ? TEXT(" (full: measure 80x80 Floyd-Warshall)") : TEXT(""));

		// ---------------- 2-A ----------------
		const int32 Sizes[] = { 20, 40, 80 };
		for (int32 Size : Sizes)
		{
			for (int32 W = 0; W < 2; ++W)
			{
				const bool bWeighted = (W == 1);
				const TCHAR* CostType = bWeighted ? TEXT("weighted") : TEXT("uniform");
				const FGrid G = MakeGrid(Size, bWeighted);
				const int32 N = G.Num();
				const int32 Start = (Size / 2) * Size + Size / 2; // 가운데 칸
				const int32 Reps = FMath::Clamp(400000 / N, 3, 1000);

				TArray<int32> BDist, BQueue, BParent;
				TArray<float> ADist, HDist;
				TArray<bool> Visited;
				TArray<FNode> Heap;
				Heap.Reserve(N * 4);
				BQueue.Reserve(N);
				int32 MaxHeap = 0;

				const double MsBFS  = TimeMs(Reps, [&]() { BFS(G, Start, BDist, BQueue); });
				const double MsArr  = TimeMs(Reps, [&]() { DijkstraArray(G, Start, ADist, Visited); });
				const double MsHeap = TimeMs(Reps, [&]() { DijkstraHeap(G, Start, HDist, Heap, MaxHeap); });

				// BFS가 고른 경로의 실제 비용(위험도 포함)과 다익스트라 최단 비용 비교
				BFS(G, Start, BDist, BQueue, &BParent);
				TArray<float> BfsPathCost;
				BfsPathCost.Init(0.f, N);
				for (int32 i = 1; i < BQueue.Num(); ++i) // BQueue 순서 = 부모가 항상 먼저 나옴
				{
					const int32 C = BQueue[i];
					BfsPathCost[C] = BfsPathCost[BParent[C]] + G.StepCost[C];
				}

				bool bSame = true;
				double SumPct = 0.0, MaxPct = 0.0;
				int32 CountPct = 0;
				for (int32 C = 0; C < N; ++C)
				{
					if (C == Start) continue;
					if (!FMath::IsNearlyEqual(BfsPathCost[C], HDist[C], 1e-3f)) bSame = false;
					const double Pct = (BfsPathCost[C] - HDist[C]) / HDist[C] * 100.0;
					SumPct += Pct;
					MaxPct = FMath::Max(MaxPct, Pct);
					++CountPct;
				}
				const FString Same = bSame ? TEXT("yes") : TEXT("no");
				const FString AvgStr = FString::Printf(TEXT("%.2f"), SumPct / FMath::Max(1, CountPct));
				const FString MaxStr = FString::Printf(TEXT("%.2f"), MaxPct);

				Row(TEXT("2A"), Size, CostType, TEXT("BFS"), Reps, MsBFS, int64(N) * 8, Same, AvgStr, MaxStr);
				Row(TEXT("2A"), Size, CostType, TEXT("DijkstraArray"), Reps, MsArr, int64(N) * 5, TEXT("-"), TEXT("-"), TEXT("-"));
				Row(TEXT("2A"), Size, CostType, TEXT("DijkstraHeap"), Reps, MsHeap, int64(N) * 4 + int64(MaxHeap) * sizeof(FNode), TEXT("-"), TEXT("-"), TEXT("-"));
			}
		}

		// ---------------- 2-B ----------------
		double MsFW40 = 0.0;
		const int32 SizesB[] = { 20, 40 };
		for (int32 Size : SizesB)
		{
			const FGrid G = MakeGrid(Size, false);
			const int32 N = G.Num();
			const int32 Reps = (Size <= 20) ? 5 : 1;

			TArray<int32> DB;
			TArray<float> DF;
			const double MsB = TimeMs(Reps, [&]() { AllPairsBFS(G, DB); }, false);
			const double MsF = TimeMs(Reps, [&]() { FloydWarshall(G, DF); }, false);
			if (Size == 40) MsFW40 = MsF;

			// 두 결과가 같은지 확인
			bool bSame = true;
			for (int32 i = 0; i < N * N && bSame; ++i)
			{
				if (!FMath::IsNearlyEqual(float(DB[i]), DF[i], 1e-3f)) bSame = false;
			}
			const FString Same = bSame ? TEXT("yes") : TEXT("no");

			Row(TEXT("2B"), Size, TEXT("uniform"), TEXT("BFSxN"), Reps, MsB, int64(N) * N * 4 + int64(N) * 8, Same, TEXT("-"), TEXT("-"));
			Row(TEXT("2B"), Size, TEXT("uniform"), TEXT("FloydWarshall"), Reps, MsF, int64(N) * N * 4, Same, TEXT("-"), TEXT("-"));
		}

		// 80x80 플로이드-워셜은 너무 오래 걸려서 40x40 결과에 (6400/1600)^3 = 64를 곱해 추정
		{
			const int32 Size = 80;
			const int32 N = Size * Size;
			const FGrid G = MakeGrid(Size, false);
			TArray<int32> DB;
			const double MsB = TimeMs(1, [&]() { AllPairsBFS(G, DB); }, false);
			Row(TEXT("2B"), Size, TEXT("uniform"), TEXT("BFSxN"), 1, MsB, int64(N) * N * 4 + int64(N) * 8, TEXT("-"), TEXT("-"), TEXT("-"));
			Row(TEXT("2B"), Size, TEXT("uniform"), TEXT("FloydWarshall_estimated"), 0, MsFW40 * 64.0, int64(N) * N * 4, TEXT("-"), TEXT("-"), TEXT("-"));

			// Tag.Bench2 full : 80x80 플로이드-워셜을 직접 잼 (30초 정도 멈춤, 메모리 약 160MB)
			if (bFull)
			{
				UE_LOG(LogTemp, Log, TEXT("[Bench2] Measuring Floyd-Warshall 80x80. This takes about 30 seconds."));
				TArray<float> DF;
				const double MsF = TimeMs(1, [&]() { FloydWarshall(G, DF); }, false);

				bool bSame = (DF.Num() == DB.Num());
				for (int32 i = 0; i < DB.Num() && bSame; ++i)
				{
					if (!FMath::IsNearlyEqual(float(DB[i]), DF[i], 1e-3f)) bSame = false;
				}
				Row(TEXT("2B"), Size, TEXT("uniform"), TEXT("FloydWarshall"), 1, MsF, int64(N) * N * 4,
					bSame ? TEXT("yes") : TEXT("no"), TEXT("-"), TEXT("-"));
			}
		}

		const FString Dir = FPaths::ProjectSavedDir() / TEXT("Experiments");
		IFileManager::Get().MakeDirectory(*Dir, true);
		const FString Path = Dir / FString::Printf(TEXT("exp2_%s.csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		FFileHelper::SaveStringToFile(Csv, *Path);
		UE_LOG(LogTemp, Log, TEXT("[Bench2] Done. Saved: %s"), *FPaths::ConvertRelativePathToFull(Path));
	}

	FAutoConsoleCommandWithWorldAndArgs GCmdBench2(
		TEXT("Tag.Bench2"),
		TEXT("Experiment 2: BFS / Dijkstra(array, heap) / Floyd-Warshall timing. Writes Saved/Experiments/exp2_*.csv"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}
