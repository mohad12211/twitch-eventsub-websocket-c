# twitch-eventsub-websocket-c

Twitch's eventsub with websocket. Implemented in POSIX sockets.
Sends a Discord notification when the stream starts. Supports multiple streamers.

Only dependency is openssl.

# Usage

1. Export enviromental variables:
   - `TWITCH_OAUTH`
   - `DISCORD_TOKEN`
   - `CLIENT_ID`
2. Edit the streamers list in `main.c`, providing respectively:
   - the discord channel id for where the message will be sent (right click on channel, Copy ID)
   - streamer's broadcaster id [(how to get it)](https://discuss.dev.twitch.tv/t/how-do-i-get-the-broadcaster-id-for-a-user-via-helix/37123)
   - streamer's username

3. Run `make`

# Discord notification example
![image](https://user-images.githubusercontent.com/51754973/205462047-f40625e2-54db-445c-a750-d5eac31a5892.png)
