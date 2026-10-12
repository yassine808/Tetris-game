#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/HUD.h"
#include "TetrisGame.generated.h"

class ATetrisBoard3D;
class USpringArmComponent;
class UCameraComponent;
class USoundBase;

namespace TetrisBoard
{
	constexpr int32 Width = 10;
	constexpr int32 Height = 20;
	constexpr int32 PieceCount = 7;
}

UCLASS()
class ATetrisGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATetrisGameMode();
	virtual void BeginPlay() override;

private:
	UPROPERTY()
	TObjectPtr<ATetrisBoard3D> Board;
};

UCLASS()
class ATetrisPawn : public APawn
{
	GENERATED_BODY()

public:
	ATetrisPawn();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	void NotifyShake(float Amplitude);

private:
	UPROPERTY()
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;

	float ShakeAmp = 0.f;
	float ShakeTime = 0.f;
	static constexpr float ShakeDuration = 0.30f;
};

UCLASS()
class ATetrisPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATetrisPlayerController();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupInputComponent() override;

	void SetBoard(ATetrisBoard3D* InBoard) { Board = InBoard; }

	int32 GetLockedCell(int32 X, int32 Y) const;
	bool IsCellInActivePiece(int32 X, int32 Y) const;
	int32 GetGhostCellY() const;
	int32 GetScore() const { return Score; }
	int32 GetLines() const { return Lines; }
	bool IsGameOver() const { return bGameOver; }
	int32 GetNextType() const { return NextType; }
	int32 GetActiveType() const { return CurType; }
	int32 GetActiveRot() const { return CurRot; }
	int32 GetActiveX() const { return CurX; }
	int32 GetActiveY() const { return CurY; }

	static void GetPieceCells(int32 Type, int32 Rot, TArray<FIntPoint>& OutCells);
	static FLinearColor GetPieceColor(int32 Type);
	static int32 GetPieceBox(int32 Type);

	void ResetGame();

private:
	void MoveLeft();
	void MoveRight();
	void RotatePiece();
	void SoftDrop();
	void HardDrop();
	void RestartGame();

	void SpawnPiece();
	bool IsValidPosition(int32 Type, int32 Rot, int32 X, int32 Y) const;
	bool TryTransformPiece(int32 NewRot, int32 NewX, int32 NewY);
	bool StepDown();
	void LockPiece();
	int32 ClearFullLines();
	void TriggerGameOver();
	void PlaySfx(USoundBase* Sound);

	UPROPERTY()
	TObjectPtr<ATetrisBoard3D> Board;

	UPROPERTY()
	TObjectPtr<USoundBase> SfxMove;

	UPROPERTY()
	TObjectPtr<USoundBase> SfxRotate;

	UPROPERTY()
	TObjectPtr<USoundBase> SfxSoftDrop;

	UPROPERTY()
	TObjectPtr<USoundBase> SfxHardDrop;

	UPROPERTY()
	TObjectPtr<USoundBase> SfxLock;

	UPROPERTY()
	TObjectPtr<USoundBase> SfxClear;

	UPROPERTY()
	TObjectPtr<USoundBase> SfxGameOver;

	bool bDropSfxPending = false;

	TArray<int32> Grid;
	int32 CurType = 0;
	int32 NextType = 0;
	int32 CurRot = 0;
	int32 CurX = 0;
	int32 CurY = 0;
	bool bGameOver = false;
	int32 Score = 0;
	int32 Lines = 0;
	float FallTimer = 0.f;
	float FallInterval = 0.8f;
};

UCLASS()
class ATetrisHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
