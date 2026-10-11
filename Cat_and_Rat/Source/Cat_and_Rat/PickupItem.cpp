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