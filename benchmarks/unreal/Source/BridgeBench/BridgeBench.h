#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "BridgeBench.generated.h"

class UInputAction;
class UInputMappingContext;

UCLASS()
class BRIDGEBENCH_API ABenchController : public APlayerController
{
    GENERATED_BODY()
public:
    // Published exact object paths let the Lua consumer use explicit bridge targets.
    UPROPERTY(BlueprintReadOnly) FString BenchmarkComponentPath;
    UPROPERTY(BlueprintReadOnly) FString BenchmarkActionPath;
    // Pure clock read: usable by the benchmark callback without a game-thread mutation.
    UFUNCTION(BlueprintCallable) double BenchmarkSeconds() const;
protected:
    virtual void SetupInputComponent() override;
private:
    UPROPERTY() TObjectPtr<UInputAction> TestAction;
    UPROPERTY() TObjectPtr<UInputMappingContext> TestContext;
};

UCLASS()
class BRIDGEBENCH_API ABenchGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ABenchGameMode();
};