#include "MKITools.h"

#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunDismantle.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Equipment/FGChainsaw.h"
#include "Equipment/FGHoverPack.h"
#include "Equipment/FGJetPack.h"
#include "Equipment/FGObjectScanner.h"
#include "Equipment/FGParachute.h"
#include "Equipment/FGResourceMiner.h"
#include "FGCharacterMovementComponent.h"
#include "FGCharacterPlayer.h"
#include "FGInventoryComponent.h"
#include "FGJumpingStilts.h"
#include "MKIPlayerSubsystem.h"
#include "FGFoliagePickup.h"
#include "FGFoliageRemovalSubsystem.h"
#include "FGFoliageIdentifier.h"
#include "FGWorkBench.h"
#include "Hologram/FGConveyorLiftHologram.h"
#include "Hologram/FGFoundationHologram.h"
#include "FoliageType.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "MKIAreaDismantle.h"
#include "MKIAreaDismantleMode.h"
#include "MKIOrphanSupports.h"
#include "MKITerrainDismantle.h"
#include "MKITerrainDismantleMode.h"
#include "MKITerrainRules.h"
#include "MKIToolsConfiguration.h"
#include "Modules/ModuleManager.h"
#if !WITH_EDITOR
#include "Patching/NativeHookManager.h"
#endif

DEFINE_LOG_CATEGORY(LogMKITools);

/** Foliage instances by component (a type alias: a comma inside a hook macro's lambda would split the macro's arguments). */
using FFoliageAround = TMap<UHierarchicalInstancedStaticMeshComponent*, TArray<int32>>;

#if !WITH_EDITOR
/** The movement speed multiplier for a player's movement component; 1 for anything else (creatures, vehicles). */
static float MovementSpeedFor(const UCharacterMovementComponent* Movement)
{
	const AActor* Owner = Movement ? Movement->GetOwner() : nullptr;
	if (!Owner || !Owner->IsA<AFGCharacterPlayer>()) return 1.f;
	// Live (2026-10-22): crouching at speed starts the game's slide, which carried the multiplied speed and threw the
	// player half across the map. Sliding and the game's own movement modes (ladders, hypertubes, ziplines, parachute)
	// keep the game's speed.
	const UFGCharacterMovementComponent* Player = Cast<UFGCharacterMovementComponent>(Movement);
	if (Movement->MovementMode == MOVE_Custom || (Player && Player->IsSliding())) return 1.f;
	return UMKIToolsConfiguration::GetMovementSpeed(Owner);
}

/** Set while the module asks a movement component for the game's own top speed, so the speed hook leaves it unscaled. */
static thread_local bool GMKIGameSpeed = false;

/** During a flying physics step of a player's flight: the game's own vertical top speed for it; 0 otherwise. */
static thread_local float GMKIFlightVertical = 0.f;
#endif

/**
 * MKI Tools' runtime module. In game builds it adds the Terrain and Area modes to the vanilla dismantle gun, after Default
 * and Blueprint, and runs the dismantle state through FMKITerrainDismantle or FMKIAreaDismantle while one is selected. The terrain subsystem
 * and the remote call object are registered by UMKIToolsWorldSubsystem and UMKIToolsGameInstanceSubsystem.
 */
class FMKIToolsModule : public IModuleInterface
{
#if !WITH_EDITOR
	/** The game's own top speed of a movement component in its current mode (the speed bonus left out). */
	static float GameMaxSpeed(const UFGCharacterMovementComponent* Movement)
	{
		TGuardValue<bool> Guard(GMKIGameSpeed, true);
		return Movement->GetMaxSpeed();
	}

	/**
	 * Movement speed bonus, sideways only (the maintainer, 2026-10-23): flying and swimming move in all three directions
	 * under one top speed, so the bonus would also speed climbing and sinking. Their vertical speed is held to the game's
	 * own (its top speed, or for flying its own vertical flying speeds, whichever is higher).
	 */
	static void HoldVerticalSpeed(UFGCharacterMovementComponent* Movement)
	{
		if (!Movement || (Movement->MovementMode != MOVE_Flying && Movement->MovementMode != MOVE_Swimming)) return;
		if (GMKIFlightVertical <= 0.f && MovementSpeedFor(Movement) <= 1.f) return;
		float Vertical = GMKIFlightVertical;
		if (Vertical <= 0.f)
		{
			Vertical = GameMaxSpeed(Movement);
			if (Movement->MovementMode == MOVE_Flying) Vertical = FMath::Max3(Vertical, Movement->mVerticalFlyingSpeed, Movement->mVerticalFlyingSprintSpeed);
		}
		if (Vertical > 0.f) Movement->Velocity.Z = FMath::Clamp(Movement->Velocity.Z, -Vertical, Vertical);
	}

	/** Whether a player flies by a double jump (the game's own Flight Mode or MKI Tools' option), not the cheat menu's ghost fly. */
	static bool IsPlayerFlight(const UFGCharacterMovementComponent* Movement)
	{
		// Not const: the game declares CheatToggleGhostFlyIsActive without const (the Shipping build refuses a const player).
		AFGCharacterPlayer* Player = Movement ? Cast<AFGCharacterPlayer>(Movement->GetOwner()) : nullptr;
		return Player && Movement->MovementMode == MOVE_Flying && Movement->bCheatFlying && !Player->CheatToggleGhostFlyIsActive();
	}

	/**
	 * Flight at ground speed (the maintainer, 2026-10-29: "ground and flight speeds match each other"). Live: with the
	 * movement speed bonus, running and sprinting on the ground were much faster than flying and flying with Shift. For
	 * one flying physics step, the game's own flying top speeds are set to its ground ones (MaxFlySpeed to MaxWalkSpeed,
	 * mMaxFlySprintSpeed to mMaxSprintSpeed, through the Blade Runners as on the ground), times the bonus, then put back; the speed hook leaves them alone meanwhile,
	 * so the bonus counts once whichever the game reads. The vertical speed stays the game's own flying one.
	 */
	template <typename TScope>
	static void PhysPlayerFlight(TScope& Scope, UFGCharacterMovementComponent* Self, float DeltaTime, int32 Iterations)
	{
		const float Speed = MovementSpeedFor(Self);
		const float GameTop = GameMaxSpeed(Self);
		const float Fly = Self->MaxFlySpeed;
		const float FlySprint = Self->mMaxFlySprintSpeed;
		// Live (2026-10-29): the Blade Runners raise the ground sprint (the game's GetMaxSpeed passes it through their
		// GetAdjustedMaxSpeed, their sprint speed factor; walking is left as it is), so flying with Shift stayed slower than
		// sprinting with them on. The flying sprint takes the same adjusted ground sprint.
		AFGJumpingStilts* Stilts = Self->GetCachedJumpingStilts();
		const float GroundSprint = Stilts ? Stilts->GetAdjustedMaxSpeed(Self->mMaxSprintSpeed) : Self->mMaxSprintSpeed;
		TGuardValue<float> Vertical(GMKIFlightVertical, FMath::Max3(GameTop, Self->mVerticalFlyingSpeed, Self->mVerticalFlyingSprintSpeed));
		{
			TGuardValue<bool> Guard(GMKIGameSpeed, true);
			Self->MaxFlySpeed = Self->MaxWalkSpeed * Speed;
			Self->mMaxFlySprintSpeed = GroundSprint * Speed;
			Scope(Self, DeltaTime, Iterations);
			Self->MaxFlySpeed = Fly;
			Self->mMaxFlySprintSpeed = FlySprint;
		}
		HoldVerticalSpeed(Self);
		// What flight does in each game is logged every few seconds while flying, to check it against the ground speeds.
		static thread_local double LastLog = -100.0;
		const UWorld* World = Self->GetWorld();
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		if (Now - LastLog < 3.0 || Self->Velocity.Size2D() < 10.0) return;
		LastLog = Now;
		UE_LOG(LogMKITools, Log, TEXT("Flight: moving %.0f sideways, %.0f up (bonus x%.1f, sprinting %d); game fly %.0f, fly sprint %.0f, vertical %.0f/%.0f; ground walk %.0f, sprint %.0f (Blade Runners %d, adjusted sprint %.0f)"),
			Self->Velocity.Size2D(), Self->Velocity.Z, Speed, Self->GetIsSprinting() ? 1 : 0, Fly, FlySprint, Self->mVerticalFlyingSpeed, Self->mVerticalFlyingSprintSpeed, Self->MaxWalkSpeed, Self->mMaxSprintSpeed, Stilts ? 1 : 0, GroundSprint);
	}

	/**
	 * Flight mode (Mods > MKI Tools, the maintainer 2026-10-23): whether a double jump toggles flying is the game's
	 * mFlyingIsToggleable (its own Flight Mode setting); the option decides it instead. While the player is flying, a
	 * double jump can still end it, so a flight started before the option went off is never stuck.
	 */
	static void ApplyFlightMode(AFGCharacterPlayer* Player)
	{
		if (!Player) return;
		UFGCharacterMovementComponent* Movement = Cast<UFGCharacterMovementComponent>(Player->GetCharacterMovement());
		const bool bFlying = Movement && Movement->MovementMode == MOVE_Flying && Movement->bCheatFlying;
		Player->mFlyingIsToggleable = bFlying || (UMKIToolsConfiguration::GetFlightMode(Player) && CanStartFlight(Player, Movement));
	}

	/**
	 * Whether a double jump may start flying (#406). Live (2026-10-24, the maintainer): a double jump while gliding with the
	 * parachute started flight and left his hands gone and every equipment refusing to equip until a reload; flight entered
	 * from the parachute's custom movement mode skips the parachute's own exit. Flight starts only from plain movement: no
	 * custom movement mode (parachute, hover pack, zipline, ladder, hyper tube, cinematic), no thrusting jetpack, no vehicle.
	 */
	static bool CanStartFlight(const AFGCharacterPlayer* Player, UFGCharacterMovementComponent* Movement)
	{
		if (!Movement || Player->IsDrivingVehicle() || Movement->MovementMode == MOVE_Custom) return false;
		const AFGJetPack* JetPack = Movement->GetCachedJetPack();
		return !(JetPack && JetPack->IsThrusting());
	}

	/**
	 * Foundation area zoop: sets a foundation hologram's zoop from where the player aims, along (X) and across (Y), after
	 * the game's own zoop update. The aim is the view ray (the build gun's trace) where it crosses the hologram's level,
	 * or the hit itself when the ray does not come down to it; each direction is the nearest whole foundation, held to the
	 * game's limit and to the directions the game blocks (a side snapped to another foundation). Aiming within half a
	 * foundation of the line gives a line. The vertical zoop mode and a zoop the game made upwards stay the game's.
	 */
	static void ApplyAreaZoop(AFGFoundationHologram* Self, const FHitResult& HitResult)
	{
		if (Self->mBuildModeVerticalZoop && Self->IsCurrentBuildMode(Self->mBuildModeVerticalZoop)) return;
		if (Self->mDesiredZoop.Z != 0 || Self->mWidth <= 0.f || Self->mDepth <= 0.f) return;
		const FTransform Transform = Self->GetActorTransform();
		FVector Aim = HitResult.bBlockingHit ? HitResult.Location : HitResult.TraceEnd;
		const FVector Start = HitResult.TraceStart, End = HitResult.TraceEnd;
		const float Level = Transform.GetLocation().Z;
		if (!Start.IsZero() && FMath::Abs(End.Z - Start.Z) > KINDA_SMALL_NUMBER)
		{
			const float T = (Level - Start.Z) / (End.Z - Start.Z);
			if (T > 0.f) Aim = Start + (End - Start) * FMath::Min(T, 1.f);
		}
		const FVector Local = Transform.InverseTransformPosition(Aim);
		const int32 Max = Self->GetMaxZoopAmount();
		const uint8 Blocked = Self->mBlockedZoopDirectionMask;
		auto Count = [Max, Blocked](float Offset, float Size, EHologramZoopDirections Plus, EHologramZoopDirections Minus)
		{
			int32 N = FMath::Clamp(FMath::RoundToInt(Offset / Size), -Max, Max);
			if ((N > 0 && (Blocked & static_cast<uint8>(Plus))) || (N < 0 && (Blocked & static_cast<uint8>(Minus)))) N = 0;
			return N;
		};
		const FIntVector Wanted(
			Count(Local.X, Self->mWidth, EHologramZoopDirections::HZD_Forward, EHologramZoopDirections::HZD_Backward),
			Count(Local.Y, Self->mDepth, EHologramZoopDirections::HZD_Right, EHologramZoopDirections::HZD_Left),
			0);
		if (Wanted == Self->mDesiredZoop) return;
		Self->SetZoopAmount(Wanted);
		const bool bClamped = Self->mDesiredZoop != Wanted;
		if (bClamped)
		{
			// The game's own setter may keep the zoop to a line; the zoop is then set directly and its instances rebuilt.
			Self->mDesiredZoop = Wanted;
			Self->RegenerateZoopInstances();
		}
		static int32 Logged = 0;
		if (Logged < 3 && Wanted.X != 0 && Wanted.Y != 0)
		{
			++Logged;
			UE_LOG(LogMKITools, Log, TEXT("Area zoop: %s zoop %d x %d (setter clamped %d), %d instances, max %d, blocked 0x%02x, size %.0f x %.0f"),
				*Self->GetClass()->GetName(), Wanted.X, Wanted.Y, bClamped ? 1 : 0, Self->GetZoopInstanceTransforms().Num(), Max, Blocked, Self->mWidth, Self->mDepth);
		}
	}
#endif

public:
	virtual void StartupModule() override
	{
#if !WITH_EDITOR
		// SML hooks are game-only (as SML's own).
		SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGBuildGunStateDismantle, GetSupportedBuildModes_Implementation, [](const UFGBuildGunStateDismantle* Self, TArray<TSubclassOf<UFGBuildGunModeDescriptor>>& OutBuildModes)
		{
			OutBuildModes.AddUnique(UMKITerrainDismantleMode::StaticClass());
			// Area mode (#402) after Terrain.
			OutBuildModes.AddUnique(UMKIAreaDismantleMode::StaticClass());
		});
		SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunStateDismantle, TickState_Implementation, [](auto& Scope, UFGBuildGunStateDismantle* Self, float DeltaTime)
		{
			if (FMKIAreaDismantle::IsAreaMode(Self)) { FMKIAreaDismantle::Tick(Self, DeltaTime); Scope.Cancel(); return; }
			if (!FMKITerrainDismantle::IsTerrainMode(Self)) return;
			FMKITerrainDismantle::Tick(Self, DeltaTime);
			Scope.Cancel();
		});
		SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunStateDismantle, PrimaryFire_Implementation, [](auto& Scope, UFGBuildGunStateDismantle* Self)
		{
			if (FMKIAreaDismantle::IsAreaMode(Self)) { FMKIAreaDismantle::PrimaryFire(Self); Scope.Cancel(); return; }
			if (!FMKITerrainDismantle::IsTerrainMode(Self)) return;
			FMKITerrainDismantle::PrimaryFire(Self);
			Scope.Cancel();
		});
		// Dismantle unused supports (the maintainer, 2026-10-29): the server adds the supports a dismantled belt or pipe leaves
		// holding nothing to the game's own dismantle and to its refund preview, so the game checks and refunds them with the
		// rest. In the Default mode they are outlined with the aimed or selected belt or pipe (after the game's own outlines).
		SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunStateDismantle, Server_DismantleActors_Implementation, [](auto& Scope, UFGBuildGunStateDismantle* Self, const TArray<AActor*>& Actors, const TArray<FDismantleLightweightBundle>& Bundles)
		{
			TArray<AActor*> WithSupports;
			if (FMKIOrphanSupports::AddUnused(Self->GetBuildGun(), Actors, WithSupports)) Scope(Self, WithSupports, Bundles);
		});
		SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunStateDismantle, Server_PeekAtDismantleRefund_Implementation, [](auto& Scope, UFGBuildGunStateDismantle* Self, const TArray<AActor*>& Actors, bool bNoBuildCost)
		{
			TArray<AActor*> WithSupports;
			if (FMKIOrphanSupports::AddUnused(Self->GetBuildGun(), Actors, WithSupports)) Scope(Self, WithSupports, bNoBuildCost);
		});
		SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGBuildGunStateDismantle, TickState_Implementation, [](UFGBuildGunStateDismantle* Self, float DeltaTime)
		{
			FMKIOrphanSupports::Tick(Self);
		});
		// The HUD's hold progress bar reads the gun's delay percentage; during a Terrain hold it reads the Terrain timer
		// instead. Only the gun's UI getter: live (2026-10-19), also overriding the state's own getter, which the gun's
		// logic reads, made the gun beep in rapid succession through the hold.
		SUBSCRIBE_METHOD(AFGBuildGun::GetCurrentBuildGunDelayPercentage, [](auto& Scope, const AFGBuildGun* Self)
		{
			const UFGBuildGunState* State = Self ? Self->GetCurrentState() : nullptr;
			const float Progress = FMath::Max(FMKITerrainDismantle::HoldProgress(State), FMKIAreaDismantle::HoldProgress(State));
			if (Progress >= 0.f) Scope.Override(Progress);
		});
		// Hand crafting multiplier (Mods > MKI Tools, the work per tick, not the tick rate): each Craft Bench production step runs the configured number of times, so a
		// craft takes that much less time. Live (2026-10-20, the maintainer): one step at N times the length still made at
		// most one item per step, so short recipes did not get faster; N steps can each finish an item. 1 (the default)
		// leaves the game's own step untouched. The bench's own timings are logged once.
		SUBSCRIBE_METHOD(UFGWorkBench::Produce, [](auto& Scope, UFGWorkBench* Self, float DeltaTime)
		{
			const int32 Speed = FMath::RoundToInt(UMKIToolsConfiguration::GetHandCraftingSpeed(Self));
			if (Speed <= 1 || !Self) return;
			static bool bLogged = false;
			if (!bLogged)
			{
				bLogged = true;
				UE_LOG(LogMKITools, Log, TEXT("Crafting: step %.3f s, hold cycle %.3f s, recipe %.3f s, speed %.2f, cooldown delay %.3f s"), DeltaTime, Self->mHoldProduceTime, Self->mRecipeDuration, Self->mManufacturingSpeed, Self->mCooldownDelay);
			}
			for (int32 Step = 0; Step < Speed; ++Step) Scope(Self, DeltaTime);
		});
		// Hand mining multiplier (Mods > MKI Tools, the maintainer 2026-10-21; the ore per swing, not the swing rate): mining a resource node by hand gives ore the
		// configured times as fast. Live: a shorter mining cycle (mMiningCycleDuration, 1.2 s) changed nothing, the swing
		// animation paces the cycles, so each completed cycle gives its reward that many times instead. Server side.
		SUBSCRIBE_UOBJECT_METHOD(AFGResourceMiner, Server_GiveMiningRewards_Implementation, [](auto& Scope, AFGResourceMiner* Self)
		{
			const int32 Speed = Self ? FMath::RoundToInt(UMKIToolsConfiguration::GetHandMiningSpeed(Self)) : 1;
			if (Speed <= 1) return;
			// Live (2026-10-24, the maintainer, #408): a crash reading a null node in GetNumResourcesPerExtract while mining a
			// single rock, flying. A reward can end the mining (a deposit runs out, the player drifts out of reach), which
			// clears the miner's node; the game's own reward runs once, and each repeat only while the miner still mines.
			Scope(Self);
			for (int32 Reward = 1; Reward < Speed && Self->IsMining(); ++Reward) Scope(Self);
			static bool bLogged = false;
			if (!bLogged) { bLogged = true; UE_LOG(LogMKITools, Log, TEXT("Mining: a hand mining cycle gave its reward %d times"), Speed); }
		});
		// Health regeneration speed (Mods > MKI Tools, the maintainer 2026-10-21): the game's regeneration step runs as if
		// that many times as much time had passed; its wait after damage and its own health curve (where it stops) stay.
		SUBSCRIBE_METHOD(AFGCharacterPlayer::TickHealthGeneration, [](auto& Scope, AFGCharacterPlayer* Self, float Delta)
		{
			const float Speed = Self ? UMKIToolsConfiguration::GetHealthRegenerationSpeed(Self) : 1.f;
			if (Speed > 1.f) Scope(Self, Delta * Speed);
		});
		// Extra inventory slots (Mods > MKI Tools, the maintainer 2026-10-21: can only grow): the game's own slot count
		// plus the slots granted in this save (AMKIPlayerSubsystem, which grows each inventory to it).
		SUBSCRIBE_METHOD(AFGCharacterPlayer::GetTotalPlayerInventorySlots, [](auto& Scope, const AFGCharacterPlayer* Self)
		{
			const int32 Extra = Self ? AMKIPlayerSubsystem::GetExtraInventorySlots(Self) : 0;
			if (Extra > 0) Scope.Override(Scope(Self) + Extra);
		});
		// Never shrink a player's own inventory: before the save's extra slots are known (or with the mod's slots counted
		// out), the game could otherwise size it back down and lose what is in the extra slots. The game itself only ever
		// grows the player inventory.
		SUBSCRIBE_METHOD(UFGInventoryComponent::Resize, [](auto& Scope, UFGInventoryComponent* Self, int32 NewSize)
		{
			const AFGCharacterPlayer* Owner = Self ? Cast<AFGCharacterPlayer>(Self->GetOwner()) : nullptr;
			if (!Owner || Owner->GetInventory() != Self || NewSize >= Self->GetSizeLinear()) return;
			UE_LOG(LogMKITools, Log, TEXT("Inventory: kept %s at %d slots (asked for %d)"), *Owner->GetName(), Self->GetSizeLinear(), NewSize);
			Scope.Cancel();
		});
		// Movement speed (Mods > MKI Tools, the maintainer 2026-10-21: how fast the player runs or moves in the air, 1-10;
		// 2026-10-23: sideways only, in every mode, the parachute and hover pack too): the player's top speed in every
		// movement mode (walk, sprint, crouch, swim, falling, flying) and its acceleration (so the higher speed is reached as
		// quickly, and air control scales with it) are multiplied. Falling only ever caps the sideways speed; flying and
		// swimming keep the game's vertical speed (HoldVerticalSpeed). Players only; creatures keep the game's own.
		SUBSCRIBE_METHOD_VIRTUAL(UFGCharacterMovementComponent::GetMaxSpeed, GetMutableDefault<UFGCharacterMovementComponent>(), [](auto& Scope, const UFGCharacterMovementComponent* Self)
		{
			if (GMKIGameSpeed) return;
			const float Speed = MovementSpeedFor(Self);
			if (Speed > 1.f) { Scope.Override(Scope(Self) * Speed); return; }
			// A slide entered at a multiplied speed is held to the game's own top speed for it, not carried on.
			if (Self && Self->IsSliding() && UMKIToolsConfiguration::GetMovementSpeed(Self->GetOwner()) > 1.f)
			{
				const float GameMax = Scope(Self);
				UFGCharacterMovementComponent* Mutable = const_cast<UFGCharacterMovementComponent*>(Self);
				if (GameMax > 0.f && Mutable->Velocity.Size2D() > GameMax)
				{
					const double Z = Mutable->Velocity.Z;
					Mutable->Velocity = Mutable->Velocity.GetClampedToMaxSize2D(GameMax);
					Mutable->Velocity.Z = Z;
				}
				Scope.Override(GameMax);
			}
		});
		SUBSCRIBE_METHOD_VIRTUAL(UCharacterMovementComponent::GetMaxAcceleration, GetMutableDefault<UFGCharacterMovementComponent>(), [](auto& Scope, const UCharacterMovementComponent* Self)
		{
			const float Speed = MovementSpeedFor(Self);
			if (Speed > 1.f) Scope.Override(Scope(Self) * Speed);
		});
		// The velocity step of flying and swimming (and the flying physics, in case it sets its own vertical speed after
		// that step) leave the vertical speed at the game's own.
		SUBSCRIBE_METHOD_VIRTUAL_AFTER(UFGCharacterMovementComponent::CalcVelocity, GetMutableDefault<UFGCharacterMovementComponent>(), [](UFGCharacterMovementComponent* Self, float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
		{
			HoldVerticalSpeed(Self);
		});
		SUBSCRIBE_METHOD_VIRTUAL(UFGCharacterMovementComponent::PhysFlying, GetMutableDefault<UFGCharacterMovementComponent>(), [](auto& Scope, UFGCharacterMovementComponent* Self, float DeltaTime, int32 Iterations)
		{
			if (IsPlayerFlight(Self)) { PhysPlayerFlight(Scope, Self, DeltaTime, Iterations); return; }
			Scope(Self, DeltaTime, Iterations);
			HoldVerticalSpeed(Self);
		});
		// The hover pack and the parachute are the game's own movement modes with their own speeds, not the top speed above.
		// For one physics step, the hover pack's speed and acceleration, and the parachute's sideways (strafe) top speeds,
		// are multiplied, then put back. The hover pack's vertical speed is then held to its own top (its speed, sprinting
		// included) and the parachute's lateral-to-fall-speed conversion is divided by as much, so the faster glide does not
		// fall faster either. Ladders, hypertubes and ziplines keep the game's speed.
		SUBSCRIBE_METHOD_VIRTUAL(UFGCharacterMovementComponent::PhysCustom, GetMutableDefault<UFGCharacterMovementComponent>(), [](auto& Scope, UFGCharacterMovementComponent* Self, float DeltaTime, int32 Iterations)
		{
			if (!Self || Self->MovementMode != MOVE_Custom) return;
			const AActor* Owner = Self->GetOwner();
			const float Speed = Owner && Owner->IsA<AFGCharacterPlayer>() ? UMKIToolsConfiguration::GetMovementSpeed(Owner) : 1.f;
			if (Speed <= 1.f) return;
			if (Self->CustomMovementMode == static_cast<uint8>(ECustomMovementMode::CMM_Hover))
			{
				AFGHoverPack* Pack = Self->GetCachedHoverPack();
				if (!Pack) return;
				const float HoverSpeed = Pack->mHoverSpeed;
				const float HoverAcceleration = Pack->mHoverAccelerationSpeed;
				Pack->mHoverSpeed = HoverSpeed * Speed;
				Pack->mHoverAccelerationSpeed = HoverAcceleration * Speed;
				Scope(Self, DeltaTime, Iterations);
				Pack->mHoverSpeed = HoverSpeed;
				Pack->mHoverAccelerationSpeed = HoverAcceleration;
				const float Up = FMath::Max(HoverSpeed, HoverSpeed * Pack->mHoverSprintMultiplier);
				const float Down = FMath::Max(Up, Pack->mFallSpeedLimitWhenPowered);
				if (Up > 0.f) Self->Velocity.Z = FMath::Clamp(Self->Velocity.Z, -Down, Up);
				return;
			}
			if (Self->IsParachuting())
			{
				UFGParachuteSettings* Settings = Self->mParachuteSettings;
				if (!Settings) return;
				const float Strafe = Settings->mStrafeMaxSpeed;
				const float ShiftStrafe = Settings->mShiftStrafeMaxSpeed;
				const float Conversion = Settings->mLateralToTerminalConversionRatio;
				Settings->mStrafeMaxSpeed = Strafe * Speed;
				Settings->mShiftStrafeMaxSpeed = ShiftStrafe * Speed;
				Settings->mLateralToTerminalConversionRatio = Conversion / Speed;
				Scope(Self, DeltaTime, Iterations);
				Settings->mStrafeMaxSpeed = Strafe;
				Settings->mShiftStrafeMaxSpeed = ShiftStrafe;
				Settings->mLateralToTerminalConversionRatio = Conversion;
			}
		});
		// Flight mode (Mods > MKI Tools): set before each jump input, where the game looks for the double jump, and after
		// the game's own update from its Flight Mode setting.
		SUBSCRIBE_METHOD(AFGCharacterPlayer::Input_Jump, [](auto& Scope, AFGCharacterPlayer* Self, const FInputActionValue& Value)
		{
			ApplyFlightMode(Self);
		});
		SUBSCRIBE_METHOD_VIRTUAL(AFGCharacterPlayer::Jump, GetMutableDefault<AFGCharacterPlayer>(), [](auto& Scope, AFGCharacterPlayer* Self)
		{
			ApplyFlightMode(Self);
		});
		SUBSCRIBE_METHOD_AFTER(AFGCharacterPlayer::UpdateFlyingIsToggleable, [](AFGCharacterPlayer* Self)
		{
			ApplyFlightMode(Self);
		});
		// Short conveyor lifts (Mods > MKI Tools, default on; the maintainer 2026-10-22): the game builds a lift shorter than
		// its usual shortest (mMinimumHeight, 4 m) only when it snaps between two connections stacked above each other
		// (down to mMinimumHeightWithVerticalConnection, 1.5 m). With the option on, a lift's shortest everywhere is the
		// first whole step (mStepHeight, 1 m) at or above that lower height: 2 m, the vanilla short lift (live: the one the
		// maintainer stood by measured 200 cm; 1.5 m was not the game's short lift and put every height off the 1 m grid).
		// It still extends in the game's own steps. Set before each update of the lift's top, where the game clamps it.
		SUBSCRIBE_METHOD(AFGConveyorLiftHologram::UpdateTopTransform, [](auto& Scope, AFGConveyorLiftHologram* Self, const FHitResult& HitResult, FRotator Rotation)
		{
			if (!Self || !UMKIToolsConfiguration::GetShortConveyorLifts(Self) || Self->mMinimumHeightWithVerticalConnection <= 0.f) return;
			const float Step = Self->mStepHeight > 0.f ? Self->mStepHeight : 100.f;
			const float Shortest = FMath::CeilToFloat(Self->mMinimumHeightWithVerticalConnection / Step) * Step;
			if (Self->mMinimumHeight > Shortest)
			{
				UE_LOG(LogMKITools, Log, TEXT("Lifts: shortest %.0f cm -> %.0f cm (step %.0f cm)"), Self->mMinimumHeight, Shortest, Step);
				Self->mMinimumHeight = Shortest;
			}
		});
		// Foundation area zoop (always on; the maintainer 2026-10-29: a permanent tool, no option): foundations zoop as a rectangle,
		// up to the game's own limit (mMaxZoopAmount, 9 more each way: 10 by 10). The game's foundation holograms are set
		// to zoop only in a line (mOnlyAllowLineZoop); live (2026-10-29), clearing that flag alone still zooped a line, so
		// after the game's own zoop update the zoop is set from the aim along and across (ApplyAreaZoop). Holograms with
		// their own zoop (ramps) are not changed.
		SUBSCRIBE_METHOD_VIRTUAL(AFGFoundationHologram::SetZoopFromHitresult, GetMutableDefault<AFGFoundationHologram>(), [](auto& Scope, AFGFoundationHologram* Self, const FHitResult& HitResult)
		{
			if (Self) Self->mOnlyAllowLineZoop = false;
		});
		SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGFoundationHologram::SetZoopFromHitresult, GetMutableDefault<AFGFoundationHologram>(), [](AFGFoundationHologram* Self, const FHitResult& HitResult)
		{
			if (Self) ApplyAreaZoop(Self, HitResult);
		});
		// Chainsaw cuts boulders (Mods > MKI Tools, default on): the game asks whether a foliage type is removable before it
		// is sawn (and before anything else clears it); the game's rock foliage says yes. Bushes, trees and every other type
		// keep the game's own answer.
		SUBSCRIBE_METHOD(AFGFoliageRemovalSubsystem::IsRemovable, [](auto& Scope, const UFoliageType* Type)
		{
			if (FMKITerrainRules::IsRockFoliageType(Type) && UMKIToolsConfiguration::GetChainsawCutsBoulders(GEngine ? GEngine->GetCurrentPlayWorld() : nullptr)) Scope.Override(true);
		});
		// Live (2026-10-20): the chainsaw did not even aim at boulders. It picks what to saw by foliage identifier (the
		// Chainsawable tag), not by IsRemovable; rock foliage carries that tag too while the option is on (not the
		// Christmas tree subclass).
		SUBSCRIBE_METHOD(AFGFoliageRemovalSubsystem::HasIdentifier, [](auto& Scope, const UHierarchicalInstancedStaticMeshComponent* Component, TSubclassOf<UFGFoliageIdentifier> Identifier)
		{
			if (Identifier == UFGFoliageIdentifier_Chainsawable::StaticClass() && Component && FMKITerrainRules::IsRockFoliageMesh(Component->GetStaticMesh())
				&& UMKIToolsConfiguration::GetChainsawCutsBoulders(GEngine ? GEngine->GetCurrentPlayWorld() : nullptr)) Scope.Override(true);
		});
		// Chainsaw clearance radius (Mods > MKI Tools, the maintainer 2026-10-20): the area around a sawn tree the chainsaw
		// also clears, as a multiple (1x-10x) of the game's own (its class default, 5 m, logged once). Applied each time the
		// chainsaw is equipped.
		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGChainsaw, Equip, [](AFGChainsaw* Self, AFGCharacterPlayer* Character)
		{
			if (!Self) return;
			const AFGChainsaw* GameDefaults = Self->GetClass()->GetDefaultObject<AFGChainsaw>();
			const float GameRadiusCm = GameDefaults ? GameDefaults->mCollateralPickupRadius : Self->mCollateralPickupRadius;
			Self->mCollateralPickupRadius = GameRadiusCm * UMKIToolsConfiguration::GetChainsawClearanceMultiplier(Self);
			static bool bLogged = false;
			if (!bLogged) { bLogged = true; UE_LOG(LogMKITools, Log, TEXT("Chainsaw: the game's clearance radius is %.0f cm"), GameRadiusCm); }
			UE_LOG(LogMKITools, Log, TEXT("Chainsaw: clearance radius %.0f cm"), Self->mCollateralPickupRadius);
		});
		// Object scanner range bonus (Mods > MKI Tools, the maintainer 2026-10-28): the scanner's detection range as the game's
		// own (its class default, logged once) times 1 to 3, set before each tick so a changed option applies at once. The
		// scanner reads its signal (in range, the beep delay between its fastest and slowest, the screen) from the distance
		// over this range, so the signal scales with it: the slowest beep at the new range, the fastest only close by.
		SUBSCRIBE_UOBJECT_METHOD(AFGObjectScanner, Tick, [](auto& Scope, AFGObjectScanner* Self, float DeltaTime)
		{
			if (!Self) return;
			const AFGObjectScanner* GameDefaults = Self->GetClass()->GetDefaultObject<AFGObjectScanner>();
			const float GameRangeCm = GameDefaults ? GameDefaults->mDetectionRange : Self->mDetectionRange;
			const float RangeCm = GameRangeCm * UMKIToolsConfiguration::GetObjectScannerRange(Self);
			if (Self->mDetectionRange == RangeCm) return;
			static bool bLogged = false;
			if (!bLogged) { bLogged = true; UE_LOG(LogMKITools, Log, TEXT("Object scanner: the game's range is %.0f cm, beeps every %.2f-%.2f s"), GameRangeCm, Self->mBeepDelayMin, Self->mBeepDelayMax); }
			Self->mDetectionRange = RangeCm;
			UE_LOG(LogMKITools, Log, TEXT("Object scanner: range %.0f cm"), RangeCm);
		});
		// A boulder sawn down gives limestone by its size (the maintainer, 2026-10-20), once: only when the instance is
		// gone after the game's own removal ran. Server side, where the chainsaw removes foliage.
		SUBSCRIBE_METHOD(AFGChainsaw::RemoveFoliageInstance, [](auto& Scope, AFGChainsaw* Self, const FFoliageInstanceStableId& StableId, const FVector& EffectLocation, const FTransform& InstanceTransform)
		{
			AFGCharacterPlayer* Character = Self ? Self->GetInstigatorCharacter() : nullptr;
			AFGFoliageRemovalSubsystem* Removal = Self ? AFGFoliageRemovalSubsystem::Get(Self->GetWorld()) : nullptr;
			if (!Character || !Character->HasAuthority() || !Removal) return;
			const FVector Location = InstanceTransform.GetLocation();
			FFoliageAround Near;
			Removal->GetFoliageWithinRadius(Location, 50.f, Near);
			UHierarchicalInstancedStaticMeshComponent* Boulder = nullptr;
			for (const auto& Entry : Near)
			{
				if (FMKITerrainRules::FindRockFoliageInstance(Entry.Key, Location, 50.0) != INDEX_NONE) { Boulder = Entry.Key; break; }
			}
			if (!Boulder) return;
			const UStaticMesh* Mesh = Boulder->GetStaticMesh();
			Scope(Self, StableId, EffectLocation, InstanceTransform);
			if (FMKITerrainRules::FindRockFoliageInstance(Boulder, Location, 50.0) != INDEX_NONE) return;
			FMKITerrainRules::GrantLimestone(Character, FMKITerrainRules::LimestoneForBoulder(Mesh, InstanceTransform.GetScale3D()), Character->GetActorLocation());
		});
		// Live (2026-10-20, the maintainer): the chainsaw's area clearing left boulders and short grass standing; the game
		// clears only its pickupable foliage around a sawn object. After the game's own clearing (server side), the rest of
		// the foliage in the clearance radius that buildings may clear goes too, and with "Chainsaw cuts boulders" on, the
		// boulders, for their limestone together. Foliage types it keeps are logged once each; the landscape's own grass is
		// not foliage the game's removal reaches at all.
		SUBSCRIBE_METHOD_AFTER(AFGChainsaw::RemoveCollateralFoliage, [](AFGChainsaw* Self, AFGFoliageRemovalSubsystem* Removal, const FVector& Location)
		{
			AFGCharacterPlayer* Character = Self ? Self->GetInstigatorCharacter() : nullptr;
			if (!Character || !Character->HasAuthority() || !Removal) return;
			const bool bBoulders = UMKIToolsConfiguration::GetChainsawCutsBoulders(Self);
			FFoliageAround Near;
			Removal->GetFoliageWithinRadius(Location, Self->mCollateralPickupRadius, Near);
			// One declaration per line: a comma here would split the hook macro's arguments.
			int32 Limestone = 0;
			int32 Boulders = 0;
			int32 Cleared = 0;
			static TSet<FString> LoggedKept;
			for (auto& Entry : Near)
			{
				UHierarchicalInstancedStaticMeshComponent* Component = Entry.Key;
				if (!IsValid(Component) || Entry.Value.Num() == 0) continue;
				if (!FMKITerrainRules::IsRockFoliageMesh(Component->GetStaticMesh()))
				{
					const FString Mesh = GetNameSafe(Component->GetStaticMesh());
					// Live (2026-10-20): pickable grass the chainsaw cuts stayed inside the clearing; asked by each tag, in case
					// the game matches tags exactly rather than by their parents.
					if (AFGFoliageRemovalSubsystem::HasIdentifier(Component, UFGFoliageIdentifier_RemovableByBuildings::StaticClass())
						|| AFGFoliageRemovalSubsystem::HasIdentifier(Component, UFGFoliageIdentifier_Pickupable::StaticClass()))
					{
						if (Removal->RemoveFoliageInstances(Cast<APlayerController>(Character->GetController()), Component, Entry.Value)) Cleared += Entry.Value.Num();
						if (!LoggedKept.Contains(Mesh))
						{
							LoggedKept.Add(Mesh);
							UE_LOG(LogMKITools, Log, TEXT("Chainsaw: area clearing clears %s"), *Mesh);
						}
					}
					else if (!LoggedKept.Contains(Mesh))
					{
						LoggedKept.Add(Mesh);
						UE_LOG(LogMKITools, Log, TEXT("Chainsaw: area clearing keeps %s (not removable by buildings)"), *Mesh);
					}
					continue;
				}
				if (!bBoulders) continue;
				// Highest index first: removing an instance can move the last one into its slot.
				Entry.Value.Sort(TGreater<int32>());
				for (const int32 Instance : Entry.Value)
				{
					FTransform Transform;
					if (!Component->GetInstanceTransform(Instance, Transform, true)) continue;
					if (!Removal->RemoveFoliageInstance(Component, Instance)) continue;
					Limestone += FMKITerrainRules::LimestoneForBoulder(Component->GetStaticMesh(), Transform.GetScale3D());
					++Boulders;
				}
			}
			UE_LOG(LogMKITools, Log, TEXT("Chainsaw: area clearing (%.0f cm) also cleared %d foliage instances and %d boulders"), Self->mCollateralPickupRadius, Cleared, Boulders);
			if (Boulders > 0) FMKITerrainRules::GrantLimestone(Character, Limestone, Character->GetActorLocation());
		});
		// Live (2026-10-25, the maintainer, #410): a crash reading a null pointer in BroadcastPickup_Implementation, sawing a
		// rock 16 s after the area clearing removed 11 boulders. With boulders chainsawable and cleared around a sawn object,
		// the game's chainsaw can meet instances it never met before; a pickup entry without its mesh or component, or no
		// instigator, is dropped before the game's broadcast (which only plays the pickup: the items are already given).
		SUBSCRIBE_UOBJECT_METHOD(AFGChainsaw, BroadcastPickup_Implementation, [](auto& Scope, AFGChainsaw* Self, const TArray<FPickedUpInstance>& Pickups, AFGFoliagePickup* Instigator)
		{
			TArray<FPickedUpInstance> Valid;
			for (const FPickedUpInstance& Pickup : Pickups) if (IsValid(Pickup.InstanceMesh) && IsValid(Pickup.SourceHISM)) Valid.Add(Pickup);
			if (Valid.Num() == Pickups.Num() && IsValid(Instigator)) return;
			static bool bLogged = false;
			if (!bLogged) { bLogged = true; UE_LOG(LogMKITools, Warning, TEXT("Chainsaw: dropped %d of %d pickups without a mesh or component%s"), Pickups.Num() - Valid.Num(), Pickups.Num(), IsValid(Instigator) ? TEXT("") : TEXT(", and no instigator")); }
			if (Valid.Num() > 0 && IsValid(Instigator)) Scope(Self, Valid, Instigator);
			Scope.Cancel();
		});
		// Leaving Terrain or Area mode, or the dismantle state, drops the rock outline or the cube; vanilla handles the rest.
		SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGBuildGunStateDismantle, OnBuildGunModeChanged_Implementation, [](UFGBuildGunStateDismantle* Self, TSubclassOf<UFGBuildGunModeDescriptor> NewMode)
		{
			if (!FMKITerrainDismantle::IsTerrainMode(Self)) FMKITerrainDismantle::Clear(Self);
			if (!FMKIAreaDismantle::IsAreaMode(Self)) FMKIAreaDismantle::Clear(Self);
			FMKIOrphanSupports::Clear(Self);
		});
		SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGBuildGunStateDismantle, EndState_Implementation, [](UFGBuildGunStateDismantle* Self)
		{
			FMKITerrainDismantle::Clear(Self);
			FMKIAreaDismantle::Clear(Self);
			FMKIOrphanSupports::Clear(Self);
		});
#endif
	}
};

IMPLEMENT_MODULE(FMKIToolsModule, MKITools);
