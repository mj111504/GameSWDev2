#include "TagCatAIController.h"
#include "TagRatAIController.h"

#include "NavigationSystem.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
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
	TEXT("Tag.CatSpeed"), 0.f, TEXT("Cat max walk speed (cm/s). 0 = keep the character's own value"));
static TAutoConsoleVariable<float> CVarCatchDistance(
	TEXT("Tag.CatchDistance"), 150.f, TEXT("Distance (cm) at which the cat catches the rat"));
static TAutoConsoleVariable<int32> CVarTagDebug(
	TEXT("Tag.Debug"), 1, TEXT("Draw AI targets. 1 = on, 0 = off"));

ATagCatAIController::ATagCatAIController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ATagCatAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	bCaught = false;
	StartTime = GetWorld()->GetTimeSeconds();
	GetWorld()->GetTimerManager().SetTimer(ThinkHandle, this, &ATagCatAIController::Think, 0.25f, true, 0.1f);
}

APawn* ATagCatAIController::FindRat()
{
	if (Rat.IsValid()) return Rat.Get();
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		if (It->GetController() && It->GetController()->IsA<ATagRatAIController>())
		{
			Rat = *It;
			return *It;
		}
	}
	return nullptr;
}

void ATagCatAIController::Think()
{
	APawn* Me = GetPawn();
	APawn* Target = FindRat();
	if (!Me || !Target || bCaught) return;

	// 속도 덮어쓰기 (실험 조건 맞추기용)
	if (ACharacter* Ch = Cast<ACharacter>(Me))
	{
		const float Speed = CVarCatSpeed.GetValueOnGameThread();
		if (Speed > 0.f) Ch->GetCharacterMovement()->MaxWalkSpeed = Speed;
	}

	const int32 Version = CVarCatVersion.GetValueOnGameThread();
	const FVector RatPos = Target->GetActorLocation();
	FVector Goal = RatPos;

	if (Version == 2)
	{
		// 예측식: 위치 + 속도 x 예측 시간
		const FVector Predicted = RatPos + Target->GetVelocity() * CVarCatPredict.GetValueOnGameThread();

		// 쥐가 아주 가까우면 예측하지 않고 바로 덮침 (예측 지점으로 가다가 지나치는 것 방지)
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

	// 잡았는지는 매 프레임 확인 (0.25초 간격이면 스쳐 지나갈 수 있어서)
	APawn* Me = GetPawn();
	APawn* Target = Rat.Get();
	if (bCaught || !Me || !Target) return;

	if (FVector::Dist2D(Me->GetActorLocation(), Target->GetActorLocation()) <= CVarCatchDistance.GetValueOnGameThread())
	{
		bCaught = true;
		const float Elapsed = GetWorld()->GetTimeSeconds() - StartTime;
		StopMovement();
		if (ATagRatAIController* RatAI = Cast<ATagRatAIController>(Target->GetController()))
		{
			RatAI->OnCaught();
		}

		const FString Msg = FString::Printf(TEXT("[TagAI] Caught! cat version %d, %.2f s"),
			CVarCatVersion.GetValueOnGameThread(), Elapsed);
		UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, Msg);
	}
}
