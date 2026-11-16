#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractionComponent.generated.h"

class APickupItem;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CAT_AND_RAT_API UInteractionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UInteractionComponent();

protected:
    virtual void BeginPlay() override;

public:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    void TraceForward();

    // 블루프린트에서 꺼내 쓸 수 있도록 권한 열기
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
    APickupItem* FocusedItem = nullptr;
};