#pragma once

#include "CoreMinimal.h"
#include "Math/Box2D.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/HUD.h"
#include "TetrisGame.generated.h"

class ATetrisBoard3D;
class USpringArmComponent;
class UCameraComponent;
class USoundBase;
class UAudioComponent;

namespace TetrisBoard
{
	constexpr int32 Width = 10;
	constexpr int32 Height = 20;
	constexpr int32 PieceCount = 7;
}

/** High level screen the player is currently on. */
UENUM(BlueprintType)
enum class ETetrisScreen : uint8
{
	Menu,
	Playing,
	GameOver
};

/** Which set of keys drives horizontal movement + rotation. */
UENUM(BlueprintType)
enum class EControlScheme : uint8
{
	Arrows UMETA(DisplayName = "Arrow Keys"),
	WASD   UMETA(DisplayName = "A / D / W")
};

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
	/** True only while a piece is actually live on the board (not menu / not paused). */
	bool IsPlayActive() const { return Screen == ETetrisScreen::Playing && !bPaused && !bGameOver; }
	int32 GetNextType() const { return NextType; }
	int32 GetActiveType() const { return CurType; }
	int32 GetActiveRot() const { return CurRot; }
	int32 GetActiveX() const { return CurX; }
	int32 GetActiveY() const { return CurY; }

	// --- screen / settings (read by the HUD) ---
	ETetrisScreen GetScreen() const { return Screen; }
	EControlScheme GetControlScheme() const { return ControlScheme; }
	bool IsMusicEnabled() const { return bMusicEnabled; }
	bool IsMusicPlaying() const;
	int32 GetMenuIndex() const { return MenuIndex; }
	float GetLastScorePct() const { return LastScorePct; }

	/** Menu hit-rects published by the HUD each frame so hover can drive selection. */
	void SetMenuRects(const TArray<FBox2D>& InRects) { MenuRects = InRects; }
	void PollMenuMouse();

	// HUD-facing menu accessors.
	int32 GetMenuCountForDraw() const { return ActiveMenuCount(); }
	FString GetMenuLabelForDraw(int32 Index) const { return MenuLabel(Index); }

	static void GetPieceCells(int32 Type, int32 Rot, TArray<FIntPoint>& OutCells);
	static FLinearColor GetPieceColor(int32 Type);
	static int32 GetPieceBox(int32 Type);

	void ResetGame();

private:
	// --- gameplay input ---
	void MoveLeft();
	void MoveRight();
	void RotatePiece();
	void SoftDrop();
	void HardDrop();
	void RestartGame();
	void TogglePause();

	// --- menu input ---
	void MenuUp();
	void MenuDown();
	void MenuConfirm();
	// Mouse click: like MenuConfirm, but only fires when the cursor is actually
	// over a menu item. Without this check any stray left-click on empty canvas
	// would activate whatever happened to be selected (e.g. QUIT).
	void MenuClickConfirm();
	void MenuBack();
	int32 ActiveMenuCount() const;
	FString MenuLabel(int32 Index) const;
	bool IsInMenu() const { return Screen == ETetrisScreen::Menu || Screen == ETetrisScreen::GameOver; }
	// True whenever a mouse-driven menu is on screen: the main menu, the
	// game-over menu, or the pause overlay (which is only up while Playing).
	bool HasClickableMenu() const { return ActiveMenuCount() > 0; }

	void BindGameplayInput();
	void ClearGameInput();

	void StartGame();
	void ReturnToMenu();
	void CycleControlScheme();
	void ToggleMusic();
	void QuitGame();

	void LoadSettings();
	void SaveSettings() const;
	void ApplyMusic();
	void ApplyScreenInputMode();

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

	UPROPERTY()
	TObjectPtr<USoundBase> MusicTrack;

	UPROPERTY()
	TObjectPtr<UAudioComponent> MusicComp;

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

	// --- screen state ---
	ETetrisScreen Screen = ETetrisScreen::Menu;
	EControlScheme ControlScheme = EControlScheme::Arrows;
	bool bMusicEnabled = true;
	int32 MenuIndex = 0;
	int32 MenuCount = 4;
	bool bPaused = false;
	float LastScorePct = 0.f;
	TArray<FBox2D> MenuRects;
};

UCLASS()
class ATetrisHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawGameOverlay(class ATetrisPlayerController* PC);
	void DrawMenu(class ATetrisPlayerController* PC);
	void DrawGameOver(class ATetrisPlayerController* PC);
	void DrawPaused(class ATetrisPlayerController* PC);
	// Shared selectable-item list. Draws Count rows starting at TopY and publishes
	// their screen-space hit boxes back to the controller, which is what lets the
	// mouse confirm work on the main menu, the game-over menu and the pause overlay.
	void DrawMenuList(class ATetrisPlayerController* PC, float TopY);
	void DrawBar(float X, float Y, float W, float H, float Pct, const FLinearColor& Fill);
};
