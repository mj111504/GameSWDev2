#include "TagAgentSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarPlayerIsRat(
	TEXT("Tag.PlayerIsRat"), 1, TEXT("1 = the player-controlled character joins as a rat, 0 = it is ignored by the cats"));

bool UTagAgentSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTagAgentSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	StartTime = InWorld.GetTimeSeconds();
	RegisterPlayers();
	InWorld.GetTimerManager().SetTimer(PlayerScanHandle,
		FTimerDelegate::CreateUObject(this, &UTagAgentSubsystem::RegisterPlayers), 0.5f, true);
}

void UTagAgentSubsystem::RegisterPlayers()
{
	if (CVarPlayerIsRat.GetValueOnGameThread() == 0) return;

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		APawn* P = PC ? PC->GetPawn() : nullptr;
		if (!P || CaughtRats.Contains(P) || Cats.Contains(P)) continue;
		RegisterRat(P);   // 이미 있으면 아무 일도 안 함
	}
}

void UTagAgentSubsystem::HandlePlayerCaught(APawn* Rat)
{
	APlayerController* PC = Rat ? Cast<APlayerController>(Rat->GetController()) : nullptr;
	if (!PC) return;

	PC->SetIgnoreMoveInput(true);
	if (ACharacter* Ch = Cast<ACharacter>(Rat))
	{
		Ch->GetCharacterMovement()->StopMovementImmediately();
	}
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, TEXT("[TagAI] You were caught!"));
}

float UTagAgentSubsystem::GetElapsedSeconds() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() - StartTime : 0.f;
}

void UTagAgentSubsystem::RegisterCat(APawn* Cat)
{
	if (Cat) Cats.AddUnique(Cat);
}

void UTagAgentSubsystem::UnregisterCat(APawn* Cat)
{
	Cats.Remove(Cat);
}

void UTagAgentSubsystem::RegisterRat(APawn* Rat)
{
	if (Rat && !AliveRats.Contains(Rat))
	{
		AliveRats.Add(Rat);
		++NumRatsTotal;
	}
}

void UTagAgentSubsystem::UnregisterRat(APawn* Rat)
{
	AliveRats.Remove(Rat);
}

void UTagAgentSubsystem::GetCats(TArray<APawn*>& OutCats) const
{
	OutCats.Reset();
	for (const TWeakObjectPtr<APawn>& P : Cats)
	{
		if (P.IsValid()) OutCats.Add(P.Get());
	}
}

void UTagAgentSubsystem::GetAliveRats(TArray<APawn*>& OutRats) const
{
	OutRats.Reset();
	for (const TWeakObjectPtr<APawn>& P : AliveRats)
	{
		if (P.IsValid()) OutRats.Add(P.Get());
	}
}

bool UTagAgentSubsystem::IsRatAlive(const APawn* Rat) const
{
	for (const TWeakObjectPtr<APawn>& P : AliveRats)
	{
		if (P.Get() == Rat) return true;
	}
	return false;
}

bool UTagAgentSubsystem::ReportCaught(APawn* Cat, APawn* Rat)
{
	if (!Rat || !IsRatAlive(Rat)) return false;

	AliveRats.Remove(Rat);
	CaughtRats.Add(Rat);
	++NumCaught;

	const float Elapsed = GetElapsedSeconds();
	const FString Msg = FString::Printf(TEXT("[TagAI] %s caught %s at %.2f s (%d / %d)"),
		Cat ? *Cat->GetName() : TEXT("?"), *Rat->GetName(), Elapsed, NumCaught, NumRatsTotal);
	UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, Msg);

	if (AliveRats.Num() == 0)
	{
		const FString Done = FString::Printf(TEXT("[TagAI] All %d rats caught in %.2f s"), NumRatsTotal, Elapsed);
		UE_LOG(LogTemp, Log, TEXT("%s"), *Done);
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Yellow, Done);
	}
	return true;
}
