#include "InteractionComponent.h"
#include "PickupItem.h"
#include "GameFramework/Pawn.h"
#include "DrawDebugHelpers.h"

UInteractionComponent::UInteractionComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UInteractionComponent::BeginPlay()
{
    Super::BeginPlay();
}

void UInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    TraceForward();
}

void UInteractionComponent::TraceForward()
{
    // 1. 이 컴포넌트를 달고 있는 액터(BP_Rat)를 폰으로 가져옵니다.
    APawn* OwnerPawn = Cast<APawn>(GetOwner());
    if (!OwnerPawn) return;

    // 2. 폰을 조종하는 플레이어 컨트롤러를 가져옵니다.
    APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
    if (!PC) return;

    // 3. 카메라 시점 가져오기
    FVector StartLoc;
    FRotator StartRot;
    PC->GetPlayerViewPoint(StartLoc, StartRot);

    FVector EndLoc = StartLoc + (StartRot.Vector() * 500.0f);

    FHitResult HitResult;
    FCollisionQueryParams CollisionParams;
    CollisionParams.AddIgnoredActor(OwnerPawn); // 자기 자신(BP_Rat 몸통) 무시

    bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, StartLoc, EndLoc, ECC_Visibility, CollisionParams);

    // 테스트용 빨간 레이저
    //DrawDebugLine(GetWorld(), StartLoc, EndLoc, FColor::Red, false, 0.1f, 0, 2.0f);

    if (bHit)
    {
        APickupItem* HitItem = Cast<APickupItem>(HitResult.GetActor());
        if (HitItem && FocusedItem != HitItem)
        {
            FocusedItem = HitItem;
            UE_LOG(LogTemp, Warning, TEXT("[컴포넌트 감지] 무게: %.1f / 점수: %d"), FocusedItem->ItemInfo.Weight, FocusedItem->ItemInfo.Score);
        }
        else if (!HitItem)
        {
            FocusedItem = nullptr;
        }
    }
    else
    {
        FocusedItem = nullptr;
    }
}