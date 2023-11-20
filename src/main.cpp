#include "mmlib.h"
#include <string>

using namespace std;

// Description of plugin
plugin_info_t Plugin_info = {
    META_INTERFACE_VERSION,	// ifvers
    "AntiSpam",	// name
    "1.0",	// version
    __DATE__,	// date
    "AntiSpam",	// author
    "github",	// url
    "ANTISPAM",	// logtag, all caps please
    PT_ANYTIME,	// (when) loadable
    PT_ANYPAUSE	// (when) unloadable
};

float getSpamThreshold();

cvar_t* g_safeChatDelay;     // can spam messages at this rate and never be throttled
cvar_t* g_spamAllowed;       // higher = more spam allowed before chats are throttled
cvar_t* g_safeRejoinDelay;   // can rejoin at this speed and never be throttled
cvar_t* g_rejoinSpamAllowed; // number of rejoins allowed before throttling

const int gmsgSayText = 76;

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

map<string,SpamState> g_player_states;
map<string,string> g_nickname_ips; // maps a nickname to an ip address. Only valid between a conect and join hook

SpamState* getSpamState(edict_t* plr)
{
    string steamId = getPlayerUniqueId(plr);
    if (steamId == "STEAM_ID_LAN") {
        steamId = plr->v.netname;
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

void SayText(edict_t* target, string msg) {
	MESSAGE_BEGIN(MSG_ONE, gmsgSayText, NULL, target);
	WRITE_BYTE(0);
	WRITE_STRING(msg.c_str());
	MESSAGE_END();
}

void SayTextAll(edict_t* target, string msg) {
	MESSAGE_BEGIN(MSG_ALL, gmsgSayText, NULL);
	WRITE_BYTE(0);
	WRITE_STRING(msg.c_str());
	MESSAGE_END();
}

void cooldown_chat_spam_counters() {
	float spamThreshold = getSpamThreshold();

	for (int i = 1; i <= gpGlobals->maxClients; i++) {
		edict_t* plr = INDEXENT(i);

		if (!isValidPlayer(plr)) {
			continue;
		}

		SpamState* state = getSpamState(plr);
		if (state == NULL)
			continue;

		if (state->spam > 0) {
			state->spam -= 1;
			//println("SPAM " + state->spam + " " + spamThreshold);
		}
		else {
			state->spam = 0;
		}

		if (state->notifyNextMsg && state->getNextSafeMessageTime() <= 0) {
			state->notifyNextMsg = false;
			state->isBlocked = false;

			SayText(plr, "[AntiSpam] You can send a message now.\n");
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
	if (ip.find(":") != -1)
		return ip.substr(0, ip.find(":"));
	else
		return ip;
}

int ClientConnect(edict_t* pEntity, const char* pszName, const char* pszAddress, char szRejectReason[128]) {
	string ip = getIpWithoutPort(pszAddress);
	string nick = pszName;
	g_nickname_ips[nick] = ip;

	SpamState* state = getSpamState(ip);

	if (state && state->spamJoins >= g_rejoinSpamAllowed->value - 1) {
		const char* sReason = UTIL_VarArgs("[AntiSpam] Your rejoins are spamming the chat. Wait %d seconds.", (int)g_safeRejoinDelay->value);
		strncpy(szRejectReason, sReason, 128);
		//SayTextAll("[AntiSpam] " + sNick + " connection rejected due to rejoin spam.\n");
		RETURN_META_VALUE(MRES_SUPERCEDE, 0);
	}
	else {
		println("[AntiSpam] Unknown IP address: " + ip);
	}
	RETURN_META_VALUE(MRES_IGNORED, 0);
}

void ClientJoin(edict_t* plr) {
	SpamState* state = getSpamState(plr);

	if (!state) {
		RETURN_META(MRES_IGNORED);
	}

	float timeSinceLastJoin = g_engfuncs.pfnTime() - state->lastJoin;

	if (timeSinceLastJoin < g_safeRejoinDelay->value) {
		state->spamJoins += 1;
	}

	state->lastJoin = g_engfuncs.pfnTime();

	string netname = STRING(plr->v.netname);

	if (g_nickname_ips.find(netname) != g_nickname_ips.end()) {
		state->ipAddr = g_nickname_ips[netname];
		g_nickname_ips.erase(netname);
		println("[AntiSpam] " + netname + " = " + state->ipAddr);
	}

	RETURN_META(MRES_IGNORED);
}

void ClientCommand(edict_t* plr) {
	CommandArgs args = CommandArgs();
	args.loadArgs();

	if (!isValidPlayer(plr) || args.isConsoleCmd || args.ArgC() < 1)
		RETURN_META(MRES_IGNORED);

	float safeDelay = g_safeChatDelay->value;
	float throttleDelay = safeDelay * 2; // account for cooldown loop
	float spamThreshold = getSpamThreshold();

	SpamState* state = getSpamState(plr);
	if (!state) {
		RETURN_META(MRES_IGNORED);
	}

	float timeSinceLastChat = g_engfuncs.pfnTime() - state->lastChat;

	if (!state->isBlocked) {
		state->lastChat = g_engfuncs.pfnTime();

		if (timeSinceLastChat < throttleDelay) {
			state->spam += throttleDelay - timeSinceLastChat;
			if (timeSinceLastChat < 1.0f) {
				state->spam += throttleDelay * 2; // flooding the chat
			}
			state->spam = Min(spamThreshold, state->spam); // never wait more than the safe message delay
		}

		if (state->spam >= spamThreshold) {
			state->isBlocked = true;
			state->notifyNextMsg = true;
			float waitTime = ceilf(state->getNextSafeMessageTime());
			//SayTextAll(HUD_PRINTNOTIFY, "[AntiSpam] " + plr.pev.netname + " can't send messages for " + waitTime + " seconds.\n");
		}
	}

	if (state->isBlocked) {
		float waitTime = ceilf(state->getNextSafeMessageTime());
		SayText(plr, UTIL_VarArgs("[AntiSpam] Chat blocked. Wait %d seconds.\n", (int)waitTime));
		RETURN_META(MRES_SUPERCEDE);
	}

	float safeMessageDelay = state->getNextSafeMessageTime();

	if (safeMessageDelay > 0) {
		if (safeMessageDelay >= 0.5f) {
			SayText(plr, UTIL_VarArgs("[AntiSpam] Wait %d seconds.\n", (int)ceilf(safeMessageDelay)));
		}
		if (safeMessageDelay > 2.0f) {
			state->notifyNextMsg = true; // hard to time messages beyond a few seconds
		}
	}

	//println("SPAM " + state->spam + " / " + spamThreshold);

	RETURN_META(MRES_IGNORED);
}

void MapInit(edict_t* edict_list, int edictCount, int clientMax) {
    g_player_states.clear();
    g_nickname_ips.clear();
}

void StartFrame() {
	g_Scheduler.Think();
	RETURN_META(MRES_IGNORED);
}

void PluginInit() {
    g_safeChatDelay = RegisterCVar("antispam.safe_chat_delay", "5", 5, 0);
    g_spamAllowed = RegisterCVar("antispam.spam_threshold", "120", 120, 0);
    g_safeRejoinDelay = RegisterCVar("antispam.safe_rejoin_delay", "60", 60, 0);
    g_rejoinSpamAllowed = RegisterCVar("antispam.rejoin_spam_allowed", "3", 3, 0);

    g_dll_hooks.pfnClientCommand = ClientCommand;
    g_dll_hooks.pfnClientConnect = ClientConnect;
    g_dll_hooks.pfnClientPutInServer = ClientJoin;
    g_dll_hooks.pfnServerActivate = MapInit;
    g_dll_hooks.pfnStartFrame = StartFrame;

    g_Scheduler.SetInterval(cooldown_chat_spam_counters, 1.0f, -1);
    g_Scheduler.SetInterval(cooldown_join_spam_counters, g_safeRejoinDelay->value, -1);
}



void PluginExit() {
}