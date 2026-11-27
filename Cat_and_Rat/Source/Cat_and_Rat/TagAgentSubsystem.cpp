#include "TagAgentSubsystem.h"
#include "TagGridSubsystem.h"
#include "PickupItem.h"
#include "InteractionComponent.h"
#include "TagRatAIController.h"
#include "NavigationSystem.h"
#include "Components/CapsuleComponent.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TriggerBox.h"
#include "Components/BoxComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "ItemSpawnManager.h"
#include "SpawnSlotBox.h"

static TAutoConsoleVariable<int32> CVarPlayerIsRat(
	TEXT("Tag.PlayerIsRat"), 1, TEXT("1 = the player-controlled character joins as a rat, 0 = it is ignored by the cats"));
static TAutoConsoleVariable<float> CVarRecoverRadius(
	TEXT("Tag.RecoverRadius"), 150.f, TEXT("An exported item counts as returned when it lies within this distance (cm) of its original location"));
static TAutoConsoleVariable<float> CVarRecoverScoreRatio(
	TEXT("Tag.RecoverScoreRatio"), 0.5f, TEXT("Cat score for returning an item = item score x this ratio"));
static TAutoConsoleVariable<float> CVarMessWinPercent(
	TEXT("Tag.MessWinPercent"), 50.f, TEXT("Rats win when messiness goes over this percent"));
static TAutoConsoleVariable<float> CVarMessMultiplier(
	TEXT("Tag.MessMultiplier"), 2.f, TEXT("Messiness each exported item adds = its share of total weight x this (1 = plain share). Capped at 100%"));
static TAutoConsoleVariable<float> CVarRespawnSeconds(
	TEXT("Tag.RespawnSeconds"), 5.f, TEXT("Seconds a caught rat waits before respawning in the export zone farthest from the cats. Negative = never respawn"));
static TAutoConsoleVariable<int32> CVarRatLives(
	TEXT("Tag.RatLives"), 0, TEXT("How many times each rat can respawn. 0 = unlimited"));
static TAutoConsoleVariable<int32> CVarCatchLimit(
	TEXT("Tag.CatchLimit"), 0, TEXT("Cats win after this many catches in total. 0 = unlimited"));
static TAutoConsoleVariable<int32> CVarItemPlaceholder(
	TEXT("Tag.ItemPlaceholder"), 1, TEXT("1 = attach a temporary cube to items that have no mesh yet (removed automatically once a real mesh is set), 0 = off"));

static FBox GetZoneBox(const AActor* Zone)
{
	// 충돌 컴포넌트(Trigger Box의 상자) 기준. 충돌이 없는 액터면 전체 모양 기준
	FVector Center, Extent;
	Zone->GetActorBounds(true, Center, Extent);
	if (Extent.IsNearlyZero()) Zone->GetActorBounds(false, Center, Extent);
	return FBox(Center - Extent, Center + Extent);
}

// ---------- 테스트용 임시 큐브 ----------
// 팀원이 물건 메시를 넣기 전까지만 씀. 게임 실행 중에만 붙이므로 BP나 맵에는 저장되지 않음.
// 물건에 진짜 메시가 생기면(또는 Tag.ItemPlaceholder 0) 다음 갱신 때 바로 떼어 냄
static const FName PlaceholderTag(TEXT("TagPlaceholder"));

static void UpdatePlaceholder(APickupItem* Item, bool bHeld)
{
	UStaticMeshComponent* Placeholder = nullptr;
	bool bHasRealMesh = false;
	TInlineComponentArray<UStaticMeshComponent*> Meshes(Item);
	for (UStaticMeshComponent* M : Meshes)
	{
		if (M->ComponentHasTag(PlaceholderTag)) Placeholder = M;
		else if (M->GetStaticMesh()) bHasRealMesh = true;
	}

	const bool bWant = CVarItemPlaceholder.GetValueOnGameThread() != 0 && !bHasRealMesh;
	if (!bWant)
	{
		if (Placeholder) Placeholder->DestroyComponent();
		return;
	}

	if (!Placeholder)
	{
		static UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube || !Item->GetRootComponent()) return;
		Placeholder = NewObject<UStaticMeshComponent>(Item);
		Placeholder->ComponentTags.Add(PlaceholderTag);
		Placeholder->SetStaticMesh(Cube);
		// 바닥·벽에는 부딪히고(떨어뜨려도 안 뚫림), 조준 레이저에도 맞지만, 캐릭터는 막지 않음
		Placeholder->SetCollisionProfileName(TEXT("BlockAllDynamic"));
		Placeholder->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Placeholder->RegisterComponent();
		// 용접(weld)해서 붙임: 내려놓을 때 물리가 켜지면 큐브 충돌도 물건 몸체에 포함돼 바닥에 걸림
		Placeholder->AttachToComponent(Item->GetRootComponent(),
			FAttachmentTransformRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepRelative, true));
		Item->AddInstanceComponent(Placeholder);
	}

	// 크기: 무게가 클수록 큰 큐브 (소형 25cm, 중형 40cm, 대형 60cm, 초대형 90cm). 무게가 나중에 들어와도 맞춰짐
	const int32 W = Item->ItemInfo.Weight;
	const float Side = W >= 10 ? 90.f : (W >= 6 ? 60.f : (W >= 3 ? 40.f : 25.f));
	Placeholder->SetRelativeScale3D(FVector(Side / 100.f));
	Placeholder->SetCollisionEnabled(bHeld ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
}

bool UTagAgentSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTagAgentSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	StartTime = InWorld.GetTimeSeconds();
	RegisterPlayers();
	InWorld.GetTimerManager().SetTimer(PlayerScanHandle,
		FTimerDelegate::CreateUObject(this, &UTagAgentSubsystem::RegisterPlayers), 0.5f, true);

	SpawnLayout(InWorld);   // 반출구역을 먼저 만들어야 아래에서 찾음
	FindExportZones();
	UpdateItems();
	InWorld.GetTimerManager().SetTimer(ItemScanHandle,
		FTimerDelegate::CreateUObject(this, &UTagAgentSubsystem::UpdateItems), 0.25f, true);
}

void UTagAgentSubsystem::RegisterPlayers()
{
	if (CVarPlayerIsRat.GetValueOnGameThread() == 0) return;

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		APawn* P = PC ? PC->GetPawn() : nullptr;
		if (!P || CaughtRats.Contains(P) || Cats.Contains(P)) continue;
		RegisterRat(P);   // 이미 있으면 아무 일도 안 함
	}
}

void UTagAgentSubsystem::HandlePlayerCaught(APawn* Rat)
{
	APlayerController* PC = Rat ? Cast<APlayerController>(Rat->GetController()) : nullptr;
	if (!PC) return;

	if (UInteractionComponent* Interact = Rat->FindComponentByClass<UInteractionComponent>())
	{
		Interact->DropItem(false);   // 잡히면 들고 있던 물건을 떨어뜨림
	}
	PC->SetIgnoreMoveInput(true);
	if (ACharacter* Ch = Cast<ACharacter>(Rat))
	{
		Ch->GetCharacterMovement()->StopMovementImmediately();
	}
	Rat->SetActorHiddenInGame(true);
	Rat->SetActorEnableCollision(false);

	// 관전: 살아 있는 다른 쥐를 비춤 (없으면 그 자리 그대로)
	if (AliveRats.Num() > 0 && AliveRats[0].IsValid())
	{
		PC->SetViewTargetWithBlend(AliveRats[0].Get(), 0.5f);
	}
	const float Wait = CVarRespawnSeconds.GetValueOnGameThread();
	const FString Msg = Wait >= 0.f ? FString::Printf(TEXT("[TagAI] You were caught! Respawn in %.0f s"), Wait)
	                                : FString(TEXT("[TagAI] You were caught!"));
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, FMath::Max(3.f, Wait), FColor::Red, Msg);
}

float UTagAgentSubsystem::GetElapsedSeconds() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() - StartTime : 0.f;
}

void UTagAgentSubsystem::RegisterCat(APawn* Cat)
{
	if (Cat) Cats.AddUnique(Cat);
}

void UTagAgentSubsystem::UnregisterCat(APawn* Cat)
{
	Cats.Remove(Cat);
}

void UTagAgentSubsystem::RegisterRat(APawn* Rat)
{
	if (Rat && !AliveRats.Contains(Rat))
	{
		AliveRats.Add(Rat);
		++NumRatsTotal;
	}
}

void UTagAgentSubsystem::UnregisterRat(APawn* Rat)
{
	AliveRats.Remove(Rat);
}

void UTagAgentSubsystem::GetCats(TArray<APawn*>& OutCats) const
{
	OutCats.Reset();
	for (const TWeakObjectPtr<APawn>& P : Cats)
	{
		if (P.IsValid()) OutCats.Add(P.Get());
	}
}

void UTagAgentSubsystem::GetAliveRats(TArray<APawn*>& OutRats) const
{
	OutRats.Reset();
	for (const TWeakObjectPtr<APawn>& P : AliveRats)
	{
		if (P.IsValid()) OutRats.Add(P.Get());
	}
}

bool UTagAgentSubsystem::IsRatAlive(const APawn* Rat) const
{
	for (const TWeakObjectPtr<APawn>& P : AliveRats)
	{
		if (P.Get() == Rat) return true;
	}
	return false;
}

bool UTagAgentSubsystem::ReportCaught(APawn* Cat, APawn* Rat)
{
	if (!Rat || !IsRatAlive(Rat)) return false;

	AliveRats.Remove(Rat);
	CaughtRats.Add(Rat);
	ReleaseReservations(Rat);
	++NumCaught;
	++CatchCount;
	const int32 RatDeaths = ++Deaths.FindOrAdd(Rat);

	// 부활할 수 있나: 부활 시간이 0 이상이고, 목숨 제한이 없거나 아직 남았으면
	const float Wait = CVarRespawnSeconds.GetValueOnGameThread();
	const int32 Lives = CVarRatLives.GetValueOnGameThread();
	const bool bRespawn = Wait >= 0.f && (Lives <= 0 || RatDeaths <= Lives);

	const float Elapsed = GetElapsedSeconds();
	const FString Msg = FString::Printf(TEXT("[TagAI] %s caught %s at %.2f s (catch #%d, %s)"),
		Cat ? *Cat->GetName() : TEXT("?"), *Rat->GetName(), Elapsed, CatchCount,
		bRespawn ? *FString::Printf(TEXT("respawn in %.0f s"), Wait) : TEXT("out"));
	UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, Msg);

	if (bRespawn)
	{
		++PendingRespawns;
		FTimerHandle Handle;
		GetWorld()->GetTimerManager().SetTimer(Handle,
			FTimerDelegate::CreateUObject(this, &UTagAgentSubsystem::RespawnRat, TWeakObjectPtr<APawn>(Rat)), FMath::Max(0.01f, Wait), false);
	}

	// 고양이 승리: 포획 횟수 제한을 넘었거나, 살아 있는 쥐도 부활 대기 중인 쥐도 없을 때
	const int32 Limit = CVarCatchLimit.GetValueOnGameThread();
	if (Limit > 0 && CatchCount >= Limit)
	{
		CatsWin(FString::Printf(TEXT("%d catches"), CatchCount));
	}
	else if (AliveRats.Num() == 0 && PendingRespawns == 0)
	{
		CatsWin(FString::Printf(TEXT("all %d rats are out"), NumRatsTotal));
	}
	return true;
}

void UTagAgentSubsystem::CatsWin(const FString& Reason)
{
	if (bCatsWon) return;
	bCatsWon = true;
	const FString Msg = FString::Printf(TEXT("[TagAI] Cats win! (%s) at %.2f s"), *Reason, GetElapsedSeconds());
	UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Yellow, Msg);
	OnCatsWin.Broadcast();
}

bool UTagAgentSubsystem::ChooseRespawnPoint(FVector& OutPos) const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	UTagGridSubsystem* Grid = World->GetSubsystem<UTagGridSubsystem>();
	if (ExportZones.Num() == 0) return false;

	// 고양이 칸들에서 여러 출발점 BFS 한 번 → 각 구역 바닥 칸까지 고양이가 걸어서 오는 거리
	TArray<int32> CatDist;
	TArray<FVector> CatPos;
	if (Grid && Grid->GetNumWalkable() > 0)
	{
		TArray<int32> CatCells;
		for (const TWeakObjectPtr<APawn>& C : Cats)
		{
			if (!C.IsValid()) continue;
			CatPos.Add(C->GetActorLocation());
			const int32 Cell = Grid->FindNearestWalkableCell(C->GetActorLocation());
			if (Cell != INDEX_NONE) CatCells.Add(Cell);
		}
		Grid->BFSMulti(CatCells, CatDist);
	}

	const AActor* BestZone = nullptr;
	float BestDist = -1.f;
	for (const TWeakObjectPtr<AActor>& Z : ExportZones)
	{
		const AActor* Zone = Z.Get();
		if (!Zone) continue;
		const FBox Box = GetZoneBox(Zone);
		const FVector Floor(Box.GetCenter().X, Box.GetCenter().Y, Box.Min.Z + 50.f);

		float D = -1.f;
		const int32 Cell = (Grid && CatDist.Num() == Grid->GetNumCells()) ? Grid->FindNearestWalkableCell(Floor) : INDEX_NONE;
		if (Cell != INDEX_NONE && CatDist[Cell] >= 0)
		{
			D = CatDist[Cell] * Grid->GetCellSize();          // 걸어서 거리 (cm)
		}
		else
		{
			D = TNumericLimits<float>::Max();                    // 격자로 못 오면: 직선거리로 대신 (고양이가 없으면 아무 구역)
			for (const FVector& P : CatPos) D = FMath::Min(D, FVector::Dist(Floor, P));
		}
		if (D > BestDist) { BestDist = D; BestZone = Zone; }
	}
	if (!BestZone) return false;

	// 구역 안 랜덤 위치 (가장자리 40%는 빼고) → NavMesh 위로. 실패하면 구역 바닥 가운데
	const FBox Box = GetZoneBox(BestZone);
	const FVector Ext = Box.GetExtent() * 0.6f;
	for (int32 Try = 0; Try < 8; ++Try)
	{
		const FVector P(Box.GetCenter().X + FMath::FRandRange(-Ext.X, Ext.X), Box.GetCenter().Y + FMath::FRandRange(-Ext.Y, Ext.Y), Box.Min.Z + 50.f);
		FNavLocation NavLoc;
		if (NavSys && NavSys->ProjectPointToNavigation(P, NavLoc, FVector(100.f, 100.f, 200.f)))
		{
			OutPos = NavLoc.Location;
			return true;
		}
	}
	OutPos = FVector(Box.GetCenter().X, Box.GetCenter().Y, Box.Min.Z);
	return true;
}

void UTagAgentSubsystem::RespawnRat(TWeakObjectPtr<APawn> RatPtr)
{
	PendingRespawns = FMath::Max(0, PendingRespawns - 1);
	APawn* Rat = RatPtr.Get();
	if (!Rat || bCatsWon) return;

	FVector Pos;
	if (!ChooseRespawnPoint(Pos))
	{
		UE_LOG(LogTemp, Warning, TEXT("[TagAI] %s could not respawn: no export zone"), *Rat->GetName());
		return;
	}

	// 바닥 위로 캡슐 반높이만큼 올려서 순간이동
	if (const ACharacter* Ch = Cast<ACharacter>(Rat))
	{
		Pos.Z += Ch->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f;
	}
	Rat->SetActorLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);
	Rat->SetActorHiddenInGame(false);
	Rat->SetActorEnableCollision(true);
	if (ACharacter* Ch = Cast<ACharacter>(Rat)) Ch->GetCharacterMovement()->StopMovementImmediately();

	CaughtRats.Remove(Rat);
	AliveRats.AddUnique(Rat);

	if (ATagRatAIController* AI = Cast<ATagRatAIController>(Rat->GetController()))
	{
		AI->OnRespawned();
	}
	else if (APlayerController* PC = Cast<APlayerController>(Rat->GetController()))
	{
		PC->SetIgnoreMoveInput(false);
		PC->SetViewTargetWithBlend(Rat, 0.3f);
	}

	const FString Msg = FString::Printf(TEXT("[TagAI] %s respawned at %s at %.2f s"), *Rat->GetName(), *Pos.ToCompactString(), GetElapsedSeconds());
	UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, Msg);
	OnRatRespawned.Broadcast(Rat);
}

// ======================== 물건 ========================

int32 UTagAgentSubsystem::GetItemScore(int32 Weight)
{
	if (Weight >= 10) return 50;
	if (Weight >= 6)  return 30;
	if (Weight >= 3)  return 15;
	return 5;
}

float UTagAgentSubsystem::GetCarrySpeedMultiplier(int32 Weight)
{
	if (Weight >= 10) return 0.30f;
	if (Weight >= 6)  return 0.50f;
	if (Weight >= 3)  return 0.75f;
	return 1.00f;
}

void UTagAgentSubsystem::UpdateItems()
{
	UWorld* World = GetWorld();
	if (!World) return;

	// 반출구역이 아직 없으면 2초마다 다시 찾아봄
	if (ExportZones.Num() == 0 && (++ZoneScanCounter % 8) == 0)
	{
		FindExportZones();
	}

	IConsoleVariable* DebugVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Tag.Debug"));
	const bool bDebug = DebugVar && DebugVar->GetInt() != 0;
	if (bDebug) DrawExportZones(0.3f);   // 반출구역 = 하늘색 상자

	// 1) 새로 생긴 물건 추가
	for (TActorIterator<APickupItem> It(World); It; ++It)
	{
		APickupItem* Item = *It;
		const bool bKnown = Items.ContainsByPredicate([Item](const FTagItemEntry& E) { return E.Item.Get() == Item; });
		if (!bKnown)
		{
			FTagItemEntry Entry;
			Entry.Item = Item;
			Items.Add(Entry);
		}
	}

	// 2) 상태 갱신
	for (int32 i = Items.Num() - 1; i >= 0; --i)
	{
		FTagItemEntry& E = Items[i];
		APickupItem* Item = E.Item.Get();
		if (!Item)
		{
			Items.RemoveAtSwap(i);   // 파괴된 물건
			continue;
		}

		// 누가 들고 있나: 물건이 캐릭터에 붙어 있으면 그 캐릭터가 들고 있는 것
		APawn* Holder = Cast<APawn>(Item->GetAttachParentActor());
		E.Holder = Holder;
		if (Holder) E.LastHolder = Holder;

		UpdatePlaceholder(Item, Holder != nullptr);

		// 물건 위에 "이름 w무게" 표시. 흰색 = 바닥, 노랑 = 누가 들고 있음, 하늘색 = 반출됨(어지러움에 들어감)
		if (bDebug)
		{
			const FColor Color = Holder ? FColor::Yellow : (E.bExported ? FColor::Cyan : FColor::White);
			DrawDebugString(World, Item->GetActorLocation() + FVector(0.f, 0.f, 70.f),
				FString::Printf(TEXT("%s w%d"), *Item->ItemInfo.ItemName, Item->ItemInfo.Weight), nullptr, Color, 0.3f, false, 1.1f);
		}

		// 원래 위치: 처음 바닥에 놓인 상태로 본 위치 (생성 직후 위치를 옮기는 경우를 피하려고 다음 갱신 때 기록)
		if (!E.bHomeSet && !Holder)
		{
			E.Home = Item->GetActorLocation();
			E.bHomeSet = true;
		}

		// 찜한 쥐가 잡혔거나 사라졌으면 찜 해제
		if (E.ReservedBy.IsValid() && !IsRatAlive(E.ReservedBy.Get()))
		{
			E.ReservedBy = nullptr;
		}

		// 찜한 고양이가 사라졌으면 찜 해제
		if (E.ReservedByCat.IsValid() && !Cats.Contains(E.ReservedByCat))
		{
			E.ReservedByCat = nullptr;
		}

		// 반출: 들고 있지 않은 물건이 반출구역 안에 있으면 완료
		if (!E.bExported && !Holder && IsInExportZone(Item->GetActorLocation()))
		{
			ExportItem(E);
		}
		// 회수: 반출됐던 물건이 원래 위치 근처에 놓여 있으면 완료 (들고 있는 동안은 아님)
		else if (E.bExported && !Holder && E.bHomeSet)
		{
			const FVector P = Item->GetActorLocation();
			const float R = CVarRecoverRadius.GetValueOnGameThread();
			if (FVector::Dist2D(P, E.Home) <= R && FMath::Abs(P.Z - E.Home.Z) <= 150.f)
			{
				RecoverItem(E);
			}
		}
	}
}

void UTagAgentSubsystem::ExportItem(FTagItemEntry& Entry)
{
	APickupItem* Item = Entry.Item.Get();
	if (!Item) return;

	Entry.bExported = true;
	Entry.ReservedBy = nullptr;

	const int32 Weight = Item->ItemInfo.Weight;
	const int32 Score = GetItemScore(Weight);
	ExportedScore += Score;
	MessWeight += Weight;

	APawn* Rat = Entry.LastHolder.Get();
	const FString Msg = FString::Printf(TEXT("[TagAI] %s exported %s (weight %d, +%d) at %.2f s. Total %d points, messiness %.1f%%"),
		Rat ? *Rat->GetName() : TEXT("?"), *Item->ItemInfo.ItemName, Weight, Score, GetElapsedSeconds(),
		ExportedScore, GetMessinessPercent());
	UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Cyan, Msg);

	OnItemExported.Broadcast(Item, Rat, Score);
	OnMessinessChanged.Broadcast(GetMessinessPercent());
	CheckRatsWin();
}

void UTagAgentSubsystem::RecoverItem(FTagItemEntry& Entry)
{
	APickupItem* Item = Entry.Item.Get();
	if (!Item) return;

	Entry.bExported = false;
	Entry.ReservedByCat = nullptr;
	Entry.ReservedBy = nullptr;

	const int32 Weight = Item->ItemInfo.Weight;
	const int32 Score = FMath::RoundToInt(GetItemScore(Weight) * CVarRecoverScoreRatio.GetValueOnGameThread());
	RecoveredScore += Score;
	MessWeight = FMath::Max(0, MessWeight - Weight);

	APawn* Cat = Entry.LastHolder.Get();
	const FString Msg = FString::Printf(TEXT("[TagAI] %s returned %s (weight %d, +%d) at %.2f s. Cat total %d points, messiness %.1f%%"),
		Cat ? *Cat->GetName() : TEXT("?"), *Item->ItemInfo.ItemName, Weight, Score, GetElapsedSeconds(),
		RecoveredScore, GetMessinessPercent());
	UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Orange, Msg);

	OnItemRecovered.Broadcast(Item, Cat, Score);
	OnMessinessChanged.Broadcast(GetMessinessPercent());
}

void UTagAgentSubsystem::CheckRatsWin()
{
	if (bRatsWon) return;
	const float Mess = GetMessinessPercent();
	if (Mess > CVarMessWinPercent.GetValueOnGameThread())
	{
		bRatsWon = true;
		const FString Msg = FString::Printf(TEXT("[TagAI] Rats win! Messiness %.1f%% at %.2f s"), Mess, GetElapsedSeconds());
		UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Yellow, Msg);
		OnRatsWin.Broadcast();
	}
}

float UTagAgentSubsystem::GetMessinessPercent() const
{
	int32 TotalWeight = 0;
	for (const FTagItemEntry& E : Items)
	{
		if (const APickupItem* Item = E.Item.Get()) TotalWeight += Item->ItemInfo.Weight;
	}
	// 물건 하나가 올리는 수치 = (무게 ÷ 전체 무게) × 배수. 배수 2면 무게 6짜리(전체 67)가 9.0% → 17.9%. 100%를 넘지 않음
	const float Mult = FMath::Max(0.f, CVarMessMultiplier.GetValueOnGameThread());
	return TotalWeight > 0 ? FMath::Min(100.f, 100.f * MessWeight / TotalWeight * Mult) : 0.f;
}

bool UTagAgentSubsystem::ReserveItem(APickupItem* Item, APawn* Rat)
{
	for (FTagItemEntry& E : Items)
	{
		if (E.Item.Get() != Item) continue;
		APawn* Other = E.ReservedBy.Get();
		if (Other && Other != Rat && IsRatAlive(Other)) return false;
		E.ReservedBy = Rat;
		return true;
	}
	return false;
}

void UTagAgentSubsystem::ReleaseReservations(APawn* Rat)
{
	for (FTagItemEntry& E : Items)
	{
		if (E.ReservedBy.Get() == Rat) E.ReservedBy = nullptr;
	}
}

void UTagAgentSubsystem::TransferReservation(APickupItem* Item, APawn* NewRat)
{
	for (FTagItemEntry& E : Items)
	{
		if (E.Item.Get() == Item) { E.ReservedBy = NewRat; return; }
	}
}

bool UTagAgentSubsystem::ReserveItemForCat(APickupItem* Item, APawn* Cat)
{
	for (FTagItemEntry& E : Items)
	{
		if (E.Item.Get() != Item) continue;
		APawn* Other = E.ReservedByCat.Get();
		if (Other && Other != Cat && Cats.Contains(Other)) return false;
		E.ReservedByCat = Cat;
		return true;
	}
	return false;
}

void UTagAgentSubsystem::ReleaseCatReservations(APawn* Cat)
{
	for (FTagItemEntry& E : Items)
	{
		if (E.ReservedByCat.Get() == Cat) E.ReservedByCat = nullptr;
	}
}

int32 UTagAgentSubsystem::CountRecoveringCats(const APawn* Except) const
{
	TArray<const APawn*, TInlineAllocator<8>> Found;
	for (const FTagItemEntry& E : Items)
	{
		const APawn* C = E.ReservedByCat.Get();
		if (C && C != Except) Found.AddUnique(C);
	}
	return Found.Num();
}

const FTagItemEntry* UTagAgentSubsystem::FindEntry(const APickupItem* Item) const
{
	return Items.FindByPredicate([Item](const FTagItemEntry& E) { return E.Item.Get() == Item; });
}

// ======================== 맵 배치 (반출구역 · 슬롯 · 스폰 매니저) ========================
// 좌표는 Config/DefaultGame.ini 의 [TagLayout] 에 있음 (에디터 Python t.save_layout() / t.capture_slots()가 씀).
// 맵 파일(.umap)에 저장하지 않아도 게임을 시작할 때마다 여기서 만듦.
// 그래서 로비에서 넘어와도, 패키징한 게임에서도 항상 깔리고, 팀원과 맵 파일이 충돌할 일이 없음.
// 이미 맵에 있는 것(에디터에서 Python이 깐 것, 맵에 저장된 것)은 건너뜀 → 두 번 깔리지 않음.
// 이 함수는 다른 액터들의 BeginPlay보다 먼저 불리므로, 스폰 매니저가 BeginPlay에서 슬롯을 찾을 때 이미 슬롯이 있음.
static const TCHAR* LayoutSection = TEXT("TagLayout");

// "a,b,c" → 숫자 Want개. 개수가 다르면 false
static bool ParseNumbers(const FString& Text, int32 First, int32 Want, TArray<float>& Out)
{
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT(","), true);
	if (Parts.Num() != First + Want) return false;
	Out.Reset();
	for (int32 i = First; i < Parts.Num(); ++i) Out.Add(FCString::Atof(*Parts[i].TrimStartAndEnd()));
	return true;
}

void UTagAgentSubsystem::SpawnLayout(UWorld& World)
{
	FString Map;
	GConfig->GetString(LayoutSection, TEXT("Map"), Map, GGameIni);
	const FString Current = UGameplayStatics::GetCurrentLevelName(&World, true);   // PIE 접두어 뺀 맵 이름
	if (Map.IsEmpty() || !Current.Equals(Map, ESearchCase::IgnoreCase)) return;   // 로비 등 다른 맵이면 아무것도 안 함

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 1) 반출구역: 태그 ExportZone 달린 액터가 하나도 없을 때만
	int32 NumZones = 0;
	bool bHasZone = false;
	for (TActorIterator<AActor> It(&World); It; ++It)
	{
		if (It->ActorHasTag(TEXT("ExportZone"))) { bHasZone = true; break; }
	}
	if (!bHasZone)
	{
		TArray<FString> Lines;
		GConfig->GetArray(LayoutSection, TEXT("Zone"), Lines, GGameIni);
		for (const FString& Line : Lines)
		{
			// 이름,x,y,z,반지름x,반지름y,반지름z
			TArray<float> V;
			if (!ParseNumbers(Line, 1, 6, V)) { UE_LOG(LogTemp, Warning, TEXT("[TagLayout] Bad Zone line: %s"), *Line); continue; }
			ATriggerBox* Zone = World.SpawnActor<ATriggerBox>(FVector(V[0], V[1], V[2]), FRotator::ZeroRotator, Params);
			if (!Zone) continue;
			if (UBoxComponent* Box = Cast<UBoxComponent>(Zone->GetCollisionComponent())) Box->SetBoxExtent(FVector(V[3], V[4], V[5]));
			Zone->Tags.Add(TEXT("ExportZone"));
			++NumZones;
		}
	}

	// 2) 슬롯: SpawnSlotBox가 하나도 없을 때만
	int32 NumSlots = 0;
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(&World, ASpawnSlotBox::StaticClass(), Found);
	if (Found.Num() == 0)
	{
		TArray<FString> Lines;
		GConfig->GetArray(LayoutSection, TEXT("Slot"), Lines, GGameIni);
		for (const FString& Line : Lines)
		{
			// x,y,z  (Python에서 손으로 맞춘 위치 그대로)
			TArray<float> V;
			if (!ParseNumbers(Line, 0, 3, V)) { UE_LOG(LogTemp, Warning, TEXT("[TagLayout] Bad Slot line: %s"), *Line); continue; }
			if (World.SpawnActor<ASpawnSlotBox>(FVector(V[0], V[1], V[2]), FRotator::ZeroRotator, Params)) ++NumSlots;
		}
	}

	// 3) 스폰 매니저: 없으면 팀원 BP(MyItemSpawnManager)를 생성 → 그 BeginPlay에서 슬롯마다 물건을 만듦
	TArray<AActor*> Managers;
	UGameplayStatics::GetAllActorsOfClass(&World, AItemSpawnManager::StaticClass(), Managers);
	bool bMadeManager = false;
	if (Managers.Num() == 0)
	{
		FString ClassPath;
		GConfig->GetString(LayoutSection, TEXT("SpawnManager"), ClassPath, GGameIni);
		UClass* Cls = ClassPath.IsEmpty() ? nullptr : LoadClass<AItemSpawnManager>(nullptr, *ClassPath);
		if (Cls)
		{
			bMadeManager = World.SpawnActor<AItemSpawnManager>(Cls, FVector::ZeroVector, FRotator::ZeroRotator, Params) != nullptr;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[TagLayout] Spawn manager class not found: %s"), *ClassPath);
		}
	}
	else if (NumSlots > 0)
	{
		// 맵에 원래 있던 매니저가 우리 슬롯보다 먼저 물건을 만들려고 했으면(0개) 다음 프레임에 한 번 더 시킴
		// 이미 만든 슬롯은 bIsOccupied라 건너뛰므로 두 번 생기지 않음
		for (AActor* A : Managers)
		{
			AItemSpawnManager* M = Cast<AItemSpawnManager>(A);
			World.GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(M, [M]()
			{
				if (M->TotalMessinessWeight == 0) M->SpawnAllItems();
			}));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[TagLayout] %s: spawned %d export zones, %d slots, %s spawn manager (already in level: zones %s, slots %d, managers %d)"),
		*Current, NumZones, NumSlots, bMadeManager ? TEXT("1") : TEXT("0"),
		bHasZone ? TEXT("yes") : TEXT("no"), Found.Num(), Managers.Num());
}

// ======================== 반출구역 ========================

void UTagAgentSubsystem::FindExportZones()
{
	ExportZones.Reset();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TEXT("ExportZone"))) ExportZones.Add(*It);
	}
	if (ExportZones.Num() > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[TagAI] Found %d export zones"), ExportZones.Num());
	}
}


bool UTagAgentSubsystem::IsInExportZone(const FVector& Pos) const
{
	for (const TWeakObjectPtr<AActor>& Z : ExportZones)
	{
		if (const AActor* Zone = Z.Get())
		{
			if (GetZoneBox(Zone).ExpandBy(FVector(0.f, 0.f, 50.f)).IsInsideOrOn(Pos)) return true;
		}
	}
	return false;
}

void UTagAgentSubsystem::DrawExportZones(float Duration) const
{
	for (const TWeakObjectPtr<AActor>& Z : ExportZones)
	{
		if (const AActor* Zone = Z.Get())
		{
			const FBox Box = GetZoneBox(Zone);
			DrawDebugBox(GetWorld(), Box.GetCenter(), Box.GetExtent(), FColor::Cyan, false, Duration, 0, 3.f);
		}
	}
}

void UTagAgentSubsystem::EnsureExportMap(UTagGridSubsystem* Grid)
{
	if (!Grid) return;
	if (ExportMapBuildCount == Grid->GetBuildCount() && ExportMapZoneCount == ExportZones.Num()) return;

	ExportMapBuildCount = Grid->GetBuildCount();
	ExportMapZoneCount = ExportZones.Num();
	ExportCells.Reset();

	for (const TWeakObjectPtr<AActor>& Z : ExportZones)
	{
		const AActor* Zone = Z.Get();
		if (!Zone) continue;

		// 칸은 바닥(NavMesh) 높이에 있으므로, 상자 아래쪽으로 1.5m 여유를 줌
		FBox Box = GetZoneBox(Zone);
		Box.Min.Z -= 150.f;

		const int32 Before = ExportCells.Num();
		for (int32 C = 0; C < Grid->GetNumCells(); ++C)
		{
			if (Box.IsInsideOrOn(Grid->CellToWorld(C))) ExportCells.Add(C);
		}

		// 상자 안에 칸이 하나도 없으면 (NavMesh 밖에 놓은 경우) 상자 바닥 가운데에서 가장 가까운 칸을 씀
		if (ExportCells.Num() == Before)
		{
			const FVector Bottom(Box.GetCenter().X, Box.GetCenter().Y, Box.Min.Z + 150.f);
			const int32 C = Grid->FindNearestWalkableCell(Bottom);
			if (C != INDEX_NONE) ExportCells.Add(C);
			UE_LOG(LogTemp, Warning, TEXT("[TagAI] Export zone %s has no grid cells inside. Using nearest cell %d"),
				*Zone->GetName(), C);
		}
	}

	Grid->BFSMulti(ExportCells, ExportDist);
	UE_LOG(LogTemp, Log, TEXT("[TagAI] Export map: %d zones, %d cells inside zones"), ExportZones.Num(), ExportCells.Num());
}

const TArray<int32>& UTagAgentSubsystem::GetExportCells(UTagGridSubsystem* Grid)
{
	EnsureExportMap(Grid);
	return ExportCells;
}

const TArray<int32>& UTagAgentSubsystem::GetExportDist(UTagGridSubsystem* Grid)
{
	EnsureExportMap(Grid);
	return ExportDist;
}
