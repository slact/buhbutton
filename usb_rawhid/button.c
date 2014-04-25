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
#include "usb_rawhid.h"
#include "analog.h"
#include "shared.h"

#define CPU_PRESCALE(n)  (CLKPR = 0x80, CLKPR = (n))

volatile uint8_t do_output=0;
state_t state;
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
  strcpy(&state.header, "FOO");
  strcpy(&state.footer, "BAR");
  
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
    if (do_output) {
      usb_rawhid_send((uint8_t *)&state, 50);
      do_output=0;
    }
    apply_state(&state);
  }
}

// This interrupt routine is run approx 61 times per second.
ISR(TIMER0_OVF_vect)
{
  static uint8_t count=0;

  // set the do_output variable every 2 seconds
  if (++count > 122) {
    count = 0;
    do_output = 1;
  }
}

int handle_rawhid_packet(state_t *s) {
  uint8_t btn = state.button;
  memcpy(&state, s, sizeof(*s));
  state.button=btn;
  do_output=1;
  state.footer[0]="ECHO";
  return 0;
}

int handle_button(void) {
  state.pinf=PINF;
  if (PINF & (1<<0)) {
    //button unpressed
    if (state.button==1)
      do_output=1;
    state.button=0;
  } else {
    if (state.button==0)
      do_output=1;
    state.button=1;
  }
}

void apply_state(state_t *s) {
  if (s->led[0]==0)
    PORTB &= ~(1<<0);
  else
    PORTB |= (1<<0);
  
  if (s->led[1]==0)
    PORTB &= ~(1<<1);
  else
    PORTB |= (1<<1);
  
  if (s->vibrate==0)
    PORTD &= ~(1<<5);
  else
    PORTD |= (1<<5);
}
