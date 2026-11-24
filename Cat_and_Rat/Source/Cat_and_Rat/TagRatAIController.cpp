#include "TagRatAIController.h"
#include "TagAgentSubsystem.h"
#include "TagGridSubsystem.h"
#include "PickupItem.h"
#include "InteractionComponent.h"

#include "NavigationSystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PawnMovementComponent.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarRatMode(
	TEXT("Tag.RatMode"), 0, TEXT("Rat flee mode. 0 = BFS safe cell, 1 = simple run away"));
static TAutoConsoleVariable<float> CVarRatSpeed(
	TEXT("Tag.RatSpeed"), 0.f, TEXT("Rat max walk speed (cm/s) for every rat when empty-handed. 0 = keep the character's own value"));
static TAutoConsoleVariable<float> CVarRatOpenBonus(
	TEXT("Tag.RatOpenBonus"), 3.f, TEXT("Score bonus per walkable neighbor (in cells) so the rat avoids corners"));
static TAutoConsoleVariable<int32> CVarRatItems(
	TEXT("Tag.RatItems"), 1, TEXT("1 = rats steal items and carry them to export zones, 0 = rats only flee"));
static TAutoConsoleVariable<float> CVarRatDanger(
	TEXT("Tag.RatDanger"), 2.0f, TEXT("Rat drops its item and flees when a cat can reach it within this many seconds"));
static TAutoConsoleVariable<int32> CVarRatMaxWeight(
	TEXT("Tag.RatMaxWeight"), 6, TEXT("Heaviest item weight a rat tries to carry (10 = extra large cannot be lifted)"));
static TAutoConsoleVariable<float> CVarRatPickupRange(
	TEXT("Tag.RatPickupRange"), 150.f, TEXT("Distance (cm) at which a rat picks up its target item"));
static TAutoConsoleVariable<float> CVarRatRiskCells(
	TEXT("Tag.RatRiskCells"), 12.f, TEXT("Carrying: cells closer than this to a cat cost extra"));
static TAutoConsoleVariable<float> CVarRatRiskWeight(
	TEXT("Tag.RatRiskWeight"), 2.f, TEXT("Carrying: extra cost per cell of closeness to a cat"));
static TAutoConsoleVariable<float> CVarRatStashRatio(
	TEXT("Tag.RatStashRatio"), 2.5f, TEXT("Carrying: stash the item when the safe route costs this many times the plain distance. 0 = never stash"));
static TAutoConsoleVariable<int32> CVarRatStashRange(
	TEXT("Tag.RatStashRange"), 20, TEXT("Carrying: look for a stash cell within this many cells"));
static TAutoConsoleVariable<float> CVarRatStashWait(
	TEXT("Tag.RatStashWait"), 4.f, TEXT("Seconds a rat waits before picking its stashed item up again"));
static TAutoConsoleVariable<int32> CVarRatHandOff(
	TEXT("Tag.RatHandOff"), 1, TEXT("1 = a rat in danger hands a heavy item to a safer empty AI rat, 0 = off"));

void ATagRatAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	bCaught = false;
	bFleeing = false;
	bStashing = false;
	TargetItem = nullptr;
	if (UTagAgentSubsystem* Agents = GetWorld()->GetSubsystem<UTagAgentSubsystem>())
	{
		Agents->RegisterRat(InPawn);
	}
	GetWorld()->GetTimerManager().SetTimer(ThinkHandle, this, &ATagRatAIController::Think, 0.25f, true, 0.1f);
}

void ATagRatAIController::OnUnPossess()
{
	if (UTagAgentSubsystem* Agents = GetWorld()->GetSubsystem<UTagAgentSubsystem>())
	{
		Agents->ReleaseReservations(GetPawn());
		Agents->UnregisterRat(GetPawn());
	}
	GetWorld()->GetTimerManager().ClearTimer(ThinkHandle);
	Super::OnUnPossess();
}

void ATagRatAIController::OnCaught()
{
	// 잡힌 쥐: 들고 있던 물건을 떨어뜨리고, 멈추고, 화면에서 숨기고, 충돌을 꺼서 다른 캐릭터를 막지 않게 함
	bCaught = true;
	StopMovement();
	GetWorld()->GetTimerManager().ClearTimer(ThinkHandle);
	if (APawn* Me = GetPawn())
	{
		if (UInteractionComponent* Interact = Me->FindComponentByClass<UInteractionComponent>())
		{
			Interact->DropItem(false);
		}
		if (UTagAgentSubsystem* Agents = GetWorld()->GetSubsystem<UTagAgentSubsystem>())
		{
			Agents->ReleaseReservations(Me);
		}
		Me->SetActorHiddenInGame(true);
		Me->SetActorEnableCollision(false);
	}
	TargetItem = nullptr;
}

float ATagRatAIController::CatCellsTo(UTagGridSubsystem* Grid, int32 C) const
{
	if (CatDist[C] >= 0) return (float)CatDist[C];

	const FVector P = Grid->CellToWorld(C);
	float BestD = TNumericLimits<float>::Max();
	for (const FVector& CatPos : CatPositions)
	{
		BestD = FMath::Min(BestD, FVector::Dist(P, CatPos));
	}
	return BestD / Cs;
}

void ATagRatAIController::Think()
{
	APawn* Me = GetPawn();
	UTagAgentSubsystem* Agents = GetWorld()->GetSubsystem<UTagAgentSubsystem>();
	if (!Me || !Agents || bCaught) return;

	UInteractionComponent* Interact = Me->FindComponentByClass<UInteractionComponent>();
	APickupItem* Held = Interact ? Interact->HeldItem : nullptr;

	// 속도 덮어쓰기 (실험 조건 맞추기용). 물건을 들고 있으면 팀원 코드와 같은 비율로 느려지게
	if (ACharacter* Ch = Cast<ACharacter>(Me))
	{
		const float Speed = CVarRatSpeed.GetValueOnGameThread();
		if (Speed > 0.f)
		{
			const float Mult = Held ? UTagAgentSubsystem::GetCarrySpeedMultiplier(Held->ItemInfo.Weight) : 1.f;
			Ch->GetCharacterMovement()->MaxWalkSpeed = Speed * Mult;
		}
	}

	TArray<APawn*> Cats;
	Agents->GetCats(Cats);
	if (Cats.Num() == 0) return;

	const FVector MyPos = Me->GetActorLocation();
	IConsoleVariable* DebugVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Tag.Debug"));
	bDebug = DebugVar ? DebugVar->GetInt() != 0 : true;

	// ---------- 도주 방식 1: 단순 도주 (가장 가까운 고양이 반대 방향). 물건은 고려 안 함 ----------
	if (CVarRatMode.GetValueOnGameThread() == 1)
	{
		APawn* Nearest = nullptr;
		float BestD2 = TNumericLimits<float>::Max();
		for (APawn* Cat : Cats)
		{
			const float D2 = FVector::DistSquared2D(MyPos, Cat->GetActorLocation());
			if (D2 < BestD2) { BestD2 = D2; Nearest = Cat; }
		}

		const FVector Away = (MyPos - Nearest->GetActorLocation()).GetSafeNormal2D();
		FVector Goal = MyPos + Away * 800.f;

		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		FNavLocation NavLoc;
		if (NavSys && NavSys->ProjectPointToNavigation(Goal, NavLoc, FVector(300.f, 300.f, 300.f)))
		{
			Goal = NavLoc.Location;
		}
		MoveToLocation(Goal, 30.f, false, true, false, true, nullptr, true);
		if (bDebug) DrawDebugSphere(GetWorld(), Goal, 40.f, 8, FColor::Cyan, false, 0.3f);
		return;
	}

	// ---------- 공통 준비: BFS 2번 ----------
	UTagGridSubsystem* Grid = GetWorld()->GetSubsystem<UTagGridSubsystem>();
	if (!Grid || Grid->GetNumWalkable() == 0) return;

	const int32 MyCell = Grid->FindNearestWalkableCell(MyPos);
	if (MyCell == INDEX_NONE) return;

	// 고양이 칸 모으기 + 가장 빠른 고양이 속도 (가장 위험한 경우 기준으로 안전하게 판단)
	CatCells.Reset();
	CatPositions.Reset();
	CatSpeed = 1.f;
	for (APawn* Cat : Cats)
	{
		CatPositions.Add(Cat->GetActorLocation());
		const int32 C = Grid->FindNearestWalkableCell(Cat->GetActorLocation());
		if (C != INDEX_NONE) CatCells.Add(C);
		CatSpeed = FMath::Max(CatSpeed, Cat->GetMovementComponent()->GetMaxSpeed());
	}

	Grid->BFS(MyCell, RatDist);         // 쥐에서 각 칸까지
	Grid->BFSMulti(CatCells, CatDist);  // 가장 가까운 고양이에서 각 칸까지 (고양이 칸이 없으면 전부 -1 → 직선거리로 대신 셈)

	Cs = Grid->GetCellSize();
	MySpeed = FMath::Max(1.f, Me->GetMovementComponent()->GetMaxSpeed());

	// ---------- 1) 위험 판단 ----------
	const float Danger = CVarRatDanger.GetValueOnGameThread();
	const float CatTimeToMe = CatCellsTo(Grid, MyCell) * Cs / CatSpeed;
	if (!bFleeing && CatTimeToMe < Danger) bFleeing = true;
	else if (bFleeing && CatTimeToMe > Danger * 1.5f) bFleeing = false;

	const bool bUseItems = CVarRatItems.GetValueOnGameThread() != 0 && Interact != nullptr && Agents->GetNumExportZones() > 0;
	if (CVarRatItems.GetValueOnGameThread() != 0 && !Interact && !bWarnedNoInteract)
	{
		bWarnedNoInteract = true;
		UE_LOG(LogTemp, Warning, TEXT("[TagAI] %s has no InteractionComponent, so it can only flee"), *Me->GetName());
	}

	// 넘기기: 위험해지기 직전, 무거운 물건을 다른 쥐에게 넘김
	if (bUseItems && Held && !bFleeing && Held->ItemInfo.Weight >= 3 && CatTimeToMe < Danger * 2.f
		&& CVarRatHandOff.GetValueOnGameThread() != 0)
	{
		if (TryHandOff(Grid, Agents, Interact, CatTimeToMe))
		{
			Held = nullptr;
			bFleeing = true;   // 넘긴 쥐는 바로 도주해서 고양이를 끌고 감
		}
	}

	if (!bUseItems || bFleeing)
	{
		bStashing = false;
		if (Held && bFleeing)
		{
			Interact->DropItem(false);
			UE_LOG(LogTemp, Log, TEXT("[TagAI] %s dropped %s (cat %.1f s away)"), *Me->GetName(), *Held->ItemInfo.ItemName, CatTimeToMe);
		}
		if (TargetItem.IsValid())
		{
			ReleaseMine(Agents);
			TargetItem = nullptr;
		}
		Flee(Grid, MyCell);
		return;
	}

	// ---------- 2) 들고 있으면 운반 ----------
	if (Held)
	{
		Carry(Grid, Agents, Interact, MyCell);
		return;
	}

	// ---------- 3) 빈손이면 물건 고르기. 고를 게 없으면 안전한 칸에 가 있음 ----------
	if (!GoToItem(Grid, Agents, Interact))
	{
		Flee(Grid, MyCell);
	}
}

void ATagRatAIController::ReleaseMine(UTagAgentSubsystem* Agents)
{
	// 가지러 가던 물건의 찜은 풀되, 숨겨 둔 물건의 찜은 남김
	Agents->ReleaseReservations(GetPawn());
	APickupItem* Stashed = StashedItem.Get();
	const FTagItemEntry* E = Stashed ? Agents->FindEntry(Stashed) : nullptr;
	if (E && !E->Holder.IsValid() && !E->bExported) Agents->ReserveItem(Stashed, GetPawn());
}

// ======================== 도주 ========================

void ATagRatAIController::Flee(UTagGridSubsystem* Grid, int32 MyCell)
{
	const float OpenBonus = CVarRatOpenBonus.GetValueOnGameThread();

	int32 Best = INDEX_NONE;
	float BestScore = -TNumericLimits<float>::Max();
	int32 Fallback = INDEX_NONE;          // 안전한 칸이 하나도 없을 때: 가장 덜 불리한 칸
	float FallbackMargin = -TNumericLimits<float>::Max();

	for (int32 C = 0; C < Grid->GetNumCells(); ++C)
	{
		if (RatDist[C] < 0) continue;     // 쥐가 못 가는 칸

		const float CatCellsC = CatCellsTo(Grid, C);
		const float MyTime = RatDist[C] * Cs / MySpeed;   // 쥐가 그 칸까지 걸리는 시간(초)
		const float CatTime = CatCellsC * Cs / CatSpeed;  // 가장 가까운 고양이가 그 칸까지 걸리는 시간(초)
		const float Margin = CatTime - MyTime;

		if (Margin > 0.f)
		{
			// 쥐가 먼저 도착하는 칸 중, 고양이에게서 멀고 주변이 열린 칸
			const float Score = CatCellsC + OpenBonus * Grid->CountWalkableNeighbors(C);
			if (Score > BestScore) { BestScore = Score; Best = C; }
		}
		if (Margin > FallbackMargin) { FallbackMargin = Margin; Fallback = C; }
	}

	const int32 Target = (Best != INDEX_NONE) ? Best : Fallback;
	if (Target == INDEX_NONE) return;

	const FVector Goal = Grid->CellToWorld(Target);
	MoveToLocation(Goal, 30.f, false, true, false, true, nullptr, true);

	if (bDebug)
	{
		const FColor Color = (Best != INDEX_NONE) ? FColor::Green : FColor::Yellow;
		DrawDebugSphere(GetWorld(), Goal, 40.f, 8, Color, false, 0.3f);
		DrawDebugLine(GetWorld(), GetPawn()->GetActorLocation(), Goal, Color, false, 0.3f, 0, 2.f);
	}
}

// ======================== 물건 고르기 (그리디) ========================

bool ATagRatAIController::GoToItem(UTagGridSubsystem* Grid, UTagAgentSubsystem* Agents, UInteractionComponent* Interact)
{
	APawn* Me = GetPawn();
	const TArray<int32>& ExportDist = Agents->GetExportDist(Grid);
	if (ExportDist.Num() != Grid->GetNumCells()) return false;

	const int32 MaxWeight = CVarRatMaxWeight.GetValueOnGameThread();

	APickupItem* Best = nullptr;
	int32 BestCell = INDEX_NONE;
	float BestValue = 0.f;
	float CurrentValue = 0.f;   // 지금 가지러 가는 물건의 값 (목표를 너무 자주 바꾸지 않게)
	int32 CurrentCell = INDEX_NONE;

	for (const FTagItemEntry& E : Agents->GetItems())
	{
		APickupItem* Item = E.Item.Get();
		if (!Item || E.bExported || E.Holder.IsValid()) continue;
		if (Item->ItemInfo.Weight > MaxWeight) continue;
		if (E.ReservedBy.IsValid() && E.ReservedBy.Get() != Me && Agents->IsRatAlive(E.ReservedBy.Get())) continue;
		if (Item == StashedItem.Get() && GetWorld()->GetTimeSeconds() < StashUntil) continue;   // 방금 숨긴 물건

		const int32 ItemCell = Grid->FindNearestWalkableCell(Item->GetActorLocation());
		if (ItemCell == INDEX_NONE || RatDist[ItemCell] < 0 || ExportDist[ItemCell] < 0) continue;

		// 걸리는 시간: 물건까지 (빈손 속도) + 물건에서 반출구역까지 (들었을 때 느려진 속도)
		const float TimeTo = RatDist[ItemCell] * Cs / MySpeed;
		const float CarrySpeed = MySpeed * UTagAgentSubsystem::GetCarrySpeedMultiplier(Item->ItemInfo.Weight);
		const float TimeCarry = ExportDist[ItemCell] * Cs / CarrySpeed;

		// 고양이가 쥐보다 먼저 그 물건에 도착하면 뺌
		const float CatTime = CatCellsTo(Grid, ItemCell) * Cs / CatSpeed;
		if (CatTime <= TimeTo) continue;

		float Value = UTagAgentSubsystem::GetItemScore(Item->ItemInfo.Weight) / FMath::Max(0.1f, TimeTo + TimeCarry);
		if (E.ReservedBy.Get() == Me) Value *= 2.f;   // 숨겨 둔 물건, 넘겨받은 물건 우선
		if (Item == TargetItem.Get()) { CurrentValue = Value; CurrentCell = ItemCell; }
		if (Value > BestValue) { BestValue = Value; Best = Item; BestCell = ItemCell; }
	}

	// 지금 목표가 아직 괜찮으면, 새 후보가 30% 넘게 좋을 때만 바꿈 (왔다 갔다 방지)
	if (CurrentCell != INDEX_NONE && BestValue < CurrentValue * 1.3f)
	{
		Best = TargetItem.Get();
		BestCell = CurrentCell;
	}

	if (!Best)
	{
		if (TargetItem.IsValid()) ReleaseMine(Agents);
		TargetItem = nullptr;
		return false;
	}

	if (Best != TargetItem.Get())
	{
		ReleaseMine(Agents);
		if (!Agents->ReserveItem(Best, Me)) return false;
		TargetItem = Best;
	}

	// 가까우면 줍기 (팀원의 PickupItem은 거리 검사를 안 해서 여기서 함)
	const FVector MyPos = Me->GetActorLocation();
	const FVector ItemPos = Best->GetActorLocation();
	if (FVector::Dist2D(MyPos, ItemPos) < CVarRatPickupRange.GetValueOnGameThread() && FMath::Abs(MyPos.Z - ItemPos.Z) < 200.f)
	{
		if (Interact->PickupItem(Best))
		{
			StopMovement();
			UE_LOG(LogTemp, Log, TEXT("[TagAI] %s picked up %s (weight %d) at %.2f s"),
				*Me->GetName(), *Best->ItemInfo.ItemName, Best->ItemInfo.Weight, Agents->GetElapsedSeconds());
		}
		return true;
	}

	const FVector Goal = Grid->CellToWorld(BestCell);
	MoveToLocation(Goal, 30.f, false, true, false, true, nullptr, true);
	if (bDebug)
	{
		DrawDebugSphere(GetWorld(), ItemPos, 50.f, 8, FColor::Magenta, false, 0.3f);
		DrawDebugLine(GetWorld(), MyPos, ItemPos, FColor::Magenta, false, 0.3f, 0, 2.f);
	}
	return true;
}

// ======================== 운반 (다익스트라) ========================

void ATagRatAIController::Carry(UTagGridSubsystem* Grid, UTagAgentSubsystem* Agents, UInteractionComponent* Interact, int32 MyCell)
{
	APawn* Me = GetPawn();
	const FVector MyPos = Me->GetActorLocation();

	// 반출구역 안이면 내려놓음 → 명단 관리자가 다음 갱신 때 반출 완료 처리
	if (Agents->IsInExportZone(MyPos))
	{
		APickupItem* Held = Interact->HeldItem;
		Interact->DropItem(false);
		ReleaseMine(Agents);
		TargetItem = nullptr;
		StopMovement();
		UE_LOG(LogTemp, Log, TEXT("[TagAI] %s delivered %s to an export zone at %.2f s"),
			*Me->GetName(), Held ? *Held->ItemInfo.ItemName : TEXT("?"), Agents->GetElapsedSeconds());
		return;
	}

	const TArray<int32>& ExportCells = Agents->GetExportCells(Grid);
	if (ExportCells.Num() == 0) return;

	// 위험도 비용: 고양이에게서 RiskCells칸보다 가까운 칸은, 가까울수록 비쌈
	const int32 N = Grid->GetNumCells();
	const float RiskCells = CVarRatRiskCells.GetValueOnGameThread();
	const float RiskWeight = CVarRatRiskWeight.GetValueOnGameThread();
	RiskCost.SetNumUninitialized(N);
	for (int32 C = 0; C < N; ++C)
	{
		RiskCost[C] = RiskWeight * FMath::Max(0.f, RiskCells - CatCellsTo(Grid, C));
	}

	Grid->Dijkstra(MyCell, RiskCost, CarryDist);

	// 비용이 가장 싼 반출구역 칸
	const float Inf = TNumericLimits<float>::Max();
	int32 Target = INDEX_NONE;
	float TargetCost = Inf;
	for (int32 C : ExportCells)
	{
		if (CarryDist[C] < TargetCost) { TargetCost = CarryDist[C]; Target = C; }
	}

	// 숨겨 두기 판단: 위험을 피해 가는 길 비용 ÷ 그냥 거리(칸 수)가 크면 고양이가 길을 막고 있는 것
	// 한 번 숨기기로 하면 비율이 0.75배 아래로 내려갈 때까지 유지 (왔다 갔다 방지)
	const float StashRatio = CVarRatStashRatio.GetValueOnGameThread();
	const TArray<int32>& ExportDist = Agents->GetExportDist(Grid);
	const float PlainCells = (ExportDist.Num() == N && ExportDist[MyCell] > 0) ? (float)ExportDist[MyCell] : 0.f;
	if (StashRatio > 0.f && Target != INDEX_NONE && PlainCells > 0.f)
	{
		const float Ratio = TargetCost / PlainCells;
		if (!bStashing && Ratio > StashRatio) bStashing = true;
		else if (bStashing && Ratio < StashRatio * 0.75f) bStashing = false;
	}
	if (bStashing)
	{
		Stash(Grid, Agents, Interact, MyCell);
		return;
	}

	FVector Goal;
	if (Target == INDEX_NONE)
	{
		// 격자로는 못 가는 경우: 첫 번째 반출구역 칸으로 NavMesh 길찾기에 맡김
		Goal = Grid->CellToWorld(ExportCells[0]);
	}
	else
	{
		// 목표 칸에서 거꾸로 되짚어서 길을 만듦.
		// 칸 X의 바로 앞 칸 = X의 이웃 중 거리 값이 가장 작은 칸 (X로 들어오는 비용은 어느 이웃에서 오든 같아서)
		TArray<int32> Path;
		int32 Cur = Target;
		for (int32 Guard = 0; Cur != MyCell && Guard < N; ++Guard)
		{
			Path.Add(Cur);
			Grid->GetNeighbors(Cur, Nbs);
			int32 Prev = INDEX_NONE;
			float PrevDist = Inf;
			for (int32 Nb : Nbs)
			{
				if (CarryDist[Nb] < PrevDist) { PrevDist = CarryDist[Nb]; Prev = Nb; }
			}
			if (Prev == INDEX_NONE) break;
			Cur = Prev;
		}
		// Path는 목표 → 쥐 순서. 쥐에서 8칸 앞 지점을 다음 목적지로 삼음
		const int32 Ahead = 8;
		const int32 Index = FMath::Max(0, Path.Num() - Ahead);
		Goal = Path.Num() > 0 ? Grid->CellToWorld(Path[Index]) : Grid->CellToWorld(Target);

		if (bDebug)
		{
			for (int32 i = 0; i + 1 < Path.Num(); ++i)
			{
				DrawDebugLine(GetWorld(), Grid->CellToWorld(Path[i]) + FVector(0, 0, 20.f),
					Grid->CellToWorld(Path[i + 1]) + FVector(0, 0, 20.f), FColor::Blue, false, 0.3f, 0, 3.f);
			}
		}
	}

	MoveToLocation(Goal, 30.f, false, true, false, true, nullptr, true);
	if (bDebug) DrawDebugSphere(GetWorld(), Goal, 40.f, 8, FColor::Blue, false, 0.3f);
}

// ======================== 넘기기 ========================

bool ATagRatAIController::TryHandOff(UTagGridSubsystem* Grid, UTagAgentSubsystem* Agents, UInteractionComponent* Interact, float CatTimeToMe)
{
	APawn* Me = GetPawn();
	APickupItem* Held = Interact->HeldItem;
	if (!Held) return false;

	// 받을 쥐: 빈손인 AI 쥐 중, 내 자리까지 고양이보다 1초 이상 먼저 올 수 있는 쥐. 여럿이면 가장 빨리 오는 쥐
	TArray<APawn*> Rats;
	Agents->GetAliveRats(Rats);
	APawn* Receiver = nullptr;
	float BestTime = TNumericLimits<float>::Max();
	for (APawn* R : Rats)
	{
		if (R == Me || !Cast<ATagRatAIController>(R->GetController())) continue;
		const UInteractionComponent* RI = R->FindComponentByClass<UInteractionComponent>();
		if (!RI || RI->IsHoldingItem()) continue;

		const int32 RCell = Grid->FindNearestWalkableCell(R->GetActorLocation());
		if (RCell == INDEX_NONE || RatDist[RCell] < 0) continue;   // RatDist = 나에게서 BFS (같은 길을 거꾸로 오는 거리)

		const float RSpeed = FMath::Max(1.f, R->GetMovementComponent()->GetMaxSpeed());
		const float Time = RatDist[RCell] * Cs / RSpeed;
		if (Time + 1.f < CatTimeToMe && Time < BestTime) { BestTime = Time; Receiver = R; }
	}
	if (!Receiver) return false;

	Interact->DropItem(false);
	Agents->TransferReservation(Held, Receiver);
	TargetItem = nullptr;
	UE_LOG(LogTemp, Log, TEXT("[TagAI] %s handed %s to %s (cat %.1f s away, receiver %.1f s away)"),
		*Me->GetName(), *Held->ItemInfo.ItemName, *Receiver->GetName(), CatTimeToMe, BestTime);
	if (bDebug)
	{
		DrawDebugLine(GetWorld(), Me->GetActorLocation(), Receiver->GetActorLocation(), FColor::White, false, 1.0f, 0, 4.f);
	}
	return true;
}

// ======================== 숨겨 두기 ========================

void ATagRatAIController::Stash(UTagGridSubsystem* Grid, UTagAgentSubsystem* Agents, UInteractionComponent* Interact, int32 MyCell)
{
	APawn* Me = GetPawn();
	APickupItem* Held = Interact->HeldItem;
	if (!Held) { bStashing = false; return; }

	// 가까운 칸(StashRange칸 이내) 중, 쥐가 고양이보다 먼저 도착하고(여유 > 0) 고양이에게서 가장 먼 칸
	// MySpeed는 들고 있어서 이미 느려진 속도
	const int32 Range = CVarRatStashRange.GetValueOnGameThread();
	int32 Best = INDEX_NONE;
	float BestScore = -TNumericLimits<float>::Max();
	for (int32 C = 0; C < Grid->GetNumCells(); ++C)
	{
		if (RatDist[C] < 0 || RatDist[C] > Range) continue;
		const float CatCellsC = CatCellsTo(Grid, C);
		const float Margin = CatCellsC * Cs / CatSpeed - RatDist[C] * Cs / MySpeed;
		if (Margin <= 0.f) continue;
		if (CatCellsC > BestScore) { BestScore = CatCellsC; Best = C; }
	}

	// 갈 곳이 없거나 이미 그 칸에 있으면 여기 내려놓음
	if (Best == INDEX_NONE || RatDist[Best] <= 1)
	{
		Interact->DropItem(false);
		Agents->ReserveItem(Held, Me);   // 찜 유지 → 안전해지면 다시 가지러 감
		TargetItem = nullptr;
		StashedItem = Held;
		StashUntil = GetWorld()->GetTimeSeconds() + CVarRatStashWait.GetValueOnGameThread();
		bStashing = false;
		StopMovement();
		UE_LOG(LogTemp, Log, TEXT("[TagAI] %s stashed %s at %.2f s (route to export blocked)"),
			*Me->GetName(), *Held->ItemInfo.ItemName, Agents->GetElapsedSeconds());
		return;
	}

	const FVector Goal = Grid->CellToWorld(Best);
	MoveToLocation(Goal, 30.f, false, true, false, true, nullptr, true);
	if (bDebug)
	{
		DrawDebugSphere(GetWorld(), Goal, 45.f, 8, FColor::White, false, 0.3f);
		DrawDebugLine(GetWorld(), Me->GetActorLocation(), Goal, FColor::White, false, 0.3f, 0, 2.f);
	}
}
