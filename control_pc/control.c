 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

#include <jansson.h>
#include <curl/curl.h>
#include "../shared.h"

#if defined(OS_LINUX) || defined(OS_MACOSX)
#include <sys/ioctl.h>
#include <termios.h>
#elif defined(OS_WINDOWS)
#include <conio.h>
#endif

typedef struct {
  char id[255];
  char init_url[1024];
  char sub_url[1024];
  CURL *curl;
  char etag[255];
  char last_modified[255];
} subscriber_t;

struct string {
  char *ptr;
  size_t len;
};

typedef struct {
  char *action;
  int (*func)(subscriber_t *sub, json_t *data);
} action_table_t;



int action_alert(subscriber_t *sub, json_t *data);

action_table_t actions_table[] = {
  {"alert", &action_alert},
  {NULL}
};

void init_string(struct string *s) {
  s->len = 0;
  s->ptr = malloc(s->len+1);
  if (s->ptr == NULL) {
    fprintf(stderr, "malloc() failed\n");
    exit(EXIT_FAILURE);
  }
  s->ptr[0] = '\0';
}

size_t writefunc(void *ptr, size_t size, size_t nmemb, struct string *s)
{
  size_t new_len = s->len + size*nmemb;
  s->ptr = realloc(s->ptr, new_len+1);
  if (s->ptr == NULL) {
    fprintf(stderr, "realloc() failed\n");
    exit(EXIT_FAILURE);
  }
  memcpy(s->ptr+s->len, ptr, size*nmemb);
  s->ptr[new_len] = '\0';
  s->len = new_len;
  
  return size*nmemb;
}


#include "hid.h"
void print_state(state_t *state);
void handle_packet(state_t *pkt);
void debug_control(state_t *st);
static char get_keystroke(void);
void subscriber_init(subscriber_t *sub);
void subscriber_check(subscriber_t *sub);
void parse_action(json_t *data, subscriber_t *sub);
state_t state;

int main()
{
  subscriber_t sub;
  memset(&sub, '\0', sizeof(sub));
  strcpy(sub.init_url, "https://slact.net/foo.json");
  subscriber_init(&sub);
  
  int r, num;
  char buf[64];
  state_t *pkt;

  r = rawhid_open(1, VENDOR_ID, PRODUCT_ID, RAWHID_USAGE_PAGE, RAWHID_USAGE);
  if (r <= 0) {
    printf("no rawhid device found\n");
    return -1;
  }
  printf("found rawhid device\n");

  while (1) {
    // check if any Raw HID state_t has arrived
    num = rawhid_recv(0, buf, 64, 220);
    if (num < 0) {
      printf("\nerror reading, device went offline\n");
      rawhid_close(0);
      return 0;
    }
    if (num > 0) {
      pkt = (state_t *)&buf;
      printf("Received packet\n");
      print_state(pkt);
      handle_packet(pkt);
    }
    subscriber_check(&sub);
    debug_control(pkt);
  }
}

#if defined(OS_LINUX) || defined(OS_MACOSX)
// Linux (POSIX) implementation of _kbhit().
// Morgan McGuire, morgan@cs.brown.edu
static int _kbhit() {
  static const int STDIN = 0;
  static int initialized = 0;
  int bytesWaiting;

  if (!initialized) {
    // Use termios to turn off line buffering
    struct termios term;
    tcgetattr(STDIN, &term);
    term.c_lflag &= ~ICANON;
    tcsetattr(STDIN, TCSANOW, &term);
    setbuf(stdin, NULL);
    initialized = 1;
  }
  ioctl(STDIN, FIONREAD, &bytesWaiting);
  return bytesWaiting;
}
static char _getch(void) {
  char c;
  if (fread(&c, 1, 1, stdin) < 1) return 0;
  return c;
}
#endif


static char get_keystroke(void)
{
  if (_kbhit()) {
    char c = _getch();
    if (c >= 32) return c;
  }
  return 0;
}

void handle_packet(state_t *pkt) {
  if (pkt->button==0)
    return;
  pkt->led[0]=0;
  pkt->led[1]=0;
  pkt->vibrate=0;
  pkt->buzz=0;
  rawhid_send(0, pkt, 64, 100);
}

void print_state(state_t *state) {
  printf("Header: %s\n", state->header);
  printf("LED1: %i LED2: %i\n", state->led[0], state->led[1]);
  printf("Vibrate: %i\n", state->vibrate);
  printf("Pattern: %i\n", state->pattern);
  if (state->buzz == 0)
    printf("Buzzer: off\n");
  else
    printf("Buzzer: %i, buggy freq guess: %dHz\n", state->buzz, 16000000/state->buzz);
  printf("Button: %i\n", state->button);
  printf("Footer: %s\n", state->footer);
  printf("Size: %zu bytes\n", sizeof(*state));
  printf("\n");
}

void debug_control(state_t *st) {
  char c;
  // check if any input on stdin
  while ((c = get_keystroke()) >= 32) {
    if (c=='1')
      st->led[0]=1;
    if (c=='2')
      st->led[1]=1;
    if (c=='v')
      st->vibrate=1;
    if (c=='b')
      st->buzz+=1;
    
    printf("Send packet\n");
    print_state(st);
    rawhid_send(0, st, 64, 100);
  }
}

void subscriber_init(subscriber_t *sub){
  CURL *curl;
  CURLcode result;
  struct string s;
  init_string(&s);
  //curl_global_init(CURL_GLOBAL_ALL);
  printf("Init at %s\n", sub->init_url);
  curl = curl_easy_init();
  curl_easy_setopt(curl, CURLOPT_URL, sub->init_url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
  result = curl_easy_perform(curl);
  if(result != CURLE_OK) {
    /* if errors have occured, tell us wath's wrong with 'result'*/
    fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(result));
    exit(1);
  }
  printf("body:\n %s\n", s.ptr);
  curl_easy_cleanup(curl);
  
  json_t *root, *id, *sub_url;
  json_error_t error;
  root = json_loadb(s.ptr, s.len, 0, &error);
  if(!root) {
    fprintf(stderr, "error: on line %d: %s\n", error.line, error.text);
    exit(1);
  }
  if(!json_is_object(root)) {
    fprintf(stderr, "json root was supposed to be an object");
    json_decref(root);
    exit(1);
  }
  id = json_object_get(root, "id");
  if(!json_is_string(id)) {
    fprintf(stderr, "error: id is not a string\n");
    json_decref(root);
    exit(1);
  }
  strcpy(sub->id, json_string_value(id));

  sub_url = json_object_get(root, "subscribe_url");
  if(!json_is_string(sub_url)) {
    fprintf(stderr, "error: id is not a string\n");
    json_decref(root);
    exit(1);
  }
  strcpy(sub->sub_url, json_string_value(sub_url));

  //init subscriber for realstruct
  sub->curl = curl_easy_init();
  curl_easy_setopt(sub->curl, CURLOPT_URL, sub->sub_url);
  curl_easy_setopt(sub->curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(sub->curl, CURLOPT_WRITEFUNCTION, writefunc);
}

void subscriber_check(subscriber_t *sub) {
  struct curl_slist *headers = NULL;
  struct string s;
  CURLcode result;
  int i;
  json_t *root;
  json_error_t error;
  
  //use all available caching information
  if(strlen(sub->last_modified)>0) {
    char last_modified[255];
    char etag[255];
    curl_easy_setopt(sub->curl, CURLOPT_HTTPHEADER, headers);
    sprintf(last_modified, "If-Modified-Since: %s", sub->last_modified);
    sprintf(etag, "If-None-Match: %s", sub->etag);
    headers = curl_slist_append(headers, last_modified);
    headers = curl_slist_append(headers, etag);
  }

  curl_easy_setopt(sub->curl, CURLOPT_WRITEDATA, &s);
  init_string(&s);

  result = curl_easy_perform(sub->curl);
  if(result != CURLE_OK) {
    /* if errors have occured, tell us wath's wrong with 'result'*/
    fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(result));
  }
  printf("body:\n %s\n", s.ptr);

  root = json_loadb(s.ptr, s.len, 0, &error);
  //parse some json
  if(json_is_object(root)) {
    fprintf(stderr, "error: root is not an array\n");
    parse_action(root, sub);
    json_decref(root);
    return;
  }
  else if(json_is_array(root)){
    for(i = 0; i < json_array_size(root); i++) {
      parse_action(json_array_get(root, i), sub);
    }
  }
  json_decref(root);
}

int action_alert(subscriber_t *sub, json_t *data) {
  return 0;
}

void parse_action(json_t *data, subscriber_t *sub) {
  json_t *action;
  int i=0;
  if(!json_is_object(data)) {
    fprintf(stderr, "error: data is not an onject\n");
    return;
  }
  action = json_object_get(data, "action");
  while (actions_table[i].action != NULL){
    if (strcmp(actions_table[i].action, json_string_value(action))) {
      actions_table[i].func(sub, data);
    }
    i++;
  }
}