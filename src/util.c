#include "websocket.h"

#define TWITCH_HOSTNAME "eventsub.wss.twitch.tv"

SSL_CTX *ssl_ctx;
char *oauth;
char *bot_token;
char *client_id;
int enable_log;

void init_ssl(void) {
  SSL_library_init();
  SSL_load_error_strings();
  ssl_ctx = SSL_CTX_new(SSLv23_method());
  if (ssl_ctx == NULL)
    ERROUT("Error in SSL_CTX_new");
}

void init_keys(void) {
  if ((oauth = getenv("TWITCH_OAUTH")) == NULL)
    ERROUT("No TWITCH_OAUTH provided");
  if ((bot_token = getenv("DISCORD_TOKEN")) == NULL)
    ERROUT("No DISCORD_TOKEN provided");
  if ((client_id = getenv("CLIENT_ID")) == NULL)
    ERROUT("No CLIENT_ID provided");
  enable_log = getenv("ENABLE_LOG") != NULL;
  oauth = strdup(oauth);
}

SSL *get_ssl_from_sockfd(int sockfd) {
  SSL *ssl = SSL_new(ssl_ctx);
  SSL_set_tlsext_host_name(ssl, TWITCH_HOSTNAME);
  if (ssl == NULL)
    ERROUT("Error in ssl_new");
  if (SSL_set_fd(ssl, sockfd) != 1)
    ERROUT("Error in ssl_set_fd");
  if (SSL_connect(ssl) != 1)
    ERROUT("Error in ssl_connect");
  return ssl;
}

int end_with_2CRLF(char *buf, int length) {
  return buf[length - 4] == '\r' && buf[length - 3] == '\n' && buf[length - 2] == '\r' && buf[length - 1] == '\n';
}

int get_sockfd(const char *host, const char *port) {
  struct addrinfo hints, *servinfo, *p;
  int return_value, sockfd = 0;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if ((return_value = getaddrinfo(host, port, &hints, &servinfo)) != 0)
    ERROUT("Error in getaddrinfo: %s", gai_strerror(return_value));
  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      continue;
    }
    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      continue;
    }
    break;
  }
  freeaddrinfo(servinfo);
  if (p == NULL)
    ERROUT("No address found");
  return sockfd;
}

void create_eventsub(char *session_id, char *broadcaster_user_id) {
  if (oauth == NULL)
    ERROUT("No TWITCH_OAUTH provided");
  printf("[INFO]: Sending subscription POST request\n");

  cJSON *data_json = cJSON_CreateObject();
  cJSON_AddStringToObject(data_json, "type", "stream.online");
  cJSON_AddStringToObject(data_json, "version", "1");

  cJSON *condition = cJSON_CreateObject();
  cJSON_AddStringToObject(condition, "broadcaster_user_id", broadcaster_user_id);
  cJSON_AddItemToObject(data_json, "condition", condition);

  cJSON *transport = cJSON_CreateObject();
  cJSON_AddStringToObject(transport, "method", "websocket");
  cJSON_AddStringToObject(transport, "session_id", session_id);
  cJSON_AddItemToObject(data_json, "transport", transport);

  static const char port[] = "443";
  static const char host[] = "api.twitch.tv";
  static const char format[] = "POST /helix/eventsub/subscriptions HTTP/1.0\r\n"
                               "Host: api.twitch.tv\r\n"
                               "authorization: Bearer %s\r\n"
                               "client-id: %s\r\n"
                               "content-type: application/json\r\n"
                               "content-length: %d\r\n\r\n"
                               "%s";
  char *data = cJSON_PrintUnformatted(data_json);
  int content_length = strlen(data);
  int request_length =
      strlen(format) + strlen(oauth) + strlen(client_id) + (int)(floor(log10(content_length)) + strlen(data));
  char request[request_length + 1];
  sprintf(request, format, oauth, client_id, content_length, data);
  request_length = strlen(request); // get exact length

  int sockfd = get_sockfd(host, port);
  SSL *ssl = get_ssl_from_sockfd(sockfd);
  if (SSL_write(ssl, request, request_length) != request_length)
    ERROUT("Error when writing");
  char response[MAXLINE];
  if (https_reponse(ssl, response) <= 0)
    ERROUT("Error when reading");
  LOG("\n[INFO]: POST response:\n%s\n", response);

  SSL_shutdown(ssl);
  SSL_free(ssl);
  if (close(sockfd) == -1)
    ERROUT("failed to close sockfd");
  free(data);
  cJSON_Delete(data_json);
}

void send_discord_notif(const streamer *stream, char *started_at) {
  printf("[INFO]: Sending Discord notification\n");
  static const char port[] = "443";
  static const char host[] = "discordapp.com";
  static const char format[] = "POST /api/channels/%s/messages HTTP/1.0\r\n"
                               "Host: discordapp.com\r\n"
                               "authorization: Bot %s\r\n"
                               "content-type: application/json\r\n"
                               "content-length: %d\r\n\r\n"
                               "%s";
  cJSON *data_json = cJSON_CreateObject();
  cJSON *embed_array = cJSON_CreateArray();
  cJSON *embed_json = get_embed_json(stream, started_at);
  cJSON_AddItemToArray(embed_array, embed_json);
  cJSON_AddItemToObject(data_json, "embeds", embed_array);
  char message[1024];
  sprintf(message, "@everyone %s is live on https://www.twitch.tv/%s", stream->broadcaster_name,
          stream->broadcaster_name);
  cJSON_AddStringToObject(data_json, "content", message);
  char *data = cJSON_PrintUnformatted(data_json);

  int content_length = strlen(data);
  int request_length = strlen(format) + strlen(stream->discord_room_id) + strlen(bot_token) +
                       (int)(floor(log10(content_length)) + content_length);
  char request[request_length + 1];
  sprintf(request, format, stream->discord_room_id, bot_token, content_length, data);
  request_length = strlen(request); // get exact length

  int sockfd = get_sockfd(host, port);
  SSL *ssl = get_ssl_from_sockfd(sockfd);
  if (SSL_write(ssl, request, request_length) != request_length)
    ERROUT("Error when writing");
  char response[MAXLINE];
  if (https_reponse(ssl, response) <= 0)
    ERROUT("Error when reading");
  LOG("\n[INFO]: POST response:\n%s\n", response);

  SSL_shutdown(ssl);
  SSL_free(ssl);
  if (close(sockfd) == -1)
    ERROUT("failed to close sockfd");
  free(data);
  cJSON_Delete(data_json);
}

cJSON *get_embed_json(const streamer *stream, char *started_at) {
  cJSON *embed_json = cJSON_CreateObject();

  char title[1024];
  char game[1024];
  get_channel_information(stream->broadcaster_user_id, title, game);
  if (strlen(game) == 0)
    strcpy(game, "Unspecified");
  char profile_image[1024];
  get_profile_image(stream->broadcaster_name, profile_image);
  char twitch_url[strlen("https://www.twitch.tv/") + strlen(stream->broadcaster_name) + 1];
  sprintf(twitch_url, "%s%s", "https://www.twitch.tv/", stream->broadcaster_name);
  char thumbnail_url[512];
  sprintf(thumbnail_url, "%s%s%s%lld", "https://static-cdn.jtvnw.net/previews-ttv/live_user_", stream->broadcaster_name,
          "-1920x1080.jpg?time=", (long long)time(NULL));
  char unix_time[50];
  get_unix_time(started_at, unix_time);
  char timestamp[50];
  sprintf(timestamp, "<t:%s:R>", unix_time);

  cJSON_AddStringToObject(embed_json, "title", title);

  cJSON_AddStringToObject(embed_json, "url", twitch_url);

  cJSON *author_json = cJSON_CreateObject();
  cJSON_AddStringToObject(author_json, "name", stream->broadcaster_name);
  cJSON_AddStringToObject(author_json, "icon_url", profile_image);

  cJSON_AddItemToObject(embed_json, "author", author_json);

  cJSON_AddNumberToObject(embed_json, "color", 53380);

  cJSON *thumbnail_json = cJSON_CreateObject();
  cJSON_AddStringToObject(thumbnail_json, "url", profile_image);
  cJSON_AddItemToObject(embed_json, "thumbnail", thumbnail_json);

  cJSON *image_json = cJSON_CreateObject();
  cJSON_AddStringToObject(image_json, "url", thumbnail_url);
  cJSON_AddItemToObject(embed_json, "image", image_json);

  cJSON *fields_array = cJSON_CreateArray();
  cJSON *game_field = cJSON_CreateObject();
  cJSON_AddStringToObject(game_field, "name", "Game");
  cJSON_AddStringToObject(game_field, "value", game);
  cJSON_AddBoolToObject(game_field, "inline", cJSON_True);
  cJSON_AddItemToArray(fields_array, game_field);
  cJSON *started_field = cJSON_CreateObject();
  cJSON_AddStringToObject(started_field, "name", "Started");
  cJSON_AddStringToObject(started_field, "value", timestamp);
  cJSON_AddBoolToObject(started_field, "inline", cJSON_True);
  cJSON_AddItemToArray(fields_array, started_field);
  cJSON_AddItemToObject(embed_json, "fields", fields_array);
  // printf("%s\n", cJSON_Print(embed_json));
  return embed_json;
}

void get_channel_information(char *broadcaster_user_id, char *title, char *game) {
  if (oauth == NULL)
    ERROUT("No TWITCH_OAUTH provided");
  printf("[INFO]: Get Channel Information\n");

  static const char port[] = "443";
  static const char host[] = "api.twitch.tv";
  static const char format[] = "GET /helix/channels?broadcaster_id=%s HTTP/1.0\r\n"
                               "Host: api.twitch.tv\r\n"
                               "authorization: Bearer %s\r\n"
                               "accept: */*\r\n"
                               "client-id: %s\r\n\r\n";
  int request_length = strlen(format) + strlen(broadcaster_user_id) + strlen(oauth) + strlen(client_id);
  char request[request_length + 1];
  sprintf(request, format, broadcaster_user_id, oauth, client_id);
  request_length = strlen(request); // get exact length

  int sockfd = get_sockfd(host, port);
  SSL *ssl = get_ssl_from_sockfd(sockfd);
  if (SSL_write(ssl, request, request_length) != request_length)
    ERROUT("Error when writing");
  char response[MAXLINE];
  if (https_reponse(ssl, response) <= 0)
    ERROUT("Error when reading");
  LOG("[INFO]: Get Channel Information response:\n%s\n", response);

  SSL_shutdown(ssl);
  SSL_free(ssl);
  if (close(sockfd) == -1)
    ERROUT("failed to close sockfd");

  cJSON *res = cJSON_Parse(strstr(response, "\r\n\r\n") + 4);
  strcpy(title, cJSONUtils_GetPointer(res, "/data/0/title")->valuestring);
  strcpy(game, cJSONUtils_GetPointer(res, "/data/0/game_name")->valuestring);
  cJSON_Delete(res);
}

void get_profile_image(char *broadcaster_name, char *profile_image) {
  if (oauth == NULL)
    ERROUT("No TWITCH_OAUTH provided");
  printf("[INFO]: Get Users\n");

  static const char port[] = "443";
  static const char host[] = "api.twitch.tv";
  static const char format[] = "GET /helix/users?login=%s HTTP/1.0\r\n"
                               "Host: api.twitch.tv\r\n"
                               "authorization: Bearer %s\r\n"
                               "accept: */*\r\n"
                               "client-id: %s\r\n\r\n";
  int request_length = strlen(format) + strlen(broadcaster_name) + strlen(oauth) + strlen(client_id);
  char request[request_length + 1];
  sprintf(request, format, broadcaster_name, oauth, client_id);
  request_length = strlen(request); // get exact length

  int sockfd = get_sockfd(host, port);
  SSL *ssl = get_ssl_from_sockfd(sockfd);
  if (SSL_write(ssl, request, request_length) != request_length)
    ERROUT("Error when writing");
  char response[MAXLINE];
  if (https_reponse(ssl, response) <= 0)
    ERROUT("Error when reading");
  LOG("[INFO]: Get Users response:\n%s\n", response);

  SSL_shutdown(ssl);
  SSL_free(ssl);
  if (close(sockfd) == -1)
    ERROUT("failed to close sockfd");

  cJSON *res = cJSON_Parse(strstr(response, "\r\n\r\n") + 4);
  strcpy(profile_image, cJSONUtils_GetPointer(res, "/data/0/profile_image_url")->valuestring);
  cJSON_Delete(res);
}

long long https_reponse(SSL *ssl, char *buf) {
  if (SSL_read(ssl, buf, 4) != 4)
    ERROUT("Error when reading");
  long long n = 4;
  while (!end_with_2CRLF(buf, n)) {
    if (SSL_read(ssl, buf + n++, 1) != 1)
      ERROUT("Error when reading, read: %d", n);
  }
  buf[n] = '\0';
  char *cl;
  if ((cl = strcasestr(buf, "content-length: "))) {
    long content_length = strtol(cl + strlen("content-length: "), NULL, 10);
    if (SSL_read(ssl, buf + n, content_length) != content_length)
      ERROUT("Error when reading");
    n += content_length;
    buf[n] = '\0';
  }
  return n;
}

void get_unix_time(char *started_at, char *unix_time) {
  struct tm tm_;
  strptime(started_at, "%FT%TZ", &tm_);
  sprintf(unix_time, "%ld", timegm(&tm_));
}

void LOG(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stdout, fmt, ap);
  fflush(stdout);
  va_end(ap);
}

void errout(const char *file, const char *line, const char *func, const char *format, ...) {
  int errno_save;
  int ssl_error_save;
  va_list ap;
  errno_save = errno;
  ssl_error_save = ERR_get_error();

  fprintf(stdout, "[ERROR] %s:%s:%s() ", file, line, func);
  va_start(ap, format);
  vfprintf(stdout, format, ap);
  fprintf(stdout, "\n");
  fflush(stdout);

  if (errno_save != 0) {
    fprintf(stdout, "(errno = %d) : %s \n", errno_save, strerror(errno_save));
    fprintf(stdout, "\n");
    fflush(stdout);
  }

  if (ssl_error_save != 0) {
    fprintf(stdout, "(ssl_error) : %s\n", ERR_error_string(ssl_error_save, NULL));
    fprintf(stdout, "\n");
    fflush(stdout);
  }

  va_end(ap);
  exit(1);
}
