#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "InteractionComponent.h" // EItemSize를 사용하기 위해 포함
#include "SpawnSlotBox.generated.h"

UCLASS()
class CAT_AND_RAT_API ASpawnSlotBox : public AActor
{
	GENERATED_BODY()

public:
	ASpawnSlotBox();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpawnSlot")
	UBoxComponent* SlotBox;

	// 이 슬롯에 들어갈 수 있는 최대 아이템 크기
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnSlot")
	EItemSize MaxAllowedSize = EItemSize::Small;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpawnSlot")
	bool bIsOccupied = false;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
};