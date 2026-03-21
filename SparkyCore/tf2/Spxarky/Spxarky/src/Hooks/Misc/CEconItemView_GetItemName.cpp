#include "../../SDK/SDK.h"

MAKE_SIGNATURE(CEconItemView_GetItemName, "client.dll", "40 53 48 83 EC ? 48 8B D9 C6 81 ? ? ? ? ? E8 ? ? ? ? 48 8B 8B", 0x0);

MAKE_HOOK(CEconItemView_GetItemName, S::CEconItemView_GetItemName(), const wchar_t*,
	void* thisptr)
{
	const auto result = CALL_ORIGINAL(thisptr);

	if (Vars::Visuals::Mods::CustomName.Value.empty())
		return result;

	// This hook will affect the name displayed for weapons in the HUD and loadout.
	// Since it's a client-side spoof, it will also affect ESP if enabled.
	static std::wstring wName;
	wName = SDK::ConvertUtf8ToWide(Vars::Visuals::Mods::CustomName.Value);
	return wName.c_str();
}
