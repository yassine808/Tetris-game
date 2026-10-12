#include "TetrisGame.h"
#include "TetrisBoard3D.h"
#include "Engine/Engine.h"
#include "Engine/GameEngine.h"
#include "Engine/Canvas.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

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

	ResetGame();
}

void ATetrisPlayerController::PlaySfx(USoundBase* Sound)
{
	if (Sound)
	{
		UGameplayStatics::PlaySound2D(this, Sound);
	}
}

void ATetrisPlayerController::TriggerGameOver()
{
	if (bGameOver)
	{
		return;
	}
	bGameOver = true;
	PlaySfx(SfxGameOver);
	if (ATetrisPawn* P = Cast<ATetrisPawn>(GetPawn()))
	{
		P->NotifyShake(30.f);
	}
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
	if (bGameOver)
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
	if (bGameOver)
	{
		return;
	}
	if (TryTransformPiece(CurRot, CurX - 1, CurY))
	{
		PlaySfx(SfxMove);
	}
}

void ATetrisPlayerController::MoveRight()
{
	if (bGameOver)
	{
		return;
	}
	if (TryTransformPiece(CurRot, CurX + 1, CurY))
	{
		PlaySfx(SfxMove);
	}
}

void ATetrisPlayerController::RotatePiece()
{
	if (bGameOver)
	{
		return;
	}
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
	if (bGameOver)
	{
		return;
	}
	if (StepDown())
	{
		PlaySfx(SfxSoftDrop);
	}
	FallTimer = 0.f;
}

void ATetrisPlayerController::HardDrop()
{
	if (bGameOver)
	{
		return;
	}
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
	FallTimer = 0.f;
	CurType = 0;
	NextType = FMath::RandRange(0, TetrisBoard::PieceCount - 1);
	CurRot = 0;
	CurX = 0;
	CurY = 0;
	SpawnPiece();
}

void ATetrisPlayerController::RestartGame()
{
	ResetGame();
}

void ATetrisPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bGameOver)
	{
		return;
	}

	FallTimer += DeltaSeconds;
	if (FallTimer >= FallInterval)
	{
		FallTimer = 0.f;
		StepDown();
	}
}

void ATetrisPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!InputComponent)
	{
		return;
	}

	InputComponent->BindAction("MoveLeft", IE_Repeat, this, &ATetrisPlayerController::MoveLeft);
	InputComponent->BindAction("MoveRight", IE_Repeat, this, &ATetrisPlayerController::MoveRight);
	InputComponent->BindAction("Rotate", IE_Pressed, this, &ATetrisPlayerController::RotatePiece);
	InputComponent->BindAction("SoftDrop", IE_Repeat, this, &ATetrisPlayerController::SoftDrop);
	InputComponent->BindAction("HardDrop", IE_Pressed, this, &ATetrisPlayerController::HardDrop);
	InputComponent->BindAction("Restart", IE_Pressed, this, &ATetrisPlayerController::RestartGame);
}

// ---------------------------------------------------------------------------
// ATetrisHUD
// ---------------------------------------------------------------------------

void ATetrisHUD::DrawHUD()
{
	Super::DrawHUD();

	const ATetrisPlayerController* PC = Cast<ATetrisPlayerController>(GetOwningPlayerController());
	if (!PC || !Canvas || Canvas->SizeX <= 0 || Canvas->SizeY <= 0)
	{
		return;
	}
	const float ScreenW = (float)Canvas->SizeX;
	const float ScreenH = (float)Canvas->SizeY;

	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Font)
	{
		return;
	}
	const bool bGameOver = PC->IsGameOver();

	DrawText(TEXT("TETRIS"), FLinearColor::White, 40.f, 28.f, Font, 1.6f);
	DrawText(FString::Printf(TEXT("SCORE %d   LINES %d"), PC->GetScore(), PC->GetLines()),
		FLinearColor(0.95f, 0.95f, 1.f, 1.f), 40.f, 86.f, Font, 1.1f);

	// NEXT label anchored above the 3D next-piece preview
	const auto NextLabel = Project(FVector(760.f, 0.f, 1010.f));
	DrawText(TEXT("NEXT"), FLinearColor::White, NextLabel.X - 34.f, NextLabel.Y - 24.f, Font, 1.f);

	DrawText(TEXT("WASD move/rotate    Space hard drop    R restart"),
		FLinearColor(0.75f, 0.8f, 0.92f, 1.f), 40.f, ScreenH - 52.f, Font, 0.85f);

	if (bGameOver)
	{
		DrawText(TEXT("GAME OVER"), FLinearColor(1.f, 0.25f, 0.25f, 1.f),
			ScreenW * 0.5f - 190.f, ScreenH * 0.42f, Font, 2.6f);
		DrawText(TEXT("Press R to restart"), FLinearColor::White,
			ScreenW * 0.5f - 140.f, ScreenH * 0.42f + 90.f, Font, 1.2f);
	}
}
