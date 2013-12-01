/*
* GPIBUSB Adapter
* usb_to_gpib.c
**
* © 2013 Steven Casagrande (scasagrande@galvant.ca).
*
* This file is a part of the InstrumentKit project.
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
#fuses HS, NOPROTECT, NOLVP, WDT, WDT512
#use delay(clock=18432000)
#use rs232(baud=460800,uart1)

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "usb_to_gpib.h"

char version[10] = "3";

char cmd_buf[64], buf[64], newBuf[64];
int partnerAddress, myAddress;

char newCmd = 0;
char eos = 1; // Default end of string character.
char eoiUse = 1; // By default, we are using EOI to signal end of 
                 // msg from instrument
byte strip = 0;
char autoread = 1;

#define INTS_PER_SECOND 3
byte int_count, timeoutPeriod, timeout;
int seconds;

#define WITH_TIMEOUT
#define WITH_WDT
//#define VERBOSE_DEBUG

#int_rtcc
void clock_isr() {
	if(--int_count==0) {
		++seconds;
		int_count=INTS_PER_SECOND;
	}
}

#int_rda
RDA_isr()
{
	gets(buf);
	newCmd = 1;
}

// Puts all the GPIB pins into their correct initial states.
void prep_gpib_pins() {
	output_low(TE); // Disables talking on data and handshake lines
	//output_high(PE); // Enable dataline pullup resistors
	output_low(PE);

	output_high(SC); // Allows transmit on REN and IFC
	output_low(DC); // Transmit ATN and receive SRQ

	output_float(DIO1);
	output_float(DIO2);
	output_float(DIO3);
	output_float(DIO4);
	output_float(DIO5);
	output_float(DIO6);
	output_float(DIO7);
	output_float(DIO8);
	
	output_high(ATN);
	output_float(EOI);
	output_float(DAV);
	output_low(NRFD);
	output_low(NDAC);
	output_high(IFC);
	output_float(SRQ);
	output_high(REN);
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
	return _gpib_write(bytes, length, 1);
}

char gpib_write(char *bytes, int length) {
    // Write a GPIB data string to the bus
	return _gpib_write(bytes, length, 0);
}

char _gpib_write(char *bytes, int length, BOOLEAN attention) {
    /* 
    * Write a string of bytes to the bus
    * bytes: array containing characters to be written
    * length: number of bytes to write, 0 if not known.
    * attention: 1 if this is a gpib command, 0 for data
    */
	char a; // Storage variable for the current character
	int i; // Loop counter variable

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
	
	for(i = 0;i < length;i++) { //Loop through each character, write to bus
		a = bytes[i]; // So I don't have to keep typing bytes[i]
		
		output_float(NDAC);
		
		
		// Wait for NDAC to go low, indicating previous bit is now done with
#ifdef WITH_TIMEOUT
		seconds = 0;
		timeout = seconds + timeoutPeriod;
		while(input(NDAC) && (seconds <= timeout)) {
			if(seconds >= timeout) {
				printf("Timeout: Waiting for NDAC to go low while writing\n");
				return 1;
			}
		}
#else
		while(input(NDAC)){} 
#endif

		// Put the byte on the data lines
		a = a^0xff;
		output_b(a);
	
		output_float(NRFD);

		// Wait for listeners to be ready for data (NRFD should be high)
#ifdef WITH_TIMEOUT
		seconds = 0;
		timeout = seconds + timeoutPeriod;
		while(!(input(NRFD)) && (seconds <= timeout)) {
			if(seconds >= timeout) {
				printf("Timeout: Waiting for NRFD to go high while writing\n");
				return 1;
			}
		}
#else		
		while(!(input(NRFD))){}
#endif
		
		if((i==length-1) && !(attention)) { // If last byte in string
			output_low(EOI); // Assert EOI
		}
		
		output_low(DAV); // Inform listeners that the data is ready to be read

		
		// Wait for NDAC to go high, all listeners have accepted the byte
#ifdef WITH_TIMEOUT
		seconds = 0;
		timeout = seconds + timeoutPeriod;
		while(!(input(NDAC)) && (seconds <= timeout)) {
			if(seconds >= timeout) {
				printf("Timeout: Waiting for NDAC to go high while writing\n");
				return 1;
			}
		}
#else
		while(!(input(NDAC))){} 
#endif
		
		output_float(DAV); // Byte has been accepted by all, indicate 
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
		output_float(ATN); // Release ATN line
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
	timeout = seconds + timeoutPeriod;
	while(input(DAV) && (seconds <= timeout)) {
		if(seconds >= timeout) {
			printf("Timeout: Waiting for DAV to go low while reading\n");
			return 0xff;
		}
	}
#else
	while(input(DAV)) {} 
#endif
	
	// Assert NRFD, informing talker to not change the data lines
	output_low(NRFD); 
		
	// Read port B, where the data lines are connected	
	a = input_b();
	a = a^0xff; // Flip all bits since GPIB uses negative logic.
	
	// Un-assert NDAC, informing talker that we have accepted the byte
	output_float(NDAC); 

	// Wait for DAV to go high (talker knows that we have read the byte)
#ifdef WITH_TIMEOUT
	timeout = seconds + timeoutPeriod;
	while(!(input(DAV)) && (seconds<=timeout) ) {
		if(seconds >= timeout) {
			printf("Timeout: Waiting for DAV to go high while reading\n");
			return 0xff;
		}
	}
#else
	while(!(input(DAV))) {} 
#endif
	
	// Prep for next byte, we have not accepted anything
	output_low(NDAC);
	
	eoiStatus = input(EOI);
	
	*byt = a;
	
	return eoiStatus;
}

char gpib_read(void) {
	char readCharacter,eoiFound;
	char readBuf[100];
	char i = 0, j=0;
	char errorFound = 0;	

	char *bufPnt;
	bufPnt = &readBuf[0];
	
	#ifdef VERBOSE_DEBUG
	printf("gpib_read start\n\r");
	#endif
	
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
	if(eoiUse == 1){
		do {
			eoiFound = gpib_receive(&readCharacter);
			if(eoiFound==0xff){return 1;}
			readBuf[i] = readCharacter; // Copy the read char into the buffer
			i++;
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

		} while (eoiFound);

		for(j=0;j<i-strip;++j){
			putc(*bufPnt);
			++bufPnt;
		}
	} else {
		do {
			eoiFound = gpib_receive(&readCharacter);
			if(eoiFound==0xff){return 1;}
			readBuf[i] = readCharacter; // Copy the read char into the buffer
			i++;
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

		} while (readCharacter != eos);

		for(j=0;j<i-strip;++j){
			putc(*bufPnt);
			++bufPnt;
		}
	}
	
	if(eos != "\r"){
		printf("\r"); // Include a CR to signal end of serial transmission
	}
	
	#ifdef VERBOSE_DEBUG
	printf("gpib_read loop end\n\r");
	#endif
	
	errorFound = 0;
	// Command all talkers and listeners to stop
	cmd_buf[0] = CMD_UNT;
	errorFound = errorFound || gpib_cmd(cmd_buf, 1);
	cmd_buf[0] = CMD_UNL;
	errorFound = errorFound || gpib_cmd(cmd_buf, 1);
	
	#ifdef VERBOSE_DEBUG
	printf("gpib_read end\n\r");
	#endif

	return errorFound;
}

void main(void) {
	char compareBuf[10];
	char writeError;
	
	char addressBuf[4] = "+a:";
	char timeoutBuf[4] = "+t:";
	char eosBuf[6] = "+eos:";
	char eoiBuf[6] = "+eoi:";
	char testBuf[6] = "+test";
	char readCmdBuf[6] = "+read";
	char getCmdBuf[5] = "+get";
	char stripBuf[8] = "+strip:";
	char versionBuf[5] = "+ver";
	char autoReadBuf[11] = "+autoread:"
	
	output_high(LED_ERROR); // Turn on the error LED
	
	// Setup the Watchdog Timer
#ifdef WITH_WDT
	setup_wdt(WDT_ON);
#endif

#ifdef WITH_TIMEOUT	
	// Setup the timer
	int_count=INTS_PER_SECOND;
	set_rtcc(0);
	setup_counters(RTCC_INTERNAL, RTCC_DIV_16);
	enable_interrupts(INT_RTCC);
	enable_interrupts(INT_RDA);
	enable_interrupts(GLOBAL);
#endif
	
	timeoutPeriod = 5; // Default timeout period, in seconds
	
	// Start all the GPIB related stuff
	gpib_init(); // Initialize the GPIB Bus
	writeError = gpib_controller_assign(0x00);
	
	output_low(LED_ERROR); // Turn off the error LED
	
	#ifdef VERBOSE_DEBUG
	switch ( restart_cause() )
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
		
		if(newCmd) { // If PC is sending input
			newCmd = 0;	
			
			if(buf[0] == '+') { // Controller commands start with a +
			
				if(strncmp((char*)buf,(char*)addressBuf,3)==0) { 
					partnerAddress = atoi( (char*)(&(buf[3])) ); // Parse out the GPIB address
				}
				else if(strncmp((char*)buf,(char*)readCmdBuf,5)==0) { 
					if(gpib_read()){
						printf("Read error occured.\n\r");
					}
				}
				else if(strncmp((char*)buf,(char*)testBuf,5)==0) { 
					printf("testing\n\r");
				}
				else if(strncmp((char*)buf,(char*)timeoutBuf,3)==0) { 
					timeoutPeriod = atoi((char*)(&(buf[3]))); // Parse out the timeout period
				}
				else if(strncmp((char*)buf,(char*)eosBuf,5)==0) { 
					eos = atoi((char*)(&(buf[5]))); // Parse out the end of string byte
				}
				else if(strncmp((char*)buf,(char*)eoiBuf,5)==0) { 
					eoiUse = atoi((char*)(&(buf[5]))); // Parse out the end of string byte
				}
				else if(strncmp((char*)buf,(char*)stripBuf,7)==0) { 
					strip = atoi((char*)(&(buf[7]))); // Parse out the end of string byte
				}
				else if(strncmp((char*)buf,(char*)versionBuf,4)==0) { 
					printf("%s\r", version);
				}
				else if(strncmp((char*)buf,(char*)getCmdBuf,4)==0) { 
					// Send a Group Execute Trigger (GET) bus command
					cmd_buf[0] = CMD_GET;
					gpib_cmd(cmd_buf, 1);
				}
				else if(strncmp((char*)buf,(char*)autoReadBuf,10)==0) { 
					autoread = atoi((char*)(&(buf[10])));
				}
				else{
				    printf("Unrecognized command.\n\r");
				}
				
			} 
			else { // Not an internal command, send to bus
				
				// Command all talkers and listeners to stop
				cmd_buf[0] = CMD_UNT;
				writeError = writeError || gpib_cmd(cmd_buf, 1);
				cmd_buf[0] = CMD_UNL;
				writeError = writeError || gpib_cmd(cmd_buf, 1);
				
				// Set target device into listen mode
				cmd_buf[0] = partnerAddress + 0x20;
				writeError = writeError || gpib_cmd(cmd_buf, 1);
				
				// Set the controller into talker mode
				cmd_buf[0] = myAddress + 0x40;
				writeError = writeError || gpib_cmd(cmd_buf, 1);
				
				// Send out command to the bus
				#ifdef VERBOSE_DEBUG
				printf("gpib_write: %s\n\r",buf);
				#endif
				writeError = writeError || gpib_write(buf, 0);
				
				if(eoiUse == 0) { // If we are not using EOI, need to output 
				                  // termination byte to inst
					buf[0] = eos;
					writeError = gpib_write(buf, 1);
				}
				
				// If cmd contains a question mark -> is a query
				if(autoread){
				    if ((strchr((char*)buf, '?') != NULL) && !(writeError)) { 
					    if(gpib_read()){
						    printf("Read error occured.\n\r");
					    }
				    }
				    else if(writeError){
					    writeError = 0;
					    printf("Write error occured.\n\r");
				    }
				}
				
			}
			
		} // End of receiving PC input
	} // End of main execution loop

}
