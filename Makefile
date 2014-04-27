CUR_DIR = $(shell pwd)
all: control button install

clean:
	$(MAKE) -C button_usb clean
	$(MAKE) -C control_pc clean

button:
	$(MAKE) -C button_usb

control:
	$(MAKE) -C control_pc

install:
	/bin/ln -sf  $(CUR_DIR)/control_pc/control $(CUR_DIR)
	teensy-loader-cli -mmcu=atmega32u4 -w $(CUR_DIR)/button_usb/button.hex
