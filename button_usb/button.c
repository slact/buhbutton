/* Teensy RawHID example
 * http://www.pjrc.com/teensy/rawhid.html
 * Copyright (c) 2009 PJRC.COM, LLC
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above description, website URL and copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "shared.h"
#include "usb_rawhid.h"
#include "analog.h"

#define CPU_PRESCALE(n)  (CLKPR = 0x80, CLKPR = (n))

volatile uint8_t do_output=0;
volatile uint8_t led1_fade=0;
volatile uint8_t motor_led1_fade_thresh=0;
volatile uint8_t led2_fade=0;

volatile state_t state;
int button_down=0;
uint8_t buffer[64];

void apply_state(state_t *s);
int handle_rawhid_packet(state_t *buffer);
int handle_button(void);

int main(void)
{
  int8_t r;
  uint8_t i;
  uint16_t val, count=0;
  memset(&state, '\0', sizeof(state));
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
  static int8_t fade_dir=1;
  static uint16_t count=0;
  count++;
  switch(state.pattern){
    case LED_FADE_IN:
      if(prev_pattern!=LED_FADE_IN) {
        led1_fade=0; led2_fade=0;
      }
      if(led1_fade<255)
        led1_fade++;
      if(led2_fade<255)
        led2_fade++;
      if(led1_fade==255 && led2_fade==255)
        state.pattern=NO_PATTERN;
      break;
    case LED_FADE_OUT:
      if(led1_fade>0)
        led1_fade--;
      if(led2_fade>0)
        led2_fade++;
      if(led1_fade==0 && led2_fade==0)
        state.pattern=NO_PATTERN;
      break;
    case LED_BLINK:
      if(count%60==0) {
        led1_fade = led1_fade==0 ? 255 : 0;
        led2_fade = led2_fade==0 ? 255 : 0;
      }
      break;
    case  LED_PULSE:
      motor_led1_fade_thresh = 200 - state.pattern_speed*3;
      if (led1_fade>=256-state.pattern_speed)
        fade_dir=-1;
      else if (led1_fade<state.pattern_speed)
        fade_dir=1;
      led1_fade+= fade_dir*state.pattern_speed;
      led2_fade=led1_fade;
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
  if (count%500==0) {
   do_output=1;
  }
}

int handle_rawhid_packet(state_t *s) {
  uint8_t btn = state.button;
  memcpy(&state, s, sizeof(*s));
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
}

void apply_state(state_t *s) {
  static uint32_t buzz_count;
  if (s->led[0]==LED_OFF)
    PORTB &= ~(1<<0);
  else {
    if(rand() % 255 < led1_fade)
      PORTB |= (1<<0);
    else
      PORTB &= ~(1<<0);
  }
  if (s->led[1]==LED_OFF)
    PORTB &= ~(1<<1);
  else{
    if(rand() % 255 < led2_fade)
      PORTB |= (1<<1);
    else
      PORTB &= ~(1<<1);
  }
  
  if (s->vibrate==MOTOR_OFF)
    PORTD &= ~(1<<5);
  else {//pulse motor
    if (motor_led1_fade_thresh>0 && led1_fade >= motor_led1_fade_thresh) 
      PORTD |= (1<<5);
    else
      PORTD &= ~(1<<5);
  }
  
  if (s->buzz==BUZZER_OFF) {
    PORTD &= ~(1<<4);
  } else {
    buzz_count++;
    if (buzz_count < s->buzz/2 ) {
      PORTD &= ~(1<<4);
    }
    else if (buzz_count < s->buzz) {
      PORTD |= (1<<4);
    }
    else if (buzz_count >= s->buzz) {
      buzz_count=0;
    }
  }
}
