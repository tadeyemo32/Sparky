#pragma once
#include "Interface.h"
#include "../Main/CBaseHandle.h"
#include "../Misc/IMatchGroupDescription.h"

MAKE_SIGNATURE(TFGameRules, XS("client.dll"), XS("48 8B 0D ? ? ? ? 4C 8B C3 48 8B D7 48 8B 01 FF 90 ? ? ? ? 84 C0"), 0x0);
MAKE_SIGNATURE(CTFGameRules_GetCurrentMatchGroup, XS("client.dll"), XS("40 53 48 83 EC ? 48 8B D9 E8 ? ? ? ? 48 8B C8 33 D2 E8 ? ? ? ? 84 C0 74 ? 8B 83 ? ? ? ? 48 83 C4"), 0x0);
MAKE_SIGNATURE(GetMatchGroupDescription, XS("client.dll"), XS("48 63 01 85 C0 78"), 0x0);

class CBaseEntity;
typedef CHandle<CBaseEntity> EHANDLE;

class CTeamplayRules
{
public:
};

class CTeamplayRoundBasedRules : public CTeamplayRules
{
public:
	NETVAR(m_iRoundState, int, XS("CTeamplayRoundBasedRulesProxy"), XS("m_iRoundState"));
	NETVAR(m_bInOvertime, bool, XS("CTeamplayRoundBasedRulesProxy"), XS("m_bInOvertime"));
	NETVAR(m_bInSetup, bool, XS("CTeamplayRoundBasedRulesProxy"), XS("m_bInSetup"));
	NETVAR(m_bSwitchedTeamsThisRound, bool, XS("CTeamplayRoundBasedRulesProxy"), XS("m_bSwitchedTeamsThisRound"));
	NETVAR(m_iWinningTeam, int, XS("CTeamplayRoundBasedRulesProxy"), XS("m_iWinningTeam"));
	NETVAR(m_iWinReason, int, XS("CTeamplayRoundBasedRulesProxy"), XS("m_iWinReason"));
	NETVAR(m_bInWaitingForPlayers, bool, XS("CTeamplayRoundBasedRulesProxy"), XS("m_bInWaitingForPlayers"));
	NETVAR(m_bAwaitingReadyRestart, bool, XS("CTeamplayRoundBasedRulesProxy"), XS("m_bAwaitingReadyRestart"));
	NETVAR(m_flRestartRoundTime, float, XS("CTeamplayRoundBasedRulesProxy"), XS("m_flRestartRoundTime"));
	NETVAR(m_flMapResetTime, float, XS("CTeamplayRoundBasedRulesProxy"), XS("m_flMapResetTime"));
	NETVAR(m_flNextRespawnWave, void*, XS("CTeamplayRoundBasedRulesProxy"), XS("m_flNextRespawnWave"));
	NETVAR(m_bTeamReady, void*, XS("CTeamplayRoundBasedRulesProxy"), XS("m_bTeamReady"));
	NETVAR(m_bStopWatch, bool, XS("CTeamplayRoundBasedRulesProxy"), XS("m_bStopWatch"));
	NETVAR(m_bMultipleTrains, bool, XS("CTeamplayRoundBasedRulesProxy"), XS("m_bMultipleTrains"));
	NETVAR(m_bPlayerReady, void*, XS("CTeamplayRoundBasedRulesProxy"), XS("m_bPlayerReady"));
	NETVAR(m_bCheatsEnabledDuringLevel, bool, XS("CTeamplayRoundBasedRulesProxy"), XS("m_bCheatsEnabledDuringLevel"));
	NETVAR(m_nRoundsPlayed, int, XS("CTeamplayRoundBasedRulesProxy"), XS("m_nRoundsPlayed"));
	NETVAR(m_flCountdownTime, float, XS("CTeamplayRoundBasedRulesProxy"), XS("m_flCountdownTime"));
	NETVAR(m_flStateTransitionTime, float, XS("CTeamplayRoundBasedRulesProxy"), XS("m_flStateTransitionTime"));
	NETVAR(m_TeamRespawnWaveTimes, void*, XS("CTeamplayRoundBasedRulesProxy"), XS("m_TeamRespawnWaveTimes"));
};

class CTFGameRules : public CTeamplayRoundBasedRules
{
public:
	NETVAR(m_nGameType, int, XS("CTFGameRulesProxy"), XS("m_nGameType"));
	NETVAR(m_nStopWatchState, int, XS("CTFGameRulesProxy"), XS("m_nStopWatchState"));
	NETVAR(m_pszTeamGoalStringRed, const char*, XS("CTFGameRulesProxy"), XS("m_pszTeamGoalStringRed"));
	NETVAR(m_pszTeamGoalStringBlue, const char*, XS("CTFGameRulesProxy"), XS("m_pszTeamGoalStringBlue"));
	NETVAR(m_flCapturePointEnableTime, float, XS("CTFGameRulesProxy"), XS("m_flCapturePointEnableTime"));
	NETVAR(m_iGlobalAttributeCacheVersion, int, XS("CTFGameRulesProxy"), XS("m_iGlobalAttributeCacheVersion"));
	NETVAR(m_nHudType, int, XS("CTFGameRulesProxy"), XS("m_nHudType"));
	NETVAR(m_bIsInTraining, bool, XS("CTFGameRulesProxy"), XS("m_bIsInTraining"));
	NETVAR(m_bAllowTrainingAchievements, bool, XS("CTFGameRulesProxy"), XS("m_bAllowTrainingAchievements"));
	NETVAR(m_bIsWaitingForTrainingContinue, bool, XS("CTFGameRulesProxy"), XS("m_bIsWaitingForTrainingContinue"));
	NETVAR(m_bIsTrainingHUDVisible, bool, XS("CTFGameRulesProxy"), XS("m_bIsTrainingHUDVisible"));
	NETVAR(m_bIsInItemTestingMode, bool, XS("CTFGameRulesProxy"), XS("m_bIsInItemTestingMode"));
	NETVAR(m_hBonusLogic, EHANDLE, XS("CTFGameRulesProxy"), XS("m_hBonusLogic"));
	NETVAR(m_bPlayingKoth, bool, XS("CTFGameRulesProxy"), XS("m_bPlayingKoth"));
	NETVAR(m_bPowerupMode, bool, XS("CTFGameRulesProxy"), XS("m_bPowerupMode"));
	NETVAR(m_bPlayingRobotDestructionMode, bool, XS("CTFGameRulesProxy"), XS("m_bPlayingRobotDestructionMode"));
	NETVAR(m_bPlayingMedieval, bool, XS("CTFGameRulesProxy"), XS("m_bPlayingMedieval"));
	NETVAR(m_bPlayingHybrid_CTF_CP, bool, XS("CTFGameRulesProxy"), XS("m_bPlayingHybrid_CTF_CP"));
	NETVAR(m_bPlayingSpecialDeliveryMode, bool, XS("CTFGameRulesProxy"), XS("m_bPlayingSpecialDeliveryMode"));
	NETVAR(m_bPlayingMannVsMachine, bool, XS("CTFGameRulesProxy"), XS("m_bPlayingMannVsMachine"));
	NETVAR(m_bMannVsMachineAlarmStatus, bool, XS("CTFGameRulesProxy"), XS("m_bMannVsMachineAlarmStatus"));
	NETVAR(m_bHaveMinPlayersToEnableReady, bool, XS("CTFGameRulesProxy"), XS("m_bHaveMinPlayersToEnableReady"));
	NETVAR(m_bBountyModeEnabled, bool, XS("CTFGameRulesProxy"), XS("m_bBountyModeEnabled"));
	NETVAR(m_bCompetitiveMode, bool, XS("CTFGameRulesProxy"), XS("m_bCompetitiveMode"));
	NETVAR(m_nMatchGroupType, int, XS("CTFGameRulesProxy"), XS("m_nMatchGroupType"));
	NETVAR(m_bMatchEnded, bool, XS("CTFGameRulesProxy"), XS("m_bMatchEnded"));
	NETVAR(m_bHelltowerPlayersInHell, bool, XS("CTFGameRulesProxy"), XS("m_bHelltowerPlayersInHell"));
	NETVAR(m_bIsUsingSpells, bool, XS("CTFGameRulesProxy"), XS("m_bIsUsingSpells"));
	NETVAR(m_bTruceActive, bool, XS("CTFGameRulesProxy"), XS("m_bTruceActive"));
	NETVAR(m_bTeamsSwitched, bool, XS("CTFGameRulesProxy"), XS("m_bTeamsSwitched"));
	NETVAR(m_hRedKothTimer, EHANDLE, XS("CTFGameRulesProxy"), XS("m_hRedKothTimer"));
	NETVAR(m_hBlueKothTimer, EHANDLE, XS("CTFGameRulesProxy"), XS("m_hBlueKothTimer"));
	NETVAR(m_nMapHolidayType, int, XS("CTFGameRulesProxy"), XS("m_nMapHolidayType"));
	NETVAR(m_pszCustomUpgradesFile, const char*, XS("CTFGameRulesProxy"), XS("m_pszCustomUpgradesFile"));
	NETVAR(m_bShowMatchSummary, bool, XS("CTFGameRulesProxy"), XS("m_bShowMatchSummary"));
	NETVAR(m_bMapHasMatchSummaryStage, bool, XS("CTFGameRulesProxy"), XS("m_bMapHasMatchSummaryStage"));
	NETVAR(m_bPlayersAreOnMatchSummaryStage, bool, XS("CTFGameRulesProxy"), XS("m_bPlayersAreOnMatchSummaryStage"));
	NETVAR(m_bStopWatchWinner, bool, XS("CTFGameRulesProxy"), XS("m_bStopWatchWinner"));
	NETVAR(m_ePlayerWantsRematch, void*, XS("CTFGameRulesProxy"), XS("m_ePlayerWantsRematch"));
	NETVAR(m_eRematchState, int, XS("CTFGameRulesProxy"), XS("m_eRematchState"));
	NETVAR(m_nNextMapVoteOptions, void*, XS("CTFGameRulesProxy"), XS("m_nNextMapVoteOptions"));
	NETVAR(m_nBossHealth, int, XS("CTFGameRulesProxy"), XS("m_nBossHealth"));
	NETVAR(m_nMaxBossHealth, int, XS("CTFGameRulesProxy"), XS("m_nMaxBossHealth"));
	NETVAR(m_fBossNormalizedTravelDistance, int, XS("CTFGameRulesProxy"), XS("m_fBossNormalizedTravelDistance"));
	NETVAR(m_itHandle, int, XS("CTFGameRulesProxy"), XS("m_itHandle"));
	NETVAR(m_hBirthdayPlayer, int, XS("CTFGameRulesProxy"), XS("m_hBirthdayPlayer"));
	NETVAR(m_nHalloweenEffect, int, XS("CTFGameRulesProxy"), XS("m_nHalloweenEffect"));
	NETVAR(m_fHalloweenEffectStartTime, float, XS("CTFGameRulesProxy"), XS("m_fHalloweenEffectStartTime"));
	NETVAR(m_fHalloweenEffectDuration, float, XS("CTFGameRulesProxy"), XS("m_fHalloweenEffectDuration"));
	NETVAR(m_halloweenScenario, int, XS("CTFGameRulesProxy"), XS("m_halloweenScenario"));
	NETVAR(m_nForceUpgrades, int, XS("CTFGameRulesProxy"), XS("m_nForceUpgrades"));
	NETVAR(m_nForceEscortPushLogic, int, XS("CTFGameRulesProxy"), XS("m_nForceEscortPushLogic"));
	NETVAR(m_bRopesHolidayLightsAllowed, bool, XS("CTFGameRulesProxy"), XS("m_bRopesHolidayLightsAllowed"));

	VIRTUAL(DefaultFOV, int, 30, this);
	VIRTUAL(GetViewVectors, CViewVectors*, 31, this);

	inline bool IsPlayerReady(int playerIndex)
	{
		if (playerIndex > 101)
			return false;
		
		static int nOffset = U::NetVars.GetNetVar(XS("CTeamplayRoundBasedRulesProxy"), XS("m_bPlayerReady"));
		bool* ReadyStatus = reinterpret_cast<bool*>(uintptr_t(this) + nOffset);
		if (!ReadyStatus)
			return false;

		return ReadyStatus[playerIndex];
	}

	SIGNATURE(GetCurrentMatchGroup, int, CTFGameRules, this);
	inline IMatchGroupDescription* GetMatchGroupDescription()
	{
		int iCurrentMatchGroup = GetCurrentMatchGroup();
		return S::GetMatchGroupDescription.Call<IMatchGroupDescription*>(std::ref(iCurrentMatchGroup));
	}
};

namespace I
{
	inline CTFGameRules* TFGameRules()
	{
		return *reinterpret_cast<CTFGameRules**>(U::Memory.RelToAbs(S::TFGameRules()));
	}
};