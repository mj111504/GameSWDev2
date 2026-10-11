#include "MyCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Math/UnrealMathUtility.h"

AMyCharacter::AMyCharacter()
{
    BaseWalkSpeed = 600.0f; // 언리얼 기본값으로 초기화 (BeginPlay에서 덮어씌워짐)
}

void AMyCharacter::BeginPlay()
{
    Super::BeginPlay();

    // 캐릭터 무브먼트에서 초기 최대 걷기 속도를 가져와 저장합니다.
    BaseWalkSpeed = GetCharacterMovement()->MaxWalkSpeed;
}

void AMyCharacter::PickupItem(APickupItem* ItemToPickup)
{
    if (HeldItem == nullptr && ItemToPickup != nullptr)
    {
        HeldItem = ItemToPickup;

        UPrimitiveComponent* ItemRoot = Cast<UPrimitiveComponent>(HeldItem->GetRootComponent());
        if (ItemRoot)
        {
            // 물리 효과 해제 및 충돌 무시 처리
            ItemRoot->SetSimulatePhysics(false);
            ItemRoot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }

        // 스켈레탈 메시의 "HandSocket" 위치에 아이템을 부착합니다.
        HeldItem->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, FName("HandSocket"));

        // 무게 1당 속도 10 감소 로직 적용 (최저 속도 50 보장)
        float SpeedPenalty = HeldItem->ItemInfo.Weight * 10.0f;
        GetCharacterMovement()->MaxWalkSpeed = FMath::Max(50.0f, BaseWalkSpeed - SpeedPenalty);
    }
}

void AMyCharacter::PlaceItem()
{
    if (HeldItem != nullptr)
    {
        // 블루프린트 로직(Add_Score)을 실행 신호와 점수 발송
        OnItemPlacedProperly(HeldItem->ItemInfo.Score);

        // 캐릭터 손에서 아이템 분리
        HeldItem->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

        // 내려놓은 아이템의 충돌체 다시 활성화 (필요한 경우)
        UPrimitiveComponent* ItemRoot = Cast<UPrimitiveComponent>(HeldItem->GetRootComponent());
        if (ItemRoot)
        {
            ItemRoot->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        }

        // 4. 이동 속도 원래대로 복구 및 포인터 초기화
        GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
        HeldItem = nullptr;
    }
}