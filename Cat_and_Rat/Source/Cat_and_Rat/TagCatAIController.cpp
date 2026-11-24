#include "TagCatAIController.h"
#include "TagRatAIController.h"
#include "TagAgentSubsystem.h"
#include "TagGridSubsystem.h"
#include "InteractionComponent.h"
#include "PickupItem.h"

#include "NavigationSystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Components/PrimitiveComponent.h"

static TAutoConsoleVariable<int32> CVarCatVersion(
	TEXT("Tag.CatVersion"), 1, TEXT("Cat AI version. 1 = chase current position, 2 = chase predicted position"));
static TAutoConsoleVariable<float> CVarCatPredict(
	TEXT("Tag.CatPredictSeconds"), 1.0f, TEXT("Cat version 2: how many seconds ahead to predict"));
static TAutoConsoleVariable<float> CVarCatSpeed(
	TEXT("Tag.CatSpeed"), 0.f, TEXT("Cat max walk speed (cm/s) for every cat. 0 = keep the character's own value"));
static TAutoConsoleVariable<float> CVarCatchDistance(
	TEXT("Tag.CatchDistance"), 150.f, TEXT("Distance (cm) at which a cat catches a rat"));
static TAutoConsoleVariable<float> CVarCatchHeight(
	TEXT("Tag.CatchHeight"), 100.f, TEXT("A cat catches a rat only if their height difference (cm) is within this"));
static TAutoConsoleVariable<int32> CVarCatCarrierBonus(
	TEXT("Tag.CatCarrierBonus"), 10, TEXT("A rat holding an item counts as this many cells closer when the cat picks a target"));
static TAutoConsoleVariable<int32> CVarCatRecover(
	TEXT("Tag.CatRecover"), 1, TEXT("1 = cats return exported items to their original location, 0 = cats only chase"));
static TAutoConsoleVariable<float> CVarCatChaseSeconds(
	TEXT("Tag.CatChaseSeconds"), 5.f, TEXT("A cat always chases a rat holding an item if it can reach it within this many seconds"));
static TAutoConsoleVariable<float> CVarCatRecoverMinMess(
	TEXT("Tag.CatRecoverMinMess"), 5.f, TEXT("Cats start returning items when messiness is at least this percent"));
static TAutoConsoleVariable<float> CVarCatDropForRat(
	TEXT("Tag.CatDropForRat"), 1.5f, TEXT("A cat carrying an item drops it and chases when a rat is within this many seconds"));
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
		Agents->ReleaseCatReservations(GetPawn());
		Agents->UnregisterCat(GetPawn());
	}
	GetWorld()->GetTimerManager().ClearTimer(ThinkHandle);
	Super::OnUnPossess();
}

UInteractionComponent* ATagCatAIController::GetOrAddInteraction()
{
	APawn* Me = GetPawn();
	if (!Me) return nullptr;
	if (UInteractionComponent* Found = Me->FindComponentByClass<UInteractionComponent>()) return Found;

	// 고양이 BP에 InteractionComponent가 없으면 여기서 붙임. 고양이 설정(속도 페널티 없음), 조준선 검사 끔
	UInteractionComponent* Comp = NewObject<UInteractionComponent>(Me, TEXT("TagCatInteraction"));
	Comp->bIsCat = true;
	Comp->bEnableCrosshairTrace = false;
	Comp->RegisterComponent();
	Me->AddInstanceComponent(Comp);
	UE_LOG(LogTemp, Log, TEXT("[TagAI] Added InteractionComponent to %s so it can return items"), *Me->GetName());
	return Comp;
}

float ATagCatAIController::RatSecondsAway(UTagGridSubsystem* Grid, APawn* Rat) const
{
	const FVector RatPos = Rat->GetActorLocation();
	if (MyCell != INDEX_NONE)
	{
		const int32 RatCell = Grid->FindNearestWalkableCell(RatPos);
		if (RatCell != INDEX_NONE && MyDist[RatCell] >= 0) return MyDist[RatCell] * Cs / MySpeed;
	}
	return FVector::Dist(GetPawn()->GetActorLocation(), RatPos) / MySpeed;
}

APawn* ATagCatAIController::ChooseTarget(const TArray<APawn*>& Rats)
{
	APawn* Me = GetPawn();
	APawn* Best = nullptr;

	// 1순위: 격자 BFS로 "걸어서" 가장 가까운 쥐. 벽 너머 쥐를 가깝다고 착각하지 않게
	// 물건을 든 쥐는 느리고 점수를 가져가려는 중이라, CarrierBonus칸만큼 더 가깝다고 보고 우선 쫓음
	const int32 CarrierBonus = CVarCatCarrierBonus.GetValueOnGameThread();
	UTagGridSubsystem* Grid = GetWorld()->GetSubsystem<UTagGridSubsystem>();
	if (MyCell != INDEX_NONE)
	{
		int32 BestDist = MAX_int32;
		for (APawn* Rat : Rats)
		{
			const int32 RatCell = Grid->FindNearestWalkableCell(Rat->GetActorLocation());
			if (RatCell == INDEX_NONE || MyDist[RatCell] < 0) continue;
			const UInteractionComponent* Interact = Rat->FindComponentByClass<UInteractionComponent>();
			const bool bCarrying = Interact && Interact->IsHoldingItem();
			const int32 Dist = MyDist[RatCell] - (bCarrying ? CarrierBonus : 0);
			if (Dist < BestDist) { BestDist = Dist; Best = Rat; }
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
	TArray<APawn*> Cats;
	Agents->GetCats(Cats);

	// 내 칸에서 BFS 한 번. 쫓을 쥐 고르기, 회수할 물건 고르기에 같이 씀
	UTagGridSubsystem* Grid = GetWorld()->GetSubsystem<UTagGridSubsystem>();
	MyCell = (Grid && Grid->GetNumWalkable() > 0) ? Grid->FindNearestWalkableCell(Me->GetActorLocation()) : INDEX_NONE;
	if (MyCell != INDEX_NONE)
	{
		Grid->BFS(MyCell, MyDist);
		Cs = Grid->GetCellSize();
	}
	MySpeed = FMath::Max(1.f, Me->GetMovementComponent()->GetMaxSpeed());

	const bool bRecoverOn = CVarCatRecover.GetValueOnGameThread() != 0 && Grid && MyCell != INDEX_NONE;
	UInteractionComponent* Interact = bRecoverOn ? GetOrAddInteraction() : Me->FindComponentByClass<UInteractionComponent>();
	APickupItem* Held = Interact ? Interact->HeldItem : nullptr;

	// 가장 가까운 쥐, 물건 든 쥐 중 가장 가까운 쥐까지 몇 초
	float NearestRatSec = TNumericLimits<float>::Max();
	float NearestCarrierSec = TNumericLimits<float>::Max();
	for (APawn* Rat : Rats)
	{
		const float Sec = RatSecondsAway(Grid, Rat);
		NearestRatSec = FMath::Min(NearestRatSec, Sec);
		const UInteractionComponent* RI = Rat->FindComponentByClass<UInteractionComponent>();
		if (RI && RI->IsHoldingItem()) NearestCarrierSec = FMath::Min(NearestCarrierSec, Sec);
	}

	// ---------- 1) 회수 중 ----------
	if (Held)
	{
		if (NearestRatSec < CVarCatDropForRat.GetValueOnGameThread())
		{
			// 들고 있으면 잡을 수 없으니, 쥐가 바로 앞이면 내려놓고 쫓음
			Interact->DropItem(false);
			Agents->ReleaseCatReservations(Me);
			RecoverItem = nullptr;
			UE_LOG(LogTemp, Log, TEXT("[TagAI] %s dropped %s to chase a rat (%.1f s away)"),
				*Me->GetName(), *Held->ItemInfo.ItemName, NearestRatSec);
		}
		else if (Grid && MyCell != INDEX_NONE)
		{
			CarryHome(Grid, Agents, Interact, Held);
			return;
		}
	}

	// ---------- 2) 쫓기 vs 회수 ----------
	// 쥐가 아주 가까우면 회수하지 않음 (주웠다가 바로 내려놓기를 반복하지 않게)
	const bool bUrgentChase = NearestCarrierSec < CVarCatChaseSeconds.GetValueOnGameThread()
		|| NearestRatSec < CVarCatDropForRat.GetValueOnGameThread() * 2.f;
	if (!bUrgentChase && bRecoverOn && Interact && !Interact->IsHoldingItem())
	{
		if (TryRecover(Grid, Agents, Interact, Cats.Num())) return;
	}
	else if (RecoverItem.IsValid())
	{
		Agents->ReleaseCatReservations(Me);
		RecoverItem = nullptr;
	}

	// ---------- 3) 쫓기 ----------
	if (Rats.Num() == 0)
	{
		StopMovement();
		CurrentTarget = nullptr;
		return;
	}
	Chase(ChooseTarget(Rats));
}

bool ATagCatAIController::TryRecover(UTagGridSubsystem* Grid, UTagAgentSubsystem* Agents, UInteractionComponent* Interact, int32 NumCats)
{
	APawn* Me = GetPawn();

	// 어지러움이 낮으면 회수보다 쫓기
	if (Agents->GetMessinessPercent() < CVarCatRecoverMinMess.GetValueOnGameThread()) return false;

	// 고양이가 2마리 이상이면 최소 1마리는 쫓음
	if (NumCats > 1 && Agents->CountRecoveringCats(Me) >= NumCats - 1) return false;

	APickupItem* Best = nullptr;
	int32 BestItemCell = INDEX_NONE;
	float BestValue = 0.f;
	float CurrentValue = 0.f;
	int32 CurrentCell = INDEX_NONE;

	for (const FTagItemEntry& E : Agents->GetItems())
	{
		APickupItem* Item = E.Item.Get();
		if (!Item || !E.bExported || E.Holder.IsValid() || !E.bHomeSet) continue;
		if (E.ReservedByCat.IsValid() && E.ReservedByCat.Get() != Me) continue;

		const int32 ItemCell = Grid->FindNearestWalkableCell(Item->GetActorLocation());
		const int32 HomeCell = Grid->FindNearestWalkableCell(E.Home);
		if (ItemCell == INDEX_NONE || HomeCell == INDEX_NONE || MyDist[ItemCell] < 0) continue;

		// 물건에서 원래 위치까지: 물건 칸에서 BFS (반출된 물건은 보통 몇 개뿐이라 매번 해도 가벼움)
		Grid->BFS(ItemCell, ItemDist);
		if (ItemDist[HomeCell] < 0) continue;

		// 고양이는 들어도 안 느려짐 (팀원 코드 bIsCat)
		const float Time = (MyDist[ItemCell] + ItemDist[HomeCell]) * Cs / MySpeed;
		const float Value = Item->ItemInfo.Weight / FMath::Max(0.1f, Time);
		if (Item == RecoverItem.Get()) { CurrentValue = Value; CurrentCell = ItemCell; }
		if (Value > BestValue) { BestValue = Value; Best = Item; BestItemCell = ItemCell; }
	}

	// 지금 목표가 아직 괜찮으면 30% 넘게 좋은 후보가 있을 때만 바꿈 (왔다 갔다 방지)
	if (CurrentCell != INDEX_NONE && BestValue < CurrentValue * 1.3f)
	{
		Best = RecoverItem.Get();
		BestItemCell = CurrentCell;
	}

	if (!Best)
	{
		if (RecoverItem.IsValid()) Agents->ReleaseCatReservations(Me);
		RecoverItem = nullptr;
		return false;
	}

	if (Best != RecoverItem.Get())
	{
		Agents->ReleaseCatReservations(Me);
		if (!Agents->ReserveItemForCat(Best, Me)) return false;
		RecoverItem = Best;
	}

	const FVector MyPos = Me->GetActorLocation();
	const FVector ItemPos = Best->GetActorLocation();
	if (FVector::Dist2D(MyPos, ItemPos) < 150.f && FMath::Abs(MyPos.Z - ItemPos.Z) < 200.f)
	{
		if (Interact->PickupItem(Best))
		{
			StopMovement();
			UE_LOG(LogTemp, Log, TEXT("[TagAI] %s picked up %s to return it at %.2f s"),
				*Me->GetName(), *Best->ItemInfo.ItemName, Agents->GetElapsedSeconds());
		}
		return true;
	}

	MoveToLocation(Grid->CellToWorld(BestItemCell), 30.f, false, true, false, true, nullptr, true);
	if (CVarTagDebug.GetValueOnGameThread() != 0)
	{
		DrawDebugSphere(GetWorld(), ItemPos, 50.f, 8, FColor::Purple, false, 0.3f);
		DrawDebugLine(GetWorld(), MyPos, ItemPos, FColor::Purple, false, 0.3f, 0, 2.f);
	}
	return true;
}

void ATagCatAIController::CarryHome(UTagGridSubsystem* Grid, UTagAgentSubsystem* Agents, UInteractionComponent* Interact, APickupItem* Held)
{
	APawn* Me = GetPawn();
	const FTagItemEntry* Entry = Agents->FindEntry(Held);
	if (!Entry || !Entry->bHomeSet)
	{
		Interact->DropItem(false);   // 모르는 물건이면 그냥 내려놓음
		return;
	}

	// 원래 위치를 NavMesh 위로 끌어온 점 = 고양이가 서야 할 곳 (물건이 탁자 위에 있었으면 탁자 옆 바닥)
	const FVector MyPos = Me->GetActorLocation();
	FVector Goal = Entry->Home;
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation NavLoc;
	if (NavSys && NavSys->ProjectPointToNavigation(Entry->Home, NavLoc, FVector(150.f, 150.f, 250.f)))
	{
		Goal = NavLoc.Location;
	}
	else
	{
		const int32 HomeCell = Grid->FindNearestWalkableCell(Entry->Home);
		if (HomeCell != INDEX_NONE) Goal = Grid->CellToWorld(HomeCell);
	}

	// 도착하면 물건을 원래 위치에 정확히 놓음 → 명단 관리자가 다음 갱신 때 회수 완료 처리
	// (그냥 떨어뜨리면 고양이 앞쪽으로 떨어져서 원래 위치에서 벗어날 수 있어서 위치를 맞춰 놓음)
	const bool bAtHome = FVector::Dist2D(MyPos, Entry->Home) < 200.f && FMath::Abs(MyPos.Z - Entry->Home.Z) < 250.f;
	const bool bAtGoal = FVector::Dist2D(MyPos, Goal) < 60.f && FMath::Abs(MyPos.Z - Goal.Z) < 250.f;
	if (bAtHome || bAtGoal)
	{
		StopMovement();
		Interact->DropItem(false);
		Held->SetActorLocation(Entry->Home, false, nullptr, ETeleportType::TeleportPhysics);
		if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Held->GetRootComponent()))
		{
			Root->SetPhysicsLinearVelocity(FVector::ZeroVector);
		}
		Agents->ReleaseCatReservations(Me);
		RecoverItem = nullptr;
		UE_LOG(LogTemp, Log, TEXT("[TagAI] %s put %s back at %.2f s"), *Me->GetName(), *Held->ItemInfo.ItemName, Agents->GetElapsedSeconds());
		return;
	}

	MoveToLocation(Goal, 20.f, false, true, false, true, nullptr, true);

	if (CVarTagDebug.GetValueOnGameThread() != 0)
	{
		DrawDebugSphere(GetWorld(), Entry->Home, 60.f, 8, FColor::Purple, false, 0.3f);
		DrawDebugLine(GetWorld(), MyPos, Entry->Home, FColor::Purple, false, 0.3f, 0, 3.f);
	}
}

void ATagCatAIController::Chase(APawn* Target)
{
	APawn* Me = GetPawn();
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
	// 평면 거리 Tag.CatchDistance(150) 안 + 높이 차이 Tag.CatchHeight(100) 안이면 잡음
	// 쫓는 쥐뿐 아니라, 옆을 지나가는 다른 쥐도 닿으면 잡음
	APawn* Me = GetPawn();
	UTagAgentSubsystem* Agents = GetWorld()->GetSubsystem<UTagAgentSubsystem>();
	if (!Me || !Agents) return;

	// 물건을 들고 있는 동안은 잡을 수 없음 (팀원 InteractionComponent 규칙)
	const UInteractionComponent* MyInteract = Me->FindComponentByClass<UInteractionComponent>();
	if (MyInteract && MyInteract->IsHoldingItem()) return;

	TArray<APawn*> Rats;
	Agents->GetAliveRats(Rats);
	const float CatchDist = CVarCatchDistance.GetValueOnGameThread();

	for (APawn* Rat : Rats)
	{
		// 평면 거리 + 높이 차이 둘 다 봄. 평면 거리만 보면 계단 위아래나 다른 층(천장 너머) 쥐까지 잡힘
		const FVector MyLoc = Me->GetActorLocation();
		const FVector RatLoc = Rat->GetActorLocation();
		if (FVector::Dist2D(MyLoc, RatLoc) > CatchDist) continue;
		if (FMath::Abs(MyLoc.Z - RatLoc.Z) > CVarCatchHeight.GetValueOnGameThread()) continue;

		if (Agents->ReportCaught(Me, Rat))
		{
			if (ATagRatAIController* RatAI = Cast<ATagRatAIController>(Rat->GetController()))
			{
				RatAI->OnCaught();             // AI 쥐: 물건 떨어뜨리고, 멈추고 사라짐
			}
			else
			{
				Agents->HandlePlayerCaught(Rat); // 플레이어 쥐: 물건 떨어뜨리고, 조작을 막고 멈춤
			}
			if (CurrentTarget.Get() == Rat)
			{
				CurrentTarget = nullptr;
				StopMovement();   // 다음 Think에서 새 쥐를 고름
			}
		}
	}
}
