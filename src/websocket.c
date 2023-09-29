#include "websocket.h"

#define DEFAULT_URL "wss://eventsub.wss.twitch.tv/ws"

wsclient *wsclient_new(char *url) {
  wsclient *client = malloc(sizeof(wsclient));
  client->url = url;
  client->ssl = NULL;
  client->new_thread = NULL;
  wsclient_do_handshake(client);
  return client;
}

void wsclient_do_handshake(wsclient *client) {
  static const char handshake_format[] = "GET %s HTTP/1.1\r\n"
                                         "Upgrade: websocket\r\n"
                                         "Connection: Upgrade\r\n"
                                         "Host: %s\r\n"
                                         "Sec-WebSocket-Key: CUckftCEKsoLDaEZvCcXrg==\r\n"
                                         "Sec-WebSocket-Version: 13\r\n\r\n";
  char *url = client->url ? client->url : DEFAULT_URL;

  int is_ssl = strstr(url, "wss://") != NULL;
  // for non-SSL connection, it's ws:// so -1 if non-SSL
  char *host_with_route = url + strlen("wss://") - (is_ssl ? 0 : 1);
  char *route = strstr(host_with_route, "/");
  // TODO: port can be 16-bit i.e. 5 + 1 bytes as string
  char port[5] = "443";
  int host_length = strlen(host_with_route) - strlen(route);
  if (strstr(host_with_route, ":")) { // check port
    host_length -= 5;                 // :xxxx
    strncpy(port, strstr(host_with_route, ":") + 1, 4);
    port[4] = '\0';
  }
  char host[host_length + 1];
  strncpy(host, host_with_route, host_length);
  host[host_length] = '\0';
  int handshake_length = strlen(handshake_format) + strlen(route) + host_length;
  char handshake[handshake_length + 1];
  sprintf(handshake, handshake_format, route, host);
  handshake_length = strlen(handshake); // get exact length

  int sockfd = get_sockfd(host, port);
  client->sockfd = sockfd;
  if (is_ssl)
    client->ssl = get_ssl_from_sockfd(sockfd);

  if (wsclient_write(client, handshake, handshake_length) != handshake_length)
    ERROUT("Error when writing");
  char buf[MAXLINE];
  if (wsclient_handshake_response(client, buf) <= 0)
    ERROUT("Error when reading");

  LOG("\n[INFO]: Handshake response:\n%s\n", buf);
}

void wsclient_run(wsclient *client) {
  wsframe *frame = wsclient_get_wsframe(client);
  while (frame != NULL) {
    wsclient_handle_wsframe(client, frame);
    wsclient_clean_wsframe(client, frame);
    frame = wsclient_get_wsframe(client);
  }
}

wsframe *wsclient_get_wsframe(wsclient *client) {
  unsigned char fin_opcode_length[2];
  if (wsclient_read(client, fin_opcode_length, 2) != 2)
    return NULL;
  wsframe *frame = malloc(sizeof(wsframe));
  frame->payload = NULL;
  frame->payload_length = 0;
  frame->fin = fin_opcode_length[0] >> 7;
  frame->opcode = fin_opcode_length[0] & MASK_OPCODE;
  int payload_length = fin_opcode_length[1] & MASK_LENGTH;

  if (payload_length <= 125) {
    frame->payload_length = payload_length;
  } else if (payload_length == 126) {
    unsigned char long_length[2];
    if (wsclient_read(client, long_length, 2) != 2)
      ERROUT("Error when reading");
    frame->payload_length = ((unsigned long long)long_length[0] << 8 | (unsigned long long)long_length[1]);
  } else if (payload_length == 127) {
    unsigned char long_long_length[8];
    if (wsclient_read(client, long_long_length, 8) != 8)
      ERROUT("Error when reading");
    if (long_long_length[0] >> 7 & 1)
      ERROUT("Invalid length, MSB is not 0");
    frame->payload_length = ((uint64_t)long_long_length[0] << 8 * 7 | (uint64_t)long_long_length[1] << 8 * 6 |
                             (uint64_t)long_long_length[2] << 8 * 5 | (uint64_t)long_long_length[3] << 8 * 4 |
                             (uint64_t)long_long_length[4] << 8 * 3 | (uint64_t)long_long_length[5] << 8 * 2 |
                             (uint64_t)long_long_length[6] << 8 * 1 | (uint64_t)long_long_length[7] << 8 * 0);
  } else {
    ERROUT("Invalid length value");
  }

  if (frame->payload_length) {
    frame->payload = malloc(frame->payload_length);
    if (wsclient_read(client, frame->payload, frame->payload_length) != frame->payload_length)
      ERROUT("Error when reading");
  }

  LOG("---------------------------------\n");
  LOG("[INFO] fin: %d, opcode: 0x%02X, length: %d, actual length: %" PRIu64 "\n", frame->fin, frame->opcode,
      payload_length, frame->payload_length);
  return frame;
}

void wsclient_handle_wsframe(wsclient *client, wsframe *frame) {
  switch (frame->opcode) {
  case OPCODE_TEXT: {
    char payload_str[frame->payload_length + 1];
    strncpy(payload_str, (char *)frame->payload, frame->payload_length);
    payload_str[frame->payload_length] = '\0';
    LOG("[INFO] Data: %s\n", payload_str);
    wsclient_handle_json(client, payload_str);
    break;
  }
  case OPCODE_PING:
    wsclient_send_pong(client);
    break;
  case OPCODE_CLOSE: {
    unsigned int status_code = ((unsigned int)frame->payload[0] << 8 | (unsigned int)frame->payload[1]);
    int text_length = frame->payload_length - 2;
    char payload_str[text_length + 1];
    strncpy(payload_str, (char *)frame->payload + 2, text_length);
    payload_str[text_length] = '\0';
    printf("[WARNING] Close frame. Status code: %u, Reason: %s\n", status_code, payload_str);
    break;
  }
  default:
    printf("[WARNING] Received unhandled opcode: 0x%02X\n", frame->opcode);
    break;
  }
}

void wsclient_handle_json(wsclient *client, char *json_str) {
  cJSON *root = cJSON_Parse(json_str);
  char *message_type = cJSONUtils_GetPointer(root, "/metadata/message_type")->valuestring;
  if (strcmp(message_type, "session_keepalive") == 0)
    goto finish;
  if (strcmp(message_type, "session_welcome") == 0 && client->url == NULL) {
    char *session_id = cJSONUtils_GetPointer(root, "/payload/session/id")->valuestring;
    for (int i = 0; i < streamers_length; i++)
      create_eventsub(session_id, streamers[i].broadcaster_user_id);
  } else if (strcmp(message_type, "session_reconnect") == 0) {
    char *reconnect_url = strdup(cJSONUtils_GetPointer(root, "/payload/session/reconnect_url")->valuestring);
    pthread_t *new_thread = malloc(sizeof(pthread_t));
    client->new_thread = new_thread;
    pthread_create(new_thread, NULL, &wsclient_thread, reconnect_url);
  } else if (strcmp(message_type, "notification") == 0) {
    char *received_id = cJSONUtils_GetPointer(root, "/payload/event/broadcaster_user_id")->valuestring;
    for (int i = 0; i < streamers_length; i++)
      if (strcmp(received_id, streamers[i].broadcaster_user_id) == 0) {
        send_discord_notif(&streamers[i], cJSONUtils_GetPointer(root, "/payload/event/started_at")->valuestring);
      }
  }

finish:
  cJSON_Delete(root);
}

void wsclient_send_pong(wsclient *client) {
  LOG("[INFO] Sending pong...\n");
  unsigned char send_buf[6] = {0x8A, 0x80, 0, 0, 0, 0};
  if (wsclient_write(client, send_buf, 6) != 6)
    ERROUT("Error when writing (pong)");
}

void wsclient_clean_wsframe(wsclient *client, wsframe *frame) {
  free(frame->payload);
  free(frame);
}

void wsclient_free(wsclient *client) {
  if (client->ssl) {
    SSL_shutdown(client->ssl);
    SSL_free(client->ssl);
  }
  if (close(client->sockfd) == -1)
    ERROUT("Error when closing sockfd");
  free(client->url);
  free(client);
}

long long wsclient_handshake_response(wsclient *client, char *buf) {
  if (wsclient_read(client, buf, 4) != 4)
    ERROUT("Error when reading");
  long long n = 4;
  while (!end_with_2CRLF(buf, n))
    if (wsclient_read(client, buf + n++, 1) != 1)
      ERROUT("Error when reading, read: %d", n);
  buf[n] = '\0';
  char *cl;
  if ((cl = strcasestr(buf, "content-length: "))) {
    long content_length = strtol(cl + strlen("content-length: "), NULL, 10);
    if (wsclient_read(client, buf + n, content_length) != content_length)
      ERROUT("Error when reading");
    n += content_length;
    buf[n] = '\0';
  }
  return n;
}

long long wsclient_read(wsclient *client, void *buf, size_t length) {
  if (client->ssl)
    return SSL_read(client->ssl, buf, length);
  else
    return recv(client->sockfd, buf, length, 0);
}

long long wsclient_write(wsclient *client, void *buf, size_t length) {
  if (client->ssl)
    return SSL_write(client->ssl, buf, length);
  else
    return send(client->sockfd, buf, length, 0);
}
