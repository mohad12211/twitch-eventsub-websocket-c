#include "websocket.h"

const char *dm_room_id = "1045474914695663626";
const streamer streamers[] = {
    {"1045421121333370912", "110644052", "mohad12211"},
};
const int streamers_length = sizeof(streamers) / sizeof(streamers[0]);

int main(int argc, char *argv[]) {
  init_ssl();
  init_keys();

  pthread_t *current_thread = NULL;
  pthread_t *new_thread = malloc(sizeof(pthread_t));
  pthread_create(new_thread, NULL, &wsclient_thread, NULL);
  while (new_thread != NULL) {
    current_thread = new_thread;
    pthread_join(*current_thread, (void **)&new_thread);
    free(current_thread);
  }
  return 0;
}

void *wsclient_thread(void *url) {
  wsclient *client = wsclient_new((char *)url);
  wsclient_run(client);
  pthread_t *new_thread = client->new_thread;
  wsclient_free(client);
  return new_thread;
}
