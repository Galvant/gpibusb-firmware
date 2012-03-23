/*
* USB to GPIB Adapter
* usb_to_gpib.c
*
* Original author: Steven Casagrande (stevencasagrande@gmail.com)
* 2012 
*
* This work is released under the Creative Commons Attribution-Sharealike 3.0 license.
* See http://creativecommons.org/licenses/by-sa/3.0/ or the included license/LICENSE.TXT file for more information.
*
* Attribution requirements can be found in license/ATTRIBUTION.TXT
*
*
* This code requires the CCS compiler from ccsinfo.com to compile. A precompiled hex file is included.
*/

#include <18F4520.h>
#fuses HS, NOPROTECT, NOLVP, WDT, WDT256
#use delay(clock=18432000)
#use rs232(baud=460800,uart1)

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "usb_to_gpib.h"

char cmd_buf[64], buf[64];
int partnerAddress, myAddress;

char eos = 1; // Default end of string character.
char eoiUse = 1; // By default, we are using EOI to signal end of msg from instrument

#define INTS_PER_SECOND 2
byte int_count, timeoutPeriod, timeout;
int seconds;

#define WITH_TIMEOUT
#define WITH_WDT

#int_rtcc
void clock_isr() {
	if(--int_count==0) {
		++seconds;
		int_count=INTS_PER_SECOND;
	}
}

// Function puts all the GPIB pins into a high impedance "floating" state.
void all_pins_high() {
	output_float(DIO1);
	output_float(DIO2);
	output_float(DIO3);
	output_float(DIO4);
	output_float(DIO5);
	output_float(DIO6);
	output_float(DIO7);
	output_float(DIO8);
	
	output_float(EOI);
	output_float(DAV);
	output_float(NRFD);
	output_float(NDAC);
	output_float(IFC);
	output_float(SRQ);
	output_float(ATN);
	output_float(REN);
}

void gpib_init() {
	
	all_pins_high(); // Put all the pins into high-impedance mode
	
	output_low(NRFD); // ?? Needed ??
	output_float(NDAC); // ?? Needed ??
	
}

char gpib_controller_assign( int address ) {
	myAddress = address;
	
	output_low(IFC); // Assert interface clear. Resets bus and makes it controller in charge.
	delay_ms(200);
	output_float(IFC); // Finishing clearing interface
	
	output_low(REN); // Put all connected devices into "remote" mode
	cmd_buf[0] = CMD_DCL;
	return gpib_cmd( cmd_buf, 1); // Send GPIB DCL cmd, clear all devices on bus
}

// Write a GPIB CMD byte to the bus
char gpib_cmd( char *bytes, int length ) {
	return _gpib_write( bytes, length, 1 );
}

// Write a GPIB data string to the bus
char gpib_write( char *bytes, int length ) {
	return _gpib_write( bytes, length, 0 );
}


/* Write a string of bytes to the bus
*  bytes: array containing characters to be written
*  length: number of bytes to write, 0 if not known.
*  attention: 1 if this is a gpib command, 0 for data
*/
char _gpib_write( char *bytes, int length, BOOLEAN attention) {
	char a; // Storage variable for the current character
	int i; // Loop counter variable
	
	if(attention) // If byte is a gpib bus command
	{
		output_low(ATN); // Assert the ATN line, informing all this is a cmd byte.
	}
	
	if(length==0) // If the length was unknown
	{
		length = strlen((char*)bytes); // Calculate the number of bytes to be sent
	}
	
	output_float(EOI);
	output_float(DAV);
	output_float(NRFD);
	
	for( i = 0 ; i < length ; i++ ) { //Loop through each character, write to bus
		a = bytes[i]; // So I don't have to keep typing bytes[i]
		
		output_float(NDAC);
		
		
		// Wait for NDAC to go low, indicating previous bit is now done with
#ifdef WITH_TIMEOUT
		seconds = 0;
		timeout = seconds + timeoutPeriod;
		while( input(NDAC) && (seconds<=timeout) ) {
			if( seconds == timeout ) {
				printf("Timeout error: Waiting for NDAC to go low while writing\n\r");
				return 0xff;
			}
		}
#else
		while(input(NDAC)){} 
#endif
		
		// Put the target bit on the data lines
		if(a&0x01) {
			output_low(DIO1);
		} else {
			output_float(DIO1);
		}
		if(a&0x02) {
			output_low(DIO2);
		} else {
			output_float(DIO2);
		}
		if(a&0x04) {
			output_low(DIO3);
		} else {
			output_float(DIO3);
		}
		if(a&0x08) {
			output_low(DIO4);
		} else {
			output_float(DIO4);
		}
		if(a&0x10) {
			output_low(DIO5);
		} else {
			output_float(DIO5);
		}
		if(a&0x20) {
			output_low(DIO6);
		} else {
			output_float(DIO6);
		}
		if(a&0x40) {
			output_low(DIO7);
		} else {
			output_float(DIO7);
		}
		if(a&0x80) {
			output_low(DIO8);
		} else {
			output_float(DIO8);
		}
	
		output_float(NRFD);

		// Wait for listeners to be ready for data (NRFD should be high)
#ifdef WITH_TIMEOUT
		seconds = 0;
		timeout = seconds + timeoutPeriod;
		while( !(input(NRFD)) && (seconds<=timeout) ) {
			if( seconds == timeout ) {
				printf("Timeout error: Waiting for NRFD to go high while writing\n\r");
				return 0xff;
			}
		}
#else		
		while(!(input(NRFD))){}
#endif
		
		if ((i==length-1) && !(attention)) { // If last byte in string
			output_low(EOI); // Assert EOI
		}
		
		output_low(DAV); // Inform listeners that the data is ready to be read

		
		// Wait for NDAC to go high, all listeners have accepted the byte
#ifdef WITH_TIMEOUT
		seconds = 0;
		timeout = seconds + timeoutPeriod;
		while( !(input(NDAC)) && (seconds<=timeout) ) {
			if( seconds == timeout ) {
				printf("Timeout error: Waiting for NDAC to go high while writing\n\r");
				return 0xff;
			}
		}
#else
		while(!(input(NDAC))){} 
#endif
		
		output_float(DAV); // Byte has been accepted by all, indicate byte is no longer valid
		
	} // Finished outputing all bytes to listeners
	
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
	
	output_float(EOI);
	
	return 0x00;
	
}

char gpib_receive( char *byt ) {
	char a = 0; // Storage for received character
	char eoiStatus; // Returns 0x00 or 0x01 depending on status of EOI line
	
	// Float NRFD, telling the talker we are ready for the byte
	output_float(NRFD);
	
	// Assert NDAC informing the talker we have not accepted the byte yet
	output_low(NDAC);
	
	output_float(DAV);
	
	// Wait for DAV to go low (talker informing us the byte is ready)
#ifdef WITH_TIMEOUT
	timeout = seconds + timeoutPeriod;
	while( input(DAV) && (seconds<=timeout) ) {
		if( seconds == timeout ) {
			printf("Timeout error: Waiting for DAV to go low while reading\n\r");
			reset_cpu();
			return 0xff;
		}
	}
#else
	while( input(DAV) ) {} 
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
	while( !(input(DAV)) && (seconds<=timeout) ) {
		if( seconds == timeout ) {
			printf("Timeout error: Waiting for DAV to go high while reading\n\r");
			return 0xff;
		}
	}
#else
	while( !(input(DAV)) ) {} 
#endif
	
	// Prep for next byte, we have not accepted anything
	output_low(NDAC);
	
	eoiStatus = input(EOI);
	
	*byt = a;
	
	return eoiStatus;
}

void gpib_read(void) {
	char readCharacter,eoiFound;
	char readBuf[100];
	char i = 0, j=0;
	
	char *bufPnt;
	bufPnt = &readBuf[0];
	
	// Command all talkers and listeners to stop
	cmd_buf[0] = CMD_UNT;
	gpib_cmd( cmd_buf, 1 );
	cmd_buf[0] = CMD_UNL;
	gpib_cmd( cmd_buf, 1 );
	
	// Set the controller into listener mode
	cmd_buf[0] = myAddress + 0x20;
	gpib_cmd( cmd_buf, 1 );
	
	// Set target device into talker mode
	cmd_buf[0] = partnerAddress + 0x40;
	gpib_cmd( cmd_buf, 1 );
	
	i = 0;
	bufPnt = &readBuf[0];
	

	/*
	* In this section you will notice that I buffer the received characters, then manually
	* iterate the pointer through the buffer, writing them to UART. If I instead just tried
	* to printf the entire 'string' it would fail. (even if I add a null char at the end).
	* This is because when transfering binary data, some actual data points can be 0x00.
	*
	* The other option of going putc(readBuf[x]);x++; Is for some reason slower than getting
	* a pointer on the first element, then iterating that pointer through the buffer (as I
	* have done here).
	*/
	if( eoiUse == 1 ){
		do {
			eoiFound = gpib_receive(&readCharacter);
			readBuf[i] = readCharacter; // Copy the read character into the buffer
			i++;
			if( i == 100 ){
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

		} while ( eoiFound );

		for(j=0;j<i;++j){
			putc(*bufPnt);
			++bufPnt;
		}
	} else {
		do {
			eoiFound = gpib_receive(&readCharacter);
			readBuf[i] = readCharacter; // Copy the read character into the buffer
			i++;
			if( i == 100 ){
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

		} while ( readCharacter != eos );

		for(j=0;j<i;++j){
			putc(*bufPnt);
			++bufPnt;
		}
	}
	
	if( eos != "\r" ){
		printf("\r"); // Include a CR to signal end of serial transmission
	}
	
	
	// Command all talkers and listeners to stop
	cmd_buf[0] = CMD_UNT;
	gpib_cmd( cmd_buf, 1 );
	cmd_buf[0] = CMD_UNL;
	gpib_cmd( cmd_buf, 1 );
}

void main(void) {
	char compareBuf[10];
	char writeError;
	
	output_high(LED_ERROR); // Turn on the error LED
	
	// Setup the Watchdog Timer
#ifdef WITH_WDT
	setup_wdt(WDT_ON);
#endif

#ifdef WITH_TIMEOUT	
	// Setup the timer
	int_count=INTS_PER_SECOND;
	set_rtcc(0);
	setup_counters( RTCC_INTERNAL, RTCC_DIV_16 );
	enable_interrupts( INT_RTCC );
	enable_interrupts( GLOBAL );
#endif
	
	timeoutPeriod = 5; // Default timeout period, in seconds
	
	// Start all the GPIB related stuff
	gpib_init(); // Initialize the GPIB Bus
	writeError = gpib_controller_assign(0x00);
	
	output_low(LED_ERROR); // Turn off the error LED

	// Main execution loop
	for(;;) {
		
#ifdef WITH_WDT
		restart_wdt();
#endif
		
		if(kbhit()) { // If PC is sending input
			gets(buf); // Recieve serial command
			
			if( buf[0] == '+' ) { // Controller commands start with a +
			
				strcpy(compareBuf,"+a:"); // "+a:" is used to set the address
				if( strncmp((char*)buf,(char*)compareBuf,3)==0 ) { 
					partnerAddress = atoi( (char*)(&(buf[3])) ); // Parse out the GPIB address
				}
				
				strcpy(compareBuf,"+t:"); // "+t:" is used to set the timeout period
				if( strncmp((char*)buf,(char*)compareBuf,3)==0 ) { 
					timeoutPeriod = atoi( (char*)(&(buf[3])) ); // Parse out the timeout period
				}
				
				strcpy(compareBuf,"+eos:"); // "+eos:" is used to set the end of string
				if( strncmp((char*)buf,(char*)compareBuf,5)==0 ) { 
					eos = atoi( (char*)(&(buf[5])) ); // Parse out the end of string byte
				}
				
				strcpy(compareBuf,"+eoi:"); // "+eoi:" is used to set EOI usage condition
				if( strncmp((char*)buf,(char*)compareBuf,5)==0 ) { 
					eoiUse = atoi( (char*)(&(buf[5])) ); // Parse out the end of string byte
				}
				
				strcpy(compareBuf,"+test"); // "+test" is used to test the controller
				if( strncmp((char*)buf,(char*)compareBuf,5)==0 ) { 
					printf("testing\n\r");
				}
								
				strcpy(compareBuf,"+read"); // "+read" is used to force the controller to read
				if( strncmp((char*)buf,(char*)compareBuf,5)==0 ) { 
					gpib_read();
				}
				
			} 
			else { // Not an internal command, send to bus
				
				// Command all talkers and listeners to stop
				cmd_buf[0] = CMD_UNT;
				gpib_cmd( cmd_buf, 1 );
				cmd_buf[0] = CMD_UNL;
				gpib_cmd( cmd_buf, 1 );
				
				// Set target device into listen mode
				cmd_buf[0] = partnerAddress + 0x20;
				gpib_cmd( cmd_buf, 1 );
				
				// Set the controller into talker mode
				cmd_buf[0] = myAddress + 0x40;
				gpib_cmd( cmd_buf, 1 );
				
				// Send out command to the bus
				writeError = gpib_write( buf, 0 );
				
				if (writeError == 0xff) {
					writeError = 1;
				}
				
				if( eoiUse == 0 ) { // If we are not using EOI, need to output termination byte to inst
					buf[0] = eos;
					writeError = gpib_write( buf, 1 );
					
					if (writeError == 0xff) {
						writeError = 1;
					}
				}
				

				if ( ( strchr( (char*)buf, '?' ) != NULL ) && !( writeError ) ) { // If cmd contains a question mark -> is a query
					
					gpib_read();
					
				}
				
			}
			
		} // End of receiving PC input
	} // End of main execution loop

}
