#include "TetrisBoard3D.h"
#include "TetrisGame.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Components/PointLightComponent.h"

namespace
{
	const FVector LockedScale(0.92f, 0.60f, 0.92f);
	const FVector GhostScale(0.88f, 0.22f, 0.88f);
	const FVector NextScale(0.80f, 0.50f, 0.80f);
	const FVector DebrisScale(0.60f, 0.50f, 0.60f);
	const TCHAR* PieceSuffixes[TetrisBoard::PieceCount] = { TEXT("I"), TEXT("O"), TEXT("T"), TEXT("S"), TEXT("Z"), TEXT("J"), TEXT("L") };
}

ATetrisBoard3D::ATetrisBoard3D()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	UPointLightComponent* Key = CreateDefaultSubobject<UPointLightComponent>(TEXT("KeyLight"));
	Key->SetupAttachment(Root);
	Key->SetRelativeLocation(FVector(0.f, -800.f, 0.f));
	Key->Intensity = 1400.f;
	Key->AttenuationRadius = 4000.f;
	Key->SetLightColor(FLinearColor(1.f, 0.95f, 0.88f));

	UPointLightComponent* FillA = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLightA"));
	FillA->SetupAttachment(Root);
	FillA->SetRelativeLocation(FVector(-750.f, -500.f, 650.f));
	FillA->Intensity = 700.f;
	FillA->AttenuationRadius = 3500.f;
	FillA->SetLightColor(FLinearColor(0.75f, 0.85f, 1.f));

	UPointLightComponent* FillB = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLightB"));
	FillB->SetupAttachment(Root);
	FillB->SetRelativeLocation(FVector(750.f, -500.f, -650.f));
	FillB->Intensity = 700.f;
	FillB->AttenuationRadius = 3500.f;
	FillB->SetLightColor(FLinearColor(1.f, 0.8f, 0.9f));

	FlashLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FlashLight"));
	FlashLight->SetupAttachment(Root);
	FlashLight->SetRelativeLocation(FVector(0.f, -300.f, 0.f));
	FlashLight->Intensity = 0.f;
	FlashLight->AttenuationRadius = 3000.f;
	FlashLight->SetLightColor(FLinearColor(1.f, 1.f, 1.f));
}

FVector ATetrisBoard3D::CellLocation(int32 X, int32 Y)
{
	const float W = TetrisBoard::Width * CellSize;
	const float H = TetrisBoard::Height * CellSize;
	return FVector(-W * 0.5f + X * CellSize + CellSize * 0.5f, 0.f, H * 0.5f - Y * CellSize - CellSize * 0.5f);
}

void ATetrisBoard3D::BeginPlay()
{
	Super::BeginPlay();

	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	ensureMsgf(CubeMesh, TEXT("TetrisBoard3D: failed to load /Engine/BasicShapes/Cube.Cube"));

	PieceMI.SetNum(TetrisBoard::PieceCount);
	for (int32 i = 0; i < TetrisBoard::PieceCount; i++)
	{
		const FString Path = FString::Printf(TEXT("/Game/Materials/MI/MI_Block_%s.MI_Block_%s"), PieceSuffixes[i], PieceSuffixes[i]);
		PieceMI[i] = LoadObject<UMaterialInterface>(nullptr, *Path);
		ensureMsgf(PieceMI[i], TEXT("TetrisBoard3D: failed to load %s"), *Path);
	}
	GhostMI = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/MI/MI_Ghost.MI_Ghost"));
	BackplateMI = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/MI/MI_Backplate.MI_Backplate"));
	ensureMsgf(GhostMI && BackplateMI, TEXT("TetrisBoard3D: failed to load ghost/backplate material"));

	const int32 NumCells = TetrisBoard::Width * TetrisBoard::Height;

	// backplate + frame
	MakeCube(FVector(0.f, 90.f, 0.f), FVector(10.8f, 0.5f, 20.8f), BackplateMI);
	MakeCube(FVector(-540.f, 60.f, 0.f), FVector(0.8f, 0.6f, 21.6f), GhostMI);
	MakeCube(FVector(540.f, 60.f, 0.f), FVector(0.8f, 0.6f, 21.6f), GhostMI);
	MakeCube(FVector(0.f, 60.f, 1040.f), FVector(11.6f, 0.6f, 0.8f), GhostMI);
	MakeCube(FVector(0.f, 60.f, -1040.f), FVector(11.6f, 0.6f, 0.8f), GhostMI);

	// flash bar (shown over cleared rows)
	FlashCube = MakeCube(FVector(0.f, -60.f, 0.f), FVector(10.f, 0.2f, 1.f), GhostMI);
	FlashCube->SetVisibility(false);

	// locked cell pool
	LockedCubes.Reserve(NumCells);
	LockedLastType.SetNumZeroed(NumCells);
	LockedPopTimer.SetNumZeroed(NumCells);
	for (int32 i = 0; i < NumCells; i++)
	{
		const int32 X = i % TetrisBoard::Width;
		const int32 Y = i / TetrisBoard::Width;
		UStaticMeshComponent* C = MakeCube(CellLocation(X, Y), LockedScale, GhostMI);
		C->SetVisibility(false);
		LockedCubes.Add(C);
	}

	// active / ghost / next / debris pools
	for (int32 i = 0; i < 4; i++)
	{
		ActiveCubes.Add(MakeCube(FVector(0.f, 0.f, 0.f), LockedScale, GhostMI));
		GhostCubes.Add(MakeCube(FVector(0.f, 45.f, 0.f), GhostScale, GhostMI));
		NextCubes.Add(MakeCube(FVector(760.f, 0.f, 880.f), NextScale, GhostMI));
	}
	for (int32 i = 0; i < 64; i++)
	{
		UStaticMeshComponent* C = MakeCube(FVector(0.f, 0.f, -2000.f), DebrisScale, GhostMI);
		C->SetVisibility(false);
		DebrisCubes.Add(C);
		DebrisVelocity.Add(FVector::ZeroVector);
		DebrisSpin.Add(FRotator::ZeroRotator);
		DebrisLife.Add(0.f);
	}
}

UStaticMeshComponent* ATetrisBoard3D::MakeCube(const FVector& Location, const FVector& Scale, UMaterialInterface* Material)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(GetRootComponent());
	C->SetStaticMesh(CubeMesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetMobility(EComponentMobility::Movable);
	if (Material)
	{
		C->SetMaterial(0, Material);
	}
	C->RegisterComponent();
	C->SetWorldLocation(Location);
	C->SetWorldScale3D(Scale);
	return C;
}

ATetrisPlayerController* ATetrisBoard3D::AcquirePC()
{
	if (CachedPC.IsValid())
	{
		return CachedPC.Get();
	}
	if (ATetrisPlayerController* PC = Cast<ATetrisPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		CachedPC = PC;
		return PC;
	}
	return nullptr;
}

void ATetrisBoard3D::NotifyLinesCleared(const TArray<int32>& Rows)
{
	ATetrisPlayerController* PC = AcquirePC();
	if (!PC || Rows.Num() == 0)
	{
		return;
	}

	// debris burst from the cells that are about to disappear
	int32 Slot = 0;
	float ZSum = 0.f;
	for (int32 Row : Rows)
	{
		ZSum += CellLocation(0, Row).Z;
		for (int32 X = 0; X < TetrisBoard::Width; X++)
		{
			const int32 Type = PC->GetLockedCell(X, Row);
			UStaticMeshComponent* D = DebrisCubes[Slot % DebrisCubes.Num()];
			UMaterialInterface* Mat = (Type > 0 && Type <= PieceMI.Num()) ? PieceMI[Type - 1] : GhostMI;
			D->SetMaterial(0, Mat);
			D->SetWorldLocation(CellLocation(X, Row) + FVector(0.f, -10.f, 0.f));
			D->SetWorldScale3D(DebrisScale);
			D->SetVisibility(true);
			DebrisVelocity[Slot % DebrisCubes.Num()] = FVector(
				FMath::FRandRange(-450.f, 450.f),
				FMath::FRandRange(-900.f, -300.f),
				FMath::FRandRange(-450.f, 450.f));
			DebrisSpin[Slot % DebrisCubes.Num()] = FRotator(
				FMath::FRandRange(-720.f, 720.f),
				FMath::FRandRange(-720.f, 720.f),
				FMath::FRandRange(-720.f, 720.f));
			DebrisLife[Slot % DebrisCubes.Num()] = 1.0f;
			Slot++;
		}
	}

	// white flash + light spike over the cleared rows
	FlashRowZ = ZSum / Rows.Num();
	FlashCube->SetWorldLocation(FVector(0.f, -60.f, FlashRowZ));
	FlashCube->SetWorldScale3D(FVector(10.f, 0.2f, FMath::Clamp(Rows.Num(), 1, 4) * 1.f + 0.4f));
	FlashCube->SetVisibility(true);
	FlashTime = 0.35f;
	FlashLight->SetRelativeLocation(FVector(0.f, -300.f, FlashRowZ));
	FlashLight->Intensity = 45000.f;

	// camera shake scaled by rows cleared
	if (ATetrisPawn* P = Cast<ATetrisPawn>(PC->GetPawn()))
	{
		P->NotifyShake(14.f + 10.f * Rows.Num());
	}
}

void ATetrisBoard3D::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ATetrisPlayerController* PC = AcquirePC();
	if (!PC || !CubeMesh)
	{
		return;
	}

	// --- locked cells ---------------------------------------------------
	const int32 NumCells = TetrisBoard::Width * TetrisBoard::Height;
	for (int32 i = 0; i < NumCells; i++)
	{
		const int32 X = i % TetrisBoard::Width;
		const int32 Y = i / TetrisBoard::Width;
		const int32 Type = PC->GetLockedCell(X, Y);
		UStaticMeshComponent* C = LockedCubes[i];

		if (Type != LockedLastType[i])
		{
			LockedLastType[i] = Type;
			if (Type > 0)
			{
				C->SetMaterial(0, PieceMI[Type - 1]);
				LockedPopTimer[i] = 0.25f;
			}
			C->SetVisibility(Type > 0);
		}

		if (LockedPopTimer[i] > 0.f)
		{
			LockedPopTimer[i] -= DeltaSeconds;
			const float F = FMath::Clamp(LockedPopTimer[i] / 0.25f, 0.f, 1.f);
			C->SetWorldScale3D(LockedScale * (1.f + 0.30f * F));
			if (LockedPopTimer[i] <= 0.f)
			{
				C->SetWorldScale3D(LockedScale);
			}
		}
	}

	// --- active piece (lerped) + ghost ----------------------------------
	if (PC->IsPlayActive())
	{
		const int32 Type = PC->GetActiveType();
		TArray<FIntPoint> Cells;
		ATetrisPlayerController::GetPieceCells(Type, PC->GetActiveRot(), Cells);

		if (Type != ActivePieceType)
		{
			ActivePieceType = Type;
			for (int32 k = 0; k < 4; k++)
			{
				ActiveCubes[k]->SetMaterial(0, PieceMI[Type]);
			}
		}

		const int32 GhostY = PC->GetGhostCellY();
		for (int32 k = 0; k < 4 && k < Cells.Num(); k++)
		{
			const int32 GX = PC->GetActiveX() + Cells[k].X;
			const int32 GY = PC->GetActiveY() + Cells[k].Y;
			UStaticMeshComponent* A = ActiveCubes[k];
			const bool bVisible = GY >= 0 && GY < TetrisBoard::Height && GX >= 0 && GX < TetrisBoard::Width;
			A->SetVisibility(bVisible);
			if (bVisible)
			{
				const FVector Target = CellLocation(GX, GY);
				A->SetWorldLocation(FMath::VInterpTo(A->GetComponentLocation(), Target, DeltaSeconds, 22.f));
			}

			const int32 GGY = GhostY + Cells[k].Y;
			const bool bGhostVisible = GGY >= 0 && GGY < TetrisBoard::Height && GX >= 0 && GX < TetrisBoard::Width;
			GhostCubes[k]->SetVisibility(bGhostVisible);
			if (bGhostVisible)
			{
				GhostCubes[k]->SetWorldLocation(CellLocation(GX, GGY) + FVector(0.f, 45.f, 0.f));
			}
		}
	}
	else
	{
		for (int32 k = 0; k < 4; k++)
		{
			ActiveCubes[k]->SetVisibility(false);
			GhostCubes[k]->SetVisibility(false);
		}
	}

	// --- next preview ----------------------------------------------------
	const int32 Next = PC->GetNextType();
	if (Next != NextTypeCache)
	{
		NextTypeCache = Next;
		TArray<FIntPoint> Cells;
		ATetrisPlayerController::GetPieceCells(Next, 0, Cells);
		float CX = 0.f, CY = 0.f;
		for (const FIntPoint& C : Cells)
		{
			CX += C.X;
			CY += C.Y;
		}
		CX /= Cells.Num();
		CY /= Cells.Num();
		const FVector Base(760.f, 0.f, 880.f);
		for (int32 k = 0; k < 4 && k < Cells.Num(); k++)
		{
			NextCubes[k]->SetMaterial(0, PieceMI[Next]);
			NextCubes[k]->SetWorldLocation(Base + FVector(
				(Cells[k].X - CX) * CellSize * 0.8f, 0.f,
				-(Cells[k].Y - CY) * CellSize * 0.8f));
		}
	}

	// --- debris -----------------------------------------------------------
	for (int32 i = 0; i < DebrisCubes.Num(); i++)
	{
		if (DebrisLife[i] <= 0.f)
		{
			continue;
		}
		DebrisLife[i] -= DeltaSeconds;
		if (DebrisLife[i] <= 0.f)
		{
			DebrisCubes[i]->SetVisibility(false);
			continue;
		}
		DebrisVelocity[i].Z -= 1600.f * DeltaSeconds;
		UStaticMeshComponent* D = DebrisCubes[i];
		D->SetWorldLocation(D->GetComponentLocation() + DebrisVelocity[i] * DeltaSeconds);
		D->AddWorldRotation(DebrisSpin[i] * DeltaSeconds);
		const float Shrink = FMath::Clamp(DebrisLife[i] / 0.4f, 0.f, 1.f);
		D->SetWorldScale3D(DebrisScale * Shrink);
	}

	// --- clear flash ------------------------------------------------------
	if (FlashTime > 0.f)
	{
		FlashTime -= DeltaSeconds;
		const float Frac = FMath::Clamp(FlashTime / 0.35f, 0.f, 1.f);
		FlashLight->Intensity = 45000.f * Frac;
		if (FlashTime <= 0.f)
		{
			FlashCube->SetVisibility(false);
			FlashLight->Intensity = 0.f;
		}
	}
}
