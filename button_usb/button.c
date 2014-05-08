/* Buhbutton: a button with a bunch of LEDs, a vibrating motor, 
 * and maybe a tiny unamped speaker. Subscribes via long-polling to a URL, 
 * receives commands from server. Sometimes it also gets it button pressed.
 * Based on http://www.pjrc.com/teensy/rawhid.html, Copyright (c) 2009 PJRC.COM, LLC
 */

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "shared.h"
#include "usb_rawhid.h"
#include "analog.h"

#include "effects.c"
#include "debug.h"
#define CPU_PRESCALE(n)  (CLKPR = 0x80, CLKPR = (n))

volatile uint8_t do_output=0;
volatile uint8_t led1_fade=0;
volatile uint8_t motor_fade=0;
volatile uint8_t led2_fade=0;

volatile state_t state;
volatile int button_down=0;
uint8_t buffer[64];

void apply_state(volatile state_t *s);
int handle_rawhid_packet(state_t *buffer);
int handle_button(void);


void dbg_print(char *str) {
  debug_packet_t dbg;
  strcpy(&dbg.header, "DBG");
  memcpy(&dbg.str, str, 58);
  usb_rawhid_send((uint8_t *)&dbg, 50);
}

int main(void)
{
  int8_t r;
  cli(); //suspend interrupts
  memset((void *)&state, '\0', sizeof(state));
  //strcpy(&state.header, "FOO");
  //strcpy(&state.footer, "BAR");
  
  // Configure all port F pins as inputs with pullup resistors
  // http://www.pjrc.com/teensy/pins.html
  DDRF = 0x00;
  PORTF = 0xFF;
  
  // Configure all port B and D pins as outputs
  // http://www.pjrc.com/teensy/pins.html
  DDRB = 0xFF;
  DDRD = 0xFF;
  PORTB = 0x00;
  PORTD = 0x00;
  PINB = 0x00;
  PIND = 0x00;
  
  // set for 16 MHz clock
  CPU_PRESCALE(0);

  // Initialize the USB, and then wait for the host to set configuration.
  // If the Teensy is powered without a PC connected to the USB port,
  // this will wait forever.
  usb_init();
  while (!usb_configured()) /* wait */ ;

  // Wait an extra second for the PC's operating system to load drivers
  // and do whatever it does to actually be ready for input
  _delay_ms(1000);

  // Configure timer 0 to generate a timer overflow interrupt every
  // 256*1024 clock cycles, or approx 61 Hz when using 16 MHz clock
  TCCR0A = 0x00;
  TCCR0B = 0x05;
  TIMSK0 = (1<<TOIE0);
  
  
  //timer 1 -- effects timer?...
  // set up timer with prescaler = 64 and CTC mode
  TCCR1B |= (1 << WGM12)|(1 << CS10);
  // initialize counter
  TCNT1 = 0;
  // enable compare interrupt
  TIMSK1 |= (1 << OCIE1A);
  // initialize compare value
  OCR1A = 200;

  
  sei(); //resume interrupts
  while (1) {
    handle_button();
    //if received data, do something with it
    r = usb_rawhid_recv(buffer, 0);
    if (r > 0) {
      handle_rawhid_packet((state_t *)buffer);
    }
    //if time to send output, transmit something interesting
    if (do_output==1) {
      usb_rawhid_send((uint8_t *)&state, 50);
      do_output=0;
    }
    apply_state(&state);
  }
}

// This interrupt routine is run approx 61 times per second.
ISR(TIMER0_OVF_vect)
{
  static int prev_pattern;
  static uint16_t count=0;
  static wf_state_t wfs[3];
  wfs[2].waveform=&wf_square;
  
  static wf_state_t wfs_busy[2];
  wfs_busy[0].waveform=&wf_cos;
  wfs_busy[0].downscale=5;
  wfs_busy[0].step_multiplier=20;
  wfs_busy[1].waveform=&wf_cos;
  wfs_busy[1].phase=0;
  wfs_busy[1].downscale=5;
  wfs_busy[1].step_multiplier=20;
  
  
  count++;
  switch(state.pattern){
    case LED_FADE_IN:
        state.pattern=NO_PATTERN;
      break;
    case LED_FADE_OUT:
        state.pattern=NO_PATTERN;
      break;
    case LED_BLINK:
      wfs[0].waveform=wf_square;
      led1_fade=waveform(&wfs[0], state.pattern_speed);
      led2_fade=led1_fade;
      break;
    case  LED_PULSE:
      if(prev_pattern!=LED_PULSE) {
        wfs[0].waveform=wf_cos;
        wfs[1].waveform=wf_cos;
        //wfs[0].subwave=&wfs_busy[0];
        //wfs[1].subwave=&wfs_busy[1];

      }
      wfs[2].threshold = 254 - state.pattern_speed*3;
      if (wfs[2].threshold < 30)
        wfs[2].threshold=29;

      led1_fade=cie1931[waveform(&wfs[0], state.pattern_speed)];
      led2_fade=cie1931[waveform(&wfs[1], state.pattern_speed)];
      motor_fade=waveform(&wfs[2], state.pattern_speed);

      break;
    case NO_PATTERN:
      led1_fade=1;
      led2_fade=1;
      break;
    default:
      led1_fade=150;
      led2_fade=150;
      break;
   
  }
  state.led_fade[0]=led1_fade;
  state.led_fade[1]=led1_fade;
  prev_pattern=state.pattern;
  if (count%680==0) {
   do_output=1;
  }
}

ISR (TIMER1_COMPA_vect)
{
  static uint8_t count;
  count++;
  // count=badrand();
  if (state.led[0]!=LED_OFF) {
    if(count < led1_fade)
      PORTB |= (1<<0);
    else
      PORTB &= ~(1<<0);
  }
  if (state.led[1]!=LED_OFF) {
    if(count < led2_fade)
      PORTB |= (1<<1);
    else
      PORTB &= ~(1<<1);
  }
  if (state.vibrate != 0) {
    if(count < motor_fade)
      PORTD |= (1<<5);
    else
      PORTD &= ~(1<<5);
  }
}

int handle_rawhid_packet(state_t *s) {
  uint8_t btn = state.button;
  memcpy((void *)&state, s, sizeof(*s));
  state.button=btn;
  do_output=1;
  return 0;
}

int handle_button(void) {
  if (PINF & (1<<0)) {
    //button unpressed
    button_down=0;
    if (state.button==1)
      do_output=1;
    state.button=0;
  } else {
    if (button_down==0) {
      button_down=1;
      do_output=1;
      state.button=1;
    }
    else {
      state.button=0;
    }
  }
  return 0;
}

void apply_state(volatile state_t *s) {
  if (s->led[0]==LED_OFF)
    PORTB &= ~(1<<0);

  if (s->led[1]==LED_OFF)
    PORTB &= ~(1<<1);
  
  if (s->vibrate==MOTOR_OFF)
    PORTD &= ~(1<<5);
  
  if (s->buzz==BUZZER_OFF)
    PORTD &= ~(1<<4);
}
