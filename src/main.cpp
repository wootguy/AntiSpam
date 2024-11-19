#include "extdll.h"
#include "util.h"
#include "PluginHooks.h"
#include "PluginManager.h"
#include "Scheduler.h"
#include "CommandArgs.h"
#include "CBasePlayer.h"
#include <string>

using namespace std;

float getSpamThreshold();

cvar_t* g_safeChatDelay;     // can spam messages at this rate and never be throttled
cvar_t* g_spamAllowed;       // higher = more spam allowed before chats are throttled
cvar_t* g_safeRejoinDelay;   // can rejoin at this speed and never be throttled
cvar_t* g_rejoinSpamAllowed; // number of rejoins allowed before throttling

HLCOOP_PLUGIN_HOOKS g_hooks;

struct SpamState {
    float lastChat = -999;
    float spam = 0;
    bool isBlocked = false;
    bool notifyNextMsg = false; // notify player when they can send a message

    float lastJoin = 0;
    int spamJoins = 0;
    string ipAddr;

    SpamState() {}

    // time in seconds needed to wait for next message or else the chat will be blocked
    float getNextSafeMessageTime() {
        float timeSinceLastChat = g_engfuncs.pfnTime() - lastChat;
        float spamLeft = getSpamThreshold() - spam;
        float safeDelay = (g_safeChatDelay->value * 2 - (spamLeft + timeSinceLastChat)) * 0.5f;
        return safeDelay;
    }
};

unordered_map<string,SpamState> g_player_states;
unordered_map<string,string> g_nickname_ips; // maps a nickname to an ip address. Only valid between a conect and join hook

SpamState* getSpamState(CBasePlayer* plr)
{
    string steamId = plr->GetSteamID();
    if (steamId == "STEAM_ID_LAN") {
        steamId = plr->DisplayName();
    }

    if (g_player_states.find(steamId) == g_player_states.end()) {
        g_player_states[steamId] = SpamState();
    }

    return &g_player_states[steamId];
}

SpamState* getSpamState(string ipAddr) {
    for (auto iter : g_player_states)
    {
        SpamState& state = iter.second;
        if (state.ipAddr == ipAddr) {
            return &g_player_states[iter.first];
        }
    }

    return NULL;
}

// convert consecutive spam messages setting into a spam limit
float getSpamThreshold() {
	return g_spamAllowed->value;
}

void cooldown_chat_spam_counters() {
	//float spamThreshold = getSpamThreshold();

	for (int i = 1; i <= gpGlobals->maxClients; i++) {
		CBasePlayer* plr = UTIL_PlayerByIndex(i);

		if (!plr) {
			continue;
		}

		SpamState* state = getSpamState(plr);
		if (state == NULL)
			continue;

		if (state->spam > 0) {
			state->spam -= 1;
			//ALERT(at_console, "SPAM " + state->spam + " " + spamThreshold);
		}
		else {
			state->spam = 0;
		}

		if (state->notifyNextMsg && state->getNextSafeMessageTime() <= 0) {
			state->notifyNextMsg = false;
			state->isBlocked = false;

			UTIL_ClientPrint(plr, print_chat, "[AntiSpam] You can send a message now.\n");
		}
	}
}

void cooldown_join_spam_counters() {
	for (auto iter : g_player_states) {
		SpamState& state = g_player_states[iter.first];
		if (state.spamJoins > 0) {
			state.spamJoins -= 1;
		}
	}
}

string getIpWithoutPort(string ip) {
	if (ip.find(":") != std::string::npos)
		return ip.substr(0, ip.find(":"));
	else
		return ip;
}

HOOK_RETURN_DATA ClientConnectHook(edict_t* pEntity, const char* pszName, const char* pszAddress, char szRejectReason[128]) {
	string ip = getIpWithoutPort(pszAddress);
	string nick = pszName;
	g_nickname_ips[nick] = ip;

	SpamState* state = getSpamState(ip);

	if (state && state->spamJoins >= g_rejoinSpamAllowed->value - 1) {
		const char* sReason = UTIL_VarArgs("[AntiSpam] Your rejoins are spamming the chat. Wait %d seconds.", (int)g_safeRejoinDelay->value);
		strncpy(szRejectReason, sReason, 128);
		//SayTextAll("[AntiSpam] " + sNick + " connection rejected due to rejoin spam.\n");
		return HOOK_HANDLED_OVERRIDE(0);
	}
	else {
		ALERT(at_console, "Unknown IP address: %s\n", ip.c_str());
	}

	return HOOK_CONTINUE;
}

HOOK_RETURN_DATA ClientJoin(CBasePlayer* plr) {
	SpamState* state = getSpamState(plr);

	if (!state) {
		return HOOK_CONTINUE;
	}

	float timeSinceLastJoin = g_engfuncs.pfnTime() - state->lastJoin;

	if (timeSinceLastJoin < g_safeRejoinDelay->value) {
		state->spamJoins += 1;
	}

	state->lastJoin = g_engfuncs.pfnTime();

	string netname = STRING(plr->pev->netname);

	if (g_nickname_ips.find(netname) != g_nickname_ips.end()) {
		state->ipAddr = g_nickname_ips[netname];
		g_nickname_ips.erase(netname);
		ALERT(at_console, "%s\n", (netname + " = " + state->ipAddr).c_str());
	}

	return HOOK_CONTINUE;
}

HOOK_RETURN_DATA ClientCommand(CBasePlayer* plr) {
	CommandArgs args;
	args.loadArgs();

	if (!IsValidPlayer(plr->edict()) || args.isConsoleCmd || args.ArgC() < 1)
		return HOOK_CONTINUE;

	float safeDelay = g_safeChatDelay->value;
	float throttleDelay = safeDelay * 2; // account for cooldown loop
	float spamThreshold = getSpamThreshold();

	SpamState* state = getSpamState(plr);
	if (!state) {
		return HOOK_CONTINUE;
	}

	float timeSinceLastChat = g_engfuncs.pfnTime() - state->lastChat;

	if (!state->isBlocked) {
		state->lastChat = g_engfuncs.pfnTime();

		if (timeSinceLastChat < throttleDelay) {
			state->spam += throttleDelay - timeSinceLastChat;
			if (timeSinceLastChat < 1.0f) {
				state->spam += throttleDelay * 2; // flooding the chat
			}
			state->spam = V_min(spamThreshold, state->spam); // never wait more than the safe message delay
		}

		if (state->spam >= spamThreshold) {
			state->isBlocked = true;
			state->notifyNextMsg = true;
			float waitTime = ceilf(state->getNextSafeMessageTime());
			UTIL_ClientPrintAll(print_console, UTIL_VarArgs("[AntiSpam] %s can't send messages for %d seconds.\n", 
					STRING(plr->pev->netname), waitTime));
		}
	}

	if (state->isBlocked) {
		float waitTime = ceilf(state->getNextSafeMessageTime());
		UTIL_ClientPrint(plr, print_chat, UTIL_VarArgs("[AntiSpam] Chat blocked. Wait %d seconds.\n", (int)waitTime));
		return HOOK_HANDLED_OVERRIDE(0);
	}

	float safeMessageDelay = state->getNextSafeMessageTime();

	if (safeMessageDelay > 0) {
		if (safeMessageDelay >= 0.5f) {
			UTIL_ClientPrint(plr, print_chat, UTIL_VarArgs("[AntiSpam] Wait %d seconds.\n", (int)ceilf(safeMessageDelay)));
		}
		if (safeMessageDelay > 2.0f) {
			state->notifyNextMsg = true; // hard to time messages beyond a few seconds
		}
	}

	//ALERT(at_console, "SPAM " + state->spam + " / " + spamThreshold);

	return HOOK_CONTINUE;
}

HOOK_RETURN_DATA MapInit() {
    g_player_states.clear();
    g_nickname_ips.clear();
	return HOOK_CONTINUE;
}

extern "C" int DLLEXPORT PluginInit() {
    g_safeChatDelay = RegisterPluginCVar("antispam.safe_chat_delay", "5", 5, 0);
    g_spamAllowed = RegisterPluginCVar("antispam.spam_threshold", "120", 120, 0);
    g_safeRejoinDelay = RegisterPluginCVar("antispam.safe_rejoin_delay", "60", 60, 0);
    g_rejoinSpamAllowed = RegisterPluginCVar("antispam.rejoin_spam_allowed", "3", 3, 0);

	g_hooks.pfnClientCommand = ClientCommand;
	g_hooks.pfnClientConnect = ClientConnectHook;
	g_hooks.pfnClientPutInServer = ClientJoin;
	g_hooks.pfnMapInit = MapInit;

    g_Scheduler.SetInterval(cooldown_chat_spam_counters, 1.0f, -1);
    g_Scheduler.SetInterval(cooldown_join_spam_counters, g_safeRejoinDelay->value, -1);

	return RegisterPlugin(&g_hooks);
}

extern "C" void DLLEXPORT PluginExit() {
}