#include "TagCatAIController.h"
#include "TagRatAIController.h"
#include "TagAgentSubsystem.h"
#include "TagGridSubsystem.h"

#include "NavigationSystem.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarCatVersion(
	TEXT("Tag.CatVersion"), 1, TEXT("Cat AI version. 1 = chase current position, 2 = chase predicted position"));
static TAutoConsoleVariable<float> CVarCatPredict(
	TEXT("Tag.CatPredictSeconds"), 1.0f, TEXT("Cat version 2: how many seconds ahead to predict"));
static TAutoConsoleVariable<float> CVarCatSpeed(
	TEXT("Tag.CatSpeed"), 0.f, TEXT("Cat max walk speed (cm/s) for every cat. 0 = keep the character's own value"));
static TAutoConsoleVariable<float> CVarCatchDistance(
	TEXT("Tag.CatchDistance"), 150.f, TEXT("Distance (cm) at which a cat catches a rat"));
static TAutoConsoleVariable<int32> CVarTagDebug(
	TEXT("Tag.Debug"), 1, TEXT("Draw AI targets. 1 = on, 0 = off"));

ATagCatAIController::ATagCatAIController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ATagCatAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (UTagAgentSubsystem* Agents = GetWorld()->GetSubsystem<UTagAgentSubsystem>())
	{
		Agents->RegisterCat(InPawn);
	}
	GetWorld()->GetTimerManager().SetTimer(ThinkHandle, this, &ATagCatAIController::Think, 0.25f, true, 0.1f);
}

void ATagCatAIController::OnUnPossess()
{
	if (UTagAgentSubsystem* Agents = GetWorld()->GetSubsystem<UTagAgentSubsystem>())
	{
		Agents->UnregisterCat(GetPawn());
	}
	GetWorld()->GetTimerManager().ClearTimer(ThinkHandle);
	Super::OnUnPossess();
}

APawn* ATagCatAIController::ChooseTarget(const TArray<APawn*>& Rats)
{
	APawn* Me = GetPawn();
	APawn* Best = nullptr;

	// 1순위: 격자 BFS로 "걸어서" 가장 가까운 쥐. 벽 너머 쥐를 가깝다고 착각하지 않게
	UTagGridSubsystem* Grid = GetWorld()->GetSubsystem<UTagGridSubsystem>();
	const int32 MyCell = (Grid && Grid->GetNumWalkable() > 0) ? Grid->FindNearestWalkableCell(Me->GetActorLocation()) : INDEX_NONE;
	if (MyCell != INDEX_NONE)
	{
		Grid->BFS(MyCell, MyDist);
		int32 BestDist = MAX_int32;
		for (APawn* Rat : Rats)
		{
			const int32 RatCell = Grid->FindNearestWalkableCell(Rat->GetActorLocation());
			if (RatCell == INDEX_NONE || MyDist[RatCell] < 0) continue;
			if (MyDist[RatCell] < BestDist) { BestDist = MyDist[RatCell]; Best = Rat; }
		}
	}

	// 격자를 못 쓰면: 직선거리로 가장 가까운 쥐
	if (!Best)
	{
		float BestD2 = TNumericLimits<float>::Max();
		for (APawn* Rat : Rats)
		{
			const float D2 = FVector::DistSquared2D(Me->GetActorLocation(), Rat->GetActorLocation());
			if (D2 < BestD2) { BestD2 = D2; Best = Rat; }
		}
	}
	return Best;
}

void ATagCatAIController::Think()
{
	APawn* Me = GetPawn();
	UTagAgentSubsystem* Agents = GetWorld()->GetSubsystem<UTagAgentSubsystem>();
	if (!Me || !Agents) return;

	// 속도 덮어쓰기 (실험 조건 맞추기용)
	if (ACharacter* Ch = Cast<ACharacter>(Me))
	{
		const float Speed = CVarCatSpeed.GetValueOnGameThread();
		if (Speed > 0.f) Ch->GetCharacterMovement()->MaxWalkSpeed = Speed;
	}

	TArray<APawn*> Rats;
	Agents->GetAliveRats(Rats);
	if (Rats.Num() == 0)
	{
		StopMovement();
		CurrentTarget = nullptr;
		return;
	}

	APawn* Target = ChooseTarget(Rats);
	CurrentTarget = Target;
	if (!Target) return;

	const int32 Version = CVarCatVersion.GetValueOnGameThread();
	const FVector RatPos = Target->GetActorLocation();
	FVector Goal = RatPos;

	if (Version == 2)
	{
		// 예측식: 위치 + 속도 x 예측 시간
		const FVector Predicted = RatPos + Target->GetVelocity() * CVarCatPredict.GetValueOnGameThread();

		// 쥐가 3m 안이면 예측하지 않고 바로 덮침 (예측 지점으로 가다가 지나치는 것 방지)
		const bool bVeryClose = FVector::Dist2D(Me->GetActorLocation(), RatPos) < 300.f;

		// 예측 지점이 벽 안이나 경기장 밖이면 NavMesh 위로 끌어옴. 실패하면 현재 위치로 감
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		FNavLocation NavLoc;
		if (!bVeryClose && NavSys && NavSys->ProjectPointToNavigation(Predicted, NavLoc, FVector(200.f, 200.f, 300.f)))
		{
			Goal = NavLoc.Location;
		}
	}

	MoveToLocation(Goal, 30.f, false, true, false, true, nullptr, true);

	if (CVarTagDebug.GetValueOnGameThread() != 0)
	{
		const FColor Color = (Version == 2) ? FColor::Orange : FColor::Red;
		DrawDebugSphere(GetWorld(), Goal, 40.f, 8, Color, false, 0.3f);
		DrawDebugLine(GetWorld(), Me->GetActorLocation(), Goal, Color, false, 0.3f, 0, 2.f);
	}
}

void ATagCatAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 잡기 판정은 매 프레임 (0.25초 간격이면 스쳐 지나가는 순간을 놓칠 수 있어서)
	// 쫓는 쥐뿐 아니라, 옆을 지나가는 다른 쥐도 닿으면 잡음
	APawn* Me = GetPawn();
	UTagAgentSubsystem* Agents = GetWorld()->GetSubsystem<UTagAgentSubsystem>();
	if (!Me || !Agents) return;

	TArray<APawn*> Rats;
	Agents->GetAliveRats(Rats);
	const float CatchDist = CVarCatchDistance.GetValueOnGameThread();

	for (APawn* Rat : Rats)
	{
		if (FVector::Dist2D(Me->GetActorLocation(), Rat->GetActorLocation()) > CatchDist) continue;

		if (Agents->ReportCaught(Me, Rat))
		{
			if (ATagRatAIController* RatAI = Cast<ATagRatAIController>(Rat->GetController()))
			{
				RatAI->OnCaught();             // AI 쥐: 멈추고 사라짐
			}
			else
			{
				Agents->HandlePlayerCaught(Rat); // 플레이어 쥐: 조작을 막고 멈춤
			}
			if (CurrentTarget.Get() == Rat)
			{
				CurrentTarget = nullptr;
				StopMovement();   // 다음 Think에서 새 쥐를 고름
			}
		}
	}
}
