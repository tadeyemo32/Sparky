#pragma once
#include "../SDK/Definitions/Types.h"
#include "../Utils/Macros/Macros.h"
#include <windows.h>
#include <unordered_map>
#include <map>
#include <typeinfo>

#define DEFAULT_BIND -1

template <class T>
class ConfigVar;

class BaseVar
{
public:
	std::vector<const char*> m_vNames;
	int m_iFlags = 0;
	union {
		int i = 0;
		float f;
	} m_unMin;
	union {
		int i = 0;
		float f;
	} m_unMax;
	union {
		int i = 0;
		float f;
	} m_unStep;
	std::vector<const char*> m_vValues = {};
	const char* m_sExtra = nullptr;

protected:
	std::string m_sName;
	const char* m_sSection;

public:
	constexpr const char* Name() const
	{
		return m_sName.c_str();
	}
	constexpr const char* Section() const
	{
		return m_sSection;
	}

public:
	size_t m_iType;

	template <class T>
	inline ConfigVar<T>* As()
	{
		if (typeid(T).hash_code() != m_iType)
			return nullptr;

		return reinterpret_cast<ConfigVar<T>*>(this);
	}
};

namespace G
{
	inline std::vector<BaseVar*> Vars = {};
};

template <class T>
class ConfigVar : public BaseVar
{
public:
	T Value;
	T Default;
	std::unordered_map<int, T> Map = {};

	ConfigVar(T tValue, std::vector<const char*> vNames, const char* sName, const char* sSection, int iFlags = 0, std::vector<const char*> vValues = {}, const char* sNone = nullptr)
	{
		Value = Default = Map[DEFAULT_BIND] = tValue;
		m_iType = typeid(T).hash_code();

		m_vNames = vNames;
		m_sName = std::string(sName).replace(strlen(sName) - 1, 1, "");
		m_sSection = sSection;

		m_iFlags = iFlags;
		m_vValues = vValues;
		m_sExtra = sNone;

		G::Vars.push_back(this);
	}
	ConfigVar(T tValue, std::vector<const char*> vNames, const char* sName, const char* sSection, int iFlags, int iMin, int iMax, int iStep = 1, const char* sFormat = "%i")
	{
		Value = Default = Map[DEFAULT_BIND] = tValue;
		m_iType = typeid(T).hash_code();

		m_vNames = vNames;
		m_sName = std::string(sName).replace(strlen(sName) - 1, 1, "");
		m_sSection = sSection;

		m_iFlags = iFlags;
		m_unMin.i = iMin;
		m_unMax.i = iMax;
		m_unStep.i = iStep;
		m_sExtra = sFormat;

		G::Vars.push_back(this);
	}
	ConfigVar(T tValue, std::vector<const char*> vNames, const char* sName, const char* sSection, int iFlags, float flMin, float flMax, float flStep = 1.f, const char* sFormat = "%g")
	{
		Value = Default = Map[DEFAULT_BIND] = tValue;
		m_iType = typeid(T).hash_code();

		m_vNames = vNames;
		m_sName = std::string(sName).replace(strlen(sName) - 1, 1, "");
		m_sSection = sSection;

		m_iFlags = iFlags;
		m_unMin.f = flMin;
		m_unMax.f = flMax;
		m_unStep.f = flStep;
		m_sExtra = sFormat;

		G::Vars.push_back(this);
	}

	inline T& operator[](int i)
	{
		return Map[i];
	}
	inline bool contains(int i) const
	{
		return Map.contains(i);
	}
};

#define NAMESPACE_BEGIN(name, ...) \
	namespace name { \
		constexpr inline const char* Section() { return !std::string(#__VA_ARGS__).empty() ? ""#__VA_ARGS__ : #name; }
#define NAMESPACE_END(name) \
	}

#define CVar(name, title, value, ...) \
	constexpr inline const char* name##_() { return __FUNCTION__; } \
	inline ConfigVar<decltype(value)> name = { value, { title }, name##_(), Section(), __VA_ARGS__ }
#define CVarValues(name, title, value, flags, none, ...) \
	constexpr inline const char* name##_() { return __FUNCTION__; } \
	inline ConfigVar<decltype(value)> name = { value, { title }, name##_(), Section(), flags, { __VA_ARGS__ }, none }
#define Enum(name, ...) \
	namespace name##Enum { enum name##Enum { __VA_ARGS__ }; }
#define CVarEnum(name, title, value, flags, none, values, ...) \
	CVarValues(name, title, value, flags, none, values); \
	Enum(name, __VA_ARGS__);

#define NONE 0
#define VISUAL (1 << 31)
#define NOSAVE (1 << 30)
#define NOBIND (1 << 29)
#define DEBUGVAR (1 << 28)

// flags to be automatically used in widgets. keep these as the same values as the flags in components, do not include visual flags
#define SLIDER_CLAMP (1 << 2)
#define SLIDER_MIN (1 << 3)
#define SLIDER_MAX (1 << 4)
#define SLIDER_PRECISION (1 << 5)
#define SLIDER_NOAUTOUPDATE (1 << 6)
#define DROPDOWN_MULTI (1 << 2)
#define DROPDOWN_MODIFIABLE (1 << 3)
#define DROPDOWN_NOSANITIZATION (1 << 4)
#define DROPDOWN_CUSTOM (1 << 2)
#define DROPDOWN_AUTOUPDATE (1 << 3)

NAMESPACE_BEGIN(Vars)
	NAMESPACE_BEGIN(Menu)
		CVar(CheatTitle, XS("Cheat title"), std::string(XS("Sparky")), VISUAL | DROPDOWN_AUTOUPDATE);
		CVar(CheatTag, XS("Cheat tag"), std::string(XS("[Sparky]")), VISUAL);
		CVar(PrimaryKey, XS("Primary key"), VK_INSERT, NOBIND);
		CVar(SecondaryKey, XS("Secondary key"), VK_F3, NOBIND);

		CVar(BindWindow, XS("Bind window"), true);
		CVar(BindWindowTitle, XS("Bind window title"), true);
		CVar(MenuShowsBinds, XS("Menu shows binds"), false, NOBIND);

		CVarEnum(Indicators, XS("Indicators"), 0b00000, VISUAL | DROPDOWN_MULTI, nullptr,
			VA_LIST(XS("Ticks"), XS("Crit hack"), XS("Spectators"), XS("Ping"), XS("Conditions"), XS("Seed prediction")),
			Ticks = 1 << 0, CritHack = 1 << 1, Spectators = 1 << 2, Ping = 1 << 3, Conditions = 1 << 4, SeedPrediction = 1 << 5);

		CVar(BindsDisplay, XS("Binds display"), DragBox_t(100, 100), VISUAL | NOBIND);
		CVar(TicksDisplay, XS("Ticks display"), DragBox_t(), VISUAL | NOBIND);
		CVar(CritsDisplay, XS("Crits display"), DragBox_t(), VISUAL | NOBIND);
		CVar(SpectatorsDisplay, XS("Spectators display"), DragBox_t(), VISUAL | NOBIND);
		CVar(PingDisplay, XS("Ping display"), DragBox_t(), VISUAL | NOBIND);
		CVar(ConditionsDisplay, XS("Conditions display"), DragBox_t(), VISUAL | NOBIND);
		CVar(SeedPredictionDisplay, XS("Seed prediction display"), DragBox_t(), VISUAL | NOBIND);

		CVar(Scale, XS("Scale"), 1.f, NOBIND | SLIDER_MIN | SLIDER_PRECISION | SLIDER_NOAUTOUPDATE, 0.75f, 2.f, 0.25f);
		CVar(CheapText, XS("Cheap text"), false);

		NAMESPACE_BEGIN(Theme)
			CVar(Accent, XS("Accent color"), Color_t(175, 150, 255, 255), VISUAL);
			CVar(Background, XS("Background color"), Color_t(0, 0, 0, 250), VISUAL);
			CVar(Active, XS("Active color"), Color_t(255, 255, 255, 255), VISUAL);
			CVar(Inactive, XS("Inactive color"), Color_t(150, 150, 150, 255), VISUAL);
		NAMESPACE_END(Theme)
	NAMESPACE_END(Menu)

	NAMESPACE_BEGIN(Colors)
		CVar(FOVCircle, XS("FOV circle color"), Color_t(255, 255, 255, 100), VISUAL);
		CVar(Local, XS("Local color"), Color_t(255, 255, 255, 0), VISUAL);

		CVar(IndicatorGood, XS("Indicator good"), Color_t(0, 255, 100, 255), NOSAVE | DEBUGVAR);
		CVar(IndicatorMid, XS("Indicator mid"), Color_t(255, 200, 0, 255), NOSAVE | DEBUGVAR);
		CVar(IndicatorBad, XS("Indicator bad"), Color_t(255, 0, 0, 255), NOSAVE | DEBUGVAR);
		CVar(IndicatorMisc, XS("Indicator misc"), Color_t(75, 175, 255, 255), NOSAVE | DEBUGVAR);
		CVar(IndicatorTextGood, XS("Indicator text good"), Color_t(150, 255, 150, 255), NOSAVE | DEBUGVAR);
		CVar(IndicatorTextMid, XS("Indicator text mid"), Color_t(255, 200, 0, 255), NOSAVE | DEBUGVAR);
		CVar(IndicatorTextBad, XS("Indicator text bad"), Color_t(255, 150, 150, 255), NOSAVE | DEBUGVAR);
		CVar(IndicatorTextMisc, XS("Indicator text misc"), Color_t(100, 255, 255, 255), NOSAVE | DEBUGVAR);

		CVar(WorldModulation, VA_LIST(XS("World modulation"), XS("World modulation color")), Color_t(255, 255, 255, 255), VISUAL);
		CVar(SkyModulation, VA_LIST(XS("Sky modulation"), XS("Sky modulation color")), Color_t(255, 255, 255, 255), VISUAL);
		CVar(PropModulation, VA_LIST(XS("Prop modulation"), XS("Prop modulation color")), Color_t(255, 255, 255, 255), VISUAL);
		CVar(ParticleModulation, VA_LIST(XS("Particle modulation"), XS("Particle modulation color")), Color_t(255, 255, 255, 255), VISUAL);
		CVar(FogModulation, VA_LIST(XS("Fog modulation"), XS("Fog modulation color")), Color_t(255, 255, 255, 255), VISUAL);

		CVar(Line, XS("Line color"), Color_t(255, 255, 255, 255), VISUAL);
		CVar(LineIgnoreZ, XS("Line ignore Z color"), Color_t(255, 255, 255, 0), VISUAL);

		CVar(PlayerPath, XS("Player path color"), Color_t(255, 255, 255, 0), VISUAL);
		CVar(PlayerPathIgnoreZ, XS("Player path ignore Z color"), Color_t(255, 255, 255, 255), VISUAL);
		CVar(ProjectilePath, XS("Projectile path color"), Color_t(255, 255, 255, 0), VISUAL);
		CVar(ProjectilePathIgnoreZ, XS("Projectile path ignore Z color"), Color_t(255, 255, 255, 255), VISUAL);
		CVar(TrajectoryPath, XS("Trajectory path color"), Color_t(255, 255, 255, 0), VISUAL);
		CVar(TrajectoryPathIgnoreZ, XS("Trajectory path ignore Z color"), Color_t(255, 255, 255, 255), VISUAL);
		CVar(ShotPath, XS("Shot path color"), Color_t(255, 255, 255, 0), VISUAL);
		CVar(ShotPathIgnoreZ, XS("Shot path ignore Z color"), Color_t(255, 255, 255, 255), VISUAL);
		CVar(SplashRadius, XS("Splash radius color"), Color_t(255, 255, 255, 0), VISUAL);
		CVar(SplashRadiusIgnoreZ, XS("Splash radius ignore Z color"), Color_t(255, 255, 255, 255), VISUAL);
		CVar(RealPath, XS("Real path color"), Color_t(255, 255, 255, 0), NOSAVE | DEBUGVAR);
		CVar(RealPathIgnoreZ, XS("Real path ignore Z color"), Color_t(255, 255, 255, 255), NOSAVE | DEBUGVAR);

		CVar(BoneHitboxEdge, XS("Bone hitbox edge color"), Color_t(255, 255, 255, 255), VISUAL);
		CVar(BoneHitboxEdgeIgnoreZ, XS("Bone hitbox edge ignore Z color"), Color_t(255, 255, 255, 0), VISUAL);
		CVar(BoneHitboxFace, XS("Bone hitbox face color"), Color_t(255, 255, 255, 0), VISUAL);
		CVar(BoneHitboxFaceIgnoreZ, XS("Bone hitbox face ignore Z color"), Color_t(255, 255, 255, 0), VISUAL);
		CVar(TargetHitboxEdge, XS("Target hitbox edge color"), Color_t(255, 150, 150, 255), VISUAL);
		CVar(TargetHitboxEdgeIgnoreZ, XS("Target hitbox edge ignore Z color"), Color_t(255, 150, 150, 0), VISUAL);
		CVar(TargetHitboxFace, XS("Target hitbox face color"), Color_t(255, 150, 150, 0), VISUAL);
		CVar(TargetHitboxFaceIgnoreZ, XS("Target hitbox face ignore Z color"), Color_t(255, 150, 150, 0), VISUAL);
		CVar(BoundHitboxEdge, XS("Bound hitbox edge color"), Color_t(255, 255, 255, 255), VISUAL);
		CVar(BoundHitboxEdgeIgnoreZ, XS("Bound hitbox edge ignore Z color"), Color_t(255, 255, 255, 0), VISUAL);
		CVar(BoundHitboxFace, XS("Bound hitbox face color"), Color_t(255, 255, 255, 0), VISUAL);
		CVar(BoundHitboxFaceIgnoreZ, XS("Bound hitbox face ignore Z color"), Color_t(255, 255, 255, 0), VISUAL);

		CVar(SpellFootstep, XS("Spell footstep color"), Color_t(255, 255, 255, 255), VISUAL);
	NAMESPACE_END(Colors)

	NAMESPACE_BEGIN(Aimbot)
		NAMESPACE_BEGIN(General, Aimbot)
			CVarEnum(AimType, XS("Aim type"), 0, NONE, nullptr,
				VA_LIST(XS("Off"), XS("Plain"), XS("Smooth"), XS("Silent"), XS("Locking"), XS("Assistive")),
				Off, Plain, Smooth, Silent, Locking, Assistive);
			CVarEnum(TargetSelection, XS("Target selection"), 0, NONE, nullptr,
				VA_LIST(XS("FOV"), XS("Distance"), XS("Hybrid")),
				FOV, Distance, Hybrid);
			CVarEnum(Target, XS("Target"), 0b0000001, DROPDOWN_MULTI, nullptr,
				VA_LIST(XS("Players"), XS("Sentries"), XS("Dispensers"), XS("Teleporters"), XS("Stickies"), XS("NPCs"), XS("Bombs")),
				Players = 1 << 0, Sentry = 1 << 1, Dispenser = 1 << 2, Teleporter = 1 << 3, Stickies = 1 << 4, NPCs = 1 << 5, Bombs = 1 << 6,
				Building = Sentry | Dispenser | Teleporter);
			CVarEnum(Ignore, XS("Ignore"), 0b00000001000, DROPDOWN_MULTI, nullptr,
				VA_LIST(XS("Friends"), XS("Party"), XS("Unprioritized"), XS("Invulnerable"), XS("Invisible"), XS("Unsimulated"), XS("Dead ringer"), XS("Vaccinator"), XS("Disguised"), XS("Taunting"), XS("Team")),
				Friends = 1 << 0, Party = 1 << 1, Unprioritized = 1 << 2, Invulnerable = 1 << 3, Invisible = 1 << 4, Unsimulated = 1 << 5, DeadRinger = 1 << 6, Vaccinator = 1 << 7, Disguised = 1 << 8, Taunting = 1 << 9, Team = 1 << 10);
			CVar(AimFOV, XS("Aim FOV"), 30.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 180.f);
			CVar(MaxTargets, XS("Max targets"), 2, SLIDER_MIN, 1, 6);
			CVar(IgnoreInvisible, XS("Ignore invisible"), 50.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 10.f, XS("%g%%"));
			CVar(AssistStrength, XS("Assist strength"), 25.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 1.f, XS("%g%%"));
			CVar(TickTolerance, XS("Tick tolerance"), 4, SLIDER_CLAMP, 0, 21);
			CVar(AutoShoot, XS("Auto shoot"), true);
			CVar(FOVCircle, XS("FOV Circle"), true, VISUAL);
			CVar(NoSpread, XS("No spread"), false);

			CVarEnum(AimHoldsFire, XS("Aim holds fire"), 2, NOSAVE | DEBUGVAR, nullptr,
				VA_LIST(XS("False"), XS("Minigun only"), XS("Always")),
				False, MinigunOnly, Always);
			CVar(NoSpreadOffset, XS("No spread offset"), 0.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -1.f, 1.f, 0.1f);
			CVar(NoSpreadAverage, XS("No spread average"), 5, NOSAVE | DEBUGVAR | SLIDER_MIN, 1, 25);
			CVar(NoSpreadInterval, XS("No spread interval"), 0.1f, NOSAVE | DEBUGVAR | SLIDER_MIN, 0.05f, 5.f, 0.1f, XS("%gs"));
			CVar(NoSpreadBackupInterval, XS("No spread backup interval"), 2.f, NOSAVE | DEBUGVAR | SLIDER_MIN, 2.f, 10.f, 0.1f, XS("%gs"));
		NAMESPACE_END(Global)

		NAMESPACE_BEGIN(Hitscan)
			CVarEnum(Hitboxes, VA_LIST(XS("Hitboxes"), XS("Hitscan hitboxes")), 0b000111, DROPDOWN_MULTI, nullptr,
				VA_LIST(XS("Head"), XS("Body"), XS("Pelvis"), XS("Arms"), XS("Legs"), XS("##Divider"), XS("Bodyaim if lethal"), XS("Headshot only")),
				Head = 1 << 0, Body = 1 << 1, Pelvis = 1 << 2, Arms = 1 << 3, Legs = 1 << 4, BodyaimIfLethal = 1 << 5, HeadshotOnly = 1 << 6);
			CVarValues(MultipointHitboxes, XS("Multipoint hitboxes"), 0b00000, DROPDOWN_MULTI, XS("All"),
				VA_LIST(XS("Head"), XS("Body"), XS("Pelvis"), XS("Arms"), XS("Legs")));
			CVarEnum(Modifiers, VA_LIST(XS("Modifiers"), XS("Hitscan modifiers")), 0b0100000, DROPDOWN_MULTI, nullptr,
				VA_LIST(XS("Tapfire"), XS("Wait for headshot"), XS("Wait for charge"), XS("Scoped only"), XS("Auto scope"), XS("Auto rev minigun"), XS("Extinguish team")),
				Tapfire = 1 << 0, WaitForHeadshot = 1 << 1, WaitForCharge = 1 << 2, ScopedOnly = 1 << 3, AutoScope = 1 << 4, AutoRev = 1 << 5, ExtinguishTeam = 1 << 6);
			CVar(MultipointScale, XS("Multipoint scale"), 0.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 5.f, XS("%g%%"));
			CVar(TapfireDistance, XS("Tapfire distance"), 1000.f, SLIDER_MIN | SLIDER_PRECISION, 250.f, 1000.f, 50.f);

			CVarEnum(PeekCheck, XS("Peek check"), 1, NOSAVE | DEBUGVAR, nullptr,
				VA_LIST(XS("Off"), XS("Doubletap only"), XS("Always")),
				Off, DoubletapOnly, Always);
			CVar(PeekAmount, XS("Peek amount"), 1, NOSAVE | DEBUGVAR, 0, 5);
			CVar(BoneSizeSubtract, XS("Bone size subtract"), 1.f, NOSAVE | DEBUGVAR | SLIDER_MIN, 0.f, 4.f, 0.25f);
			CVar(BoneSizeMinimumScale, XS("Bone size minimum scale"), 1.f, NOSAVE | DEBUGVAR | SLIDER_CLAMP, 0.f, 1.f, 0.1f);
		NAMESPACE_END(HITSCAN)

		NAMESPACE_BEGIN(Projectile)
			CVarEnum(StrafePrediction, VA_LIST(XS("Predict"), XS("Strafe prediction")), 0b11, DROPDOWN_MULTI, XS("Off"),
				VA_LIST(XS("Air strafing"), XS("Ground strafing")),
				Air = 1 << 0, Ground = 1 << 1);
			CVarEnum(SplashPrediction, VA_LIST(XS("Splash"), XS("Splash prediction")), 0, NONE, nullptr,
				VA_LIST(XS("Off"), XS("Include"), XS("Prefer"), XS("Only")),
				Off, Include, Prefer, Only);
			CVarEnum(AutoDetonate, XS("Auto detonate"), 0b00, DROPDOWN_MULTI, XS("Off"),
				VA_LIST(XS("Stickies"), XS("Flares"), XS("##Divider"), XS("Prevent self damage"), XS("Ignore invisible")),
				Stickies = 1 << 0, Flares = 1 << 1, PreventSelfDamage = 1 << 2, IgnoreInvisible = 1 << 3);
			CVarEnum(AutoAirblast, XS("Auto airblast"), 0b000, DROPDOWN_MULTI, XS("Off"), // todo: implement advanced redirect!!
				VA_LIST(XS("Enabled"), XS("##Divider"), XS("Redirect"), XS("Ignore FOV")),
				Enabled = 1 << 0, Redirect = 1 << 1, IgnoreFOV = 1 << 2);
			CVarEnum(Hitboxes, VA_LIST(XS("Hitboxes"), XS("Projectile hitboxes")), 0b001111, DROPDOWN_MULTI, nullptr,
				VA_LIST(XS("Auto"), XS("##Divider"), XS("Head"), XS("Body"), XS("Feet"), XS("##Divider"), XS("Bodyaim if lethal"), XS("Prioritize feet")),
				Auto = 1 << 0, Head = 1 << 1, Body = 1 << 2, Feet = 1 << 3, BodyaimIfLethal = 1 << 4, PrioritizeFeet = 1 << 5);
			CVarEnum(Modifiers, VA_LIST(XS("Modifiers"), XS("Projectile modifiers")), 0b1010, DROPDOWN_MULTI, nullptr,
				VA_LIST(XS("Charge weapon"), XS("Cancel charge"), XS("Use arm time")),
				ChargeWeapon = 1 << 0, CancelCharge = 1 << 1, UseArmTime = 1 << 2);
			CVar(MaxSimulationTime, XS("Max simulation time"), 2.f, SLIDER_MIN | SLIDER_PRECISION, 0.1f, 2.5f, 0.25f, XS("%gs"));
			CVar(HitChance, XS("Hit chance"), 0.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 10.f, XS("%g%%"));
			CVar(AutodetRadius, XS("Autodet radius"), 90.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 10.f, XS("%g%%"));
			CVar(SplashRadius, XS("Splash radius"), 90.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 10.f, XS("%g%%"));
			CVar(AutoRelease, XS("Auto release"), 0.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 5.f, XS("%g%%"));

			CVar(GroundSamples, XS("Samples"), 33, NOSAVE | DEBUGVAR, 3, 66);
			CVar(GroundStraightFuzzyValue, XS("Straight fuzzy value"), 100.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 500.f, 25.f);
			CVar(GroundLowMinimumSamples, XS("Low min samples"), 16, NOSAVE | DEBUGVAR, 3, 66);
			CVar(GroundHighMinimumSamples, XS("High min samples"), 33, NOSAVE | DEBUGVAR, 3, 66);
			CVar(GroundLowMinimumDistance, XS("Low min distance"), 0.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 2500.f, 100.f);
			CVar(GroundHighMinimumDistance, XS("High min distance"), 1000.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 2500.f, 100.f);
			CVar(GroundMaxChanges, XS("Max changes"), 0, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0, 5);
			CVar(GroundMaxChangeTime, XS("Max change time"), 0, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0, 66);

			CVar(AirSamples, XS("Samples"), 33, NOSAVE | DEBUGVAR, 3, 66);
			CVar(AirStraightFuzzyValue, XS("Straight fuzzy value"), 0.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 500.f, 25.f);
			CVar(AirLowMinimumSamples, XS("Low min samples"), 16, NOSAVE | DEBUGVAR, 3, 66);
			CVar(AirHighMinimumSamples, XS("High min samples"), 16, NOSAVE | DEBUGVAR, 3, 66);
			CVar(AirLowMinimumDistance, XS("Low min distance"), 100000.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 2500.f, 100.f);
			CVar(AirHighMinimumDistance, XS("High min distance"), 100000.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 2500.f, 100.f);
			CVar(AirMaxChanges, XS("Max changes"), 2, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0, 5);
			CVar(AirMaxChangeTime, XS("Max change time"), 16, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0, 66);

			CVar(VelocityAverageCount, XS("Velocity average count"), 5, NOSAVE | DEBUGVAR, 1, 10);
			CVar(VerticalShift, XS("Vertical shift"), 5.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f, 0.5f);
			CVar(DragOverride, XS("Drag override"), 0.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 1.f, 0.01f);
			CVar(TimeOverride, XS("Time override"), 0.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 2.f, 0.01f);
			CVar(HuntsmanLerp, XS("Huntsman lerp"), 50.f, NOSAVE | DEBUGVAR | SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 1.f, XS("%g%%"));
			CVar(HuntsmanLerpLow, XS("Huntsman lerp low"), 100.f, NOSAVE | DEBUGVAR | SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 1.f, XS("%g%%"));
			CVar(HuntsmanAdd, XS("Huntsman add"), 0.f, NOSAVE | DEBUGVAR | SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 20.f);
			CVar(HuntsmanAddLow, XS("Huntsman add low"), 0.f, NOSAVE | DEBUGVAR | SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 20.f);
			CVar(HuntsmanClamp, XS("Huntsman clamp"), 5.f, NOSAVE | DEBUGVAR | SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 10.f, 0.5f);
			CVar(HuntsmanPullPoint, XS("Huntsman pull point"), false, NOSAVE | DEBUGVAR);
			CVar(HuntsmanPullNoZ, XS("Pull no Z"), false, NOSAVE | DEBUGVAR);

			CVar(SplashPointsDirect, XS("Splash points direct"), 100, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0, 400, 5);
			CVar(SplashPointsArc, XS("Splash points arc"), 100, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0, 400, 5);
			CVar(SplashCountDirect, XS("Splash count direct"), 100, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 1, 400, 5);
			CVar(SplashCountArc, XS("Splash count arc"), 5, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 1, 400, 5);
			CVar(SplashRotateX, XS("Splash Rx"), -1.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, -1.f, 360.f);
			CVar(SplashRotateY, XS("Splash Ry"), -1.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, -1.f, 360.f);
			CVar(SplashTraceInterval, XS("Splash trace interval"), 10, NOSAVE | DEBUGVAR, 1, 10);
			CVar(SplashNormalSkip, XS("Splash normal skip"), 1, NOSAVE | DEBUGVAR | SLIDER_MIN, 1, 10);
			CVarEnum(SplashMode, XS("Splash mode"), 0, NOSAVE | DEBUGVAR, nullptr,
				VA_LIST(XS("Multi"), XS("Single")),
				Multi, Single);
			CVarEnum(RocketSplashMode, XS("Rocket splash mode"), 0, NOSAVE | DEBUGVAR, nullptr,
				VA_LIST(XS("Regular"), XS("Special light"), XS("Special heavy")),
				Regular, SpecialLight, SpecialHeavy);
			CVar(SplashGrates, XS("Splash grates"), true, NOSAVE | DEBUGVAR);
			CVar(Out2NormalMin, XS("Out2 normal min"), -0.7f, NOSAVE | DEBUGVAR | SLIDER_CLAMP | SLIDER_PRECISION, -1.f, 1.f, 0.1f);
			CVar(Out2NormalMax, XS("Out2 normal max"), 0.7f, NOSAVE | DEBUGVAR | SLIDER_CLAMP | SLIDER_PRECISION, -1.f, 1.f, 0.1f);

			CVar(DeltaCount, XS("Delta count"), 5, NOSAVE | DEBUGVAR, 1, 5);
			CVarEnum(DeltaMode, XS("Delta mode"), 0, NOSAVE | DEBUGVAR, nullptr,
				VA_LIST(XS("Average"), XS("Max")),
				Average, Max);
			CVarEnum(MovesimFrictionFlags, XS("Movesim friction flags"), 0b01, NOSAVE | DEBUGVAR | DROPDOWN_MULTI, nullptr,
				VA_LIST(XS("Run reduce"), XS("Calculate increase")),
				RunReduce = 1 << 0, CalculateIncrease = 1 << 1);
		NAMESPACE_END(Projectile)

		NAMESPACE_BEGIN(Melee)
			CVar(AutoBackstab, XS("Auto backstab"), true);
			CVar(IgnoreRazorback, XS("Ignore razorback"), true);
			CVar(SwingPrediction, XS("Swing prediction"), false);
			CVar(WhipTeam, XS("Whip team"), false);

			CVar(SwingOffset, XS("Swing offset"), -1, NOSAVE | DEBUGVAR, -1, 1);
			CVar(SwingPredictLag, XS("Swing predict lag"), true, NOSAVE | DEBUGVAR);
			CVar(BackstabAccountPing, XS("Backstab account ping"), true, NOSAVE | DEBUGVAR);
			CVar(BackstabDoubleTest, XS("Backstab double test"), true, NOSAVE | DEBUGVAR);
		NAMESPACE_END(Melee)

		NAMESPACE_BEGIN(Healing)
			CVarEnum(HealPriority, XS("Heal Priority"), 0, NONE, nullptr,
				VA_LIST(XS("None"), XS("Prioritize team"), XS("Prioritize friends"), XS("Friends only")),
				None, PrioritizeTeam, PrioritizeFriends, FriendsOnly);
			CVar(AutoHeal, XS("Auto heal"), false);
			CVar(AutoArrow, XS("Auto arrow"), false);
			CVar(AutoRepair, XS("Auto repair"), false);
			CVar(AutoSandvich, XS("Auto sandvich"), false);
			CVar(AutoVaccinator, XS("Auto vaccinator"), false);
			CVar(ActivateOnVoice, XS("Activate on voice"), false);

			CVar(AutoVaccinatorBulletScale, XS("Auto vaccinator bullet scale"), 100.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 200.f, 10.f, XS("%g%%"));
			CVar(AutoVaccinatorBlastScale, XS("Auto vaccinator blast scale"), 100.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 200.f, 10.f, XS("%g%%"));
			CVar(AutoVaccinatorFireScale, XS("Auto vaccinator fire scale"), 100.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 200.f, 10.f, XS("%g%%"));
			CVar(AutoVaccinatorFlamethrowerDamageOnly, XS("Auto vaccinator flamethrower damage only"), false, NOSAVE | DEBUGVAR);
		NAMESPACE_END(Healing)
	NAMESPACE_END(Aimbot)
	
	NAMESPACE_BEGIN(CritHack, Crit Hack)
		CVar(ForceCrits, XS("Force crits"), false);
		CVar(AvoidRandomCrits, XS("Avoid random crits"), false);
		CVar(AlwaysMeleeCrit, XS("Always melee crit"), false);
	NAMESPACE_END(CritHack)

	NAMESPACE_BEGIN(Backtrack)
		CVar(Latency, XS("Fake latency"), 0, SLIDER_CLAMP, 0, 1000, 5);
		CVar(Interp, XS("Fake interp"), 0, SLIDER_CLAMP | SLIDER_PRECISION, 0, 1000, 5);
		CVar(Window, VA_LIST(XS("Window"), XS("Backtrack window")), 185, SLIDER_CLAMP | SLIDER_PRECISION, 0, 200, 5);
		CVar(PreferOnShot, XS("Prefer on shot"), false);

		CVar(Offset, XS("Offset"), 0, NOSAVE | DEBUGVAR, -1, 1);
	NAMESPACE_END(Backtrack)

	NAMESPACE_BEGIN(Doubletap)
		CVar(Doubletap, XS("Doubletap"), false);
		CVar(Warp, XS("Warp"), false);
		CVar(RechargeTicks, XS("Recharge ticks"), false);
		CVar(AntiWarp, XS("Anti-warp"), true);
		CVar(TickLimit, XS("Tick limit"), 22, SLIDER_CLAMP, 2, 22);
		CVar(WarpRate, XS("Warp rate"), 22, SLIDER_CLAMP, 2, 22);
		CVar(RechargeLimit, XS("Recharge limit"), 24, SLIDER_MIN, 1, 24);
		CVar(PassiveRecharge, XS("Passive recharge"), 0, SLIDER_CLAMP, 0, 67);
	NAMESPACE_END(DoubleTap)

	NAMESPACE_BEGIN(Fakelag)
		CVarEnum(Fakelag, XS("Fakelag"), 0, NONE, nullptr,
			VA_LIST(XS("Off"), XS("Plain"), XS("Random"), XS("Adaptive")),
			Off, Plain, Random, Adaptive);
		CVarEnum(Options, VA_LIST(XS("Options"), XS("Fakelag options")), 0b000, DROPDOWN_MULTI, nullptr,
			VA_LIST(XS("Only moving"), XS("On unduck"), XS("Not airborne")),
			OnlyMoving = 1 << 0, OnUnduck = 1 << 1, NotAirborne = 1 << 2);
		CVar(PlainTicks, XS("Plain ticks"), 12, SLIDER_CLAMP, 1, 22);
		CVar(RandomTicks, XS("Random ticks"), IntRange_t(14, 18), SLIDER_CLAMP, 1, 22, 1, XS("%i - %i"));
		CVar(UnchokeOnAttack, XS("Unchoke on attack"), true);
		CVar(RetainBlastJump, XS("Retain blastjump"), false);

		CVar(RetainSoldierOnly, XS("Retain blastjump soldier only"), true, NOSAVE | DEBUGVAR);
	NAMESPACE_END(FakeLag)

	NAMESPACE_BEGIN(AutoPeek, Auto Peek)
		CVar(Enabled, VA_LIST(XS("Enabled"), XS("Auto peek")), false);
	NAMESPACE_END(AutoPeek)

	NAMESPACE_BEGIN(Speedhack)
		CVar(Enabled, VA_LIST(XS("Enabled"), XS("Speedhack enabled")), false);
		CVar(Amount, VA_LIST(XS("Amount"), XS("SpeedHack amount")), 1, NONE, 1, 50);
	NAMESPACE_END(Speedhack)

	NAMESPACE_BEGIN(AntiAim, Antiaim)
		CVar(Enabled, VA_LIST(XS("Enabled"), XS("Antiaim enabled")), false);
		CVarEnum(PitchReal, XS("Real pitch"), 0, NONE, nullptr,
			VA_LIST(XS("None"), XS("Up"), XS("Down"), XS("Zero"), XS("Jitter"), XS("Reverse jitter")),
			None, Up, Down, Zero, Jitter, ReverseJitter);
		CVarEnum(PitchFake, XS("Fake pitch"), 0, NONE, nullptr,
			VA_LIST(XS("None"), XS("Up"), XS("Down"), XS("Jitter"), XS("Reverse jitter")),
			None, Up, Down, Jitter, ReverseJitter);
		Enum(Yaw, Forward, Left, Right, Backwards, Edge, Jitter, Spin);
		CVarValues(YawReal, XS("Real yaw"), 0, NONE, nullptr,
			XS("Forward"), XS("Left"), XS("Right"), XS("Backwards"), XS("Edge"), XS("Jitter"), XS("Spin"));
		CVarValues(YawFake, XS("Fake yaw"), 0, NONE, nullptr,
			XS("Forward"), XS("Left"), XS("Right"), XS("Backwards"), XS("Edge"), XS("Jitter"), XS("Spin"));
		Enum(YawMode, View, Target);
		CVarValues(RealYawBase, XS("Real base"), 0, NONE, nullptr,
			XS("View"), XS("Target"));
		CVarValues(FakeYawBase, XS("Fake base"), 0, NONE, nullptr,
			XS("View"), XS("Target"));
		CVar(RealYawOffset, XS("Real offset"), 0.f, SLIDER_CLAMP | SLIDER_PRECISION, -180.f, 180.f, 5.f);
		CVar(FakeYawOffset, XS("Fake offset"), 0.f, SLIDER_CLAMP | SLIDER_PRECISION, -180.f, 180.f, 5.f);
		CVar(RealYawValue, XS("Real value"), 90.f, SLIDER_CLAMP | SLIDER_PRECISION, -180.f, 180.f, 5.f);
		CVar(FakeYawValue, XS("Fake value"), -90.f, SLIDER_CLAMP | SLIDER_PRECISION, -180.f, 180.f, 5.f);
		CVar(SpinSpeed, XS("Spin speed"), 15.f, SLIDER_PRECISION, -30.f, 30.f);
		CVar(MinWalk, XS("Minwalk"), true);
		CVar(AntiOverlap, XS("Anti-overlap"), false);
		CVar(InvalidShootPitch, XS("Hide pitch on shot"), false);
	NAMESPACE_END(AntiAim)

	NAMESPACE_BEGIN(Resolver)
		CVar(Enabled, VA_LIST(XS("Enabled"), XS("Resolver enabled")), false);
		CVar(AutoResolve, XS("Auto resolve"), false);
		CVar(AutoResolveCheatersOnly, XS("Auto resolve cheaters only"), false);
		CVar(AutoResolveHeadshotOnly, XS("Auto resolve headshot only"), false);
		CVar(AutoResolveYawAmount, XS("Auto resolve yaw"), 90.f, SLIDER_CLAMP | SLIDER_PRECISION, -180.f, 180.f, 45.f);
		CVar(AutoResolvePitchAmount, XS("Auto resolve pitch"), 90.f, SLIDER_CLAMP, -180.f, 180.f, 90.f);
		CVar(CycleYaw, XS("Cycle yaw"), 0.f, SLIDER_CLAMP | SLIDER_PRECISION, -180.f, 180.f, 45.f);
		CVar(CyclePitch, XS("Cycle pitch"), 0.f, SLIDER_CLAMP, -180.f, 180.f, 90.f);
		CVar(CycleView, XS("Cycle view"), false);
		CVar(CycleMinwalk, XS("Cycle minwalk"), false);
	NAMESPACE_END(Resolver)

	NAMESPACE_BEGIN(CheaterDetection, Cheater Detection)
		CVarEnum(Methods, XS("Detection methods"), 0b0000, DROPDOWN_MULTI, nullptr,
			VA_LIST(XS("Invalid pitch"), XS("Packet choking"), XS("Aim flicking"), XS("Duck Speed")),
			InvalidPitch = 1 << 0, PacketChoking = 1 << 1, AimFlicking = 1 << 2, DuckSpeed = 1 << 3);
		CVar(DetectionsRequired, XS("Detections required"), 10, SLIDER_MIN, 0, 50);
		CVar(MinimumChoking, XS("Minimum choking"), 20, SLIDER_MIN, 4, 22);
		CVar(MinimumFlick, XS("Minimum flick angle"), 20.f, SLIDER_PRECISION, 10.f, 30.f); // min flick size to suspect
		CVar(MaximumNoise, XS("Maximum flick noise"), 1.f, SLIDER_PRECISION, 1.f, 10.f); // max difference between angles before and after flick
	NAMESPACE_END(CheaterDetection)

	NAMESPACE_BEGIN(ESP)
		CVarValues(ActiveGroups, XS("Active groups"), int(0b11111111111111111111111111111111), VISUAL | DROPDOWN_MULTI | DROPDOWN_NOSANITIZATION, nullptr);
	NAMESPACE_END(ESP)

	NAMESPACE_BEGIN(Visuals)
		NAMESPACE_BEGIN(UI)
			CVarEnum(StreamerMode, XS("Streamer mode"), 0, VISUAL, nullptr,
				VA_LIST(XS("Off"), XS("Local"), XS("Friends"), XS("Party"), XS("All")),
				Off, Local, Friends, Party, All);
			CVarEnum(ChatTags, XS("Chat tags"), 0b000, VISUAL | DROPDOWN_MULTI, nullptr,
				VA_LIST(XS("Local"), XS("Friends"), XS("Party"), XS("Assigned")),
				Local = 1 << 0, Friends = 1 << 1, Party = 1 << 2, Assigned = 1 << 3);
			CVar(FieldOfView, XS("Field of view## FOV"), 0.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 160.f, 5.f);
			CVar(ZoomFieldOfView, XS("Zoomed field of view## Zoomed FOV"), 0.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 160.f, 5.f);
			CVar(AspectRatio, XS("Aspect ratio"), 0.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 5.f, 0.05f);
			CVar(RevealScoreboard, XS("Reveal scoreboard"), false, VISUAL);
			CVar(ScoreboardUtility, XS("Scoreboard utility"), false);
			CVar(ScoreboardColors, XS("Scoreboard colors"), false, VISUAL);
			CVar(CleanScreenshots, XS("Clean screenshots"), true);
		NAMESPACE_END(UI)

		NAMESPACE_BEGIN(Thirdperson)
			CVar(Enabled, XS("Thirdperson"), false, VISUAL);
			CVar(Crosshair, VA_LIST(XS("Crosshair"), XS("Thirdperson crosshair")), false, VISUAL);
			CVar(Distance, XS("Thirdperson distance"), 150.f, VISUAL | SLIDER_PRECISION, 0.f, 400.f, 10.f);
			CVar(Right, XS("Thirdperson right"), 0.f, VISUAL | SLIDER_PRECISION, -100.f, 100.f, 5.f);
			CVar(Up, XS("Thirdperson up"), 0.f, VISUAL | SLIDER_PRECISION, -100.f, 100.f, 5.f);

			CVar(Scale, XS("Thirdperson scales"), true, NOSAVE | DEBUGVAR);
			CVar(Collide, XS("Thirdperson collides"), true, NOSAVE | DEBUGVAR);
		NAMESPACE_END(ThirdPerson)

		NAMESPACE_BEGIN(Removals)
			CVar(Interpolation, VA_LIST(XS("Interpolation"), XS("Remove interpolation")), false);
			CVar(Lerp, VA_LIST(XS("Lerp"), XS("Remove lerp")), true);
			CVar(Disguises, VA_LIST(XS("Disguises"), XS("Remove disguises")), false, VISUAL);
			CVar(Taunts, VA_LIST(XS("Taunts"), XS("Remove taunts")), false, VISUAL);
			CVar(Scope, VA_LIST(XS("Scope"), XS("Remove scope")), false, VISUAL);
			CVar(PostProcessing, VA_LIST(XS("Post processing"), XS("Remove post processing")), false, VISUAL);
			CVar(ScreenOverlays, VA_LIST(XS("Screen overlays"), XS("Remove screen overlays")), false, VISUAL);
			CVar(ScreenEffects, VA_LIST(XS("Screen effects"), XS("Remove screen effects")), false, VISUAL);
			CVar(ViewPunch, VA_LIST(XS("View punch"), XS("Remove view punch")), false, VISUAL);
			CVar(AngleForcing, VA_LIST(XS("Angle forcing"), XS("Remove angle forcing")), false, VISUAL);
			CVar(Ragdolls, VA_LIST(XS("Ragdolls"), XS("Remove ragdoll")), false, VISUAL);
			CVar(Gibs, VA_LIST(XS("Gibs"), XS("Remove gibs")), false, VISUAL);
			CVar(MOTD, VA_LIST(XS("MOTD"), XS("Remove MOTD")), false, VISUAL);
		NAMESPACE_END(Removals)

		NAMESPACE_BEGIN(Effects)
			CVarValues(BulletTracer, XS("Bullet tracer"), std::string(XS("Default")), VISUAL | DROPDOWN_CUSTOM, nullptr,
				XS("Default"), XS("None"), XS("Big nasty"), XS("Distortion trail"), XS("Machina"), XS("Sniper rail"), XS("Short circuit"), XS("C.A.P.P.E.R"), XS("Merasmus ZAP"), XS("Merasmus ZAP 2"), XS("Black ink"), XS("Line"), XS("Line ignore Z"), XS("Beam"));
			CVarValues(CritTracer, XS("Crit tracer"), std::string(XS("Default")), VISUAL | DROPDOWN_CUSTOM, nullptr,
				XS("Default"), XS("None"), XS("Big nasty"), XS("Distortion trail"), XS("Machina"), XS("Sniper rail"), XS("Short circuit"), XS("C.A.P.P.E.R"), XS("Merasmus ZAP"), XS("Merasmus ZAP 2"), XS("Black ink"), XS("Line"), XS("Line ignore Z"), XS("Beam"));
			CVarValues(MedigunBeam, XS("Medigun beam"), std::string(XS("Default")), VISUAL | DROPDOWN_CUSTOM, nullptr,
				XS("Default"), XS("None"), XS("Uber"), XS("Dispenser"), XS("Passtime"), XS("Bombonomicon"), XS("White"), XS("Orange"));
			CVarValues(MedigunCharge, XS("Medigun charge"), std::string(XS("Default")), VISUAL | DROPDOWN_CUSTOM, nullptr,
				XS("Default"), XS("None"), XS("Electrocuted"), XS("Halloween"), XS("Fireball"), XS("Teleport"), XS("Burning"), XS("Scorching"), XS("Purple energy"), XS("Green energy"), XS("Nebula"), XS("Purple stars"), XS("Green stars"), XS("Sunbeams"), XS("Spellbound"), XS("Purple sparks"), XS("Yellow sparks"), XS("Green zap"), XS("Yellow zap"), XS("Plasma"), XS("Frostbite"), XS("Time warp"), XS("Purple souls"), XS("Green souls"), XS("Bubbles"), XS("Hearts"));
			CVarValues(ProjectileTrail, XS("Projectile trail"), std::string(XS("Default")), VISUAL | DROPDOWN_CUSTOM, nullptr,
				XS("Default"), XS("None"), XS("Rocket"), XS("Critical"), XS("Energy"), XS("Charged"), XS("Ray"), XS("Fireball"), XS("Teleport"), XS("Fire"), XS("Flame"), XS("Sparks"), XS("Flare"), XS("Trail"), XS("Health"), XS("Smoke"), XS("Bubbles"), XS("Halloween"), XS("Monoculus"), XS("Sparkles"), XS("Rainbow"));
			CVarEnum(SpellFootsteps, XS("Spell footsteps"), 0, VISUAL, nullptr,
				VA_LIST(XS("Off"), XS("Color"), XS("Team"), XS("Halloween")),
				Off, Color, Team, Halloween);
			CVarEnum(RagdollEffects, XS("Ragdoll effects"), 0b000000, VISUAL | DROPDOWN_MULTI, nullptr,
				VA_LIST(XS("Burning"), XS("Electrocuted"), XS("Ash"), XS("Dissolve"), XS("##Divider"), XS("Gold"), XS("Ice")),
				Burning = 1 << 0, Electrocuted = 1 << 1, Ash = 1 << 2, Dissolve = 1 << 3, Gold = 1 << 4, Ice = 1 << 5);
			CVar(DrawIconsThroughWalls, XS("Draw icons through walls"), false, VISUAL);
			CVar(DrawDamageNumbersThroughWalls, XS("Draw damage numbers through walls"), false, VISUAL);
		NAMESPACE_END(Tracers)

		NAMESPACE_BEGIN(Viewmodel)
			CVar(CrosshairAim, XS("Crosshair aim position"), false, VISUAL);
			CVar(ViewmodelAim, XS("Viewmodel aim position"), false, VISUAL);
			CVar(OffsetX, VA_LIST(XS("Offset X"), XS("Viewmodel offset X")), 0.f, VISUAL | SLIDER_PRECISION, -45.f, 45.f, 5.f);
			CVar(OffsetY, VA_LIST(XS("Offset Y"), XS("Viewmodel offset Y")), 0.f, VISUAL | SLIDER_PRECISION, -45.f, 45.f, 5.f);
			CVar(OffsetZ, VA_LIST(XS("Offset Z"), XS("Viewmodel offset Z")), 0.f, VISUAL | SLIDER_PRECISION, -45.f, 45.f, 5.f);
			CVar(Pitch, VA_LIST(XS("Pitch"), XS("Viewmodel pitch")), 0.f, VISUAL | SLIDER_CLAMP | SLIDER_PRECISION, -180.f, 180.f, 5.f);
			CVar(Yaw, VA_LIST(XS("Yaw"), XS("Viewmodel yaw")), 0.f, VISUAL | SLIDER_CLAMP | SLIDER_PRECISION, -180.f, 180.f, 5.f);
			CVar(Roll, VA_LIST(XS("Roll"), XS("Viewmodel roll")), 0.f, VISUAL | SLIDER_CLAMP | SLIDER_PRECISION, -180.f, 180.f, 5.f);
			CVar(SwayScale, VA_LIST(XS("Sway scale"), XS("Viewmodel sway scale")), 0.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 5.f, 0.5f);
			CVar(SwayInterp, VA_LIST(XS("Sway interp"), XS("Viewmodel sway interp")), 0.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 1.f, 0.1f);
		NAMESPACE_END(Viewmodel)

		NAMESPACE_BEGIN(World)
			CVarEnum(Modulations, XS("Modulations"), 0b00000, VISUAL | DROPDOWN_MULTI, nullptr,
				VA_LIST(XS("World"), XS("Sky"), XS("Prop"), XS("Particle"), XS("Fog")),
				World = 1 << 0, Sky = 1 << 1, Prop = 1 << 2, Particle = 1 << 3, Fog = 1 << 4);
			CVarValues(SkyboxChanger, XS("Skybox changer"), std::string(XS("Off")), VISUAL | DROPDOWN_CUSTOM, nullptr,
				VA_LIST(XS("Off")));
			CVarValues(WorldTexture, XS("World texture"), std::string(XS("Default")), VISUAL | DROPDOWN_CUSTOM, nullptr,
				XS("Default"), XS("Dev"), XS("Camo"), XS("Black"), XS("White"), XS("Gray"), XS("Flat"));
			CVar(NearPropFade, XS("Near prop fade"), false, VISUAL);
			CVar(NoPropFade, XS("No prop fade"), false, VISUAL);
		NAMESPACE_END(World)

		NAMESPACE_BEGIN(Beams) // as of now, these will stay out of the menu
			CVar(Model, XS("Model"), std::string(XS("sprites/physbeam.vmt")), VISUAL);
			CVar(Life, XS("Life"), 2.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f);
			CVar(Width, XS("Width"), 2.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f);
			CVar(EndWidth, XS("End width"), 2.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f);
			CVar(FadeLength, XS("Fade length"), 10.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 30.f);
			CVar(Amplitude, XS("Amplitude"), 2.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f);
			CVar(Brightness, XS("Brightness"), 255.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 255.f);
			CVar(Speed, XS("Speed"), 0.2f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 5.f);
			CVar(Segments, XS("Segments"), 2, VISUAL | SLIDER_MIN, 1, 10);
			CVar(Color, XS("Color"), Color_t(255, 255, 255, 255), VISUAL);
			CVarEnum(Flags, XS("Flags"), 0b10000000100000000, VISUAL | DROPDOWN_MULTI, nullptr,
				VA_LIST(XS("Start entity"), XS("End entity"), XS("Fade in"), XS("Fade out"), XS("Sine noise"), XS("Solid"), XS("Shade in"), XS("Shade out"), XS("Only noise once"), XS("No tile"), XS("Use hitboxes"), XS("Start visible"), XS("End visible"), XS("Is active"), XS("Forever"), XS("Halobeam"), XS("Reverse")),
				StartEntity = 1 << 0, EndEntity = 1 << 1, FadeIn = 1 << 2, FadeOut = 1 << 3, SineNoise = 1 << 4, Solid = 1 << 5, ShadeIn = 1 << 6, ShadeOut = 1 << 7, OnlyNoiseOnce = 1 << 8, NoTile = 1 << 9, UseHitboxes = 1 << 10, StartVisible = 1 << 11, EndVisible = 1 << 12, IsActive = 1 << 13, Forever = 1 << 14, Halobeam = 1 << 15, Reverse = 1 << 16);
		NAMESPACE_END(Beams)

		NAMESPACE_BEGIN(Line)
			CVar(Enabled, XS("Line tracers"), false, VISUAL);
			CVar(DrawDuration, VA_LIST(XS("Draw duration"), XS("Line draw duration")), 5.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f);
		NAMESPACE_END(Line)

		NAMESPACE_BEGIN(Hitbox)
			CVarEnum(BonesEnabled, VA_LIST(XS("Bones enabled"), XS("Hitbox bones enabled")), 0b00, VISUAL | DROPDOWN_MULTI, XS("Off"),
				VA_LIST(XS("On shot"), XS("On hit")),
				OnShot = 1 << 0, OnHit = 1 << 1);
			CVarEnum(BoundsEnabled, VA_LIST(XS("Bounds enabled"), XS("Hitbox bounds enabled")), 0b000, VISUAL | DROPDOWN_MULTI, XS("Off"),
				VA_LIST(XS("On shot"), XS("On hit"), XS("Aim point")),
				OnShot = 1 << 0, OnHit = 1 << 1, AimPoint = 1 << 2);
			CVar(DrawDuration, VA_LIST(XS("Draw duration"), XS("Hitbox draw duration")), 5.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f);
		NAMESPACE_END(Hitbox)

		NAMESPACE_BEGIN(Simulation)
			Enum(Style, Off, Line, Separators, Spaced, Arrows, Boxes);
			CVarValues(PlayerPath, XS("Player path"), 0, VISUAL, nullptr,
				XS("Off"), XS("Line"), XS("Separators"), XS("Spaced"), XS("Arrows"), XS("Boxes"));
			CVarValues(ProjectilePath, XS("Projectile path"), 0, VISUAL, nullptr,
				XS("Off"), XS("Line"), XS("Separators"), XS("Spaced"), XS("Arrows"), XS("Boxes"));
			CVarValues(TrajectoryPath, XS("Trajectory path"), 0, VISUAL, nullptr,
				XS("Off"), XS("Line"), XS("Separators"), XS("Spaced"), XS("Arrows"), XS("Boxes"));
			CVarValues(ShotPath, XS("Shot path"), 0, VISUAL, nullptr,
				XS("Off"), XS("Line"), XS("Separators"), XS("Spaced"), XS("Arrows"), XS("Boxes"));
			CVarEnum(SplashRadius, XS("Splash radius"), 0b0, VISUAL | DROPDOWN_MULTI, XS("Off"),
				VA_LIST(XS("Rockets"), XS("Stickies"), XS("Pipes"), XS("Scorch shot"), XS("##Divider"), XS("Trace"), XS("Sphere")),
				Rockets = 1 << 0, Stickies = 1 << 1, Pipes = 1 << 2, ScorchShot = 1 << 3, Trace = 1 << 4, Sphere = 1 << 5,
				Enabled = Rockets | Stickies | Pipes | ScorchShot);
			CVar(Timed, VA_LIST(XS("Timed"), XS("Timed path")), false, VISUAL);
			CVar(Box, VA_LIST(XS("Box"), XS("Path box")), true, VISUAL);
			CVar(ProjectileCamera, XS("Projectile camera"), false, VISUAL);
			CVar(ProjectileWindow, XS("Projectile window"), WindowBox_t(), VISUAL | NOBIND);
			CVar(SwingLines, XS("Swing lines"), false, VISUAL);
			CVar(DrawDuration, VA_LIST(XS("Draw duration"), XS("Simulation draw duration")), 5.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f);

			CVarValues(RealPath, XS("Real path"), 0, NOSAVE | DEBUGVAR, nullptr,
				XS("Off"), XS("Line"), XS("Separators"), XS("Spaced"), XS("Arrows"), XS("Boxes"));
			CVar(SeparatorSpacing, XS("Separator spacing"), 4, NOSAVE | DEBUGVAR, 1, 16);
			CVar(SeparatorLength, XS("Separator length"), 12.f, NOSAVE | DEBUGVAR, 2.f, 16.f);
		NAMESPACE_END(Simulation)

		NAMESPACE_BEGIN(Trajectory)
			CVar(Override, XS("Simulation override"), false, NOSAVE | DEBUGVAR);
			CVar(OffsetX, XS("Offset X"), 16.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -25.f, 25.f, 0.5f);
			CVar(OffsetY, XS("Offset Y"), 8.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -25.f, 25.f, 0.5f);
			CVar(OffsetZ, XS("Offset Z"), -6.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -25.f, 25.f, 0.5f);
			CVar(ForwardRedirect, XS("Forward redirect"), 2000.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 2000.f, 100.f);
			CVar(ForwardCutoff, XS("Forward cutoff"), 0.1f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 1.f, 0.1f);
			CVar(Hull, XS("Hull"), 5.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f, 0.5f);
			CVar(Speed, XS("Speed"), 1200.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 5000.f, 50.f);
			CVar(Gravity, XS("Gravity"), 1.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 1.f, 0.1f);
			CVar(LifeTime, XS("Life time"), 2.2f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f, 0.1f);
			CVar(UpVelocity, XS("Up velocity"), 200.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 1000.f, 50.f);
			CVar(AngularVelocityX, XS("Angular velocity X"), 600.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -1000.f, 1000.f, 50.f);
			CVar(AngularVelocityY, XS("Angular velocity Y"), -1200.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -1000.f, 1000.f, 50.f);
			CVar(AngularVelocityZ, XS("Angular velocity Z"), 0.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -1000.f, 1000.f, 50.f);
			CVar(Drag, XS("Drag"), 1.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 2.f, 0.1f);
			CVar(DragX, XS("Drag X"), 0.003902f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 0.1f, 0.01f, XS("%.15g"));
			CVar(DragY, XS("Drag Y"), 0.009962f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 0.1f, 0.01f, XS("%.15g"));
			CVar(DragZ, XS("Drag Z"), 0.009962f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 0.1f, 0.01f, XS("%.15g"));
			CVar(AngularDragX, XS("Angular drag X"), 0.003618f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 0.1f, 0.01f, XS("%.15g"));
			CVar(AngularDragY, XS("Angular drag Y"), 0.001514f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 0.1f, 0.01f, XS("%.15g"));
			CVar(AngularDragZ, XS("Angular drag Z"), 0.001514f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 0.1f, 0.01f, XS("%.15g"));
			CVar(MaxVelocity, XS("Max velocity"), 2000.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 4000.f, 50.f);
			CVar(MaxAngularVelocity, XS("Max angular velocity"), 3600.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 7200.f, 50.f);
		NAMESPACE_END(ProjectileTrajectory)
	NAMESPACE_END(Visuals)

	NAMESPACE_BEGIN(Misc)
		NAMESPACE_BEGIN(Movement)
			CVarEnum(AutoStrafe, XS("Auto strafe"), 0, NONE, nullptr,
				VA_LIST(XS("Off"), XS("Legit"), XS("Directional")),
				Off, Legit, Directional);
			CVar(AutoStrafeTurnScale, XS("Auto strafe turn scale"), 0.5f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 1.f, 0.1f);
			CVar(AutoStrafeMaxDelta, XS("Auto strafe max delta"), 180.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 180.f, 5.f);
			CVar(Bunnyhop, XS("Bunnyhop"), false);
			CVar(EdgeJump, XS("Edge jump"), false);
			CVar(AutoJumpbug, XS("Auto jumpbug"), false);
			CVar(NoPush, XS("No push"), false);
			CVar(AutoRocketJump, XS("Auto rocket jump"), false);
			CVar(AutoCTap, XS("Auto ctap"), false);
			CVar(FastStop, XS("Fast stop"), false);
			CVar(FastAccelerate, XS("Fast accelerate"), false);
			CVar(DuckSpeed, XS("Duck speed"), false);
			CVar(MovementLock, XS("Movement lock"), false);
			CVar(BreakJump, XS("Break jump"), false);
			CVar(ShieldTurnRate, XS("Shield turn rate"), false);

			CVar(AutoRocketJumpChokeGrounded, XS("Choke grounded"), 1, NOSAVE | DEBUGVAR, 0, 3);
			CVar(AutoRocketJumpChokeAir, XS("Choke air"), 1, NOSAVE | DEBUGVAR, 0, 3);
			CVar(AutoRocketJumpSkipGround, XS("Skip grounded"), 0, NOSAVE | DEBUGVAR, 0, 3);
			CVar(AutoRocketJumpSkipAir, XS("Skip air"), 1, NOSAVE | DEBUGVAR, 0, 3);
			CVar(AutoRocketJumpTimingOffset, XS("Timing offset"), 0, NOSAVE | DEBUGVAR, 0, 3);
			CVar(AutoRocketJumpApplyAbove, XS("Apply offset above"), 0, NOSAVE | DEBUGVAR, 0, 10);
		NAMESPACE_END(Movement)

		NAMESPACE_BEGIN(Automation)
			CVarEnum(AntiBackstab, XS("Anti-backstab"), 0, NONE, nullptr,
				VA_LIST(XS("Off"), XS("Yaw"), XS("Pitch"), XS("Fake")),
				Off, Yaw, Pitch, Fake);
			CVar(AntiAFK, XS("Anti-AFK"), false);
			CVar(AntiAutobalance, XS("Anti-autobalance"), false);
			CVar(TauntControl, XS("Taunt control"), false);
			CVar(KartControl, XS("Kart control"), false);
			CVar(AutoF2Ignored, XS("Auto F2 ignored"), false);
			CVar(AutoF1Priority, XS("Auto F1 priority"), false);
			CVar(AcceptItemDrops, XS("Auto accept item drops"), false);
		NAMESPACE_END(Automation)

		NAMESPACE_BEGIN(Exploits)
			CVar(PureBypass, XS("Pure bypass"), false);
			CVar(CheatsBypass, XS("Cheats bypass"), false);
			CVar(UnlockCVars, XS("Unlock CVars"), false);
			CVar(EquipRegionUnlock, XS("Equip region unlock"), false);
			CVar(BackpackExpander, XS("Backpack expander"), false);
			CVar(NoisemakerSpam, XS("Noisemaker spam"), false);
			CVar(PingReducer, XS("Ping reducer"), false);
			CVar(PingTarget, XS("cl_cmdrate"), 1, SLIDER_CLAMP, 1, 66);
		NAMESPACE_END(Exploits)

		NAMESPACE_BEGIN(Game)
			CVar(AntiCheatCompatibility, XS("Anti-cheat compatibility"), false);
			CVar(F2PChatBypass, XS("F2P chat bypass"), false);
			CVar(NetworkFix, XS("Network fix"), false);
			CVar(SetupBonesOptimization, XS("Bones optimization"), false);

			CVar(AntiCheatCritHack, XS("Anti-cheat crit hack"), false, NOSAVE | DEBUGVAR);
		NAMESPACE_END(Game)

		NAMESPACE_BEGIN(Queueing)
			CVarEnum(ForceRegions, XS("Force regions"), 0b0, DROPDOWN_MULTI, nullptr, // i'm not sure all of these are actually used for tf2 servers
				VA_LIST(XS("Atlanta"), XS("Chicago"), XS("Dallas"), XS("Los Angeles"), XS("Seattle"), XS("Virginia"), XS("##Divider"), XS("Amsterdam"), XS("Falkenstein"), XS("Frankfurt"), XS("Helsinki"), XS("London"), XS("Madrid"), XS("Paris"), XS("Stockholm"), XS("Vienna"), XS("Warsaw"), XS("##Divider"), XS("Buenos Aires"), XS("Lima"), XS("Santiago"), XS("Sao Paulo"), XS("##Divider"), XS("Chennai"), XS("Dubai"), XS("Hong Kong"), XS("Mumbai"), XS("Seoul"), XS("Singapore"), XS("Tokyo"), XS("##Divider"), XS("Sydney"), XS("##Divider"), XS("Johannesburg")),
				// North America
				ATL = 1 << 0, // Atlanta
				ORD = 1 << 1, // Chicago
				DFW = 1 << 2, // Dallas
				LAX = 1 << 3, // Los Angeles
				SEA = 1 << 4, // Seattle (+DC_EAT?)
				IAD = 1 << 5, // Virginia
				// Europe
				AMS = 1 << 6, // Amsterdam
				FSN = 1 << 7, // Falkenstein
				FRA = 1 << 8, // Frankfurt
				HEL = 1 << 9, // Helsinki
				LHR = 1 << 10, // London
				MAD = 1 << 11, // Madrid
				PAR = 1 << 12, // Paris
				STO = 1 << 13, // Stockholm
				VIE = 1 << 14, // Vienna
				WAW = 1 << 15, // Warsaw
				// South America
				EZE = 1 << 16, // Buenos Aires
				LIM = 1 << 17, // Lima
				SCL = 1 << 18, // Santiago
				GRU = 1 << 19, // Sao Paulo
				// Asia
				MAA = 1 << 20, // Chennai
				DXB = 1 << 21, // Dubai
				HKG = 1 << 22, // Hong Kong
				BOM = 1 << 23, // Mumbai
				SEO = 1 << 24, // Seoul
				SGP = 1 << 25, // Singapore
				TYO = 1 << 26, // Tokyo
				// Australia
				SYD = 1 << 27, // Sydney
				// Africa
				JNB = 1 << 28, // Johannesburg
			);
			CVar(ExtendQueue, XS("Extend queue"), false);
			CVar(AutoCasualQueue, XS("Auto casual queue"), false);
		NAMESPACE_END(Queueing)

		NAMESPACE_BEGIN(MannVsMachine, Mann vs. Machine)
			CVar(InstantRespawn, XS("Instant respawn"), false);
			CVar(InstantRevive, XS("Instant revive"), false);
			CVar(AllowInspect, XS("Allow inspect"), false);
		NAMESPACE_END(Sound)

		NAMESPACE_BEGIN(Sound)
			CVarEnum(Block, VA_LIST(XS("Block"), XS("Sound block")), 0b0000, DROPDOWN_MULTI, nullptr,
				VA_LIST(XS("Footsteps"), XS("Noisemaker"), XS("Frying pan"), XS("Water")),
				Footsteps = 1 << 0, Noisemaker = 1 << 1, FryingPan = 1 << 2, Water = 1 << 3);
			CVar(HitsoundAlways, XS("Hitsound always"), false);
			CVar(RemoveDSP, XS("Remove DSP"), false);
			CVar(GiantWeaponSounds, XS("Giant weapon sounds"), false);
		NAMESPACE_END(Sound)
	NAMESPACE_END(Misc)

	NAMESPACE_BEGIN(Logging)
		CVarEnum(Logs, XS("Logs"), 0b0000011, DROPDOWN_MULTI, XS("Off"),
			VA_LIST(XS("Vote start"), XS("Vote cast"), XS("Class changes"), XS("Damage"), XS("Cheat detection"), XS("Tags"), XS("Aliases"), XS("Resolver")),
			VoteStart = 1 << 0, VoteCast = 1 << 1, ClassChanges = 1 << 2, Damage = 1 << 3, CheatDetection = 1 << 4, Tags = 1 << 5, Aliases = 1 << 6, Resolver = 1 << 7);
		Enum(LogTo, Toasts = 1 << 0, Chat = 1 << 1, Party = 1 << 2, Console = 1 << 3, Menu = 1 << 4, Debug = 1 << 5);
		CVarEnum(NotificationPosition, XS("Notification position"), 0, VISUAL, nullptr,
			VA_LIST(XS("Top left"), XS("Top right"), XS("Bottom left"), XS("Bottom right")),
			TopLeft, TopRight, BottomLeft, BottomRight);
		CVar(Lifetime, XS("Notification time"), 5.f, VISUAL, 0.5f, 5.f, 0.5f);

		NAMESPACE_BEGIN(VoteStart, Logging)
			CVarValues(LogTo, VA_LIST(XS("Log to"), XS("Vote start log to")), 0b000001, DROPDOWN_MULTI, nullptr,
				XS("Toasts"), XS("Chat"), XS("Party"), XS("Console"), XS("Menu"), XS("Debug"));
		NAMESPACE_END(VoteStart)

		NAMESPACE_BEGIN(VoteCast, Logging)
			CVarValues(LogTo, VA_LIST(XS("Log to"), XS("Vote cast log to")), 0b000001, DROPDOWN_MULTI, nullptr,
				XS("Toasts"), XS("Chat"), XS("Party"), XS("Console"), XS("Menu"), XS("Debug"));
		NAMESPACE_END(VoteCast)

		NAMESPACE_BEGIN(ClassChange, Logging)
			CVarValues(LogTo, VA_LIST(XS("Log to"), XS("Class change log to")), 0b000001, DROPDOWN_MULTI, nullptr,
				XS("Toasts"), XS("Chat"), XS("Party"), XS("Console"), XS("Menu"), XS("Debug"));
		NAMESPACE_END(ClassChange)

		NAMESPACE_BEGIN(Damage, Logging)
			CVarValues(LogTo, VA_LIST(XS("Log to"), XS("Damage log to")), 0b000001, DROPDOWN_MULTI, nullptr,
				XS("Toasts"), XS("Chat"), XS("Party"), XS("Console"), XS("Menu"), XS("Debug"));
		NAMESPACE_END(Damage)

		NAMESPACE_BEGIN(CheatDetection, Logging)
			CVarValues(LogTo, VA_LIST(XS("Log to"), XS("Cheat detection log to")), 0b000001, DROPDOWN_MULTI, nullptr,
				XS("Toasts"), XS("Chat"), XS("Party"), XS("Console"), XS("Menu"), XS("Debug"));
		NAMESPACE_END(CheatDetection)

		NAMESPACE_BEGIN(Tags, Logging)
			CVarValues(LogTo, VA_LIST(XS("Log to"), XS("Tags log to")), 0b000001, DROPDOWN_MULTI, nullptr,
				XS("Toasts"), XS("Chat"), XS("Party"), XS("Console"), XS("Menu"), XS("Debug"));
		NAMESPACE_END(Tags)

		NAMESPACE_BEGIN(Aliases, Logging)
			CVarValues(LogTo, VA_LIST(XS("Log to"), XS("Aliases log to")), 0b000001, DROPDOWN_MULTI, nullptr,
				XS("Toasts"), XS("Chat"), XS("Party"), XS("Console"), XS("Menu"), XS("Debug"));
		NAMESPACE_END(Aliases)

		NAMESPACE_BEGIN(Resolver, Logging)
			CVarValues(LogTo, VA_LIST(XS("Log to"), XS("Resolver log to")), 0b000001, DROPDOWN_MULTI, nullptr,
				XS("Toasts"), XS("Chat"), XS("Party"), XS("Console"), XS("Menu"), XS("Debug"));
		NAMESPACE_END(Resolver)
	NAMESPACE_END(Logging)

	NAMESPACE_BEGIN(Debug)
		CVar(Info, XS("Debug info"), false, NOSAVE);
		CVar(Logging, XS("Debug logging"), false, NOSAVE);
		CVar(Options, XS("Debug options"), false, NOSAVE);
		CVar(DrawHitboxes, XS("Show hitboxes"), false, NOSAVE);
		CVar(AntiAimLines, XS("Antiaim lines"), false);
		CVar(CrashLogging, XS("Crash logging"), true);
#ifdef DEBUG_TRACES
		CVar(VisualizeTraces, XS("Visualize traces"), false, NOSAVE);
		CVar(VisualizeTraceHits, XS("Visualize trace hits"), false, NOSAVE);
#endif
	NAMESPACE_END(Debug)
NAMESPACE_END(Vars)