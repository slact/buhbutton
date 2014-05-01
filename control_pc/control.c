 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

#include <yaml.h>
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
  char etag[255];
  char last_modified[255];
} subscriber_t;

struct string {
  char *ptr;
  size_t len;
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
void test_yaml(void);
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
        
    debug_control(pkt);
    test_yaml();
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

void test_yaml(){
  yaml_parser_t parser;
  /* Initialize parser */
  if(!yaml_parser_initialize(&parser))
    fputs("Failed to initialize parser!\n", stderr);
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
  
  yaml_parser_t parser;
  yaml_token_t token; 
  if (!yaml_parser_initialize(&parser))
    fprintf(stderr, "Failed to initialize yaml parser!\n");
  yaml_parser_set_input_string(&parser, s.ptr, s.len);
  
  
  do {
    yaml_parser_scan(&parser, &token);
    switch(token.type)
    {
      int id_next=0, sub_url_next=0;
      int token_key=0;
      case YAML_KEY_TOKEN:   token_key=1; break;
      case YAML_VALUE_TOKEN: token_key=0; break;
      //ugly code follows
      case YAML_SCALAR_TOKEN:
        if (token_key==1 && strcmp((void *)token.data.scalar.value, "id"))
          id_next=1;
        else if (token_key==1 && strcmp((void *)token.data.scalar.value, "subscribe_url"))
          sub_url_next=1;
        else if (token_key==0 && id_next==1) {
          //TODO: size check!!
          memcpy(sub->id, token.data.scalar.value, token.data.scalar.length);
          id_next=0;
        }
        else if (token_key==0 && sub_url_next==1) {
          //TODO: size check!!
          sub_url_next=0;
          memcpy(sub->sub_url, token.data.scalar.value, token.data.scalar.length);
        }
        break;
      default:
        break;
    }
    if(token.type != YAML_STREAM_END_TOKEN)
      yaml_token_delete(&token);
  } while(token.type != YAML_STREAM_END_TOKEN);
  
  yaml_token_delete(&token);
  free(s.ptr);
}