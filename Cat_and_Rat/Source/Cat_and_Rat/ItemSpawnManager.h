#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "SpawnSlotBox.h"
#include "ItemSpawnManager.generated.h"

UCLASS()
class CAT_AND_RAT_API AItemSpawnManager : public AActor
{
	GENERATED_BODY()

public:
	AItemSpawnManager();

	// DT_ItemSizeData 연결
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnConfig")
	UDataTable* ItemDataTable;

	// MyPickupItem 블루프린트 클래스 연결
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnConfig")
	TSubclassOf<AActor> PickupItemClass;

	// 스폰 완료 후 확정되는 어지러움 수치 기준값
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Messiness")
	int32 TotalMessinessWeight = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Messiness")
	float BaseMessinessPercent = 0.0f;

	UFUNCTION(BlueprintCallable, Category = "Spawn")
	void SpawnAllItems();

	// 스폰된 MyPickupItem에 RowName을 넘겨 InitializeItem을 호출하기 위한 블루프린트 이벤트
	UFUNCTION(BlueprintImplementableEvent, Category = "Spawn")
	void OnItemSpawned(AActor* SpawnedItem, FName RowName, int32 ItemWeight);

	// 등급별 등장 확률 가중치 (기본값: 소형 40%, 중형 30%, 대형 20%, 초대형 10%)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBalance", meta = (ClampMin = "0"))
	int32 SmallChance = 40;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBalance", meta = (ClampMin = "0"))
	int32 MediumChance = 30;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBalance", meta = (ClampMin = "0"))
	int32 LargeChance = 20;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBalance", meta = (ClampMin = "0"))
	int32 ExtraLargeChance = 10;


protected:
	virtual void BeginPlay() override;

private:
	int32 GetWeightBySize(EItemSize Size) const;
	EItemSize GetItemSizeFromRow(FName RowName) const;
	int32 RollRandomSizeIndex() const;
};