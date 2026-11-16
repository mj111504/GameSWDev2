#include "PickupItem.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"

APickupItem::APickupItem()
{
    PrimaryActorTick.bCanEverTick = false;

    // 1. 충돌체 생성 및 루트 컴포넌트로 지정
    CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
    CollisionComponent->InitSphereRadius(50.0f);
    RootComponent = CollisionComponent;

    // 2. 메시 컴포넌트 생성 및 루트(충돌체)에 부착
    ItemMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ItemMesh"));
    ItemMesh->SetupAttachment(RootComponent);
}

void APickupItem::NotifyActorBeginCursorOver()
{
    Super::NotifyActorBeginCursorOver();

    // 테스트용: UI 위젯 대신 화면 좌상단에 녹색 텍스트로 데이터 정보 출력 (3초간 유지)
    if (GEngine)
    {
        UE_LOG(LogTemp, Warning, TEXT("[데이터 연결 테스트] 큐브 무게: %.1f / 점수: %d"), ItemInfo.Weight, ItemInfo.Score);
    }
}

void APickupItem::NotifyActorEndCursorOver()
{
    Super::NotifyActorEndCursorOver();

    // 마우스가 큐브에서 빠져나갔을 때 실행될 로직을 적는 곳입니다.
    // 나중에 UI 툴팁을 숨길 때 사용할 수 있도록 지금은 뼈대만 만들어 둡니다.
}