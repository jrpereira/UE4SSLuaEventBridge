#include "BridgeBench.h"
#include "Modules/ModuleManager.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/DefaultPawn.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, BridgeBench, "BridgeBench");

ABenchGameMode::ABenchGameMode()
{
    PlayerControllerClass = ABenchController::StaticClass();
    DefaultPawnClass = ADefaultPawn::StaticClass();
}

double ABenchController::BenchmarkSeconds() const
{
    return FPlatformTime::Seconds();
}

void ABenchController::SetupInputComponent()
{
    Super::SetupInputComponent();
    auto* Component = Cast<UEnhancedInputComponent>(InputComponent);
    auto* Local = GetLocalPlayer();
    auto* Subsystem = Local ? Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
    if (!Component || !Subsystem) return;

    TestAction = NewObject<UInputAction>(this, TEXT("BenchmarkF9"));
    TestAction->ValueType = EInputActionValueType::Boolean;
    TestContext = NewObject<UInputMappingContext>(this, TEXT("BenchmarkContext"));
    TestContext->MapKey(TestAction, EKeys::F9);
    Subsystem->AddMappingContext(TestContext, 1000);

    // Assign the engine's weak serial before the bridge's fail-closed native bind.
    const auto SoftReference = UKismetSystemLibrary::Conv_ObjectToSoftObjectReference(TestAction);
    (void)SoftReference;
    BenchmarkComponentPath = Component->GetPathName();
    BenchmarkActionPath = TestAction->GetPathName();
    SetInputMode(FInputModeGameOnly());
}