 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

#include "../shared.h"

#if defined(OS_LINUX) || defined(OS_MACOSX)
#include <sys/ioctl.h>
#include <termios.h>
#elif defined(OS_WINDOWS)
#include <conio.h>
#endif

#include "hid.h"
void print_state(state_t *state);
void handle_packet(state_t *pkt);
void debug_control(state_t *st);
static char get_keystroke(void);
state_t state;

int main()
{
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


