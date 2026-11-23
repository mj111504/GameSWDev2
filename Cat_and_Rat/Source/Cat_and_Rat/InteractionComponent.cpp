#include "InteractionComponent.h"
#include "PickupItem.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/PrimitiveComponent.h"

UInteractionComponent::UInteractionComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
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

    // AI 등 트레이스가 필요 없는 경우 Tick 자체를 꺼서 성능 절약
    if (!bEnableCrosshairTrace)
    {
        SetComponentTickEnabled(false);
    }
}

void UInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (bEnableCrosshairTrace)
    {
        TraceForward();
    }
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

    FVector EndLoc = StartLoc + (StartRot.Vector() * TraceDistance);

    FHitResult HitResult;
    FCollisionQueryParams CollisionParams;
    CollisionParams.AddIgnoredActor(OwnerPawn);
    if (HeldItem)
    {
        CollisionParams.AddIgnoredActor(HeldItem); // 들고 있는 물건에 레이저가 막히지 않도록 무시
    }

    bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, StartLoc, EndLoc, ECC_Visibility, CollisionParams);

    if (bHit)
    {
        APickupItem* HitItem = Cast<APickupItem>(HitResult.GetActor());
        FocusedItem = HitItem;
    }
    else
    {
        FocusedItem = nullptr;
    }
}

float UInteractionComponent::GetSpeedMultiplierByWeight(int32 Weight) const
{
    // 기획서 크기별 이동속도 배율: 소형(1) 100%, 중형(3) 75%, 대형(6) 50%, 초대형(10) 30%
    if (Weight >= 10) return 0.30f;
    if (Weight >= 6)  return 0.50f;
    if (Weight >= 3)  return 0.75f;
    return 1.00f;
}

void UInteractionComponent::TogglePickupOrDrop(bool bThrowOnDrop)
{
    if (HeldItem != nullptr)
    {
        DropItem(bThrowOnDrop);
    }
    else if (FocusedItem != nullptr)
    {
        PickupItem(FocusedItem);
    }
}

bool UInteractionComponent::PickupItem(APickupItem* ItemToPickup)
{
    if (HeldItem != nullptr || ItemToPickup == nullptr)
    {
        return false;
    }

    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    if (!OwnerCharacter) return false;

    const int32 ItemWeight = ItemToPickup->ItemInfo.Weight;

    // 기획서 규칙: 쥐는 초대형(가중치 10 이상) 물건을 들 수 없음 (고양이는 가능)
    if (!bIsCat && ItemWeight >= 10)
    {
        return false;
    }

    HeldItem = ItemToPickup;

    if (UPrimitiveComponent* ItemRoot = Cast<UPrimitiveComponent>(HeldItem->GetRootComponent()))
    {
        ItemRoot->SetSimulatePhysics(false);
        ItemRoot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    HeldItem->AttachToComponent(
        OwnerCharacter->GetMesh(),
        FAttachmentTransformRules::SnapToTargetNotIncludingScale,
        HandSocketName
    );

    // 쥐에게만 크기별 이동속도 페널티 적용 (고양이는 항상 100%)
    if (!bIsCat)
    {
        if (UCharacterMovementComponent* MovementComp = OwnerCharacter->GetCharacterMovement())
        {
            MovementComp->MaxWalkSpeed = BaseWalkSpeed * GetSpeedMultiplierByWeight(ItemWeight);
        }
    }

    OnItemPickedUp.Broadcast(HeldItem);
    return true;
}

void UInteractionComponent::DropItem(bool bThrow)
{
    if (HeldItem == nullptr) return;

    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    APickupItem* DroppedItem = HeldItem;

    DroppedItem->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

    if (UPrimitiveComponent* ItemRoot = Cast<UPrimitiveComponent>(DroppedItem->GetRootComponent()))
    {
        ItemRoot->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        ItemRoot->SetSimulatePhysics(true);

        if (bThrow && OwnerCharacter)
        {
            FVector ThrowDir = OwnerCharacter->GetActorForwardVector();
            if (APlayerController* PC = Cast<APlayerController>(OwnerCharacter->GetController()))
            {
                FVector CamLoc;
                FRotator CamRot;
                PC->GetPlayerViewPoint(CamLoc, CamRot);
                ThrowDir = CamRot.Vector();
            }
            ItemRoot->AddImpulse(ThrowDir * ThrowImpulse, NAME_None, true);
        }
    }

    if (OwnerCharacter)
    {
        if (UCharacterMovementComponent* MovementComp = OwnerCharacter->GetCharacterMovement())
        {
            MovementComp->MaxWalkSpeed = BaseWalkSpeed;
        }
    }

    HeldItem = nullptr;
    OnItemDropped.Broadcast(DroppedItem);
}

void UInteractionComponent::PlaceItem()
{
    if (HeldItem != nullptr)
    {
        const int32 WeightVal = HeldItem->ItemInfo.Weight;
        DropItem(false);
        OnItemPlacedProperly.Broadcast(WeightVal);
    }
}