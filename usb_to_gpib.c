/*
* GPIBUSB Adapter
* usb_to_gpib.c
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

#include <18F4520.h>
#fuses HS, NOPROTECT, NOLVP, WDT, WDT4096
#use delay(clock=18432000)
#use rs232(baud=460800,uart1)

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "usb_to_gpib.h"

#define BUF_SIZE 256
#define MAX_SAFE_LINE_SIZE 32
#define BUF_HIGH_WATER (BUF_SIZE-MAX_SAFE_LINE_SIZE)

const unsigned int version = 5;

char cmd_buf[10];
char buf[BUF_SIZE];
char *buf_in_ptr;
char *buf_out_ptr;
int lines_buffered = 0;

int partnerAddress = 1;
int myAddress;

char eos = 10; // Default end of string character.
char eos_string[3] = "";
char eos_code = 3;
char eoiUse = 1; // By default, we are using EOI to signal end of 
                 // msg from instrument
char debug = 0; // enable or disable read&write error messages

byte strip = 0;
char autoread = 1;
char eot_enable = 1;
char eot_char = 13; // default CR
char listen_only = 0;
char mode = 1;
char save_cfg = 1;
unsigned int status_byte = 0;

unsigned int32 timeout = 1000;
unsigned int32 seconds = 0;

// Variables for device mode
boolean device_talk = false;
boolean device_listen = false;
boolean device_srq = false;

// EEPROM variables
const char VALID_EEPROM_CODE = 0xAA;

#define WITH_TIMEOUT
#define WITH_WDT
//#define VERBOSE_DEBUG

#int_timer2
void clock_isr() {
	++seconds;
}

#int_rda
RDA_isr()
{
	char c;

	while (kbhit()) {
		if (buf_in_ptr == buf_out_ptr && lines_buffered > 0) {
			/* We wrapped around and caught up with the consumer.
			 * Stop reading from input.
			 * WARNING: There is data still to read, but without
			 * another interrupt in the future no one will read it.
			 */
			break;
		}

		c = getc();

		// if human readable ascii char
		if (c >= 32 && c <= 126) {
			*buf_in_ptr = c;
		} else if (c == '\n' || c == '\r') {
			*buf_in_ptr = '\0';
			lines_buffered++;
		}

		if (buf_in_ptr != buf+BUF_SIZE-1) {
			buf_in_ptr++;
		} else {
			/* whoops, ran out of buffer space
			 * more than likely we are now corrupting a message
			 */
			*buf_in_ptr = '\0';
			lines_buffered++;
			buf_in_ptr = buf;
		}
	}

	if (buf_in_ptr > buf+BUF_HIGH_WATER) {
		buf_in_ptr = buf;
	}
}

inline void buf_out_ptr_advance(void) {
	char *next;

	next = buf_out_ptr + strlen(buf_out_ptr) + 1;
	if (next > buf+BUF_HIGH_WATER) {
		next = buf;
	}
	disable_interrupts(INT_RDA);
	buf_out_ptr = next;
	lines_buffered--;
	enable_interrupts(INT_RDA);
}

// Puts all the GPIB pins into their correct initial states.
void prep_gpib_pins() {
	output_low(TE); // Disables talking on data and handshake lines
	output_low(PE);
    
    if (mode) {
	    output_high(SC); // Allows transmit on REN and IFC
	    output_low(DC); // Transmit ATN and receive SRQ
	}
	else {
	    output_low(SC);
	    output_high(DC);
	}

	output_float(DIO1);
	output_float(DIO2);
	output_float(DIO3);
	output_float(DIO4);
	output_float(DIO5);
	output_float(DIO6);
	output_float(DIO7);
	output_float(DIO8);
	
	if (mode) {
	    output_high(ATN);
	    output_float(EOI);
	    output_float(DAV);
	    output_low(NRFD);
	    output_low(NDAC);
	    output_high(IFC);
	    output_float(SRQ);
	    output_low(REN);
	}
	else {
	    output_float(ATN);
	    output_float(EOI);
	    output_float(DAV);
	    output_float(NRFD);
	    output_float(NDAC);
	    output_float(IFC);
	    output_float(SRQ);
	    output_float(REN);
	}
}

void gpib_init() {
	
	prep_gpib_pins(); // Put all the pins into high-impedance mode
	
	//output_low(NRFD); // ?? Needed ??
	output_high(NDAC); // ?? Needed ??
	
}

char gpib_controller_assign(int address) {
	myAddress = address;
	
	output_low(IFC); // Assert interface clear. Resets bus and makes it 
	                 // controller in charge.
	delay_ms(200);
	output_float(IFC); // Finishing clearing interface
	
	output_low(REN); // Put all connected devices into "remote" mode
	cmd_buf[0] = CMD_DCL;
	return gpib_cmd(cmd_buf, 1); // Send GPIB DCL cmd, clear all devices on bus
}

char gpib_cmd(char *bytes, int length) {
    // Write a GPIB CMD byte to the bus
	return _gpib_write(bytes, length, 1, 0);
}

char gpib_write(char *bytes, int length, useEOI) {
    // Write a GPIB data string to the bus
	return _gpib_write(bytes, length, 0, useEOI);
}

char _gpib_write(char *bytes, int length, BOOLEAN attention, BOOLEAN useEOI) {
    /* 
    * Write a string of bytes to the bus
    * bytes: array containing characters to be written
    * length: number of bytes to write, 0 if not known.
    * attention: 1 if this is a gpib command, 0 for data
    */
	char byte;
	int i;

	output_high(PE);
	
	if(attention) // If byte is a gpib bus command
	{
		output_low(ATN); // Assert the ATN line, informing all 
		                 // this is a cmd byte.
	}
	
	if(length==0) // If the length was unknown
	{
		length = strlen((char*)bytes); // Calculate the number of bytes to 
		                               // be sent
	}
	
	output_high(TE); // Enable talking
	
	output_high(EOI);
	output_high(DAV);
	output_float(NRFD);
	output_float(NDAC);
	
	// Before we start transfering, we have to make sure that NRFD is high
	// and NDAC is low
    #ifdef WITH_TIMEOUT
	seconds = 0;
	enable_interrupts(INT_TIMER2);
	while((input(NDAC) || !(input(NRFD))) && (seconds <= timeout)) {
	    restart_wdt();
		if(seconds >= timeout) {
		    if (debug == 1) {
			    printf("Timeout: Before writing %c %x ", bytes[0], bytes[0]);
			}
			device_talk = false;
			device_srq = false;
			prep_gpib_pins();
			return 1;
		}
	}
	disable_interrupts(INT_TIMER2);
    #else
	while(input(NDAC)){} 
    #endif
	
	
	
	for(i = 0;i < length;i++) { //Loop through each character, write to bus
		byte = bytes[i];
		
		#ifdef VERBOSE_DEBUG
		printf("Writing byte: %c %x %c", byte, byte, eot_char);
		#endif
		
		// Wait for NDAC to go low, indicating previous bit is now done with
    #ifdef WITH_TIMEOUT
		seconds = 0;
		enable_interrupts(INT_TIMER2);
		while(input(NDAC) && (seconds <= timeout)) {
		    restart_wdt();
			if(seconds >= timeout) {
			    if (debug == 1) {
				    printf("Timeout: Waiting for NDAC to go low while writing%c", eot_char);
				}
				device_talk = false;
				device_srq = false;
				prep_gpib_pins();
				return 1;
			}
		}
		disable_interrupts(INT_TIMER2);
    #else
		while(input(NDAC)){} 
    #endif

		// Put the byte on the data lines
		output_b(~byte);

		output_float(NRFD);

		// Wait for listeners to be ready for data (NRFD should be high)
    #ifdef WITH_TIMEOUT
		seconds = 0;
		enable_interrupts(INT_TIMER2);
		while(!(input(NRFD)) && (seconds <= timeout)) {
		    restart_wdt();
			if(seconds >= timeout) {
			    if (debug == 1) {
				    printf("Timeout: Waiting for NRFD to go high while writing%c", eot_char);
			    }
			    device_talk = false;
			    device_srq = false;
			    prep_gpib_pins();
				return 1;
			}
		}
		disable_interrupts(INT_TIMER2);
    #else
		while(!(input(NRFD))){}
    #endif
		
		if((i==length-1) && (useEOI)) { // If last byte in string
			output_low(EOI); // Assert EOI
		}
		
		output_low(DAV); // Inform listeners that the data is ready to be read

		
		// Wait for NDAC to go high, all listeners have accepted the byte
    #ifdef WITH_TIMEOUT
		seconds = 0;
		enable_interrupts(INT_TIMER2);
		while(!(input(NDAC)) && (seconds <= timeout)) {
		    restart_wdt();
			if(seconds >= timeout) {
			    if (debug == 1) {
			        printf("Timeout: Waiting for NDAC to go high while writing%c", eot_char);
			    }
			    device_talk = false;
			    device_srq = false;
			    prep_gpib_pins();
				return 1;
			}
		}
		disable_interrupts(INT_TIMER2);
    #else
		while(!(input(NDAC))){} 
    #endif
		
		output_high(DAV); // Byte has been accepted by all, indicate 
		                   // byte is no longer valid
		
	} // Finished outputing all bytes to listeners

	output_low(TE); // Disable talking on datalines

	// Float all data lines 
	output_float(DIO1);
	output_float(DIO2);
	output_float(DIO3);
	output_float(DIO4);
	output_float(DIO5);
	output_float(DIO6);
	output_float(DIO7);
	output_float(DIO8);
	
	if(attention) { // If byte was a gpib cmd byte
		output_high(ATN); // Release ATN line
	}
	
	output_float(DAV);
	output_float(EOI);
	output_high(NDAC);
	output_high(NRFD);

	output_low(PE);
	
	return 0;
	
}

char gpib_receive(char *byt) {
	char a = 0; // Storage for received character
	char eoiStatus; // Returns 0x00 or 0x01 depending on status of EOI line

	// Raise NRFD, telling the talker we are ready for the byte
	output_high(NRFD);
	
	// Assert NDAC informing the talker we have not accepted the byte yet
	output_low(NDAC);
	
	output_float(DAV);
	
	// Wait for DAV to go low (talker informing us the byte is ready)
    #ifdef WITH_TIMEOUT
    seconds = 0;
    enable_interrupts(INT_TIMER2);
	while(input(DAV) && (seconds <= timeout)) {
	    restart_wdt();
		if(seconds >= timeout) {
		    if (debug == 1) {
			    printf("Timeout: Waiting for DAV to go low while reading%c", eot_char);
		    }
		    device_listen = false;
		    prep_gpib_pins();
			return 0xff;
		}
	}
	disable_interrupts(INT_TIMER2);
    #else
	while(input(DAV)) {} 
    #endif
	
	// Assert NRFD, informing talker to not change the data lines
	output_low(NRFD); 
		
	// Read port B, where the data lines are connected	
	a = input_b();
	a = a^0xff; // Flip all bits since GPIB uses negative logic.
	eoiStatus = input(EOI);
	
	#ifdef VERBOSE_DEBUG
	printf("Got byte: %c %x ", a, a);
	#endif
	
	// Un-assert NDAC, informing talker that we have accepted the byte
	output_float(NDAC); 

	// Wait for DAV to go high (talker knows that we have read the byte)
    #ifdef WITH_TIMEOUT
    seconds = 0;
    enable_interrupts(INT_TIMER2);
	while(!(input(DAV)) && (seconds<=timeout) ) {
	    restart_wdt();
		if(seconds >= timeout) {
		    if (debug == 1){
			    printf("Timeout: Waiting for DAV to go high while reading%c", eot_char);
		    }
		    device_listen = false;
		    prep_gpib_pins();
			return 0xff;
		}
	}
	disable_interrupts(INT_TIMER2);
    #else
	while(!(input(DAV))) {} 
    #endif
	
	// Prep for next byte, we have not accepted anything
	output_low(NDAC);
	
	#ifdef VERBOSE_DEBUG
	printf("EOI: %c%c", eoiStatus, eot_char);
	#endif
	
	*byt = a;
	
	return eoiStatus;
}

char gpib_read(boolean read_until_eoi) {
	char readCharacter,eoiStatus;
	char readBuf[100];
	char *bufPnt = readBuf;
	char i = 0, j=0;
	char errorFound = 0;
	boolean reading_done = false;	

	#ifdef VERBOSE_DEBUG
	printf("gpib_read start\n\r");
	#endif
	
	if (mode) {
	    // Command all talkers and listeners to stop
	    cmd_buf[0] = CMD_UNT;
	    errorFound = errorFound || gpib_cmd(cmd_buf, 1);
	    cmd_buf[0] = CMD_UNL;
	    errorFound = errorFound || gpib_cmd(cmd_buf, 1);
	    if(errorFound){return 1;}
	
	    // Set the controller into listener mode
	    cmd_buf[0] = myAddress + 0x20;
	    errorFound = errorFound || gpib_cmd(cmd_buf, 1);
	    if(errorFound){return 1;}
	
	    // Set target device into talker mode
	    cmd_buf[0] = partnerAddress + 0x40;
	    errorFound = gpib_cmd(cmd_buf, 1);
	    if(errorFound){return 1;}
	}
	
	i = 0;
	bufPnt = &readBuf[0];
	

	/*
	* In this section you will notice that I buffer the received characters, 
	* then manually iterate the pointer through the buffer, writing them to 
	* UART. If I instead just tried to printf the entire 'string' it would 
	* fail. (even if I add a null char at the end). This is because when 
	* transfering binary data, some actual data points can be 0x00.
	*
	* The other option of going putc(readBuf[x]);x++; Is for some reason slower 
	* than getting a pointer on the first element, then iterating that pointer 
	* through the buffer (as I have done here).
	*/
	#ifdef VERBOSE_DEBUG
	printf("gpib_read loop start\n\r");
	#endif
	if(read_until_eoi == 1){
		do {
			eoiStatus = gpib_receive(&readCharacter); // eoiStatus is line lvl
			if(eoiStatus==0xff){return 1;}
			if (eos_code != 0) {
			    if((readCharacter != eos_string[0]) || (eoiStatus)){ // Check for EOM char
			        readBuf[i] = readCharacter; //Copy the read char into the buffer
			        i++;
			    }
			}
			else {
			    if((readCharacter == eos_string[1]) && (eoiStatus == 0)) {
			        if (readBuf[i-1] == eos_string[0]) {
			            i--;
			        }
			    }
			    else {
			        readBuf[i] = readCharacter;
			        i++;
			    }
			}
			if(i == 100){
				for(j=0;j<100;++j){
					putc(*bufPnt);
					++bufPnt;
				}
				i = 0;
				bufPnt = &readBuf[0];
				#ifdef WITH_WDT
				restart_wdt();
				#endif
			}

		} while (eoiStatus);

		for(j=0;j<i-strip;++j){
			putc(*bufPnt);
			++bufPnt;
		}
	} else {
		do {
			eoiStatus = gpib_receive(&readCharacter);
			if(eoiStatus==0xff){return 1;}
			if (eos_code != 0) {
			    if(readCharacter != eos_string[0]){ // Check for EOM char
			        readBuf[i] = readCharacter; //Copy the read char into the buffer
			        i++;
			    }
			    else {
			        reading_done = true;
			    }
			}
			else {
			    if(readCharacter == eos_string[1]) {
			        if (readBuf[i-1] == eos_string[0]) {
			            i--;
			            reading_done = true;
			        }
			    }
			    else {
			        readBuf[i] = readCharacter;
			        i++;
			    }
			}
			if(i == 100){
				for(j=0;j<100;++j){
					putc(*bufPnt);
					++bufPnt;
				}
				i = 0;
				bufPnt = &readBuf[0];
				#ifdef WITH_WDT
				restart_wdt();
				#endif
			}

		} while (reading_done == false);
		reading_done = false;

		for(j=0;j<i-strip;++j){
			putc(*bufPnt);
			++bufPnt;
		}
	}
	
	if (eot_enable == 1) {
		printf("%c", eot_char);
	}
	
	#ifdef VERBOSE_DEBUG
	printf("gpib_read loop end\n\r");
	#endif
	
	if (mode) {
	    errorFound = 0;
	    // Command all talkers and listeners to stop
	    cmd_buf[0] = CMD_UNT;
	    errorFound = errorFound || gpib_cmd(cmd_buf, 1);
	    cmd_buf[0] = CMD_UNL;
	    errorFound = errorFound || gpib_cmd(cmd_buf, 1);
	}
	
	#ifdef VERBOSE_DEBUG
	printf("gpib_read end\n\r");
	#endif
    
	return errorFound;
}

char addressTarget(int address) {
    /*
    * Address the currently specified GPIB address (as set by the ++addr cmd)
    * to listen
    */
    char writeError = 0;
    cmd_buf[0] = CMD_UNT;
	writeError = writeError || gpib_cmd(cmd_buf, 1);
    cmd_buf[0] = CMD_UNL; // Everyone stop listening
    writeError = writeError || gpib_cmd(cmd_buf, 1);
    cmd_buf[0] = address + 0x20;
    writeError = writeError || gpib_cmd(cmd_buf, 1);
    return writeError;
}

boolean srq_state(void) {
    return !((boolean)input(SRQ));
}

void serial_poll(int address) {
    char error = 0;
    char status_byte;
    cmd_buf[0] = CMD_SPE; // enable serial poll
	error = error || gpib_cmd(cmd_buf, 1);
	cmd_buf[0] = address + 0x40;
    error = error || gpib_cmd(cmd_buf, 1);
    if (error) return;
    error = gpib_receive(&status_byte);
    if (error == 1) error = 0; // gpib_receive returns EOI lvl and 0xFF on errors
    if (error == 0xFF) error = 1;
    cmd_buf[0] = CMD_SPD; // disable serial poll
	gpib_cmd(cmd_buf, 1);
	if (!error)
	    printf("%c%c", status_byte, eot_char);
}

// Original Command Set
char addressBuf[4] = "+a:";
char timeoutBuf[4] = "+t:";
char eosBuf[6] = "+eos:";
char eoiBuf[6] = "+eoi:";
char testBuf[6] = "+test";
char readCmdBuf[6] = "+read";
char getCmdBuf[5] = "+get";
char stripBuf[8] = "+strip:";
char versionBuf[5] = "+ver";
char autoReadBuf[11] = "+autoread:";
char resetBuf[7] = "+reset";
char debugBuf[8] = "+debug:";

// Prologix Compatible Command Set
char addrBuf[7] = "++addr";
char autoBuf[7] = "++auto";
char clrBuf[6] = "++clr";
char eotEnableBuf[13] = "++eot_enable";
char eotCharBuf[11] = "++eot_char";
char ifcBuf[6] = "++ifc";
char lloBuf[6] = "++llo";
char locBuf[6] = "++loc";
char lonBuf[6] = "++lon"; //TODO: Listen mode
char modeBuf[7] = "++mode";
char readTimeoutBuf[14] = "++read_tmo_ms";
char rstBuf[6] = "++rst";
char savecfgBuf[10] = "++savecfg";
char spollBuf[8] = "++spoll";
char srqBuf[6] = "++srq";
char statusBuf[9] = "++status";
char trgBuf[6] = "++trg";
char verBuf[6] = "++ver";
char helpBuf[7] = "++help"; //TODO

inline void process_line_from_pc(void)
{
	char writeError = 0;
	char *buf_pnt = buf_out_ptr;

	if(*buf_pnt == '+') { // Controller commands start with a +
		// +a:N
		if(strncmp(buf_pnt, addressBuf, 3)==0) {
			partnerAddress = atoi(buf_pnt+3); // Parse out the GPIB address
		}
		// ++addr N
		else if(strncmp(buf_pnt, addrBuf, 6)==0) {
			if (*(buf_pnt+6) == 0x00) {
				printf("%i%c", partnerAddress, eot_char);
			}
			else if (*(buf_pnt+6) == 32) {
				partnerAddress = atoi(buf_pnt+7);
			}
		}
		// +t:N
		else if(strncmp(buf_pnt, timeoutBuf, 3)==0) {
			timeout = atoi32(buf_pnt+3); // Parse out the timeout period
		}
		// ++read_tmo_ms N
		else if(strncmp(buf_pnt, readTimeoutBuf, 13)==0) {
			if (*(buf_pnt+13) == 0x00) {
				printf("%Lu%c", timeout, eot_char);
			}
			else if (*(buf_pnt+13) == 32) {
				timeout = atoi32(buf_pnt+14);
			}
		}
		// +read
		else if((strncmp(buf_pnt, readCmdBuf, 5)==0) && mode) {
			if(gpib_read(eoiUse)){
				if (debug == 1) {printf("Read error occured.%c", eot_char);}
				//delay_ms(1);
				//reset_cpu();
			}
		}
		// ++read
		else if((strncmp(buf_pnt+1, readCmdBuf, 5)==0) && mode) {
			if (*(buf_pnt+6) == 0x00) {
				gpib_read(false); // read until EOS condition
			}
			else if (*(buf_pnt+7) == 101) {
				gpib_read(true); // read until EOI flagged
			}
			/*else if (*(buf_pnt+6) == 32) {
			// read until specified character
			}*/
		}
		// +test
		else if(strncmp(buf_pnt, testBuf, 5)==0) {
			printf("testing%c", eot_char);
		}
		// +eos:N
		else if(strncmp(buf_pnt, eosBuf, 5)==0) {
			eos = atoi(buf_pnt+5); // Parse out the end of string byte
			eos_string[0] = eos;
			eos_string[1] = 0x00;
			eos_code = 4;
		}
		// ++eos {0|1|2|3}
		else if(strncmp(buf_pnt+1, eosBuf, 4)==0) {
			if (*(buf_pnt+5) == 0x00) {
				printf("%i%c", eos_code, eot_char);
			}
			else if (*(buf_pnt+5) == 32) {
				eos_code = atoi(buf_pnt+6);
				switch (eos_code) {
				case 0:
					eos_code = 0;
					eos_string[0] = 13;
					eos_string[1] = 10;
					eos_string[2] = 0x00;
					eos = 10;
					break;
				case 1:
					eos_code = 1;
					eos_string[0] = 13;
					eos_string[1] = 0x00;
					eos = 13;
					break;
				case 2:
					eos_code = 2;
					eos_string[0] = 10;
					eos_string[1] = 0x00;
					eos = 10;
					break;
				default:
					eos_code = 3;
					eos_string[0] = 0x00;
					eos = 0;
					break;
				}
			}
		}
		// +eoi:{0|1}
		else if(strncmp(buf_pnt, eoiBuf, 5)==0) {
			eoiUse = atoi(buf_pnt+5); // Parse out the end of string byte
		}
		// ++eoi {0|1}
		else if(strncmp(buf_pnt+1, eoiBuf, 4)==0) {
			if (*(buf_pnt+5) == 0x00) {
				printf("%i%c", eoiUse, eot_char);
			}
			else if (*(buf_pnt+5) == 32) {
				eoiUse = atoi(buf_pnt+6);
			}
		}
		// +strip:{0|1}
		else if(strncmp(buf_pnt, stripBuf, 7)==0) {
			strip = atoi(buf_pnt+7); // Parse out the end of string byte
		}
		// +ver
		else if(strncmp(buf_pnt, versionBuf, 4)==0) {
			printf("%i%c", version, eot_char);
		}
		// ++ver
		else if(strncmp(buf_pnt+1, versionBuf, 4)==0) {
			printf("Version %i.0%c", version, eot_char);
		}
		// +get
		else if((strncmp(buf_pnt, getCmdBuf, 4)==0) && mode) {
			if (*(buf_pnt+5) == 0x00) {
				writeError = writeError || addressTarget(partnerAddress);
				cmd_buf[0] = CMD_GET;
				gpib_cmd(cmd_buf, 1);
			}
			/*else if (*(buf_pnt+5) == 32) {
			  TODO: Add support for specified addresses
			  }*/
		}
		// ++trg
		else if((strncmp(buf_pnt, trgBuf, 5)==0) && mode) {
			if (*(buf_pnt+5) == 0x00) {
				writeError = writeError || addressTarget(partnerAddress);
				cmd_buf[0] = CMD_GET;
				gpib_cmd(cmd_buf, 1);
			}
			/*else if (*(buf_pnt+5) == 32) {
			  TODO: Add support for specified addresses
			  }*/
		}
		// +autoread:{0|1}
		else if(strncmp(buf_pnt, autoReadBuf, 10)==0) {
			autoread = atoi(buf_pnt+10);
		}
		// ++auto {0|1}
		else if(strncmp(buf_pnt, autoBuf, 6)==0) {
			if (*(buf_pnt+6) == 0x00) {
				printf("%i%c", autoRead, eot_char);
			}
			else if (*(buf_pnt+6) == 32) {
				autoread = atoi(buf_pnt+7);
				if ((autoread != 0) && (autoread != 1)) {
					autoread = 1; // If non-bool sent, set to enable
				}
			}
		}
		// +reset
		else if(strncmp(buf_pnt, resetBuf, 6)==0) {
			delay_ms(1);
			reset_cpu();
		}
		// ++rst
		else if(strncmp(buf_pnt, rstBuf, 5)==0) {
			delay_ms(1);
			reset_cpu();
		}
		// +debug:{0|1}
		else if(strncmp(buf_pnt, debugBuf, 7)==0) {
			debug = atoi(buf_pnt+7);
		}
		// ++debug {0|1}
		else if(strncmp(buf_pnt+1, debugBuf, 6)==0) {
			if (*(buf_pnt+7) == 0x00) {
				printf("%i%c", debug, eot_char);
			} else if (*(buf_pnt+7) == 32) {
				debug = atoi(buf_pnt+8);
				if ((debug != 0) && (debug != 1)) {
					debug = 0; // If non-bool sent, set to disabled
				}
			}
		}
		// ++clr
		else if((strncmp(buf_pnt, clrBuf, 5)==0) && mode) {
			// This command is special in that we must
			// address a specific instrument.
			writeError = writeError || addressTarget(partnerAddress);
			cmd_buf[0] = CMD_SDC;
			writeError = writeError || gpib_cmd(cmd_buf, 1);
		}
		// ++eot_enable {0|1}
		else if(strncmp(buf_pnt, eotEnableBuf, 12)==0) {
			if (*(buf_pnt+12) == 0x00) {
				printf("%i%c", eot_enable, eot_char);
			} else if (*(buf_pnt+12) == 32) {
				eot_enable = atoi(buf_pnt+13);
				if ((eot_enable != 0) && (eot_enable != 1)) {
					eot_enable = 1; // If non-bool sent, set to enable
				}
			}
		}
		// ++eot_char N
		else if(strncmp(buf_pnt, eotCharBuf, 10)==0) {
			if (*(buf_pnt+10) == 0x00) {
				printf("%i%c", eot_char, eot_char);
			} else if (*(buf_pnt+10) == 32) {
				eot_char = atoi(buf_pnt+11);
			}
		}
		// ++ifc
		else if((strncmp(buf_pnt, ifcBuf, 5)==0) && mode) {
			output_low(IFC); // Assert interface clear.
			delay_us(150);
			output_float(IFC); // Finishing clearing interface
		}
		// ++llo
		else if((strncmp(buf_pnt, lloBuf, 5)==0) && mode) {
			writeError = writeError || addressTarget(partnerAddress);
			cmd_buf[0] = CMD_LLO;
			writeError = writeError || gpib_cmd(cmd_buf, 1);
		}
		// ++loc
		else if((strncmp(buf_pnt, locBuf, 5)==0) && mode) {
			writeError = writeError || addressTarget(partnerAddress);
			cmd_buf[0] = CMD_GTL;
			writeError = writeError || gpib_cmd(cmd_buf, 1);
		}
		// ++lon {0|1}
		else if((strncmp(buf_pnt, lonBuf, 5)==0) && !mode) {
			if (*(buf_pnt+5) == 0x00) {
				printf("%i%c", listen_only, eot_char);
			} else if (*(buf_pnt+5) == 32) {
				listen_only = atoi(buf_pnt+6);
				if ((listen_only != 0) && (listen_only != 1)) {
					listen_only = 0; // If non-bool sent, set to disable
				}
			}
		}
		// ++mode {0|1}
		else if(strncmp(buf_pnt, modeBuf, 6)==0) {
			if (*(buf_pnt+6) == 0x00) {
				printf("%i%c", mode, eot_char);
			} else if (*(buf_pnt+6) == 32) {
				mode = atoi(buf_pnt+7);
				if ((mode != 0) && (mode != 1)) {
					mode = 1; // If non-bool sent, set to control mode
				}
				prep_gpib_pins();
				if (mode) {
					gpib_controller_assign(0x00);
				}
			}
		}
		// ++savecfg {0|1}
		else if(strncmp(buf_pnt, savecfgBuf, 9)==0) {
			if (*(buf_pnt+9) == 0x00) {
				printf("%i%c", save_cfg, eot_char);
			} else if (*(buf_pnt+9) == 32) {
				save_cfg = atoi(buf_pnt+10);
				if ((save_cfg != 0) && (save_cfg != 1)) {
					save_cfg = 1; // If non-bool sent, set to enable
				}
				if (save_cfg == 1) {
					write_eeprom(0x01, mode);
					write_eeprom(0x02, partnerAddress);
					write_eeprom(0x03, eot_char);
					write_eeprom(0x04, eot_enable);
					write_eeprom(0x05, eos_code);
					write_eeprom(0x06, eoiUse);
					write_eeprom(0x07, autoread);
					write_eeprom(0x08, listen_only);
					write_eeprom(0x09, save_cfg);
				}
			}
		}
		// ++srq
		else if((strncmp(buf_pnt, srqBuf, 5)==0) && mode) {
			printf("%i%c", srq_state(), eot_char);
		}
		// ++spoll N
		else if((strncmp(buf_pnt, spollBuf, 7)==0) && mode) {
			if (*(buf_pnt+7) == 0x00) {
				serial_poll(partnerAddress);
			} else if (*(buf_pnt+7) == 32) {
				serial_poll(atoi(buf_pnt+8));
			}
		}
		// ++status
		else if((strncmp(buf_pnt, statusBuf, 8)==0) && !mode) {
			if (*(buf_pnt+8) == 0x00) {
				printf("%u%c", status_byte, eot_char);
			} else if (*(buf_pnt+8) == 32) {
				status_byte = atoi(buf_pnt+9);
			}
		} else if (debug == 1) {
			printf("Unrecognized command.%c", eot_char);
		}
	} else {
		// Not an internal command, send to bus
		// Command all talkers and listeners to stop
		// and tell target to listen.
		if (mode) {
			writeError = writeError || addressTarget(partnerAddress);

			// Set the controller into talker mode
			cmd_buf[0] = myAddress + 0x40;
			writeError = writeError || gpib_cmd(cmd_buf, 1);
		}

		// Send out command to the bus
#ifdef VERBOSE_DEBUG
		printf("gpib_write: %s%c",buf_pnt, eot_char);
#endif
		if (mode || device_talk) {
			if (eos_code != 3) { // If have an EOS char, need to output
				// termination byte to inst
				writeError = writeError || gpib_write(buf_pnt, 0, 0);
				if (!writeError)
					writeError = gpib_write(eos_string, 0, eoiUse);
#ifdef VERBOSE_DEBUG
				printf("eos_string: %s",eos_string);
#endif
			} else {
				writeError = writeError || gpib_write(buf_pnt, 0, 1);
			}
		}

		// If cmd contains a question mark -> is a query
		if(autoread && mode) {
			if ((strchr(buf_pnt, '?') != NULL) && !writeError) {
				gpib_read(eoiUse);
			} else if(writeError) {
				writeError = 0;
			}
		}
	} // end of sending internal command
}

inline void do_device_mode(void)
{
	// When in device mode we should be checking the status of the 
	// ATN line to see what we should be doing
	if (!input(ATN) && !input(ATN)) {
		output_low(NDAC);
		gpib_receive(cmd_buf); // Get the CMD byte sent by the controller
		output_high(NRFD);
		if (cmd_buf[0] == partnerAddress + 0x40) {
                        device_talk = true;
#ifdef VERBOSE_DEBUG
                        printf("Instructed to talk%c", eot_char);
#endif
		}
		else if (cmd_buf[0] == partnerAddress + 0x20) {
                        device_listen = true;
#ifdef VERBOSE_DEBUG
                        printf("Instructed to listen%c", eot_char);
#endif
		}
		else if (cmd_buf[0] == CMD_UNL) {
                        device_listen = false;
#ifdef VERBOSE_DEBUG
                        printf("Instructed to stop listen%c", eot_char);
#endif
		}
		else if (cmd_buf[0] == CMD_UNT) {
                        device_talk = false;
#ifdef VERBOSE_DEBUG
                        printf("Instructed to stop talk%c", eot_char);
#endif
		}
		else if (cmd_buf[0] == CMD_SPE) {
                        device_srq = true;
#ifdef VERBOSE_DEBUG
                        printf("SQR start%c", eot_char);
#endif
		}
		else if (cmd_buf[0] == CMD_SPD) {
                        device_srq = false;
#ifdef VERBOSE_DEBUG
                        printf("SQR end%c", eot_char);
#endif
		}
		else if (cmd_buf[0] == CMD_DCL) {
                        printf("%c%c", CMD_DCL, eot_char);
                        device_listen = false;
                        device_talk = false;
                        device_srq = false;
                        status_byte = 0;
		}
		else if ((cmd_buf[0] == CMD_LLO) && (device_listen)) {
                        printf("%c%c", CMD_LLO, eot_char);
		}
		else if ((cmd_buf[0] == CMD_GTL) && (device_listen)) {
                        printf("%c%c", CMD_GTL, eot_char);
		}
		else if ((cmd_buf[0] == CMD_GET) && (device_listen)) {
                        printf("%c%c", CMD_GET, eot_char);
		}
		output_high(NDAC);
	} else {
		delay_us(10);
		if(input(ATN)) {
			if ((device_listen)) {
				output_low(NDAC);
#ifdef VERBOSE_DEBUG
				printf("Starting device mode gpib_read%c", eot_char);
#endif
				gpib_read(eoiUse);
				device_listen = false;
			}
			else if (device_talk && device_srq) {
				gpib_write(&status_byte, 1, 0);
				device_srq = false;
				device_talk = false;
			}
		}
	}
}

void main(void) {
	// Turn on the error LED
	output_high(LED_ERROR);

	// Setup the Watchdog Timer
#ifdef WITH_WDT
	setup_wdt(WDT_ON);
#endif

#ifdef WITH_TIMEOUT
	// Setup the timer
	set_rtcc(0);
	setup_timer_2(T2_DIV_BY_16,144,2); // 1ms interupt
	enable_interrupts(GLOBAL);
	disable_interrupts(INT_TIMER2);
#endif

    // Handle the EEPROM stuff
    if (read_eeprom(0x00) == VALID_EEPROM_CODE) {
        mode = read_eeprom(0x01);
        partnerAddress = read_eeprom(0x02);
        eot_char = read_eeprom(0x03);
        eot_enable = read_eeprom(0x04);
        eos_code = read_eeprom(0x05);
        switch (eos_code) {
            case 0:
                eos_code = 0;
                eos_string[0] = 13;
                eos_string[1] = 10;
                eos_string[2] = 0x00;
                eos = 10;
                break;
            case 1:
                eos_code = 1;
                eos_string[0] = 13;
                eos_string[1] = 0x00;
                eos = 13;
                break;
            case 2:
                eos_code = 2;
                eos_string[0] = 10;
                eos_string[1] = 0x00;
                eos = 10;
                break;
            default:
                eos_code = 3;
                eos_string[0] = 0x00;
                eos = 0;
                break;
        }
        eoiUse = read_eeprom(0x06);
        autoread = read_eeprom(0x07);
        listen_only = read_eeprom(0x08);
        save_cfg = read_eeprom(0x09);
    }
    else {
        write_eeprom(0x00, VALID_EEPROM_CODE);
        write_eeprom(0x01, 1); // mode
        write_eeprom(0x02, 1); // partnerAddress
        write_eeprom(0x03, 13); // eot_char
        write_eeprom(0x04, 1); // eot_enable
        write_eeprom(0x05, 3); // eos_code
        write_eeprom(0x06, 1); // eoiUse
        write_eeprom(0x07, 1); // autoread
        write_eeprom(0x08, 0); // listen_only
        write_eeprom(0x09, 1); // save_cfg
    }

	// Start all the GPIB related stuff
	gpib_init(); // Initialize the GPIB Bus
	if (mode) {
	    gpib_controller_assign(0x00);
    }

    /*
    * The following little block helps provide some visual feedback as to which
    * stage of the startup process the microcontroller is in. This is because
    * during testing I found that enabling the RDA interrupt would cause issues
    * on my dev system (ubuntu gnome edition 13.10 64bit) when first plugged
    * into the computer. This, in combination with the fact that the serial
    * port is unaccessable to my user account for approx 30sec after initial
    * enumeration (but is able to be opened by root) leads me to believe that
    * some update to ubuntu or the linux kernel or something is probing newly
    * connected usb->serial adapters. Whatever it is that my PC is sending is
    * causing the adapater to have a fit. This is probably due to a high volume
    * of RDA interrupts and the system is unable to process them before the 
    * next. I imagine maybe that in the end, "buf" is getting messed up, but
    * in the end the WDT solves the lockup issue.
    *
    * Note this problem is only on initial USB connection and not when pushing 
    * the reset button.
    *
    * UPDATE: It turns out this is due to the software package "modemmanager".
    * The easiest solution is just to remove it. On Debian-based distros run
    * apt-get purge modemmanager
    */
	output_low(LED_ERROR); // Turn off the error LED
	restart_wdt();
	delay_ms(100);
	restart_wdt();
	output_high(LED_ERROR);
	restart_wdt();
	delay_ms(100);
	restart_wdt();
	enable_interrupts(INT_RDA);
	restart_wdt();
	output_low(LED_ERROR);

	#ifdef VERBOSE_DEBUG
	switch (restart_cause())
	{
		case WDT_TIMEOUT:
		{
			printf("WDT restart\r\n");
			break;
		}
		case NORMAL_POWER_UP:
		{
			printf("Normal power up\r\n");
			break;
		}
	}
	#endif

	// Main execution loop
	for(;;) {
#ifdef WITH_WDT
		restart_wdt();
#endif
		if (lines_buffered) {
			if (*buf_out_ptr != '\0') {
				process_line_from_pc();
			}
			buf_out_ptr_advance();
		}

		if (!mode) {
			do_device_mode():
		}
	} // End of main execution loop
}
