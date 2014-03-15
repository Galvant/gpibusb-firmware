/*
* GPIBUSB Adapter
* usb_to_gpib.h
**
* © 2013-2014 Steven Casagrande (scasagrande@galvant.ca).
*
* This file is a part of the GPIBUSB Adapter project.
* Licensed under the AGPL version 3.
**
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU Affero General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU Affero General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
**
*
* This code requires the CCS compiler from ccsinfo.com to compile.
* A precompiled hex file is included at github.com/Galvant/gpibusb-firmware
*/

#define DIO1 PIN_B0
#define DIO2 PIN_B1
#define DIO3 PIN_B2
#define DIO4 PIN_B3
#define DIO5 PIN_B4
#define DIO6 PIN_B5
#define DIO7 PIN_B6
#define DIO8 PIN_B7

#define REN PIN_E1
#define EOI PIN_A2
#define DAV PIN_A3
#define NRFD PIN_A4
#define NDAC PIN_A5
#define ATN PIN_A1
#define SRQ PIN_A0
#define IFC PIN_E0

#define SC PIN_D7
#define TE PIN_D6
#define PE PIN_D5
#define DC PIN_D4

#define LED_ERROR PIN_C5

#define CMD_DCL 0x14
#define CMD_UNL 0x3f
#define CMD_UNT 0x5f
#define CMD_GET 0x8
#define CMD_SDC 0x04
#define CMD_LLO 0x11
#define CMD_GTL 0x1
#define CMD_SPE 0x18
#define CMD_SPD 0x19

extern char gpib_cmd( char *bytes, int length );
extern char _gpib_write( char *bytes, int length, BOOLEAN attention, BOOLEAN useEOI);

extern char gpib_receive( char *byt );
