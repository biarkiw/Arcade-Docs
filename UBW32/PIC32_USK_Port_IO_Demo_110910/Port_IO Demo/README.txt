PIC32 USB Starter Kit II: Running the “Port_IO” demo
------------------------------------------------------
 
Required Development Resources:
-----------------------------------
a. PIC32MX USB Starter Kit II (DM320003-2)
b. MPLAB IDE.
c. C Compiler.


Compiling and Running the Demo:
-------------------------------
1. Load the demo code into MPLAB by double clicking the *.mcp project file.
2. Connect the mini-B debugger port on-board the Starter board to an USB port on the development computer using the USB cable provided in the kit.
3. Choose the PIC32 Starter Kit debugger tool in MPLAB IDE by selecting Debugger>Select Tool and then click on PIC32 Starter Kit.
4. Build the project by selecting Project>Build All.
5. Download your code into the evaluation board microcontroller by selecting Debugger>Programming>Program All Memories.
6. Run the demo code by selecting Debugger>Run.

This example code uses the PIC32 Peripheral Library macros to configure PORTD IO port pins as digital outputs and digital inputs as well as configure IO pin Change Notice, weak pin pullups and Change Notice Interrupts.

The program demonstrates two different methods to monitor SW1 (PORTD.RD6) and SW2 (PORTD.RD7) on the PIC32 Starter Kit.

Method #1 polls SW1 in software.

When SW1 is pressed, LED1 (PORTD.RD0) is turned on and a debug message is sent to the Starter Kit console window.

When SW1 is released, LED1 is turned off and a different message is sent to the Starter Kit console window.

Method #2 uses IO pin Change Notice for SW2 to generate an interrupt.

Pressing or releasing SW2 causes a Change Notice interrupt to be generated and the Change Notice ISR reads the PORT to determine the state of SW2.

When SW2 is pressed, LED2 (PORTD.RD1) is turned on and a debug message is sent to the Starter Kit console window.

When SW1 is released, LED2 is turned off and a different message is sent to the Starter Kit console window.

Also demonstrated is the use of the Debug Print Library macros to send user messages to the MPLAB IDE Starter Kit console window.
