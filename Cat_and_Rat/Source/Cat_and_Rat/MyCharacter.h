#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "PickupItem.h" // 아이템 클래스와 구조체 사용을 위해 포함
#include "MyCharacter.generated.h"

UCLASS()
class  CAT_AND_RAT_API AMyCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AMyCharacter();

protected:
    virtual void BeginPlay() override;

    // 원래의 이동 속도 캐싱
    float BaseWalkSpeed;

public:
    // 현재 손에 들고 있는 아이템 포인터
    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Interaction")
    APickupItem* HeldItem = nullptr;

    // 블루프린트로 점수 전달용 신호 이벤트
    UFUNCTION(BlueprintImplementableEvent, Category = "Score")
    void OnItemPlacedProperly(int32 ScoreToGive);

    // 상호작용 함수들
    UFUNCTION(BlueprintCallable, Category = "Interaction")
    void PickupItem(APickupItem* ItemToPickup);

    UFUNCTION(BlueprintCallable, Category = "Interaction")
    void PlaceItem();
};