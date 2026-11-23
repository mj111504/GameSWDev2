#include "TagRatAIController.h"
#include "TagAgentSubsystem.h"
#include "TagGridSubsystem.h"

#include "NavigationSystem.h"
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
	TEXT("Tag.RatSpeed"), 0.f, TEXT("Rat max walk speed (cm/s) for every rat. 0 = keep the character's own value"));
static TAutoConsoleVariable<float> CVarRatOpenBonus(
	TEXT("Tag.RatOpenBonus"), 3.f, TEXT("Score bonus per walkable neighbor (in cells) so the rat avoids corners"));

void ATagRatAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	bCaught = false;
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
		Agents->UnregisterRat(GetPawn());
	}
	GetWorld()->GetTimerManager().ClearTimer(ThinkHandle);
	Super::OnUnPossess();
}

void ATagRatAIController::OnCaught()
{
	// 잡힌 쥐: 멈추고, 화면에서 숨기고, 충돌을 꺼서 다른 캐릭터를 막지 않게 함
	bCaught = true;
	StopMovement();
	GetWorld()->GetTimerManager().ClearTimer(ThinkHandle);
	if (APawn* Me = GetPawn())
	{
		Me->SetActorHiddenInGame(true);
		Me->SetActorEnableCollision(false);
	}
}

void ATagRatAIController::Think()
{
	APawn* Me = GetPawn();
	UTagAgentSubsystem* Agents = GetWorld()->GetSubsystem<UTagAgentSubsystem>();
	if (!Me || !Agents || bCaught) return;

	if (ACharacter* Ch = Cast<ACharacter>(Me))
	{
		const float Speed = CVarRatSpeed.GetValueOnGameThread();
		if (Speed > 0.f) Ch->GetCharacterMovement()->MaxWalkSpeed = Speed;
	}

	TArray<APawn*> Cats;
	Agents->GetCats(Cats);
	if (Cats.Num() == 0) return;

	const FVector MyPos = Me->GetActorLocation();
	IConsoleVariable* DebugVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Tag.Debug"));
	const bool bDebug = DebugVar ? DebugVar->GetInt() != 0 : true;

	// ---------- 방식 1: 단순 도주 (가장 가까운 고양이 반대 방향) ----------
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

	// ---------- 방식 0: BFS 안전 칸 + 그리디 ----------
	UTagGridSubsystem* Grid = GetWorld()->GetSubsystem<UTagGridSubsystem>();
	if (!Grid || Grid->GetNumWalkable() == 0) return;

	const int32 MyCell = Grid->FindNearestWalkableCell(MyPos);
	if (MyCell == INDEX_NONE) return;

	// 고양이 칸 모으기 + 가장 빠른 고양이 속도 (가장 위험한 경우 기준으로 안전하게 판단)
	CatCells.Reset();
	float CatSpeed = 1.f;
	for (APawn* Cat : Cats)
	{
		const int32 C = Grid->FindNearestWalkableCell(Cat->GetActorLocation());
		if (C != INDEX_NONE) CatCells.Add(C);
		CatSpeed = FMath::Max(CatSpeed, Cat->GetMovementComponent()->GetMaxSpeed());
	}
	if (CatCells.Num() == 0) return;

	Grid->BFS(MyCell, RatDist);         // 쥐에서 각 칸까지
	Grid->BFSMulti(CatCells, CatDist);  // 가장 가까운 고양이에서 각 칸까지

	const float Cs = Grid->GetCellSize();
	const float MySpeed = FMath::Max(1.f, Me->GetMovementComponent()->GetMaxSpeed());
	const float OpenBonus = CVarRatOpenBonus.GetValueOnGameThread();

	int32 Best = INDEX_NONE;
	float BestScore = -TNumericLimits<float>::Max();
	int32 Fallback = INDEX_NONE;          // 안전한 칸이 하나도 없을 때: 가장 덜 불리한 칸
	float FallbackMargin = -TNumericLimits<float>::Max();

	for (int32 C = 0; C < Grid->GetNumCells(); ++C)
	{
		if (RatDist[C] < 0 || CatDist[C] < 0) continue;

		const float MyTime = RatDist[C] * Cs / MySpeed;   // 쥐가 그 칸까지 걸리는 시간(초)
		const float CatTime = CatDist[C] * Cs / CatSpeed; // 가장 가까운 고양이가 그 칸까지 걸리는 시간(초)
		const float Margin = CatTime - MyTime;

		if (Margin > 0.f)
		{
			// 쥐가 먼저 도착하는 칸 중, 고양이에게서 멀고 주변이 열린 칸
			const float Score = CatDist[C] + OpenBonus * Grid->CountWalkableNeighbors(C);
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
		DrawDebugLine(GetWorld(), MyPos, Goal, Color, false, 0.3f, 0, 2.f);
	}
}
