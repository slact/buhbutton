#define INIT_URL "https:/localhost:9092/hello"
#define MAX_ALERTS 20
 
 
#define STATE_IDLE 0
#define STATE_ALERT 1
#define STATE_ALERT_URGENT 2
#define STATE_WAITING 3
 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>

#include <jansson.h>
#include <curl/curl.h>
#include "../shared.h"

#include "hid.h"

#if defined(OS_LINUX) || defined(OS_MACOSX)
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#elif defined(OS_WINDOWS)
#include <conio.h>
#include <Windows.h>
#endif

struct string {
  char *ptr;
  size_t len;
};

typedef struct {
  char id[255];
  char init_url[1024];
  char sub_url[1024];
  CURL *curl;
  CURLM  *mcurl;
  int requests_running;
  struct string str;
  char etag[255];
  char last_modified[255];
} subscriber_t;

typedef struct {
  char *action;
  int (*func)(subscriber_t *sub, json_t *data, state_t *state);
} action_table_t;

typedef struct alert_s {
  char *url;
  time_t time;
  struct alert_s *prev;
  struct alert_s *next;
} alert_t;
alert_t  *first_alert, *last_alert;
uint16_t alerts_count;
int queue_alert(const char *url, time_t time) {
  if (alerts_count>=MAX_ALERTS) {  
    fprintf(stderr, "reached MAX_ALERTS of %i\n", MAX_ALERTS);
    return 1;
  }
  alert_t *new_alert;
  if((new_alert=malloc(sizeof(alert_t)))==NULL) {
    fprintf(stderr, "can't allocate memory for new alert\n");
    return 1;
  }
  if((new_alert->url=malloc(strlen(url)+1))==NULL) {
    fprintf(stderr, "can't allocate memory for new alert url\n");
    return 1;
  }
  strcpy(new_alert->url, url);
  new_alert->time=time;
  new_alert->prev=last_alert;
  new_alert->next=NULL;
  if(first_alert==NULL) {
    first_alert=new_alert;
  }
  if(last_alert!=NULL) {
    last_alert->next=new_alert;
  }
  last_alert=new_alert;
  alerts_count++;
  return 0;
}

int free_alert(alert_t *alt) {
  if (alt==NULL)
    return 1;
  if(alt==first_alert) {
    first_alert=alt->next;
  }
  if(alt==last_alert) {
    last_alert=alt->prev;
  }
  if (alt->prev!=NULL) {
    alt->prev->next=alt->next;
  }
  if (alt->next!=NULL) {
    alt->next->prev=alt->prev;
  }
  free(alt->url);
  free(alt);
  alerts_count--;
  return 0;
}
void dump_alerts() {
  alert_t *cur=first_alert;
  int i=0;
  printf("Listing %i alert(s):\n", alerts_count);
  while(cur!=NULL) {
    printf("%i: %s (%ld)", i+1, cur->url, cur->time);
    cur=cur->next;
  }
}

int action_alert(subscriber_t *sub, json_t *data, state_t *state);

action_table_t actions_table[] = {
  {"alert", &action_alert},
  {NULL}
};

#if defined(OS_LINUX)
  #define OPENURL_COMMAND_FORMAT "xdg-open \"%s\" &"
#elif defined(OS_MACOSX)
  #define OPENURL_COMMAND_FORMAT "open \"%s\" &"
#elif defined(OS_WINDOWS)
  #define OPENURL_COMMAND_FORMAT "explorer \"%s\""
#endif

void init_string(struct string *s) {
  printf("init string!!");
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
  printf("writefunc");
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

size_t setheaderfunc( void *ptr, size_t size, size_t nmemb, void *userdata){
  subscriber_t *sub=(subscriber_t *)userdata;
  size_t headerval_len=0;
  if(strncmp("Etag: ", ptr, strlen("Etag: "))==0) { 
    headerval_len= size*nmemb-strlen("Etag: ")-2; //-2 for /r/n at the end
    memcpy(sub->etag, ptr + strlen("Etag: "), headerval_len);
    memset(sub->etag+headerval_len, '\0', 1);
  }
  else if(strncmp("Last-Modified: ", ptr, strlen("Last-Modified: "))==0) { 
    headerval_len= size*nmemb-strlen("Last-Modified: ")-2; //-2 for /r/n at the end
    memcpy(sub->last_modified, ptr + strlen("Last-Modified: "), headerval_len);
    memset(sub->last_modified+headerval_len, '\0', 1);
  }
  return size*nmemb;
}


void print_state(state_t *state);
void handle_button_press(state_t *pkt);
void debug_control(state_t *st);
static char get_keystroke(void);
void subscriber_init(subscriber_t *sub);
void subscriber_check(subscriber_t *sub, state_t *state);
void parse_action(json_t *data, subscriber_t *sub, state_t *state);
void set_state(state_t *st, int state);
state_t state;

int send_state; //send state packet to button? 0:no, 1:yes
int main(int argc, char *argv[]){
  alerts_count=0;
  send_state=0;
  
  subscriber_t sub;
  memset(&sub, '\0', sizeof(sub));
  printf("handshake at \"%s\"\n", argv[1]);
  strcpy(sub.init_url, argv[1]);
  subscriber_init(&sub);
  first_alert=NULL;
  last_alert=NULL;
  
  int r=0, num;
  char buf[64];
  state_t *pkt;

  while (1) {
    subscriber_check(&sub, &state);
    debug_control(&state);
    if (r <= 0) {
      r = rawhid_open(1, VENDOR_ID, PRODUCT_ID, RAWHID_USAGE_PAGE, RAWHID_USAGE);
      if (r > 0){
        //initialize button in idle state
        if (alerts_count==0)
          set_state(&state, STATE_IDLE);
        else if(alerts_count==1)
          set_state(&state, STATE_ALERT);
        else if(alerts_count>1)
          set_state(&state, STATE_ALERT_URGENT);
      }
    }
    else {
      // check if any Raw HID state_t has arrived
      num = rawhid_recv(0, buf, 64, 220);
      if (num < 0) {
        printf("\nerror reading, device went offline\n");
        //free_all_hid();
        r=0;
      }
      if (num > 0) {
        pkt = (state_t *)&buf;
        printf("Received packet\n");
        print_state(pkt);
        //memcpy(&state, pkt, sizeof(state));
        if (pkt->button>0) {
          handle_button_press(pkt);
        }
      }
      if(send_state==1) {
        send_state=0;
        printf("Sending packet\n");
        print_state(&state);
        rawhid_send(0, &state, 64, 100);
      }
    }
    if(r==0) {
      printf("no button device found\n");
      sleep(1);
    }
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

void handle_button_press(state_t *pkt) {
  if (pkt->button==0)
    return;
  alert_t *alert = first_alert;
  if (alert==NULL) {
    return;
  }
  
  char *cmd=malloc(snprintf(NULL, 0, OPENURL_COMMAND_FORMAT, alert->url) + 1);
  sprintf(cmd, OPENURL_COMMAND_FORMAT, alert->url);
  
  system(cmd);
  
  free(cmd);
  free_alert(alert);
  if (alerts_count==0) {
    set_state(&state, STATE_IDLE);
  } 
  else if(alerts_count==1) {
    set_state(&state, STATE_ALERT);
  }
  else if(alerts_count>1) {
    set_state(&state, STATE_ALERT_URGENT);
  }
  pkt->button=0;
}

void set_state(state_t *st, int state) {
  switch(state) {
    case STATE_IDLE:
      st->led[0]=LED_OFF;
      st->led[1]=LED_OFF;
      st->vibrate=MOTOR_OFF;
      st->pattern=LED_FADE_OUT;
      st->buzz=BUZZER_OFF;
      break;
    case STATE_WAITING:
      st->led[0]=LED_OFF;
      st->led[1]=LED_OFF;
      st->vibrate=MOTOR_OFF;
      st->pattern=LED_BLINK;
      st->pattern_speed=5;
      st->buzz=BUZZER_OFF;
      break;
    case STATE_ALERT:
      st->pattern_speed= alerts_count*5<255 ? alerts_count*5: 255;
      st->led[0]=4;
      st->led[1]=7;
      st->vibrate=MOTOR_OFF;
      st->pattern=LED_PULSE;
      st->buzz=BUZZER_OFF;
      break;
    case STATE_ALERT_URGENT:
      st->pattern_speed= alerts_count*5<255 ? alerts_count*5: 255;
      st->led[0]=3;
      st->led[1]=3;
      st->vibrate=MOTOR_ON;
      st->pattern=LED_PULSE;
      st->buzz=BUZZER_OFF;
      break;
  }
  send_state=1;
}

void print_state(state_t *state) {
  //printf("Header: %s\n", state->header);led1_fade=255;
  printf("LED1: %i LED2: %i\n", state->led[0], state->led[1]);
  printf("LED1fade: %i LED2fade: %i\n", state->led_fade[0], state->led_fade[1]);
  printf("Vibrate: %i\n", state->vibrate);
  printf("Pattern: %i speed %i threshold %i\n", state->pattern, state->pattern_speed, state->pattern_threshold);
  if (state->buzz == 0)
    printf("Buzzer: off\n");
  else
    printf("Buzzer: %i, buggy freq guess: %dHz\n", state->buzz, 16000000/state->buzz);
  printf("Button: %i\n", state->button);
  //printf("Footer: %s\n", state->footer);
  printf("Size: %zu bytes\n", sizeof(*state));
  printf("\n");
}

void debug_control(state_t *st) {
  char c;
  // check if any input on stdin
  while ((c = get_keystroke()) >= 32) {
    if (c=='1')
      st->led[0]++;
    if (c=='2')
      st->led[1]++;
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
}

void setcurlopts(subscriber_t *sub) {
  //curl_easy_setopt(sub->curl, CURLOPT_VERBOSE, 1);
  curl_easy_setopt(sub->curl, CURLOPT_URL, sub->sub_url);
  curl_easy_setopt(sub->curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(sub->curl, CURLOPT_WRITEFUNCTION, writefunc);
  curl_easy_setopt(sub->curl, CURLOPT_HEADERFUNCTION, setheaderfunc);
  curl_easy_setopt(sub->curl, CURLOPT_WRITEDATA, &sub->str);
  curl_easy_setopt(sub->curl, CURLOPT_WRITEHEADER, sub);
}


void subscriber_check(subscriber_t *sub, state_t *state) {
  struct curl_slist *headers = NULL;
  CURLcode result = CURLE_FAILED_INIT;
  int i, prev_req_running = sub->requests_running;
  json_t *root;
  json_error_t error;

  if(sub->curl==NULL) {
    //init subscriber for real
    sub->curl = curl_easy_init();
    sub->mcurl = curl_multi_init();
    curl_multi_add_handle(sub->mcurl, sub->curl);
    init_string(&sub->str);
    setcurlopts(sub);
  }
  curl_multi_perform(sub->mcurl, &sub->requests_running);
  if(prev_req_running != sub->requests_running) { //request finished
    //is there a result available?
    CURLMsg *msg;
    int msgs_left=1;
    msg=curl_multi_info_read(sub->mcurl, &msgs_left);
    if(msg==NULL) {
      fprintf(stderr, "curl_multi_info_read() msg is NULL?..\n");
    } else {
      result = msg->data.result;
      if(result != CURLE_OK) {
        /* if errors have occured, tell us wath's wrong with 'result'*/
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(result));
      }
      printf("body:\n %s\n", sub->str.ptr);
      
      root = json_loadb(sub->str.ptr, sub->str.len, 0, &error);
      if(!root) {
        fprintf(stderr, "error: on line %d: %s\n", error.line, error.text);
      }
      else {
        if(json_is_object(root)) {
          parse_action(root, sub, state);
        }
        else if(json_is_array(root)){
          for(i = 0; i < json_array_size(root); i++) {
            parse_action(json_array_get(root, i), sub, state);
          }
        }
        json_decref(root);
      }
    }
  }
  if(sub->requests_running==0) {
    curl_easy_reset(sub->curl);
    free(sub->str.ptr);
    init_string(&sub->str);
    setcurlopts(sub);
    //use all available caching information
    if(strlen(sub->last_modified)>0) {
      char last_modified[255];
      char etag[255];
      sprintf(last_modified, "If-Modified-Since: %s", sub->last_modified);
      sprintf(etag, "If-None-Match: %s", sub->etag);
      headers = curl_slist_append(headers, last_modified);
      headers = curl_slist_append(headers, etag);
      curl_easy_setopt(sub->curl, CURLOPT_HTTPHEADER, headers);
      curl_multi_remove_handle(sub->mcurl, sub->curl);
      curl_multi_add_handle(sub->mcurl, sub->curl);
    }
    
  }
  else {
    usleep(100);
  }
}

int action_alert(subscriber_t *sub, json_t *data, state_t *state) {
  printf("action: alert\n");
  json_t *cur;
  cur = json_object_get(data, "url");
  if(cur==NULL) {
    fprintf(stderr, "error: alert url not found\n");
    return 1;
  }
  queue_alert(json_string_value(cur), 0);
  set_state(state, alerts_count==1 ? STATE_ALERT : STATE_ALERT_URGENT);
  return 0;
}

void parse_action(json_t *data, subscriber_t *sub, state_t *state) {
  json_t *action;
  int i=0;
  if(!json_is_object(data)) {
    fprintf(stderr, "error: data is not an object\n");
    return;
  }
  if((action = json_object_get(data, "action"))==NULL) {
    fprintf(stderr, "error: action parameter missing.");
    return;
  }
  while (actions_table[i].action != NULL){
    if (strcmp(actions_table[i].action, json_string_value(action))==0) {
      actions_table[i].func(sub, data, state);
    }
    i++;
  }
}