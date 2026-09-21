// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/World.h"
#include "ShooterCharacter.h"
#include "ShooterBulletCounterUI.h"
#include "NetworkDemo.h"
#include "Widgets/Input/SVirtualJoystick.h"

namespace
{
	/** 返回便于区分 Listen Server/Client 的网络模式文本 */
	const TCHAR* GetNetModeLabel(const UObject* Object)
	{
		const UWorld* World = Object ? Object->GetWorld() : nullptr;
		if (!World)
		{
			return TEXT("Unknown");
		}

		switch (World->GetNetMode())
		{
		case NM_Standalone:
			return TEXT("Standalone");
		case NM_DedicatedServer:
			return TEXT("DedicatedServer");
		case NM_ListenServer:
			return TEXT("ListenServer");
		case NM_Client:
			return TEXT("Client");
		default:
			return TEXT("Unknown");
		}
	}
}

void AShooterPlayerController::BeginPlay()
{
	Super::BeginPlay();
}

void AShooterPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// add the input mapping contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}

		if (ShouldUseTouchControls())
		{
			// spawn the mobile controls widget
			MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

			if (MobileControlsWidget)
			{
				// add the controls to the player screen
				MobileControlsWidget->AddToPlayerScreen(0);

			} else {

				UE_LOG(LogNetworkDemo, Error, TEXT("Could not spawn mobile controls widget."));

			}
		}

		// create the bullet counter widget and add it to the screen
		BulletCounterUI = CreateWidget<UShooterBulletCounterUI>(this, BulletCounterUIClass);

		if (BulletCounterUI)
		{
			BulletCounterUI->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogNetworkDemo, Error, TEXT("Could not spawn bullet counter widget."));

		}
	}
}

void AShooterPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (!InPawn)
	{
		return;
	}

	// 服务器监听 Pawn 销毁事件，以便死亡后执行原有重生流程
	InPawn->OnDestroyed.AddDynamic(this, &AShooterPlayerController::OnPawnDestroyed);

	// 只有射击角色才需要设置玩家标记、队伍和 HUD 委托
	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(InPawn))
	{
		// 添加玩家标记，供原有 Gameplay 逻辑识别
		ShooterCharacter->Tags.Add(PlayerPawnTag);

		// 将 PlayerController 的服务器权威队伍信息写入角色
		ShooterCharacter->SetTeam(TeamByte);

		// 只有本地控制器需要监听 HUD 委托，远端控制器不应更新本地 UI
		if (IsLocalPlayerController())
		{
			BindLocalCharacterDelegates(ShooterCharacter);
		}
	}
}

void AShooterPlayerController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);

	UE_LOG(LogNetworkDemo, Log, TEXT("[NetHUD] AcknowledgePossession Controller=%s Character=%s NetMode=%s"),
		*GetNameSafe(this), *GetNameSafe(P), GetNetModeLabel(this));

	// 客户端在确认占有后再绑定一次，覆盖 Pawn 已复制但 OnPossess 尚未完成本地 HUD 初始化的时序
	if (IsLocalPlayerController())
	{
		BindLocalCharacterDelegates(Cast<AShooterCharacter>(P));
	}
}

void AShooterPlayerController::OnUnPossess()
{
	// 先解除旧角色监听，避免换 Pawn 后旧角色的复制回调继续更新当前 HUD
	UnbindLocalCharacterDelegates();

	Super::OnUnPossess();
}

void AShooterPlayerController::BindLocalCharacterDelegates(AShooterCharacter* ShooterCharacter)
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	UE_LOG(LogNetworkDemo, Log, TEXT("[NetHUD] BindLocalCharacterDelegates Controller=%s Character=%s NetMode=%s"),
		*GetNameSafe(this), *GetNameSafe(ShooterCharacter), GetNetModeLabel(this));

	// 切换角色时先移除旧角色的两个 HUD 监听；同一角色重复进入时 AddUniqueDynamic 仍保持幂等
	if (BoundShooterCharacter.Get() != ShooterCharacter)
	{
		UnbindLocalCharacterDelegates();
	}

	if (!IsValid(ShooterCharacter))
	{
		return;
	}

	BoundShooterCharacter = ShooterCharacter;
	ShooterCharacter->OnBulletCountUpdated.AddUniqueDynamic(this, &AShooterPlayerController::OnBulletCountUpdated);
	ShooterCharacter->OnDamaged.AddUniqueDynamic(this, &AShooterPlayerController::OnPawnDamaged);

	// 绑定完成后立即用角色当前值刷新生命条，避免依赖一次已经错过的广播
	OnPawnDamaged(ShooterCharacter->GetHealthPercent());
}

void AShooterPlayerController::UnbindLocalCharacterDelegates()
{
	if (AShooterCharacter* ShooterCharacter = BoundShooterCharacter.Get())
	{
		ShooterCharacter->OnBulletCountUpdated.RemoveDynamic(this, &AShooterPlayerController::OnBulletCountUpdated);
		ShooterCharacter->OnDamaged.RemoveDynamic(this, &AShooterPlayerController::OnPawnDamaged);
	}

	BoundShooterCharacter.Reset();
}

void AShooterPlayerController::OnPawnDestroyed(AActor* DestroyedActor)
{
	// reset the bullet counter HUD
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_UpdateBulletCounter(0, 0);
	}

	if (TeamByte < TeamTags.Num())
	{
		// find the player start
		TArray<AActor*> ActorList;
		UGameplayStatics::GetAllActorsOfClassWithTag(GetWorld(), APlayerStart::StaticClass(), TeamTags[TeamByte], ActorList);

		if (ActorList.Num() > 0)
		{
			// select a random player start
			AActor* RandomPlayerStart = ActorList[FMath::RandRange(0, ActorList.Num() - 1)];

			// spawn a character at the player start
			const FTransform SpawnTransform = RandomPlayerStart->GetActorTransform();

			if (AShooterCharacter* RespawnedCharacter = GetWorld()->SpawnActor<AShooterCharacter>(CharacterClass, SpawnTransform))
			{
				// possess the character
				Possess(RespawnedCharacter);
			}
		}
	}
}

void AShooterPlayerController::OnBulletCountUpdated(int32 MagazineSize, int32 Bullets)
{
	// update the UI
	if (BulletCounterUI)
	{
		BulletCounterUI->BP_UpdateBulletCounter(MagazineSize, Bullets);
	}
}

void AShooterPlayerController::OnPawnDamaged(float LifePercent)
{
	// HUD 只存在于本地 PlayerController；Widget 尚未创建时安全跳过
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_Damaged(LifePercent);
	}
}

bool AShooterPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

void AShooterPlayerController::SetTeam(uint8 Team)
{
	TeamByte = Team;

	// if we already have a pawn, set its team
	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(GetPawn()))
	{
		ShooterCharacter->SetTeam(Team);
	}
}
