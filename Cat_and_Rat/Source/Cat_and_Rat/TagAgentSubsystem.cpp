#include "TagAgentSubsystem.h"
#include "TagGridSubsystem.h"
#include "PickupItem.h"
#include "InteractionComponent.h"

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

static TAutoConsoleVariable<int32> CVarPlayerIsRat(
	TEXT("Tag.PlayerIsRat"), 1, TEXT("1 = the player-controlled character joins as a rat, 0 = it is ignored by the cats"));
static TAutoConsoleVariable<float> CVarRecoverRadius(
	TEXT("Tag.RecoverRadius"), 150.f, TEXT("An exported item counts as returned when it lies within this distance (cm) of its original location"));
static TAutoConsoleVariable<float> CVarRecoverScoreRatio(
	TEXT("Tag.RecoverScoreRatio"), 0.5f, TEXT("Cat score for returning an item = item score x this ratio"));
static TAutoConsoleVariable<float> CVarMessWinPercent(
	TEXT("Tag.MessWinPercent"), 80.f, TEXT("Rats win when messiness goes over this percent"));
static TAutoConsoleVariable<int32> CVarItemPlaceholder(
	TEXT("Tag.ItemPlaceholder"), 1, TEXT("1 = attach a temporary cube to items that have no mesh yet (removed automatically once a real mesh is set), 0 = off"));

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
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, TEXT("[TagAI] You were caught!"));
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

	const float Elapsed = GetElapsedSeconds();
	const FString Msg = FString::Printf(TEXT("[TagAI] %s caught %s at %.2f s (%d / %d)"),
		Cat ? *Cat->GetName() : TEXT("?"), *Rat->GetName(), Elapsed, NumCaught, NumRatsTotal);
	UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, Msg);

	if (AliveRats.Num() == 0)
	{
		const FString Done = FString::Printf(TEXT("[TagAI] All %d rats caught in %.2f s"), NumRatsTotal, Elapsed);
		UE_LOG(LogTemp, Log, TEXT("%s"), *Done);
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Yellow, Done);
	}
	return true;
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
	return TotalWeight > 0 ? 100.f * MessWeight / TotalWeight : 0.f;
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

static FBox GetZoneBox(const AActor* Zone)
{
	// 충돌 컴포넌트(Trigger Box의 상자) 기준. 충돌이 없는 액터면 전체 모양 기준
	FVector Center, Extent;
	Zone->GetActorBounds(true, Center, Extent);
	if (Extent.IsNearlyZero()) Zone->GetActorBounds(false, Center, Extent);
	return FBox(Center - Extent, Center + Extent);
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
