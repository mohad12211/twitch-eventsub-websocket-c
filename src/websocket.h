#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/types.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "../libs/cJSON.h"
#include "../libs/cJSON_Utils.h"

#define MAXLINE 4096

#define MASK_OPCODE 0x0F
#define MASK_LENGTH 0x7F

#define OPCODE_TEXT 0x1
#define OPCODE_CLOSE 0x8
#define OPCODE_PING 0x9

#define ns2(s) #s
#define ns(s) ns2(s)
#define ERROUT(...) errout(__FILE__, ns(__LINE__), __func__, __VA_ARGS__)

typedef struct {
  char *discord_room_id;
  char *broadcaster_user_id;
  char *broadcaster_name;
} streamer;

typedef struct {
  int sockfd;
  pthread_t *new_thread;
  char *url;
  SSL *ssl;
} wsclient;

typedef struct {
  unsigned int fin;
  unsigned int opcode;
  long long payload_length;
  unsigned char *payload;
} wsframe;

extern SSL_CTX *ssl_ctx;
extern const streamer streamers[];
extern const int streamers_length;
extern char *oauth;
extern char *bot_token;
extern char *client_id;
extern int enable_log;

void errout(const char *file, const char *line, const char *func, const char *format, ...);
void LOG(char *format, ...);
void init_ssl(void);
void init_keys(void);
int get_sockfd(const char *host, const char *port);
SSL *get_ssl_from_sockfd(int sockfd);
int end_with_2CRLF(char *buf, int len);
long long https_reponse(SSL *ssl, char *buf);
void create_eventsub(char *session_id, char *broadcaster_user_id);
void send_discord_notif(const streamer *streamer, char *started_at);
cJSON *get_embed_json(const streamer *streamer, char *started_at);
void get_channel_information(char *broadcaster_user_id, char *title, char *game);
void get_profile_image(char *broadcaster_name, char *profile_image);
void get_unix_time(char *started_at, char *unix_time);

long long wsclient_read(wsclient *client, void *buf, size_t length);
long long wsclient_write(wsclient *client, void *buf, size_t length);
long long wsclient_handshake_response(wsclient *client, char *buf);

wsclient *wsclient_new(char *url);
void wsclient_free(wsclient *client);
void wsclient_run(wsclient *client);
void wsclient_do_handshake(wsclient *client);
wsframe *wsclient_get_wsframe(wsclient *client);
void wsclient_handle_wsframe(wsclient *client, wsframe *frame);
void wsclient_clean_wsframe(wsclient *client, wsframe *frame);
void wsclient_send_pong(wsclient *client);
void wsclient_handle_json(wsclient *client, char *json_str);

void *wsclient_thread(void *args);
