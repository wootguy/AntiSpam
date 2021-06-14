void print(string text) { g_Game.AlertMessage( at_console, text); }
void println(string text) { print(text + "\n"); }

class SpamState {
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
		float timeSinceLastChat = g_EngineFuncs.Time() - lastChat;
		float spamLeft = getSpamThreshold() - spam;
		float safeDelay = (g_safeChatDelay.GetFloat()*2 - (spamLeft + timeSinceLastChat)) * 0.5f;
		return safeDelay;
	}
}

dictionary g_player_states;
dictionary g_nickname_ips; // maps a nickname to an ip address. Only valid between a conect and join hook

CCVar@ g_safeChatDelay;     // can spam messages at this rate and never be throttled
CCVar@ g_spamAllowed;       // higher = more spam allowed before chats are throttled
CCVar@ g_safeRejoinDelay;   // can rejoin at this speed and never be throttled
CCVar@ g_rejoinSpamAllowed; // number of rejoins allowed before throttling

void PluginInit() {
	g_Module.ScriptInfo.SetAuthor( "w00tguy" );
	g_Module.ScriptInfo.SetContactInfo( "https://github.com/wootguy" );
	
	g_Hooks.RegisterHook( Hooks::Player::ClientSay, @ClientSay );
	g_Hooks.RegisterHook(Hooks::Player::ClientConnected, @ClientConnect);
	g_Hooks.RegisterHook( Hooks::Player::ClientPutInServer, @ClientJoin );

	@g_safeChatDelay = CCVar( "safe_chat_delay", 10.0f );
	@g_spamAllowed = CCVar( "spam_threshold", 150.0f );
	@g_safeRejoinDelay = CCVar( "safe_rejoin_delay", 60.0f );
	@g_rejoinSpamAllowed = CCVar( "rejoin_spam_allowed", 3 );
	
	g_Scheduler.SetInterval("cooldown_chat_spam_counters", 1.0f, -1);
	g_Scheduler.SetInterval("cooldown_join_spam_counters", g_safeRejoinDelay.GetInt(), -1);
}

void MapInit() {
	g_player_states.clear();
	g_nickname_ips.clear();
}

SpamState@ getSpamState(CBasePlayer@ plr)
{
	string steamId = g_EngineFuncs.GetPlayerAuthId( plr.edict() );
	if (steamId == 'STEAM_ID_LAN') {
		steamId = plr.pev.netname;
	}
	
	if ( !g_player_states.exists(steamId) ) {
		SpamState state;
		g_player_states[steamId] = state;
	}
	return cast<SpamState@>( g_player_states[steamId] );
}

SpamState@ getSpamState(string ipAddr) {
	array<string>@ spam_keys = g_player_states.getKeys();
	
	for (uint i = 0; i < spam_keys.length(); i++)
	{
		SpamState@ state = cast<SpamState@>(g_player_states[spam_keys[i]]);
		if (state.ipAddr == ipAddr) {
			return state;
		}
	}
	
	return null;
}

// convert consecutive spam messages setting into a spam limit
float getSpamThreshold() {
	return g_spamAllowed.GetFloat();
}

void cooldown_chat_spam_counters() {
	float spamThreshold = getSpamThreshold();
	
	for (int i = 1; i <= g_Engine.maxClients; i++) {
		CBasePlayer@ plr = g_PlayerFuncs.FindPlayerByIndex(i);
		
		if (plr is null or !plr.IsConnected()) {
			continue;
		}
		
		SpamState@ state = getSpamState(plr);
		
		if (state.spam > 0) {
			state.spam -= 1;
			//println("SPAM " + state.spam + " " + spamThreshold);
		} else {
			state.spam = 0;
		}
		
		if (state.notifyNextMsg && state.getNextSafeMessageTime() <= 0) {
			state.notifyNextMsg = false;
			state.isBlocked = false;
			g_PlayerFuncs.ClientPrint(plr, HUD_PRINTTALK, "[AntiSpam] You can send a message now.\n");
		}
	}
}

void cooldown_join_spam_counters() {		
	array<string>@ spam_keys = g_player_states.getKeys();
	
	for (uint i = 0; i < spam_keys.length(); i++)
	{
		SpamState@ state = cast<SpamState@>(g_player_states[spam_keys[i]]);
		if (state.spamJoins > 0) {
			state.spamJoins -= 1;
		}
	}
}

string getIpWithoutPort(string ip) {
	return ip.SubString(0, ip.Find(":"));
}

HookReturnCode ClientConnect(edict_t@ eEdict, const string &in sNick, const string &in sIp, bool &out bNoJoin, string &out sReason) {
	string ip = getIpWithoutPort(sIp);
	g_nickname_ips[sNick] = ip;
	
	SpamState@ state = getSpamState(ip);
	
	if (state !is null and state.spamJoins >= g_rejoinSpamAllowed.GetInt()-1) {
		bNoJoin = false;
		sReason = "[AntiSpam] Your rejoins are spamming the chat. Wait " + g_safeRejoinDelay.GetInt() + " seconds.";
		g_PlayerFuncs.ClientPrintAll(HUD_PRINTNOTIFY, "[AntiSpam] " + sNick + " connection rejected due to rejoin spam.\n");
	} else {
		println("[AntiSpam] Unknown IP address: " + ip);
	}
	return HOOK_CONTINUE;
}

HookReturnCode ClientJoin(CBasePlayer@ plr) {
	SpamState@ state = getSpamState(plr);
	
	float timeSinceLastJoin = g_EngineFuncs.Time() - state.lastJoin;
	
	if (timeSinceLastJoin < g_safeRejoinDelay.GetInt()) {
		state.spamJoins += 1;
	}
	
	state.lastJoin = g_EngineFuncs.Time();

	if (g_nickname_ips.exists(plr.pev.netname)) {
		g_nickname_ips.get(plr.pev.netname, state.ipAddr);
		g_nickname_ips.delete(plr.pev.netname);
		println("[AntiSpam] " + plr.pev.netname + " = " + state.ipAddr);
	}
	
	return HOOK_CONTINUE;
}

HookReturnCode ClientSay( SayParameters@ pParams ) {
	const CCommand@ pArguments = pParams.GetArguments();
	CBasePlayer@ plr = pParams.GetPlayer();
	
	if (plr is null || pArguments.ArgC() < 1)
		return HOOK_CONTINUE;

	float safeDelay = g_safeChatDelay.GetFloat();
	float throttleDelay = safeDelay*2; // account for cooldown loop
	float spamThreshold = getSpamThreshold();
	
	SpamState@ state = getSpamState(plr);
	
	float timeSinceLastChat = g_EngineFuncs.Time() - state.lastChat;
	
	if (!state.isBlocked) {
		state.lastChat = g_EngineFuncs.Time();
		
		if (timeSinceLastChat < throttleDelay) {
			state.spam += throttleDelay - timeSinceLastChat;
			if (timeSinceLastChat < 1.0f) {
				state.spam += throttleDelay; // flooding the chat
			}
			state.spam = Math.min(spamThreshold, state.spam); // never wait more than the safe message delay
		}
		
		if (state.spam >= spamThreshold) {
			state.isBlocked = true;
			state.notifyNextMsg = true;
			float waitTime = Math.Ceil(state.getNextSafeMessageTime());
			g_PlayerFuncs.ClientPrintAll(HUD_PRINTNOTIFY, "[AntiSpam] " + plr.pev.netname + " can't send messages for " + waitTime + " seconds.\n");
		}
	}
	
	if (state.isBlocked) {
		float waitTime = Math.Ceil(state.getNextSafeMessageTime());
		g_PlayerFuncs.ClientPrint(plr, HUD_PRINTTALK, "[AntiSpam] Chat blocked. Wait " + waitTime + " seconds.\n");
		pParams.ShouldHide = true;
		return HOOK_HANDLED;
	}
	
	float safeMessageDelay = state.getNextSafeMessageTime();
	
	if (safeMessageDelay > 0) {
		if (safeMessageDelay >= 0.5f) {
			g_PlayerFuncs.ClientPrint(plr, HUD_PRINTTALK, "[AntiSpam] Wait " + Math.Ceil(safeMessageDelay) + " seconds.\n");
		}
		if (safeMessageDelay > 2.0f) {
			state.notifyNextMsg = true; // hard to time messages beyond a few seconds
		}
	}
	
	//println("SPAM " + state.spam + " / " + spamThreshold);

	return HOOK_CONTINUE;
}
