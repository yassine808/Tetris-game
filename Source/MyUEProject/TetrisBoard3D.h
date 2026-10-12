#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TetrisBoard3D.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UPointLightComponent;

UCLASS()
class ATetrisBoard3D : public AActor
{
	GENERATED_BODY()

public:
	ATetrisBoard3D();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	void NotifyLinesCleared(const TArray<int32>& Rows);

	static constexpr float CellSize = 100.f;
	static FVector CellLocation(int32 X, int32 Y);

private:
	class ATetrisPlayerController* AcquirePC();
	UStaticMeshComponent* MakeCube(const FVector& Location, const FVector& Scale, UMaterialInterface* Material);

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> PieceMI;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> GhostMI;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BackplateMI;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> LockedCubes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ActiveCubes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> GhostCubes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> NextCubes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> DebrisCubes;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> FlashCube;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> FlashLight;

	UPROPERTY(Transient)
	TArray<int32> LockedLastType;

	UPROPERTY(Transient)
	TArray<float> LockedPopTimer;

	UPROPERTY(Transient)
	TArray<FVector> DebrisVelocity;

	UPROPERTY(Transient)
	TArray<FRotator> DebrisSpin;

	UPROPERTY(Transient)
	TArray<float> DebrisLife;

	int32 ActivePieceType = -1;
	int32 NextTypeCache = -1;
	float FlashTime = 0.f;
	float FlashRowZ = 0.f;
	TWeakObjectPtr<class ATetrisPlayerController> CachedPC;
};
