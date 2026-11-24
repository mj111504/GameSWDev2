#include "SpawnSlotBox.h"

ASpawnSlotBox::ASpawnSlotBox()
{
	PrimaryActorTick.bCanEverTick = false;

	SlotBox = CreateDefaultSubobject<UBoxComponent>(TEXT("SlotBox"));
	RootComponent = SlotBox;

	// 게임 중 캐릭터나 물건과 충돌하지 않도록 콜리전 끄기
	SlotBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SlotBox->SetBoxExtent(FVector(30.f, 30.f, 30.f));
}

void ASpawnSlotBox::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 에디터에서 슬롯 크기를 한눈에 구분하기 위한 색상 설정
	switch (MaxAllowedSize)
	{
	case EItemSize::Small:      SlotBox->ShapeColor = FColor::Green;  break;
	case EItemSize::Medium:     SlotBox->ShapeColor = FColor::Yellow; break;
	case EItemSize::Large:      SlotBox->ShapeColor = FColor::Orange; break;
	case EItemSize::ExtraLarge: SlotBox->ShapeColor = FColor::Red;    break;
	}
}