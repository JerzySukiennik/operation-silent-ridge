// Mission state machine, world population (plant, air defences, airbase), decoys, lasers, radio, debug keys, strike autotest hook.
#include "MissionState.h"
#include "MissionCommon.h"
#include "MissionLayout.h"
#include "MissionCombat.h"
#include "MissionFx.h"
#include "MissionTarget.h"
#include "MissionAutotest.h"
#include "RadarSite.h"
#include "EnemyAirbase.h"
#include "EnemyFighter.h"
#include "SamMissile.h"
#include "WeaponBomb.h"
#include "WeaponsComponent.h"
#include "OsrJetPawn.h"
#include "OsrPadInput.h"
#include "OsrWorldLayout.h"
#include "OsrTerrain.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

using namespace OsrMission;

namespace
{
	TUniquePtr<FOsrStrikeAutotest> GStrike;
	TMap<TWeakObjectPtr<AOsrJetPawn>, int32> GCrashSeen;
}

AOsrMissionState::AOsrMissionState()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

AOsrMissionState* AOsrMissionState::Get(const UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) { return nullptr; }
	for (TActorIterator<AOsrMissionState> It(W); It; ++It) { return *It; }
	return nullptr;
}

const TCHAR* AOsrMissionState::PhaseName(EOsrMissionPhase P)
{
	switch (P)
	{
	case EOsrMissionPhase::Deck: return TEXT("DECK");
	case EOsrMissionPhase::Launched: return TEXT("LAUNCHED");
	case EOsrMissionPhase::Ingress: return TEXT("INGRESS");
	case EOsrMissionPhase::Target: return TEXT("TARGET");
	case EOsrMissionPhase::Egress: return TEXT("EGRESS");
	case EOsrMissionPhase::Recovery: return TEXT("RECOVERY");
	case EOsrMissionPhase::Success: return TEXT("SUCCESS");
	default: return TEXT("FAIL");
	}
}

void AOsrMissionState::BeginPlay()
{
	Super::BeginPlay();
	const TCHAR* V[] = {TEXT("r_ingress"), TEXT("r_spiked"), TEXT("r_target_area"), TEXT("r_hit"), TEXT("r_miss"), TEXT("r_bandits"), TEXT("r_feet_wet"), TEXT("r_down"), TEXT("r_success")};
	for (const TCHAR* N : V)
	{
		if (USoundBase* S = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Mission/Audio/%s.%s"), N, N))) { Voices.Add(FName(N), S); }
	}
	FString Mode;
	if (FParse::Value(FCommandLine::Get(), TEXT("autotest="), Mode) && Mode.StartsWith(TEXT("strike")))
	{
		GStrike = MakeUnique<FOsrStrikeAutotest>();
		BriefingUntil = 0.0;
	}
	else
	{
		GStrike.Reset();
	}
	GCrashSeen.Reset();
	EnsureWorld();
}

void AOsrMissionState::EnsureWorld()
{
	if (bWorldSpawned) { return; }
	bWorldSpawned = true;
	UWorld* W = GetWorld();
	FOsrTerrain::Get();
	AOsrMissionFx::Get(this);
	FActorSpawnParameters Sp;
	Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	W->SpawnActor<AOsrMissionTarget>(AOsrMissionTarget::StaticClass(), FTransform::Identity, Sp);
	auto Site = [&](EOsrSiteType T, const FVector2D& At, const FVector2D& Face)
	{
		AOsrRadarSite* S = W->SpawnActor<AOsrRadarSite>(AOsrRadarSite::StaticClass(), FTransform::Identity, Sp);
		const FVector2D D = (Face - At).GetSafeNormal();
		S->Setup(T, At, FMath::Atan2(D.X, D.Y));
	};
	const FVector2D Vent = OsrLayout::Vent;
	Site(EOsrSiteType::SearchRadar, OsrLayout::SearchRadar, Vent);
	Site(EOsrSiteType::S400, OsrLayout::S400, Vent);
	Site(EOsrSiteType::Pantsir, OsrLayout::PantsirA, FVector2D(9000.0, -1600.0));
	Site(EOsrSiteType::Pantsir, OsrLayout::PantsirB, FVector2D(10000.0, -1600.0));
	Site(EOsrSiteType::Tor, OsrLayout::TorAirbase, Vent);
	W->SpawnActor<AOsrEnemyAirbase>(AOsrEnemyAirbase::StaticClass(), FTransform::Identity, Sp);
	Radio(TEXT("WATCHTOWER"), TEXT("Ridge 1, Watchtower. Mission is go: canyon ingress, vent shaft, laser-guided bombs. Read the briefing."), NAME_None, 7.0);
}

FString AOsrMissionState::BriefingText() const
{
	return TEXT("OPERATION SILENT RIDGE — STRIKE BRIEFING\n")
		TEXT("Target: an underground plant at the end of the canyon, 48 km inland. Its only weak point is the ventilation\n")
		TEXT("shaft (4 x 4 m) on the spur east of the tunnel portals. Destroy it with a laser-guided bomb.\n")
		TEXT("Threats: long-range S-400 fire unit + search radar on the ridges, Pantsir point defence at the plant (missiles + 30 mm),\n")
		TEXT("a Su-57 pair on the airbase to the north-east. GPS is jammed — laser guidance only.\n")
		TEXT("Plan: fly the fjord and the canyon LOW (terrain masks you from the radars), pop up at the valley, targeting pod on the\n")
		TEXT("shaft (TRACK), laser armed, release when IN RNG, keep lasing until impact, then egress low and fast to the sea and trap.\n")
		TEXT("Loadout: 2 x GBU-12 class LGB, GAU-22/A 25 mm pod (220 rds), 30 flares, 30 chaff. Laser code 1688.");
}

FString AOsrMissionState::ObjectiveText(const AActor* ForPawn) const
{
	const AOsrMissionTarget* T = AOsrMissionTarget::Find(this);
	FString Dist;
	if (const AOsrJetPawn* J = Cast<AOsrJetPawn>(ForPawn))
	{
		if (T)
		{
			const FVector D = T->VentCentre - J->Model.Position;
			const double Brg = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(D.X, -D.Z)) + 360.0, 360.0);
			Dist = FString::Printf(TEXT("   VENT %03.0f° %.1f km"), Brg, FVector2D(D.X, D.Z).Size() / 1000.0);
		}
	}
	switch (Phase)
	{
	case EOsrMissionPhase::Deck: return TEXT("DECK — man your jet and launch");
	case EOsrMissionPhase::Launched: return TEXT("TRANSIT — fly to the fjord mouth, stay low") + Dist;
	case EOsrMissionPhase::Ingress: return TEXT("INGRESS — follow the canyon below the ridgelines") + Dist;
	case EOsrMissionPhase::Target: return TEXT("TARGET — pod on the VENT SHAFT, laser, release in range") + Dist;
	case EOsrMissionPhase::Egress: return TEXT("EGRESS — target destroyed, get low and head for the sea");
	case EOsrMissionPhase::Recovery: return TEXT("RECOVERY — return to the carrier and trap");
	case EOsrMissionPhase::Success: return TEXT("MISSION SUCCESS");
	default: return TEXT("MISSION FAILED — ") + FailReason;
	}
}

void AOsrMissionState::SetPhase(EOsrMissionPhase P)
{
	if (P == Phase) { return; }
	UE_LOG(LogTemp, Display, TEXT("REPORT mission_phase %s -> %s t=%.1f"), PhaseName(Phase), PhaseName(P), MissionTime);
	Phase = P;
	PhaseTime = 0.0;
	if (P == EOsrMissionPhase::Launched && Stats.LaunchTime < 0.0) { Stats.LaunchTime = MissionTime; }
	if (P == EOsrMissionPhase::Ingress && !bIngressCall) { bIngressCall = true; Radio(TEXT("WATCHTOWER"), TEXT("Ridge 1, feet dry. Stay low in the canyon, the valley radars are up."), "r_ingress"); }
	if (P == EOsrMissionPhase::Target) { Radio(TEXT("WATCHTOWER"), TEXT("Ridge 1, target area. Pop up, lase the vent shaft and drop."), "r_target_area"); }
	if (P == EOsrMissionPhase::Recovery && !bFeetWetCall) { bFeetWetCall = true; Radio(TEXT("WATCHTOWER"), TEXT("Ridge 1, feet wet. Mother is west, bring it home."), "r_feet_wet"); }
	if (P == EOsrMissionPhase::Success)
	{
		Stats.EndTime = MissionTime;
		Radio(TEXT("WATCHTOWER"), TEXT("Ridge 1 is aboard. Target destroyed. Outstanding work."), "r_success", 8.0);
	}
}

void AOsrMissionState::ResetMission()
{
	Stats = FOsrMissionStats();
	bTargetDestroyed = false;
	bFightersScrambled = false;
	bIngressCall = bSpikeCall = bFeetWetCall = false;
	FailReason.Reset();
	MissionTime = 0.0;
	Phase = EOsrMissionPhase::Deck;
	PhaseTime = 0.0;
	if (AOsrMissionTarget* T = AOsrMissionTarget::Find(this)) { T->ResetTarget(); }
	for (AOsrRadarSite* S : AOsrRadarSite::All(GetWorld())) { S->ResetSite(); }
	if (AOsrEnemyAirbase* A = AOsrEnemyAirbase::Find(this)) { A->ResetBase(); }
	for (TActorIterator<AOsrMissile> It(GetWorld()); It; ++It) { It->Destroy(); }
	for (AOsrJetPawn* J : OsrCombat::Jets(GetWorld(), false)) { if (UOsrWeaponsComponent* W = UOsrWeaponsComponent::Of(J)) { W->Rearm(); } }
	Decoys.Reset();
	UE_LOG(LogTemp, Display, TEXT("REPORT mission_reset"));
}

// ---------------------------------------------------------------- carrier contract

void AOsrMissionState::NotifySpawnedOnDeck(APawn* Pawn)
{
	if (Phase != EOsrMissionPhase::Success) { SetPhase(EOsrMissionPhase::Deck); }
}

void AOsrMissionState::NotifyCatapultLaunched(APawn* Pawn)
{
	if (Phase == EOsrMissionPhase::Deck || Phase == EOsrMissionPhase::Fail) { SetPhase(EOsrMissionPhase::Launched); }
}

void AOsrMissionState::NotifyTrap(APawn* Pawn, const FString& Grade, int32 Wire)
{
	Stats.LsoGrade = Grade;
	Stats.Wire = Wire;
	UE_LOG(LogTemp, Display, TEXT("REPORT trap grade=%s wire=%d target_destroyed=%d"), *Grade, Wire, bTargetDestroyed ? 1 : 0);
	if (bTargetDestroyed) { SetPhase(EOsrMissionPhase::Success); }
	else
	{
		FailReason = TEXT("recovered with the target intact");
		SetPhase(EOsrMissionPhase::Fail);
	}
}

void AOsrMissionState::NotifyBolter(APawn* Pawn)
{
	++Stats.Bolters;
	Radio(TEXT("LSO"), TEXT("Bolter, bolter, bolter."), NAME_None, 3.0);
}

void AOsrMissionState::NotifyPlayerKilled(APawn* Pawn, const FString& Cause)
{
	if (AOsrJetPawn* J = Cast<AOsrJetPawn>(Pawn))
	{
		GCrashSeen.Add(J, J->CrashCount + 1);    // the crash that follows is this kill
	}
	++Stats.Deaths;
	FailReason = Cause;
	UE_LOG(LogTemp, Display, TEXT("REPORT player_killed cause=%s phase=%s"), *Cause, PhaseName(Phase));
	Radio(TEXT("WATCHTOWER"), FString::Printf(TEXT("Ridge 1 is down! (%s)"), *Cause), "r_down", 6.0);
	if (Phase != EOsrMissionPhase::Success) { SetPhase(EOsrMissionPhase::Fail); }
}

// ---------------------------------------------------------------- combat events

void AOsrMissionState::NotifyTargetDestroyed()
{
	if (bTargetDestroyed) { return; }
	bTargetDestroyed = true;
	++Stats.BombsOnTarget;
	UE_LOG(LogTemp, Display, TEXT("REPORT target_destroyed t=%.1f"), MissionTime);
	Radio(TEXT("WATCHTOWER"), TEXT("Good hit! Secondaries! The plant is done — get out of there!"), "r_hit", 7.0);
	SetPhase(EOsrMissionPhase::Egress);
}

void AOsrMissionState::NotifyBombImpact(bool bOnTarget, double MissM, const FString& What)
{
	if (MissM >= 0.0 && (Stats.BestMissM < 0.0 || MissM < Stats.BestMissM)) { Stats.BestMissM = MissM; }
	if (!bOnTarget && !bTargetDestroyed)
	{
		Radio(TEXT("WATCHTOWER"), FString::Printf(TEXT("Impact on the %s, %.0f m from the shaft. No effect — the plant is underground. Re-attack the vent."), *What.ToLower(), MissM), "r_miss", 6.0);
	}
}

void AOsrMissionState::NotifyDetected(const AActor* Target, const FString& By)
{
	++Stats.Detections;
	UE_LOG(LogTemp, Display, TEXT("REPORT detected by=%s t=%.1f"), *By, MissionTime);
	if (!bSpikeCall)
	{
		bSpikeCall = true;
		Radio(TEXT("WATCHTOWER"), FString::Printf(TEXT("Ridge 1, you're spiked — %s has you. Get low!"), *By), "r_spiked", 5.0);
	}
}

void AOsrMissionState::Radio(const FString& Who, const FString& Text, FName Voice, double Duration)
{
	FOsrRadioLine L;
	L.Who = Who; L.Text = Text; L.Time = MissionTime; L.Duration = Duration;
	RadioLines.Add(L);
	if (RadioLines.Num() > 6) { RadioLines.RemoveAt(0); }
	if (Voice != NAME_None) { PlayVoice(Voice); }
}

void AOsrMissionState::PlayVoice(FName Voice)
{
	if (USoundBase* S = Voices.FindRef(Voice)) { UGameplayStatics::PlaySound2D(this, S, 0.9f); }
}

// ---------------------------------------------------------------- lasers + decoys

void AOsrMissionState::SetLaser(AActor* Owner, int32 Code, bool bOn, const FVector& From, const FVector& Spot)
{
	FOsrLaserSpot* L = Lasers.FindByPredicate([Owner](const FOsrLaserSpot& S) { return S.Owner.Get() == Owner; });
	if (!L) { L = &Lasers.AddDefaulted_GetRef(); L->Owner = Owner; }
	L->Code = Code; L->bOn = bOn; L->From = From; L->Spot = Spot; L->LastUpdate = MissionTime;
}

bool AOsrMissionState::FindLaserSpot(int32 Code, const FVector& Seeker, const FVector& Axis, double HalfFov, FVector& OutSpot) const
{
	double Best = 1e12;
	bool bOk = false;
	for (const FOsrLaserSpot& L : Lasers)
	{
		if (!L.bOn || L.Code != Code || MissionTime - L.LastUpdate > 0.5 || !L.Owner.IsValid()) { continue; }
		const FVector To = L.Spot - Seeker;
		if (AngleBetween(Axis, To) > HalfFov) { continue; }
		if (!LineOfSight(Seeker, L.Spot + FVector(0, 1.0, 0), 0.3)) { continue; }
		const double D = To.SizeSquared();
		if (D < Best) { Best = D; OutSpot = L.Spot; bOk = true; }
	}
	return bOk;
}

int32 AOsrMissionState::DropDecoy(AActor* Owner, bool bFlare, const FVector& P, const FVector& V)
{
	FOsrDecoy D;
	D.Id = NextDecoy++;
	D.bFlare = bFlare;
	D.P = P; D.V = V;
	D.Life = bFlare ? 4.5 : 3.0;
	D.Owner = Owner;
	if (AOsrMissionFx* Fx = AOsrMissionFx::Get(this))
	{
		if (bFlare) { D.TrailId = Fx->BeginTrail(2.2, 0.5, 2.8, FLinearColor(0.85f, 0.85f, 0.85f), 0.55, false); }
		else
		{
			for (int32 K = 0; K < 3; ++K) { Fx->Puff(P, V * 0.3 + FVector(FMath::FRandRange(-4.0, 4.0), FMath::FRandRange(-4.0, 4.0), FMath::FRandRange(-4.0, 4.0)), 2.5, 1.5, 9.0, FLinearColor(0.82f, 0.84f, 0.88f), 0.35, false, 3.0); }
		}
	}
	Decoys.Add(D);
	return D.Id;
}

void AOsrMissionState::UpdateDecoys(double Dt)
{
	AOsrMissionFx* Fx = AOsrMissionFx::Get(this);
	for (FOsrDecoy& D : Decoys)
	{
		D.Age += Dt;
		D.V *= FMath::Exp(-(D.bFlare ? 1.2 : 4.0) * Dt);
		D.V.Y -= (D.bFlare ? 9.81 : 1.0) * Dt;
		D.P += D.V * Dt;
		if (Fx && D.bFlare)
		{
			const double Burn = 1.0 - FMath::Clamp((D.Age - D.Life + 0.8) / 0.8, 0.0, 1.0);
			Fx->Puff(D.P, FVector::ZeroVector, 0.07, 1.4, 2.4, FLinearColor(1.0f, 0.92f, 0.75f) * float(40.0 * Burn), 1.0, true, 0.0);
			Fx->TrailPoint(D.TrailId, D.P);
		}
	}
	for (FOsrDecoy& D : Decoys)
	{
		if (D.Age >= D.Life && Fx && D.bFlare) { Fx->EndTrail(D.TrailId); }
	}
	Decoys.RemoveAll([](const FOsrDecoy& D) { return D.Age >= D.Life; });
}

// ---------------------------------------------------------------- tick

APawn* AOsrMissionState::LocalPawn() const
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	return PC ? PC->GetPawn() : nullptr;
}

void AOsrMissionState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Dt = FMath::Min<double>(DeltaSeconds, 0.1);
	MissionTime += Dt;
	PhaseTime += Dt;
	// weapons on every jet (players join later in multiplayer)
	for (AOsrJetPawn* J : OsrCombat::Jets(GetWorld(), false))
	{
		if (!UOsrWeaponsComponent::Of(J))
		{
			UOsrWeaponsComponent* W = NewObject<UOsrWeaponsComponent>(J, TEXT("OsrWeapons"));
			W->RegisterComponent();
		}
		// crashes not caused by a weapon (terrain) also end the sortie (manned jets only)
		if (!J->IsPlayerControlled() && !J->bAutopilot) { GCrashSeen.Add(J, J->CrashCount); continue; }
		const int32* Seen = GCrashSeen.Find(J);
		if (!Seen) { GCrashSeen.Add(J, J->CrashCount); }
		else if (J->CrashCount > *Seen)
		{
			GCrashSeen.Add(J, J->CrashCount);
			NotifyPlayerKilled(J, TEXT("crashed into the terrain"));
			GCrashSeen.Add(J, J->CrashCount);
		}
	}
	if (!bCarrierBound) { BindCarrier(); }
	UpdateDecoys(Dt);
	UpdatePhase(Dt);
	if (bDebugKeys) { DebugKeys(); }
	if (GStrike.IsValid()) { GStrike->Step(this, Dt); }
	// briefing: dismissed by any weapon/fire input or after the timer
	if (!bBriefingDismissed && MissionTime > 3.0)
	{
		const FOsrPadState P = FOsrPadInput::Get();
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		const bool bKey = PC && (PC->IsInputKeyDown(EKeys::Enter) || PC->IsInputKeyDown(EKeys::SpaceBar) || PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom));
		if (bKey || P.bCross || MissionTime > BriefingUntil) { bBriefingDismissed = true; }
	}
}

AOsrJetPawn* AOsrMissionState::PlayerJet() const
{
	if (AOsrJetPawn* L = Cast<AOsrJetPawn>(LocalPawn())) { return L->bAlive ? L : nullptr; }
	for (AOsrJetPawn* J : OsrCombat::Jets(GetWorld(), true))
	{
		if ((J->IsPlayerControlled() || J->bAutopilot) && OsrCombat::JetAirborne(J)) { return J; }
	}
	return nullptr;
}

void AOsrMissionState::UpdatePhase(double Dt)
{
	TArray<AOsrJetPawn*> Alive = OsrCombat::Jets(GetWorld(), true);
	AOsrJetPawn* J = PlayerJet();
	const AOsrMissionTarget* T = AOsrMissionTarget::Find(this);
	switch (Phase)
	{
	case EOsrMissionPhase::Deck:
		if (J && OsrCombat::JetAirborne(J) && J->Model.Tas > 90.0 && J->Model.Position.Y > 15.0) { SetPhase(EOsrMissionPhase::Launched); }
		break;
	case EOsrMissionPhase::Launched:
		if (J && (J->Model.Position.X > OsrWorld::Mouth.X || Hdist(J->Model.Position, OsrWorld::Mouth) < 3000.0)) { SetPhase(EOsrMissionPhase::Ingress); }
		[[fallthrough]];
	case EOsrMissionPhase::Ingress:
		if (J && T && Hdist(J->Model.Position, T->VentCentre) < 9000.0) { SetPhase(EOsrMissionPhase::Target); }
		if (bTargetDestroyed) { SetPhase(EOsrMissionPhase::Egress); }
		break;
	case EOsrMissionPhase::Target:
		if (bTargetDestroyed) { SetPhase(EOsrMissionPhase::Egress); }
		break;
	case EOsrMissionPhase::Egress:
		if (J && (J->Model.Position.X < OsrWorld::Mouth.X - 2000.0 || Hdist(J->Model.Position, OsrWorld::Carrier) < 25000.0)) { SetPhase(EOsrMissionPhase::Recovery); }
		break;
	case EOsrMissionPhase::Recovery:
		break;
	case EOsrMissionPhase::Success:
		if (PhaseTime > 4.0)
		{
			const FOsrPadState P = FOsrPadInput::Get();
			const APlayerController* PC = GetWorld()->GetFirstPlayerController();
			if (P.bCross || (PC && PC->WasInputKeyJustPressed(EKeys::Enter)) || (PC && PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom))) { ResetMission(); }
		}
		break;
	case EOsrMissionPhase::Fail:
		// back in a jet (respawn on deck / in the air): the sortie continues, the target state persists
		if (PhaseTime > 3.0 && LocalPawn()) { SetPhase(bTargetDestroyed ? EOsrMissionPhase::Egress : EOsrMissionPhase::Deck); }
		break;
	}
	// fighters: scramble 12 s after the strike, or when a player is detected near the airbase
	if (!bFightersScrambled)
	{
		bool bGo = bTargetDestroyed && Stats.LaunchTime >= 0.0;
		static double DestroyedAt = -1.0;
		if (bTargetDestroyed && DestroyedAt < 0.0) { DestroyedAt = MissionTime; }
		if (!bTargetDestroyed) { DestroyedAt = -1.0; }
		bGo = bTargetDestroyed && MissionTime - DestroyedAt > 12.0;
		if (AOsrEnemyAirbase* A = AOsrEnemyAirbase::Find(this))
		{
			for (AOsrJetPawn* P : Alive)
			{
				if (Hdist(P->Model.Position, A->Centre) < 12000.0)
				{
					for (AOsrRadarSite* S : AOsrRadarSite::All(GetWorld())) { const AOsrRadarSite::FTrack* Tr = S->TrackOf(P); if (Tr && Tr->Q >= 0.5) { bGo = true; } }
				}
			}
			if (bGo && OsrCombat::WeaponsFree(GetWorld()))
			{
				bFightersScrambled = true;
				A->Scramble();
				Radio(TEXT("WATCHTOWER"), TEXT("Bandits! Two Su-57s scrambling from the north-east airfield!"), "r_bandits", 6.0);
			}
		}
	}
}

void AOsrMissionState::DebugKeys()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) { return; }
	AOsrJetPawn* J = Cast<AOsrJetPawn>(PC->GetPawn());
	if (PC->WasInputKeyJustPressed(EKeys::F5)) { NotifyCatapultLaunched(J); }
	if (PC->WasInputKeyJustPressed(EKeys::F6)) { NotifyTrap(J, TEXT("OK"), 3); }
	if (PC->WasInputKeyJustPressed(EKeys::F7)) { NotifyBolter(J); }
	if (PC->WasInputKeyJustPressed(EKeys::F8)) { ResetMission(); }
	if (J && PC->WasInputKeyJustPressed(EKeys::F9))
	{
		// canyon entrance: first dry path point, 90 m above the floor, heading up the canyon
		FString Txt;
		FFileHelper::LoadFileToString(Txt, *(FPaths::ProjectContentDir() / TEXT("Data/canyon_path.txt")));
		TArray<FString> Lines;
		Txt.ParseIntoArrayLines(Lines);
		TArray<FVector> Path;
		for (const FString& L : Lines) { TArray<FString> Pp; L.ParseIntoArrayWS(Pp); if (Pp.Num() == 3) { Path.Add(FVector(FCString::Atod(*Pp[0]), FCString::Atod(*Pp[1]), FCString::Atod(*Pp[2]))); } }
		int32 I = 0;
		for (int32 K = 0; K < Path.Num(); ++K) { if (FOsrTerrain::Get().HeightAt(Path[K].X, Path[K].Z) > 5.0) { I = FMath::Max(K - 2, 0); break; } }
		if (Path.Num() > I + 2)
		{
			FVector P = Path[I]; P.Y = Ground(P.X, P.Z) + 90.0;
			const FVector D = Flat(Path[I + 2] - Path[I]).GetSafeNormal();
			J->RespawnAt(P, AOsrJetPawn::LookingAt(D, FVector(0, 1, 0)), 215.0);
			if (Phase == EOsrMissionPhase::Deck || Phase == EOsrMissionPhase::Launched) { SetPhase(EOsrMissionPhase::Ingress); }
		}
	}
	if (J && PC->WasInputKeyJustPressed(EKeys::F10))
	{
		// 14 km west of the vent, 600 m above the valley, heading east
		const FVector V(OsrLayout::Vent.X, 0, OsrLayout::Vent.Y);
		FVector P = V + FVector(-14000.0, 0, 1500.0);
		P.Y = Ground(P.X, P.Z) + 600.0;
		J->RespawnAt(P, AOsrJetPawn::LookingAt(Flat(V - P).GetSafeNormal(), FVector(0, 1, 0)), 230.0);
	}
}
