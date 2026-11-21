#include "TagRatAIController.h"
#include "TagCatAIController.h"
#include "TagGridSubsystem.h"

#include "NavigationSystem.h"
#include "EngineUtils.h"
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
	TEXT("Tag.RatSpeed"), 0.f, TEXT("Rat max walk speed (cm/s). 0 = keep the character's own value"));
static TAutoConsoleVariable<float> CVarRatOpenBonus(
	TEXT("Tag.RatOpenBonus"), 3.f, TEXT("Score bonus per walkable neighbor (in cells) so the rat avoids corners"));

void ATagRatAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	bCaught = false;
	GetWorld()->GetTimerManager().SetTimer(ThinkHandle, this, &ATagRatAIController::Think, 0.25f, true, 0.1f);
}

void ATagRatAIController::OnCaught()
{
	bCaught = true;
	StopMovement();
	GetWorld()->GetTimerManager().ClearTimer(ThinkHandle);
}

APawn* ATagRatAIController::FindCat()
{
	if (Cat.IsValid()) return Cat.Get();
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		if (It->GetController() && It->GetController()->IsA<ATagCatAIController>())
		{
			Cat = *It;
			return *It;
		}
	}
	return nullptr;
}

// 캐릭터가 벽 가까이 있으면 그 칸의 중심이 NavMesh 밖이라 걸을 수 없는 칸으로 잡힐 수 있음.
// 그럴 때 주변 2칸 안에서 가장 가까운 걸을 수 있는 칸을 찾음
int32 ATagRatAIController::FindNearestWalkableCell(const UTagGridSubsystem* Grid, const FVector& Pos) const
{
	const int32 Center = Grid->WorldToCell(Pos);
	if (Grid->IsWalkable(Center)) return Center;

	const float Cs = Grid->GetCellSize();
	int32 Best = INDEX_NONE;
	float BestD2 = TNumericLimits<float>::Max();
	for (int32 Dy = -2; Dy <= 2; ++Dy)
	{
		for (int32 Dx = -2; Dx <= 2; ++Dx)
		{
			const int32 C = Grid->WorldToCell(Pos + FVector(Dx * Cs, Dy * Cs, 0.f));
			if (!Grid->IsWalkable(C)) continue;
			const float D2 = FVector::DistSquared2D(Grid->CellToWorld(C), Pos);
			if (D2 < BestD2) { BestD2 = D2; Best = C; }
		}
	}
	return Best;
}

void ATagRatAIController::Think()
{
	APawn* Me = GetPawn();
	APawn* Enemy = FindCat();
	if (!Me || !Enemy || bCaught) return;

	if (ACharacter* Ch = Cast<ACharacter>(Me))
	{
		const float Speed = CVarRatSpeed.GetValueOnGameThread();
		if (Speed > 0.f) Ch->GetCharacterMovement()->MaxWalkSpeed = Speed;
	}

	const FVector MyPos = Me->GetActorLocation();
	const FVector CatPos = Enemy->GetActorLocation();
	const bool bDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("Tag.Debug"))
		? IConsoleManager::Get().FindConsoleVariable(TEXT("Tag.Debug"))->GetInt() != 0 : true;

	// ---------- 방식 1: 단순 도주 ----------
	if (CVarRatMode.GetValueOnGameThread() == 1)
	{
		const FVector Away = (MyPos - CatPos).GetSafeNormal2D();
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

	const int32 MyCell = FindNearestWalkableCell(Grid, MyPos);
	const int32 CatCell = FindNearestWalkableCell(Grid, CatPos);
	if (MyCell == INDEX_NONE || CatCell == INDEX_NONE) return;

	Grid->BFS(MyCell, RatDist);
	Grid->BFS(CatCell, CatDist);

	const float Cs = Grid->GetCellSize();
	const float MySpeed = FMath::Max(1.f, Me->GetMovementComponent()->GetMaxSpeed());
	const float CatSpeed = FMath::Max(1.f, Enemy->GetMovementComponent()->GetMaxSpeed());
	const float OpenBonus = CVarRatOpenBonus.GetValueOnGameThread();

	int32 Best = INDEX_NONE;
	float BestScore = -TNumericLimits<float>::Max();
	int32 Fallback = INDEX_NONE;          // 안전한 칸이 하나도 없을 때: 가장 덜 불리한 칸
	float FallbackMargin = -TNumericLimits<float>::Max();

	for (int32 C = 0; C < Grid->GetNumCells(); ++C)
	{
		if (RatDist[C] < 0 || CatDist[C] < 0) continue;

		const float MyTime = RatDist[C] * Cs / MySpeed;   // 쥐가 그 칸까지 걸리는 시간(초)
		const float CatTime = CatDist[C] * Cs / CatSpeed; // 고양이가 그 칸까지 걸리는 시간(초)
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
