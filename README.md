# AntiSpam
This prevents players from flooding the chat or consistently sending messages just slow enough to bypass [AntiFlood](https://github.com/alliedmodders/amxmodx/blob/master/plugins/antiflood.sma) detection. It still allows you to have some fun by spamming a few messages all at once, but then you have to wait 2 minutes (default) to do that again.

There's a "spam meter" that fills up a little bit each time you send a messages faster than 5 seconds apart (default). Sending messages less than 1 second apart fills the meter ~4x faster. The meter slowly decreases over time. This way players can chat fast or even spam for a reasonable amount of time, but not forever.

A separate meter for rejoin spam also exists. Leaving/rejoining the server too fast fills this meter up. Once full, the player has to wait 60 seconds (default) between rejoins or else they can't connect.
# CVars
`as_command antispam.safe_chat_delay 5` time in seconds that players should be waiting between messages.  
`as_command antispam.spam_threshold 120` size of the spam buffer. Larger value = more spam allowed.  
`as_command antispam.safe_rejoin_delay 60` time in seconds that players should be waiting before rejoining the server after leaving.  
`as_command antispam.rejoin_spam_allowed 3` number of fast rejoins allowed before throttling connection attempts.

# Installation
1. Copy the script to `scripts/plugins/AntiSpam.as`
1. Add this to default_plugins.txt:
```
	"plugin"
	{
		"name" "AntiSpam"
		"script" "AntiSpam"
		"concommandns" "antispam"
	}
```
