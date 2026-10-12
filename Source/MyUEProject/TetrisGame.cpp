#include "TetrisGame.h"
#include "TetrisBoard3D.h"
#include "Engine/Engine.h"
#include "Engine/GameEngine.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "Components/AudioComponent.h"
#include "Misc/ConfigCacheIni.h"

// ---------------------------------------------------------------------------
// Piece definitions
// ---------------------------------------------------------------------------

namespace
{
	struct FPieceDef
	{
		int32 Box;
		FIntPoint Cells[4];
	};

	// Local coordinates, y grows downward. Rotations are clockwise in-box.
	const FPieceDef PieceDefs[TetrisBoard::PieceCount] =
	{
		{ 4, { FIntPoint(0,1), FIntPoint(1,1), FIntPoint(2,1), FIntPoint(3,1) } }, // I
		{ 4, { FIntPoint(1,1), FIntPoint(2,1), FIntPoint(1,2), FIntPoint(2,2) } }, // O
		{ 3, { FIntPoint(1,0), FIntPoint(0,1), FIntPoint(1,1), FIntPoint(2,1) } }, // T
		{ 3, { FIntPoint(1,0), FIntPoint(2,0), FIntPoint(0,1), FIntPoint(1,1) } }, // S
		{ 3, { FIntPoint(0,0), FIntPoint(1,0), FIntPoint(1,1), FIntPoint(2,1) } }, // Z
		{ 3, { FIntPoint(0,0), FIntPoint(0,1), FIntPoint(1,1), FIntPoint(2,1) } }, // J
		{ 3, { FIntPoint(2,0), FIntPoint(0,1), FIntPoint(1,1), FIntPoint(2,1) } }, // L
	};

	const FLinearColor PieceColors[TetrisBoard::PieceCount] =
	{
		FLinearColor(0.15f, 0.85f, 1.00f), // I cyan
		FLinearColor(1.00f, 0.88f, 0.10f), // O yellow
		FLinearColor(0.70f, 0.25f, 0.95f), // T purple
		FLinearColor(0.20f, 0.90f, 0.35f), // S green
		FLinearColor(1.00f, 0.30f, 0.30f), // Z red
		FLinearColor(0.25f, 0.45f, 1.00f), // J blue
		FLinearColor(1.00f, 0.60f, 0.10f), // L orange
	};

	// Settings live in the per-user config so a packaged build can write them.
	const TCHAR* SettingsSection = TEXT("TetrisSettings");
}

// ---------------------------------------------------------------------------
// ATetrisGameMode
// ---------------------------------------------------------------------------

ATetrisGameMode::ATetrisGameMode()
{
	PlayerControllerClass = ATetrisPlayerController::StaticClass();
	HUDClass = ATetrisHUD::StaticClass();
	DefaultPawnClass = ATetrisPawn::StaticClass();
}

void ATetrisGameMode::BeginPlay()
{
	Super::BeginPlay();
	Board = GetWorld()->SpawnActor<ATetrisBoard3D>(ATetrisBoard3D::StaticClass(), FTransform::Identity);
}

// ---------------------------------------------------------------------------
// ATetrisPawn
// ---------------------------------------------------------------------------

ATetrisPawn::ATetrisPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(Root);
	SpringArm->TargetArmLength = 3500.f;
	SpringArm->bDoCollisionTest = false;
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritYaw = false;
	SpringArm->bInheritRoll = false;
	SpringArm->SetRelativeRotation(FRotator(8.f, 72.f, 0.f));

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->FieldOfView = 65.f;
}

void ATetrisPawn::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);
}

void ATetrisPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (ShakeTime > 0.f)
	{
		ShakeTime -= DeltaSeconds;
		const float Frac = FMath::Clamp(ShakeTime / ShakeDuration, 0.f, 1.f);
		SpringArm->SetRelativeLocation(FMath::VRand() * ShakeAmp * Frac);
		if (ShakeTime <= 0.f)
		{
			SpringArm->SetRelativeLocation(FVector::ZeroVector);
			ShakeAmp = 0.f;
		}
	}
}

void ATetrisPawn::NotifyShake(float Amplitude)
{
	ShakeAmp = FMath::Max(ShakeAmp, Amplitude);
	ShakeTime = FMath::Max(ShakeTime, ShakeDuration);
}

// ---------------------------------------------------------------------------
// ATetrisPlayerController
// ---------------------------------------------------------------------------

ATetrisPlayerController::ATetrisPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	Grid.Init(0, TetrisBoard::Width * TetrisBoard::Height);
}

void ATetrisPlayerController::BeginPlay()
{
	Super::BeginPlay();

	SfxMove = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/S_Move.S_Move"));
	SfxRotate = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/S_Rotate.S_Rotate"));
	SfxSoftDrop = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/S_SoftDrop.S_SoftDrop"));
	SfxHardDrop = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/S_HardDrop.S_HardDrop"));
	SfxLock = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/S_Lock.S_Lock"));
	SfxClear = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/S_Clear.S_Clear"));
	SfxGameOver = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/S_GameOver.S_GameOver"));
	MusicTrack = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/M_Music/M_Music.M_Music"));

	LoadSettings();

	// The board stays live behind the menu, but no piece is in play yet.
	Grid.Init(0, TetrisBoard::Width * TetrisBoard::Height);
	bGameOver = false;
	Screen = ETetrisScreen::Menu;
	MenuIndex = 0;

	ApplyMusic();
	ApplyScreenInputMode();
}

void ATetrisPlayerController::ApplyMusic()
{
	if (bMusicEnabled && MusicTrack)
	{
		if (MusicComp && MusicComp->IsPlaying())
		{
			return;
		}
		if (USoundWave* SW = Cast<USoundWave>(MusicTrack))
		{
			SW->bLooping = true;
		}
		MusicComp = UGameplayStatics::SpawnSound2D(this, MusicTrack, 0.55f, 1.f, 0.f,
			nullptr, /*bPersistAcrossLevelTransition*/ true, /*bAutoDestroy*/ false);
	}
	else if (MusicComp)
	{
		MusicComp->Stop();
		MusicComp = nullptr;
	}
}

void ATetrisPlayerController::LoadSettings()
{
	FString Scheme;
	if (GConfig->GetString(SettingsSection, TEXT("ControlScheme"), Scheme, GGameUserSettingsIni))
	{
		ControlScheme = (Scheme == TEXT("WASD")) ? EControlScheme::WASD : EControlScheme::Arrows;
	}
	GConfig->GetBool(SettingsSection, TEXT("bMusicEnabled"), bMusicEnabled, GGameUserSettingsIni);
}

void ATetrisPlayerController::SaveSettings() const
{
	GConfig->SetString(SettingsSection, TEXT("ControlScheme"),
		(ControlScheme == EControlScheme::WASD) ? TEXT("WASD") : TEXT("Arrows"), GGameUserSettingsIni);
	GConfig->SetBool(SettingsSection, TEXT("bMusicEnabled"), bMusicEnabled, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

bool ATetrisPlayerController::IsMusicPlaying() const
{
	return MusicComp && MusicComp->IsPlaying();
}

void ATetrisPlayerController::PlaySfx(USoundBase* Sound)
{
	if (Sound)
	{
		UGameplayStatics::PlaySound2D(this, Sound);
	}
}

// --- screen transitions -----------------------------------------------------

void ATetrisPlayerController::ApplyScreenInputMode()
{
	const bool bMenuish = IsInMenu();

	if (UGameViewportClient* VP = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		VP->SetMouseCaptureMode(bMenuish ? EMouseCaptureMode::CaptureDuringMouseDown
			: EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);
	}

	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	if (bMenuish)
	{
		Mode.SetHideCursorDuringCapture(false);
	}
	SetInputMode(Mode);
	bShowMouseCursor = bMenuish;
}

void ATetrisPlayerController::StartGame()
{
	Screen = ETetrisScreen::Playing;
	bPaused = false;
	MenuIndex = 0;
	ResetGame();
	ApplyScreenInputMode();
}

void ATetrisPlayerController::ReturnToMenu()
{
	Grid.Init(0, TetrisBoard::Width * TetrisBoard::Height);
	bGameOver = false;
	bPaused = false;
	Screen = ETetrisScreen::Menu;
	MenuIndex = 0;
	ApplyScreenInputMode();
}

void ATetrisPlayerController::CycleControlScheme()
{
	ControlScheme = (ControlScheme == EControlScheme::Arrows)
		? EControlScheme::WASD : EControlScheme::Arrows;
	SaveSettings();
}

void ATetrisPlayerController::ToggleMusic()
{
	bMusicEnabled = !bMusicEnabled;
	SaveSettings();
	ApplyMusic();
}

void ATetrisPlayerController::QuitGame()
{
	UKismetSystemLibrary::QuitGame(GetWorld(), this, EQuitPreference::Quit, false);
}

// --- menu -------------------------------------------------------------------

int32 ATetrisPlayerController::ActiveMenuCount() const
{
	switch (Screen)
	{
	case ETetrisScreen::GameOver: return 2;
	case ETetrisScreen::Playing:  return bPaused ? 3 : 0;
	default:                      return 4;
	}
}

FString ATetrisPlayerController::MenuLabel(int32 Index) const
{
	if (Screen == ETetrisScreen::GameOver)
	{
		return Index == 0 ? TEXT("PLAY AGAIN") : TEXT("MAIN MENU");
	}
	if (bPaused)
	{
		switch (Index)
		{
		case 0:  return TEXT("RESUME");
		case 1:  return TEXT("RESTART");
		default: return TEXT("MAIN MENU");
		}
	}
	switch (Index)
	{
	case 0:  return TEXT("PLAY");
	case 1:  return ControlScheme == EControlScheme::Arrows
		         ? TEXT("CONTROLS: ARROW KEYS") : TEXT("CONTROLS: A / D / W");
	case 2:  return bMusicEnabled ? TEXT("MUSIC: ON") : TEXT("MUSIC: OFF");
	default: return TEXT("QUIT");
	}
}

void ATetrisPlayerController::MenuUp()
{
	const int32 N = ActiveMenuCount();
	if (N <= 0) return;
	MenuIndex = (MenuIndex + N - 1) % N;
	PlaySfx(SfxMove);
}

void ATetrisPlayerController::MenuDown()
{
	const int32 N = ActiveMenuCount();
	if (N <= 0) return;
	MenuIndex = (MenuIndex + 1) % N;
	PlaySfx(SfxMove);
}

void ATetrisPlayerController::MenuBack()
{
	if (Screen == ETetrisScreen::GameOver)
	{
		ReturnToMenu();
	}
	else if (bPaused)
	{
		bPaused = false;
		ApplyScreenInputMode();
	}
}

void ATetrisPlayerController::MenuConfirm()
{
	const int32 N = ActiveMenuCount();
	if (N <= 0) return;
	const int32 Idx = FMath::Clamp(MenuIndex, 0, N - 1);
	PlaySfx(SfxLock);

	if (bPaused)
	{
		switch (Idx)
		{
		case 0:  bPaused = false; ApplyScreenInputMode(); break;
		case 1:  bPaused = false; ResetGame(); ApplyScreenInputMode(); break;
		default: ReturnToMenu(); break;
		}
		return;
	}

	if (Screen == ETetrisScreen::GameOver)
	{
		if (Idx == 0) { StartGame(); }
		else          { ReturnToMenu(); }
		return;
	}

	switch (Idx)
	{
	case 0:  StartGame(); break;
	case 1:  CycleControlScheme(); break;
	case 2:  ToggleMusic(); break;
	default: QuitGame(); break;
	}
}

void ATetrisPlayerController::PollMenuMouse()
{
	if (!IsInMenu())
	{
		return;
	}
	float MX = 0.f, MY = 0.f;
	if (!GetMousePosition(MX, MY))
	{
		return;
	}
	for (int32 i = 0; i < MenuRects.Num(); i++)
	{
		if (MenuRects[i].IsInside(FVector2D(MX, MY)))
		{
			if (i != MenuIndex)
			{
				MenuIndex = i;
			}
			return;
		}
	}
}

// --- gameplay ---------------------------------------------------------------

void ATetrisPlayerController::TriggerGameOver()
{
	if (bGameOver)
	{
		return;
	}
	bGameOver = true;
	LastScorePct = 0.f;
	Screen = ETetrisScreen::GameOver;
	MenuIndex = 0;
	PlaySfx(SfxGameOver);
	if (ATetrisPawn* P = Cast<ATetrisPawn>(GetPawn()))
	{
		P->NotifyShake(30.f);
	}
	ApplyScreenInputMode();
}

void ATetrisPlayerController::GetPieceCells(int32 Type, int32 Rot, TArray<FIntPoint>& OutCells)
{
	OutCells.Reset(4);
	const FPieceDef& Def = PieceDefs[FMath::Clamp(Type, 0, TetrisBoard::PieceCount - 1)];
	Rot = ((Rot % 4) + 4) % 4;
	for (int32 i = 0; i < 4; i++)
	{
		int32 X = Def.Cells[i].X;
		int32 Y = Def.Cells[i].Y;
		for (int32 r = 0; r < Rot; r++)
		{
			const int32 NewX = (Def.Box - 1) - Y;
			const int32 NewY = X;
			X = NewX;
			Y = NewY;
		}
		OutCells.Add(FIntPoint(X, Y));
	}
}

FLinearColor ATetrisPlayerController::GetPieceColor(int32 Type)
{
	return PieceColors[FMath::Clamp(Type, 0, TetrisBoard::PieceCount - 1)];
}

int32 ATetrisPlayerController::GetPieceBox(int32 Type)
{
	return PieceDefs[FMath::Clamp(Type, 0, TetrisBoard::PieceCount - 1)].Box;
}

int32 ATetrisPlayerController::GetLockedCell(int32 X, int32 Y) const
{
	if (X < 0 || X >= TetrisBoard::Width || Y < 0 || Y >= TetrisBoard::Height)
	{
		return 0;
	}
	return Grid[Y * TetrisBoard::Width + X];
}

bool ATetrisPlayerController::IsCellInActivePiece(int32 X, int32 Y) const
{
	if (!IsPlayActive())
	{
		return false;
	}
	TArray<FIntPoint> Cells;
	GetPieceCells(CurType, CurRot, Cells);
	for (const FIntPoint& C : Cells)
	{
		if (CurX + C.X == X && CurY + C.Y == Y)
		{
			return true;
		}
	}
	return false;
}

int32 ATetrisPlayerController::GetGhostCellY() const
{
	if (!IsPlayActive())
	{
		return -1;
	}
	int32 Y = CurY;
	while (IsValidPosition(CurType, CurRot, CurX, Y + 1))
	{
		Y++;
	}
	return Y;
}

bool ATetrisPlayerController::IsValidPosition(int32 Type, int32 Rot, int32 X, int32 Y) const
{
	TArray<FIntPoint> Cells;
	GetPieceCells(Type, Rot, Cells);
	for (const FIntPoint& C : Cells)
	{
		const int32 GX = X + C.X;
		const int32 GY = Y + C.Y;
		if (GX < 0 || GX >= TetrisBoard::Width || GY >= TetrisBoard::Height)
		{
			return false;
		}
		if (GY >= 0 && Grid[GY * TetrisBoard::Width + GX] != 0)
		{
			return false;
		}
	}
	return true;
}

bool ATetrisPlayerController::TryTransformPiece(int32 NewRot, int32 NewX, int32 NewY)
{
	if (IsValidPosition(CurType, NewRot, NewX, NewY))
	{
		CurRot = NewRot;
		CurX = NewX;
		CurY = NewY;
		return true;
	}
	return false;
}

void ATetrisPlayerController::SpawnPiece()
{
	CurType = NextType;
	NextType = FMath::RandRange(0, TetrisBoard::PieceCount - 1);
	CurRot = 0;
	CurX = (TetrisBoard::Width - GetPieceBox(CurType)) / 2;
	CurY = 0;
	FallTimer = 0.f;

	if (!IsValidPosition(CurType, CurRot, CurX, CurY))
	{
		TriggerGameOver();
	}
}

void ATetrisPlayerController::LockPiece()
{
	TArray<FIntPoint> Cells;
	GetPieceCells(CurType, CurRot, Cells);
	for (const FIntPoint& C : Cells)
	{
		const int32 GX = CurX + C.X;
		const int32 GY = CurY + C.Y;
		if (GY < 0)
		{
			TriggerGameOver();
			return;
		}
		Grid[GY * TetrisBoard::Width + GX] = CurType + 1;
	}

	if (bDropSfxPending)
	{
		bDropSfxPending = false;
	}
	else
	{
		PlaySfx(SfxLock);
	}
	if (ATetrisPawn* P = Cast<ATetrisPawn>(GetPawn()))
	{
		P->NotifyShake(6.f);
	}

	const int32 Cleared = ClearFullLines();
	if (Cleared > 0)
	{
		Score += Cleared * 100;
		Lines += Cleared;
		PlaySfx(SfxClear);
		FallInterval = FMath::Max(0.15f, 0.8f - 0.035f * Lines);
	}
	SpawnPiece();
}

int32 ATetrisPlayerController::ClearFullLines()
{
	// pass 1: collect full rows (top to bottom)
	TArray<int32> FullRows;
	for (int32 Y = 0; Y < TetrisBoard::Height; Y++)
	{
		bool bFull = true;
		for (int32 X = 0; X < TetrisBoard::Width; X++)
		{
			if (Grid[Y * TetrisBoard::Width + X] == 0)
			{
				bFull = false;
				break;
			}
		}
		if (bFull)
		{
			FullRows.Add(Y);
		}
	}
	if (FullRows.Num() == 0)
	{
		return 0;
	}

	// let the 3D board spawn debris/flash before the data disappears
	if (Board)
	{
		Board->NotifyLinesCleared(FullRows);
	}

	// pass 2: remove rows (ascending indices stay valid: shifts only move
	// content downward into rows at or below the removed index)
	int32 Cleared = 0;
	for (int32 Y : FullRows)
	{
		for (int32 Row = Y; Row > 0; Row--)
		{
			for (int32 X = 0; X < TetrisBoard::Width; X++)
			{
				Grid[Row * TetrisBoard::Width + X] = Grid[(Row - 1) * TetrisBoard::Width + X];
			}
		}
		for (int32 X = 0; X < TetrisBoard::Width; X++)
		{
			Grid[X] = 0;
		}
		Cleared++;
	}
	return Cleared;
}

bool ATetrisPlayerController::StepDown()
{
	if (IsValidPosition(CurType, CurRot, CurX, CurY + 1))
	{
		CurY++;
		return true;
	}
	LockPiece();
	return false;
}

void ATetrisPlayerController::MoveLeft()
{
	if (!IsPlayActive()) return;
	if (TryTransformPiece(CurRot, CurX - 1, CurY))
	{
		PlaySfx(SfxMove);
	}
}

void ATetrisPlayerController::MoveRight()
{
	if (!IsPlayActive()) return;
	if (TryTransformPiece(CurRot, CurX + 1, CurY))
	{
		PlaySfx(SfxMove);
	}
}

void ATetrisPlayerController::RotatePiece()
{
	if (!IsPlayActive()) return;
	const int32 NewRot = (CurRot + 1) % 4;
	const int32 KicksX[] = { 0, -1, 1, -2, 2 };
	for (int32 KickX : KicksX)
	{
		if (TryTransformPiece(NewRot, CurX + KickX, CurY))
		{
			PlaySfx(SfxRotate);
			return;
		}
	}
}

void ATetrisPlayerController::SoftDrop()
{
	if (!IsPlayActive()) return;
	if (StepDown())
	{
		PlaySfx(SfxSoftDrop);
	}
	FallTimer = 0.f;
}

void ATetrisPlayerController::HardDrop()
{
	if (!IsPlayActive()) return;
	const int32 StartY = CurY;
	while (IsValidPosition(CurType, CurRot, CurX, CurY + 1))
	{
		CurY++;
	}
	Score += (CurY - StartY);
	bDropSfxPending = true;
	PlaySfx(SfxHardDrop);
	if (ATetrisPawn* P = Cast<ATetrisPawn>(GetPawn()))
	{
		P->NotifyShake(12.f);
	}
	LockPiece();
}

void ATetrisPlayerController::ResetGame()
{
	Grid.Init(0, TetrisBoard::Width * TetrisBoard::Height);
	Score = 0;
	Lines = 0;
	bGameOver = false;
	bPaused = false;
	FallTimer = 0.f;
	FallInterval = 0.8f;
	LastScorePct = 0.f;
	CurType = 0;
	NextType = FMath::RandRange(0, TetrisBoard::PieceCount - 1);
	CurRot = 0;
	CurX = 0;
	CurY = 0;
	SpawnPiece();
}

void ATetrisPlayerController::RestartGame()
{
	if (Screen == ETetrisScreen::GameOver || Screen == ETetrisScreen::Playing)
	{
		Screen = ETetrisScreen::Playing;
		ResetGame();
		ApplyScreenInputMode();
	}
}

void ATetrisPlayerController::TogglePause()
{
	if (Screen != ETetrisScreen::Playing)
	{
		return;
	}
	bPaused = !bPaused;
	MenuIndex = 0;
	ApplyScreenInputMode();
}

void ATetrisPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsInMenu())
	{
		PollMenuMouse();
		return;
	}

	if (!IsPlayActive())
	{
		return;
	}

	LastScorePct = FMath::Fmod((float)Lines, 10.f) / 10.f;

	FallTimer += DeltaSeconds;
	if (FallTimer >= FallInterval)
	{
		FallTimer = 0.f;
		StepDown();
	}
}

// --- input ------------------------------------------------------------------

void ATetrisPlayerController::ClearGameInput()
{
	if (InputComponent)
	{
		InputComponent->ClearActionBindings();
	}
}

void ATetrisPlayerController::BindGameplayInput()
{
	if (!InputComponent)
	{
		return;
	}

	// Menu / overlay navigation (always live; handlers check the screen).
	InputComponent->BindAction("MenuUp", IE_Pressed, this, &ATetrisPlayerController::MenuUp);
	InputComponent->BindAction("MenuDown", IE_Pressed, this, &ATetrisPlayerController::MenuDown);
	InputComponent->BindAction("MenuConfirm", IE_Pressed, this, &ATetrisPlayerController::MenuConfirm);
	InputComponent->BindAction("MenuBack", IE_Pressed, this, &ATetrisPlayerController::MenuBack);
	InputComponent->BindAction("Pause", IE_Pressed, this, &ATetrisPlayerController::TogglePause);
	InputComponent->BindAction("Restart", IE_Pressed, this, &ATetrisPlayerController::RestartGame);
	InputComponent->BindAction("MenuClick", IE_Pressed, this, &ATetrisPlayerController::MenuConfirm);

	// --- scheme A: arrow keys ---
	InputComponent->BindAction("ArrowsLeft", IE_Repeat, this, &ATetrisPlayerController::MoveLeft);
	InputComponent->BindAction("ArrowsRight", IE_Repeat, this, &ATetrisPlayerController::MoveRight);
	InputComponent->BindAction("ArrowsRotate", IE_Pressed, this, &ATetrisPlayerController::RotatePiece);
	InputComponent->BindAction("ArrowsSoftDrop", IE_Repeat, this, &ATetrisPlayerController::SoftDrop);

	// --- scheme B: A / D / W / S ---
	InputComponent->BindAction("WasdLeft", IE_Repeat, this, &ATetrisPlayerController::MoveLeft);
	InputComponent->BindAction("WasdRight", IE_Repeat, this, &ATetrisPlayerController::MoveRight);
	InputComponent->BindAction("WasdRotate", IE_Pressed, this, &ATetrisPlayerController::RotatePiece);
	InputComponent->BindAction("WasdSoftDrop", IE_Repeat, this, &ATetrisPlayerController::SoftDrop);

	// Shared across both schemes.
	InputComponent->BindAction("HardDrop", IE_Pressed, this, &ATetrisPlayerController::HardDrop);
}

void ATetrisPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	ClearGameInput();
	BindGameplayInput();
}

// ---------------------------------------------------------------------------
// ATetrisHUD
// ---------------------------------------------------------------------------

void ATetrisHUD::DrawBar(float X, float Y, float W, float H, float Pct, const FLinearColor& Fill)
{
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), X, Y, W, H);
	DrawRect(Fill, X, Y, W * FMath::Clamp(Pct, 0.f, 1.f), H);
}

void ATetrisHUD::DrawHUD()
{
	Super::DrawHUD();

	ATetrisPlayerController* PC = Cast<ATetrisPlayerController>(GetOwningPlayerController());
	if (!PC || !Canvas || Canvas->SizeX <= 0 || Canvas->SizeY <= 0)
	{
		return;
	}

	switch (PC->GetScreen())
	{
	case ETetrisScreen::Menu:
		DrawMenu(PC);
		break;
	case ETetrisScreen::GameOver:
		DrawMenu(PC);
		DrawGameOver(PC);
		break;
	case ETetrisScreen::Playing:
	default:
		DrawGameOverlay(PC);
		if (PC->GetMenuIndex() >= 0 && PC->GetMenuCountForDraw() > 0)
		{
			DrawPaused(PC);
		}
		break;
	}
}

void ATetrisHUD::DrawGameOverlay(ATetrisPlayerController* PC)
{
	const float ScreenW = (float)Canvas->SizeX;
	const float ScreenH = (float)Canvas->SizeY;
	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Font)
	{
		return;
	}

	DrawText(TEXT("TETRIS"), FLinearColor::White, 40.f, 28.f, Font, 1.6f);
	DrawText(FString::Printf(TEXT("SCORE %d   LINES %d"), PC->GetScore(), PC->GetLines()),
		FLinearColor(0.95f, 0.95f, 1.f, 1.f), 40.f, 86.f, Font, 1.1f);

	// speed / progress meter
	DrawText(TEXT("SPEED"), FLinearColor(0.7f, 0.75f, 0.9f, 1.f), 40.f, 132.f, Font, 0.8f);
	DrawBar(40.f, 160.f, 260.f, 14.f, PC->GetLastScorePct(), FLinearColor(0.15f, 0.85f, 1.f, 0.9f));

	// NEXT label anchored above the 3D next-piece preview
	const auto NextLabel = Project(FVector(760.f, 0.f, 1010.f));
	DrawText(TEXT("NEXT"), FLinearColor::White, NextLabel.X - 34.f, NextLabel.Y - 24.f, Font, 1.f);

	const bool bArrows = PC->GetControlScheme() == EControlScheme::Arrows;
	const FString Hint = bArrows
		? TEXT("Arrows move/rotate    Down soft drop    Space hard drop    P pause    R restart")
		: TEXT("A / D move    W rotate    S soft drop    Space hard drop    P pause    R restart");
	DrawText(Hint, FLinearColor(0.75f, 0.8f, 0.92f, 1.f), 40.f, ScreenH - 52.f, Font, 0.85f);

	// music pip, so the toggle state is visible while playing
	DrawText(PC->IsMusicEnabled() ? TEXT("MUSIC ON") : TEXT("MUSIC OFF"),
		FLinearColor(0.7f, 0.85f, 0.7f, 0.9f), ScreenW - 190.f, ScreenH - 52.f, Font, 0.8f);
}

void ATetrisHUD::DrawMenu(ATetrisPlayerController* PC)
{
	const float ScreenW = (float)Canvas->SizeX;
	const float ScreenH = (float)Canvas->SizeY;
	UFont* BigFont = GEngine ? GEngine->GetLargeFont() : nullptr;
	UFont* SmallFont = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!BigFont)
	{
		return;
	}

	// dim the board behind the menu
	DrawRect(FLinearColor(0.02f, 0.03f, 0.07f, 0.90f), 0.f, 0.f, ScreenW, ScreenH);

	const float CX = ScreenW * 0.5f;
	DrawText(TEXT("TETRIS"), FLinearColor::White, CX - 150.f, ScreenH * 0.10f, BigFont, 3.2f);
	DrawText(TEXT("3D BLOCK PUZZLE"), FLinearColor(0.5f, 0.85f, 1.f, 1.f),
		CX - 118.f, ScreenH * 0.10f + 92.f, BigFont, 0.9f);

	const int32 Count = PC->GetMenuCountForDraw();
	TArray<FBox2D> Rects;

	const float ItemH = 52.f;
	const float ItemW = 460.f;
	const float TopY = ScreenH * 0.42f;
	const int32 Sel = PC->GetMenuIndex();

	for (int32 i = 0; i < Count; i++)
	{
		const FString Label = PC->GetMenuLabelForDraw(i);
		const float Y = TopY + i * (ItemH + 12.f);
		const bool bSel = (i == Sel);

		const FLinearColor Bg = bSel
			? FLinearColor(0.12f, 0.55f, 0.85f, 0.95f)
			: FLinearColor(0.f, 0.f, 0.f, 0.35f);
		DrawRect(Bg, CX - ItemW * 0.5f, Y, ItemW, ItemH);
		if (bSel)
		{
			DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.95f), CX - ItemW * 0.5f, Y, 6.f, ItemH);
		}

		UFont* F = BigFont;
		const float Scale = bSel ? 1.15f : 1.0f;
		// rough centre: text scale 1.0 is ~14px tall for the large font
		DrawText(Label, bSel ? FLinearColor::White : FLinearColor(0.78f, 0.82f, 0.9f, 1.f),
			CX - ItemW * 0.5f + 34.f, Y + 12.f, F, Scale);

		Rects.Add(FBox2D(FVector2D(CX - ItemW * 0.5f, Y),
			FVector2D(CX + ItemW * 0.5f, Y + ItemH)));
	}

	PC->SetMenuRects(Rects);

	DrawText(TEXT("W/S or Up/Down to select    Enter / Click to confirm    Esc to go back"),
		FLinearColor(0.6f, 0.65f, 0.78f, 1.f), CX - 300.f, ScreenH - 64.f,
		SmallFont ? SmallFont : BigFont, 1.f);
}

void ATetrisHUD::DrawGameOver(ATetrisPlayerController* PC)
{
	const float ScreenW = (float)Canvas->SizeX;
	const float ScreenH = (float)Canvas->SizeY;
	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Font)
	{
		return;
	}
	DrawRect(FLinearColor(0.35f, 0.02f, 0.05f, 0.45f), 0.f, 0.f, ScreenW, ScreenH * 0.34f);
	DrawText(TEXT("GAME OVER"), FLinearColor(1.f, 0.3f, 0.3f, 1.f),
		ScreenW * 0.5f - 190.f, ScreenH * 0.08f, Font, 2.6f);
	DrawText(FString::Printf(TEXT("FINAL SCORE %d      LINES %d"), PC->GetScore(), PC->GetLines()),
		FLinearColor::White, ScreenW * 0.5f - 250.f, ScreenH * 0.08f + 84.f, Font, 1.1f);
}

void ATetrisHUD::DrawPaused(ATetrisPlayerController* PC)
{
	DrawText(TEXT("PAUSED"), FLinearColor::White, 40.f, 210.f,
		GEngine ? GEngine->GetLargeFont() : nullptr, 2.f);
}
