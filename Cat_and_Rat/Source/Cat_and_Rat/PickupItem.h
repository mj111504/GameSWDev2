#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PickupItem.generated.h"

// 1. 아이템 데이터 구조체
USTRUCT(BlueprintType)
struct FItemData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Stats")
    FString ItemName;//아이템 이름

    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Stats")
    float mass;//아이템 질량(무게) 

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Stats")
    int32 Weight;//아이템 가중치

    FItemData()
    {
		ItemName = "Default Item";
        mass = 1.0f;
        Weight = 1;
    }
};

// 2. 아이템 액터 클래스
UCLASS()
class CAT_AND_RAT_API APickupItem : public AActor
{
    GENERATED_BODY()

public:
    APickupItem();

    virtual void NotifyActorBeginCursorOver() override;
    virtual void NotifyActorEndCursorOver() override;

    // 에디터에서 설정할 아이템 정보
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Stats")
    FItemData ItemInfo;

protected:
    // 충돌 판정용 구체 컴포넌트
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    class USphereComponent* CollisionComponent;

    // 시각적 외형(3D 모델)을 담당할 메시 컴포넌트
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    class UStaticMeshComponent* ItemMesh;
};