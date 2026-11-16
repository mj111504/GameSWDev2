#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "InteractionComponent.generated.h"

class APickupItem;
class ACharacter;

// 아이템 사이즈 분류 Enum
UENUM(BlueprintType)
enum class EItemSize : uint8
{
    Small       UMETA(DisplayName = "소형"),
    Medium      UMETA(DisplayName = "중형"),
    Large       UMETA(DisplayName = "대형"),
    ExtraLarge  UMETA(DisplayName = "초대형")
};

// 30종의 아이템 정보를 보관할 데이터 테이블 구조체
USTRUCT(BlueprintType)
struct FItemSizeData : public FTableRowBase
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ItemData")
    FString ItemName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ItemData")
    EItemSize ItemSize;//아이템 사이즈 분류

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ItemData")
    int32 Weight;//아이템 가중치

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ItemData")
    UStaticMesh* ItemMesh;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnItemPlacedProperlySignature, int32, ScoreToGive);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CAT_AND_RAT_API UInteractionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UInteractionComponent();

protected:
    virtual void BeginPlay() override;

    float BaseWalkSpeed;

public:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    void TraceForward();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
    APickupItem* FocusedItem = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Interaction")
    APickupItem* HeldItem = nullptr;

    UPROPERTY(BlueprintAssignable, Category = "Interaction|Score")
    FOnItemPlacedProperlySignature OnItemPlacedProperly;

    UFUNCTION(BlueprintCallable, Category = "Interaction")
    void PickupItem(APickupItem* ItemToPickup);

    UFUNCTION(BlueprintCallable, Category = "Interaction")
    void PlaceItem();
};