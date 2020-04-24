/***********************************************************************************
				
				PIC32MX Starter Kit Example Code - PORT Input Output

 ***********************************************************************************
 * FileName:        port_io.c
 * Dependencies:    None
 * Company:         Microchip Technology, Inc.
 *
 * Copyright (c) 2008 Microchip Technology, Inc.
 * Software License Agreement
 *
 * The software supplied herewith by Microchip Technology Incorporated
 * (the “Company”) for its PIC32 Microcontroller is intended
 * and supplied to you, the Company’s customer, for use solely and
 * exclusively on Microchip PIC32 Microcontroller products.
 * The software is owned by the Company and/or its supplier, and is
 * protected under applicable copyright laws. All rights are reserved.
 * Any use in violation of the foregoing restrictions may subject the
 * user to criminal sanctions under applicable laws, as well as to
 * civil liability for the breach of the terms and conditions of this
 * license.
 *
 * THIS SOFTWARE IS PROVIDED IN AN “AS IS” CONDITION. NO WARRANTIES,
 * WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT NOT LIMITED
 * TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE APPLY TO THIS SOFTWARE. THE COMPANY SHALL NOT,
 * IN ANY CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL OR
 * CONSEQUENTIAL DAMAGES, FOR ANY REASON WHATSOEVER.
 *
 ***************************************************************************
 * Description:
 *			The example code demonstrates simple switch input methods and 
 *			methods for configuring pins as outputs.
 *
 *
 *
 *************************************************************************************/

// Adds support for PIC32 Peripheral library functions and macros
#include <plib.h>

// Configuration Bits
#pragma config FNOSC    = PRIPLL        // Oscillator Selection
#pragma config FPLLIDIV = DIV_2         // PLL Input Divider (PIC32 Starter Kit: use divide by 2 only)
#pragma config FPLLMUL  = MUL_20        // PLL Multiplier
#pragma config FPLLODIV = DIV_1         // PLL Output Divider
#pragma config FPBDIV   = DIV_1         // Peripheral Clock divisor
#pragma config FWDTEN   = OFF           // Watchdog Timer
#pragma config WDTPS    = PS1           // Watchdog Timer Postscale
#pragma config FCKSM    = CSDCMD        // Clock Switching & Fail Safe Clock Monitor
#pragma config OSCIOFNC = OFF           // CLKO Enable
#pragma config POSCMOD  = XT            // Primary Oscillator
#pragma config IESO     = OFF           // Internal/External Switch-over
#pragma config FSOSCEN  = OFF           // Secondary Oscillator Enable
#pragma config CP       = OFF           // Code Protect
#pragma config BWP      = OFF           // Boot Flash Write Protect
#pragma config PWP      = OFF           // Program Flash Write Protect
#pragma config ICESEL   = ICS_PGx2      // ICE/ICD Comm Channel Select
#pragma config DEBUG    = OFF           // Debugger Disabled for Starter Kit

//  The following is used by the main application
#define SYS_FREQ		(80000000)

// IOPORT bit masks can be found in ports.h
#define CONFIG          (CN_ON)
#define PINS            (CN16_ENABLE)
#define PULLUPS         (CN15_PULLUP_ENABLE | CN16_PULLUP_ENABLE)
#define INTERRUPT       (CHANGE_INT_ON | CHANGE_INT_PRI_2)

unsigned int dummy;

//  port_io application code
int main(void)
{
    unsigned int last_sw_state = 1;
       
    // Configure the device for maximum performance, but do not change the PBDIV clock divisor.
	// Given the options, this function will change the program Flash wait states,
	// RAM wait state and enable prefetch cache, but will not change the PBDIV.
    // The PBDIV value is already set via the pragma FPBDIV option above.
   	SYSTEMConfig(SYS_FREQ, SYS_CFG_WAIT_STATES | SYS_CFG_PCACHE);

	// configure IOPORTS PORTD.RD0, RD1 as outputs
	// could also use mPORTDSetPinsDigitalOut(BIT_6 | BIT_7);
    PORTSetPinsDigitalOut(IOPORT_D, BIT_0 | BIT_1);
    
	// initialize the port pins states = output low
    PORTClearBits(IOPORT_D, BIT_0 | BIT_1);
    
	// PORTD.RD6, RD7 as inputs
	// could also use mPORTDSetPinsDigitalIn(BIT_6 | BIT_7);
    PORTSetPinsDigitalIn(IOPORT_D, BIT_6 | BIT_7);  

	// configure the Change Notice Feature
	// Note: It is recommended to disable vectored interrupts prior to
    // configuring the change notice module, (if they are enabled).
    // The user must read one or more IOPORTs to clear any IO pin
    // change notice mismatch condition, then clear the change notice
    // interrupt flag before re-enabling the vector interrupts.

    // Enable change notice, enable discrete pins and weak pullups
    mCNOpen(CONFIG, PINS, PULLUPS);

    // Read the port to clear any mismatch on change notice pins
    dummy = mPORTDRead();
   
    // Clear change notice interrupt flag
    ConfigIntCN(INTERRUPT);

    // Ok now to enable multi-vector interrupts
    INTEnableSystemMultiVectoredInt();

	//Initialize the DB_UTILS IO channel
	DBINIT();
	
	// Display a message
	DBPRINTF("Welcome to the PIC32 PORT input/output example. \n");
	DBPRINTF("The build date and time is ... (" __DATE__ "," __TIME__ ")\n");
	DBPRINTF("Press SW1 to toggle LED1, press SW2 to toggle LED2 \n");

   
   // loop here polling for SW1, SW2 is handled by Change Notice Interrupt
   while(1)
   {
 	  if(PORTDbits.RD6 == 0)					// 0 = switch is pressed
 	  {
 	  	PORTSetBits(IOPORT_D, BIT_0);			// RED LED = on (same as LATDSET = 0x0001)
 	  	if(last_sw_state == 1)					// display a message only when switch changes state
		{   
 	  	    DBPRINTF("Switch SW1 has been pressed. \n");
 	  	    last_sw_state = 0;
 	  	}    
 	  }
 	  else										// 1 = switch is not pressed
 	  {
 	  	PORTClearBits(IOPORT_D, BIT_0);			// RED LED = off (same as LATDCLR = 0x0001)
 	  	if(last_sw_state == 0)                 // display a message only when switch changes state
 	  	{
 	  	    DBPRINTF("Switch SW1 has been released. \n");
 	  	    last_sw_state = 1;
        }
 	  }	
   };

}
/******************************************************************************
*	Change Notice Interrupt Service Routine
*
*   Note: Switch debouncing is not performed.
*   Code comes here if SW2 (CN16) PORTD.RD7 is pressed or released.
*   The user must read the IOPORT to clear the IO pin change notice mismatch
*	condition first, then clear the change notice interrupt flag.
******************************************************************************/ 
void __ISR(_CHANGE_NOTICE_VECTOR, ipl2) ChangeNotice_Handler(void)
{
    // Step #1 - always clear the mismatch condition first
    dummy = PORTReadBits(IOPORT_D, BIT_7);
    
    // Step #2 - then clear the interrupt flag
    mCNClearIntFlag();
    
    // Step #3 - process the switches
    if(dummy == BIT_7)
    {
        PORTClearBits(IOPORT_D, BIT_1);       // turn off LED2
        DBPRINTF("Switch SW2 has been released. \n");
    }   
    else
    {
        PORTSetBits(IOPORT_D, BIT_1);     // turn on LED2
        DBPRINTF("Switch SW2 has been pressed. \n");
    }    
    
    // additional processing here...

 }

