pyuuta-tutor-rom-emulator
=========================

ROM cartridge emulator for the Tomy Pyuuta (and most likely the Tomy Tutor and
other related machines) using a STM32F407 board. 

More info at   https://www.kernelcrash.com/blog/the-tomy-pyuuta

Based on the ti994a-rom-grom-ram-and-floppy-emulator and other ROM emulators at kernelcrash.com


Overview
--------
- Emulates ROM cartridges (8K or 16K only, not the 32K ones)
- Take a cheap STM32F407 board (US$10 or so 'before the chip shortage'). ebay/aliexpress list several
  different STM32F407VET6/VGT6 boards with micro-SD adapters. Put it on some proto board and insert into
  the Pyuuta cartridge slot on top (I only have a Tomy Pyuuta, so have not tested it on the other
  related models).
- Make a FAT32 partition on a micro SD card and put rom and dsk disk images on it.
- Plug the micro SD into the STM32F4 board.
- The STM32F4 board presents a rom in real time


Wiring it
---------

Using a STM32F407VET6 or STM32F407VGT6 board
```
   PA2         - wire a button between it and GND. No need for a pullup. This is 'NEXT'
   PA3         - wire a button between it and GND. No need for a pullup. This is 'PREV'

   PE15 to PE2 - A2(MSB) to A15(LSB)
   PD15 to PD8 - D0(MSB) to D7(LSB)

   PC0         - _CS_CROM0
   PC1         - _WE/CRUCLK

   GND         - GND

```
Wire the board into the cartridge slot on top (I used a prototype board with an edge 
connector to do this).  Pins 1 and 2 are closest to the rear right corner of the Pyuuta when looking at the 
computer front-on with the keyboard closest to you.
```

       BACK ROW PINS          FRONT ROW PINS
       -------------          --------------

       1 GND - GND            2 GND - GND
       3 D7 - PD8             4 N.C.
       5 D6 - PD9             6 N.C.
       7 D5 - PD10            8 A15 / CRUOUT - PE0
       9 D4 - PD11           10 A13 - PE2
      11 D3 - PD12           12 A12 - PE3
      13 D2 - PD13           14 A11 - PE4
      15 D1 - PD14           16 A10 - PE5
      17 D0 - PD15           18 A9  - PE6
      19 VCC                 20 A8  - PE7
      21 _CS_CROM1           22 A7  - PE8
      23 A14 - PE1           24 A3  - PE12
      25 A2 - PE13           26 A6  - PE9
      27 GROMCLK             28 A5  - PE10
      29 _DBIN               30 A4  - PE11
      31 _WE / CRUCLK - PC1  32 N.C.
      33 N.C.                34 N.C.
      35 _CS_CROM0 - PC0     36 N.C.

 NB: I think _WE/CRUCLK is on a different pin on the Pyuuta Jr.
```
If you get a board with a microSD card slot, then the 'standard' wiring of the SD adapter
is fine.

I usually power the STM32F407 board from USB, but you should be able to power it from the cartridge connector. Just connect VCC to 
the +5V of STM32F407 board.

The  DEVEBOX STM32F407 board I used during development has an LED attached to PA1, so various errors will result in PA1 flashing.

It is simpler if you just tie the GND pins of the STM32F407 board to the Pyuuta , and power the STM32F407 board seperately from a 
USB cable. This just makes it easier to cycle through games on the SD card while the Pyuuta is turned off.

Setting up the micro SD card and using it
-----------------------------------------

I tend to format a micro SD card with a smallish partition (less than 1GB) with 
FAT32. 
```
  - Create a directory called 'pyuuta' in the root of the SD card
  - Copy ROM cartridge images to the pyuuta directory. The file extension does not matter.
```

Power on the STM32F407 board first. Then power on the Pyuuta.

Initially, the first file that you copied to the pyuuta directory on the SD card will appear as a cartridge to the Tutor/Pyuuta.
At the main 'GRAPHIC/G-BASIC/CARTRIDGE' screen select cartridge and the game should start.  The NEXT and PREV buttons allow you to
step through the list of cartridge images in the pyuuta directory. I tend to use one of two ways
- If the STM32F407 board is powered seperately via USB, power off the Tutor/Pyuuta and press NEXT or PREV one or more times. Then power
on the Tutor/Pyuuta and select CARTRIDGE from the menu.
- You can 'get away' with pressing NEXT and PREV while the Tutor/Pyuuta is powered on by ; first press the MON key. The Tutor/Pyuuta
should reset. When you get to the 'GRAPHIC/G-BASIC/CARTRIDGE' screen press the NEXT and PREV buttons one or more times, then select
CARTRIDGE.

The order you step through the files in the pyuuta directory are the order in which you copied the ROM image files to the SD card. 
ie. not alphabetical order.

Compiling the firmware
----------------------

Use the ARM GNU Toolchain from developer.arm.com. I only use linux for compiling, so have no idea how to do it on other platforms.

You will need the STM32F4DISCOVERY board firmware package from st.com. This is called STSW-STM32068.

Just type 'make' to build the firmware.



Copying the firmware to the STM32F407 board
-------------------------------------------

There are lots of options for writing firmware with the stm32 chips. There is 
an example of using dfu-util in the transfer.sh script. In order for this to 
work you would need to set the BOOT0 and or BOOT1 settings such that plugging
the board in via usb will show a DFU device. Then you can run transfer.sh. Remove
the BOOT0 or BOOT1 jumpers after you do this.

Technical
---------

This project is really just a very cut down version of the code for the TI99/4a project
(ti994a-rom-grom-ram-and-floppy-emulator). Look at that project for more technical info.

Thanks
------

- The floodgap site ; https://www.floodgap.com/retrobits/tomy/
- Enri's home page ; http://www43.tok2.com/home/cmpslv/index.htm (you might need to grab it off archive.org)




