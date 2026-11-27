#include "ItemSpawnManager.h"
#include "Kismet/GameplayStatics.h"

AItemSpawnManager::AItemSpawnManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AItemSpawnManager::BeginPlay()
{
	Super::BeginPlay();

	// 멀티플레이 환경 대비: 서버에서만 스폰 실행
	if (HasAuthority())
	{
		SpawnAllItems();
	}
}

int32 AItemSpawnManager::GetWeightBySize(EItemSize Size) const
{
	switch (Size)
	{
	case EItemSize::Small:      return 1;
	case EItemSize::Medium:     return 3;
	case EItemSize::Large:      return 6;
	case EItemSize::ExtraLarge: return 10;
	default:                    return 1;
	}
}

EItemSize AItemSpawnManager::GetItemSizeFromRow(FName RowName) const
{
	if (!ItemDataTable) return EItemSize::Small;

	static const FString ContextString(TEXT("ItemSpawnManager_FindRow"));
	if (const FItemSizeData* Row = ItemDataTable->FindRow<FItemSizeData>(RowName, ContextString))
	{
		return Row->ItemSize;
	}

	return EItemSize::Small;
}

int32 AItemSpawnManager::RollRandomSizeIndex() const
{
	int32 TotalChance = SmallChance + MediumChance + LargeChance + ExtraLargeChance;
	if (TotalChance <= 0)
	{
		return FMath::RandRange(0, 3);
	}

	int32 Roll = FMath::RandRange(1, TotalChance);

	if (Roll <= SmallChance) return 0; // Small
	Roll -= SmallChance;

	if (Roll <= MediumChance) return 1; // Medium
	Roll -= MediumChance;

	if (Roll <= LargeChance) return 2; // Large
	return 3; // ExtraLarge
}

void AItemSpawnManager::SpawnAllItems()
{
	if (!ItemDataTable || !PickupItemClass) return;

	// 1. 데이터 테이블의 모든 행을 크기 등급별(0~3) 버킷으로 분류
	//    메시(ItemMesh)가 아직 없는 행은 빼서 아예 안 나오게 함 (메시를 넣으면 자동으로 다시 나옴)
	TArray<FName> ItemBuckets[4];
	TArray<FName> AllRowNames;
	int32 SkippedNoMesh = 0;
	{
		static const FString MeshContext(TEXT("ItemSpawnManager_MeshCheck"));
		for (const FName& RowName : ItemDataTable->GetRowNames())
		{
			const FItemSizeData* Row = ItemDataTable->FindRow<FItemSizeData>(RowName, MeshContext);
			if (!Row || !Row->ItemMesh)
			{
				++SkippedNoMesh;
				continue;
			}
			AllRowNames.Add(RowName);
		}
	}
	if (SkippedNoMesh > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[ItemSpawnManager] 메시가 없는 행 %d개는 스폰에서 제외"), SkippedNoMesh);
	}
	if (AllRowNames.Num() == 0) return;

	for (const FName& RowName : AllRowNames)
	{
		EItemSize Size = GetItemSizeFromRow(RowName);
		ItemBuckets[static_cast<int32>(Size)].Add(RowName);
	}

	// 2. 레벨에 배치된 모든 ASpawnSlotBox 수집
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ASpawnSlotBox::StaticClass(), FoundActors);

	TotalMessinessWeight = 0;
	int32 SpawnedCount = 0;

	for (AActor* Actor : FoundActors)
	{
		ASpawnSlotBox* Slot = Cast<ASpawnSlotBox>(Actor);
		if (!Slot || Slot->bIsOccupied) continue;

		// 3. 가중 확률(40/30/20/10)에 따라 박스에 랜덤 등급 부여
		int32 RandomSizeIdx = RollRandomSizeIndex();
		EItemSize AssignedSize = static_cast<EItemSize>(RandomSizeIdx);
		Slot->MaxAllowedSize = AssignedSize;

		// 해당 등급 버킷에서 랜덤 아이템 선택 (비어 있으면 전체 목록에서 대체)
		FName ChosenRow;
		if (ItemBuckets[RandomSizeIdx].Num() > 0)
		{
			int32 RandItemIdx = FMath::RandRange(0, ItemBuckets[RandomSizeIdx].Num() - 1);
			ChosenRow = ItemBuckets[RandomSizeIdx][RandItemIdx];
		}
		else
		{
			ChosenRow = AllRowNames[FMath::RandRange(0, AllRowNames.Num() - 1)];
			AssignedSize = GetItemSizeFromRow(ChosenRow);
		}

		// 4. 아이템 스폰 및 가중치 합산
		Slot->bIsOccupied = true;

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		AActor* SpawnedItem = GetWorld()->SpawnActor<AActor>(
			PickupItemClass,
			Slot->GetActorLocation(),
			Slot->GetActorRotation(),
			SpawnParams
		);

		if (SpawnedItem)
		{
			int32 Weight = GetWeightBySize(AssignedSize);
			TotalMessinessWeight += Weight;
			SpawnedCount++;

			// 메쉬가 없어도 스폰 여부와 할당된 데이터를 확인하기 위한 로그
			UE_LOG(LogTemp, Warning, TEXT("[Spawn #%d] 슬롯: %s | Row: %s | 등급: %d | 가중치: %d | 위치: %s"),
				SpawnedCount,
				*Slot->GetName(),
				*ChosenRow.ToString(),
				static_cast<int32>(AssignedSize),
				Weight,
				*SpawnedItem->GetActorLocation().ToString()
			);

			OnItemSpawned(SpawnedItem, ChosenRow, Weight);
		}
	}

	// 5. 최종 어지러움 기본 퍼센트 계산
	BaseMessinessPercent = (TotalMessinessWeight > 0) ? (100.0f / static_cast<float>(TotalMessinessWeight)) : 0.0f;

	UE_LOG(LogTemp, Log, TEXT("[ItemSpawnManager] 스폰 완료: %d개 | 총 가중치: %d | 기본 퍼센트: %.2f%%"),
		SpawnedCount, TotalMessinessWeight, BaseMessinessPercent);
}
