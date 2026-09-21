// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ShooterPlayerController.generated.h"

class UInputMappingContext;
class AShooterCharacter;
class UShooterBulletCounterUI;

/**
 *  Simple PlayerController for a first person shooter game
 *  Manages input mappings
 *  Respawns the player pawn when it's destroyed
 */
UCLASS(abstract, config="Game")
class NETWORKDEMO_API AShooterPlayerController : public APlayerController
{
	GENERATED_BODY()
	
protected:

	/** Input mapping contexts for this player */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/** Character class to respawn when the possessed pawn is destroyed */
	UPROPERTY(EditAnywhere, Category="Shooter|Respawn")
	TSubclassOf<AShooterCharacter> CharacterClass;

	/** Type of bullet counter UI widget to spawn */
	UPROPERTY(EditAnywhere, Category="Shooter|UI")
	TSubclassOf<UShooterBulletCounterUI> BulletCounterUIClass;

	/** Tag to grant the possessed pawn to flag it as the player */
	UPROPERTY(EditAnywhere, Category="Shooter|Player")
	FName PlayerPawnTag = FName("Player");

	/** Pointer to the bullet counter UI widget */
	UPROPERTY()
	TObjectPtr<UShooterBulletCounterUI> BulletCounterUI;

	/** Team ID for this player */
	uint8 TeamByte = 0;

	/** Tags that identify each team for PlayerStart selection upon respawning */
	UPROPERTY(EditAnywhere, Category="Shooter|Team")
	TArray<FName> TeamTags;

	/** 当前本地控制器已经绑定 HUD 委托的角色，换 Pawn 时用于解除旧监听 */
	TWeakObjectPtr<AShooterCharacter> BoundShooterCharacter;

protected:

	/** Gameplay Initialization */
	virtual void BeginPlay() override;

	/** Initialize input bindings */
	virtual void SetupInputComponent() override;

	/** 服务器占有 Pawn 时执行 Gameplay 初始化，并为本地玩家绑定 HUD 委托 */
	virtual void OnPossess(APawn* InPawn) override;

	/** 客户端确认占有 Pawn 后再次确保本地 HUD 委托已绑定 */
	virtual void AcknowledgePossession(APawn* P) override;

	/** 解除旧 Pawn 上的本地 HUD 委托 */
	virtual void OnUnPossess() override;

	/** 为本地控制器绑定角色 HUD 委托 */
	void BindLocalCharacterDelegates(AShooterCharacter* ShooterCharacter);

	/** 解除当前角色上的 HUD 委托 */
	void UnbindLocalCharacterDelegates();

	/** 当前占有的 Pawn 被销毁时，负责清空 HUD 并触发服务器重生流程 */
	UFUNCTION()
	void OnPawnDestroyed(AActor* DestroyedActor);

	/** 收到角色弹药数量变化通知后刷新本地 HUD */
	UFUNCTION()
	void OnBulletCountUpdated(int32 MagazineSize, int32 Bullets);

	/** 收到角色生命值变化通知后刷新本地 HUD */
	UFUNCTION()
	void OnPawnDamaged(float LifePercent);

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

public:

	/** Assigns a team ID to this player */
	void SetTeam(uint8 Team);
};
