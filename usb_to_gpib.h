/*
* USB to GPIB Adapter
* usb_to_gpib.h
*
* Original author: Steven Casagrande (stevencasagrande@gmail.com)
* 2012 
*
* This work is released under the Creative Commons Attribution-Sharealike 3.0 license.
* See http://creativecommons.org/licenses/by-sa/3.0/ or the included license/LICENSE.TXT file for more information.
*
* Attribution requirements can be found in license/ATTRIBUTION.TXT
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
