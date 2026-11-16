#include "InteractionComponent.h"
#include "PickupItem.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"

UInteractionComponent::UInteractionComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    BaseWalkSpeed = 600.0f;
}

void UInteractionComponent::BeginPlay()
{
    Super::BeginPlay();

    if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
    {
        if (UCharacterMovementComponent* MovementComp = OwnerCharacter->GetCharacterMovement())
        {
            BaseWalkSpeed = MovementComp->MaxWalkSpeed;
        }
    }
}

void UInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    TraceForward();
}

void UInteractionComponent::TraceForward()
{
    APawn* OwnerPawn = Cast<APawn>(GetOwner());
    if (!OwnerPawn) return;

    APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
    if (!PC) return;

    FVector StartLoc;
    FRotator StartRot;
    PC->GetPlayerViewPoint(StartLoc, StartRot);

    FVector EndLoc = StartLoc + (StartRot.Vector() * 500.0f);

    FHitResult HitResult;
    FCollisionQueryParams CollisionParams;
    CollisionParams.AddIgnoredActor(OwnerPawn);

    bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, StartLoc, EndLoc, ECC_Visibility, CollisionParams);

    if (bHit)
    {
        APickupItem* HitItem = Cast<APickupItem>(HitResult.GetActor());
        if (HitItem && FocusedItem != HitItem)
        {
            FocusedItem = HitItem;
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

void UInteractionComponent::PickupItem(APickupItem* ItemToPickup)
{
    if (HeldItem == nullptr && ItemToPickup != nullptr)
    {
        ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
        if (!OwnerCharacter) return;

        HeldItem = ItemToPickup;

        UPrimitiveComponent* ItemRoot = Cast<UPrimitiveComponent>(HeldItem->GetRootComponent());
        if (ItemRoot)
        {
            ItemRoot->SetSimulatePhysics(false);
            ItemRoot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }

        HeldItem->AttachToComponent(OwnerCharacter->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, FName("HandSocket"));

        if (UCharacterMovementComponent* MovementComp = OwnerCharacter->GetCharacterMovement())
        {
            // 오류 수정: HeldItem->ItemInfo.Weight -> HeldItem->ItemInfo.mass
            // 속도 패널티는 아이템의 '질량(무게)'인 mass를 기준으로 계산해야 합니다.
            float SpeedPenalty = HeldItem->ItemInfo.mass * 10.0f;
            MovementComp->MaxWalkSpeed = FMath::Max(50.0f, BaseWalkSpeed - SpeedPenalty);
        }
    }
}

void UInteractionComponent::PlaceItem()
{
    if (HeldItem != nullptr)
    {
        ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());

        // 참고: PickupItem.h 구조체에 Score 변수가 없으므로, 문맥상 가중치(Weight)를 점수 대신 전송합니다.
        OnItemPlacedProperly.Broadcast(HeldItem->ItemInfo.Weight);
        HeldItem->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

        UPrimitiveComponent* ItemRoot = Cast<UPrimitiveComponent>(HeldItem->GetRootComponent());
        if (ItemRoot)
        {
            ItemRoot->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        }

        if (OwnerCharacter)
        {
            if (UCharacterMovementComponent* MovementComp = OwnerCharacter->GetCharacterMovement())
            {
                MovementComp->MaxWalkSpeed = BaseWalkSpeed;
            }
        }

        HeldItem = nullptr;
    }
}