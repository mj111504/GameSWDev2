#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "InteractionComponent.generated.h"

class APickupItem;
class ACharacter;

// 아이템 사이즈 분류 Enum (기존 데이터 테이블 호환 유지)
UENUM(BlueprintType)
enum class EItemSize : uint8
{
    Small       UMETA(DisplayName = "소형"),
    Medium      UMETA(DisplayName = "중형"),
    Large       UMETA(DisplayName = "대형"),
    ExtraLarge  UMETA(DisplayName = "초대형")
};

// 데이터 테이블 구조체 (기존 MyPickupItem 호환 유지)
USTRUCT(BlueprintType)
struct FItemSizeData : public FTableRowBase
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ItemData")
    FString ItemName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ItemData")
    EItemSize ItemSize = EItemSize::Small;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ItemData")
    int32 Weight = 1; // 소형 1, 중형 3, 대형 6, 초대형 10

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ItemData")
    UStaticMesh* ItemMesh = nullptr;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnItemStateChangedSignature, APickupItem*, TargetItem);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnItemPlacedProperlySignature, int32, WeightValue);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CAT_AND_RAT_API UInteractionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UInteractionComponent();

protected:
    virtual void BeginPlay() override;

    float BaseWalkSpeed = 600.0f;

    // 가중치(1, 3, 6, 10)에 따른 쥐 이동속도 배율 계산
    float GetSpeedMultiplierByWeight(int32 Weight) const;

    UPROPERTY()
    USceneComponent* HoldPointComp = nullptr;

public:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    void TraceForward();

    // --- [진영 및 동작 설정] 블루프린트 디테일 패널에서 캐릭터별로 체크 ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Config")
    bool bIsCat = false; // true: 고양이(이속 페널티 없음, 초대형 들기 가능) / false: 쥐

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Config")
    bool bEnableCrosshairTrace = true; // AI 캐릭터에서는 false로 꺼둠

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Config")
    float TraceDistance = 500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Config")
    float ThrowImpulse = 600.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Config")
    FName HoldPointName = FName("HoldPoint");

    // --- [상태 변수] UI 및 블루프린트 참조용 ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction|State")
    APickupItem* FocusedItem = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Interaction|State")
    APickupItem* HeldItem = nullptr;

    // --- [이벤트] 무기 숨김, UI 갱신, 스폰구역 표시 등에 바인딩 ---
    UPROPERTY(BlueprintAssignable, Category = "Interaction|Events")
    FOnItemStateChangedSignature OnItemPickedUp;

    UPROPERTY(BlueprintAssignable, Category = "Interaction|Events")
    FOnItemStateChangedSignature OnItemDropped;

    UPROPERTY(BlueprintAssignable, Category = "Interaction|Events")
    FOnItemPlacedProperlySignature OnItemPlacedProperly;

    // --- [상호작용 함수] ---
    // 좌클릭 하나에 바인딩할 통합 함수 (빈손이면 줍기, 들고 있으면 놓기/던지기)
    UFUNCTION(BlueprintCallable, Category = "Interaction")
    void TogglePickupOrDrop(bool bThrowOnDrop = false);

    UFUNCTION(BlueprintCallable, Category = "Interaction")
    bool PickupItem(APickupItem* ItemToPickup);

    // 물건 내려놓기 / 던지기 / 쥐 포획 시 강제 낙하 공용
    UFUNCTION(BlueprintCallable, Category = "Interaction")
    void DropItem(bool bThrow = false);

    // 기존 블루프린트 호환용 (내부적으로 DropItem 호출)
    UFUNCTION(BlueprintCallable, Category = "Interaction")
    void PlaceItem();

    // 고양이 공격 가능 여부 체크용 (물건을 들고 있으면 공격 불가)
    UFUNCTION(BlueprintPure, Category = "Interaction")
    bool IsHoldingItem() const { return HeldItem != nullptr; }
};