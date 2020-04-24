#include "plib.h"
#include "usb_config.h"
#include "USB/usb.h"
#include "USB/usb_function_cdc.h"
#include "HardwareProfile.h"
#include "D32.h"
#include "Test.h"

enum PinArrayType
{
	PAI_Zero,	// 0,0,
	PAI_A15,	// 'A',15,
	PAI_A14,	// 'A',14,
	PAI_A5,		// 'A',5,
	PAI_A4,		// 'A',4,
	PAI_A3,		// 'A',3,
	PAI_A2,		// 'A',2,
	PAI_F8,		// 'F',8,
	PAI_F2,		// 'F',2,
	PAI_F5,		// 'F',5,
	PAI_F4,		// 'F',4,
	PAI_D15,	// 'D',15,
	PAI_D14,	// 'D',14,
	PAI_B15,	// 'B',15,
	PAI_B14,	// 'B',14,
	PAI_B13,	// 'B',13,
	PAI_B12,	// 'B',12,
	PAI_F12,	// 'F',12,
	PAI_F13,	// 'F',13,
	PAI_A1,		// 'A',1,
	PAI_B11,	// 'B',11,
	PAI_B10,	// 'B',10,
	PAI_B9,		// 'B',9,
	PAI_B8,		// 'B',8,
	PAI_A10,	// 'A',10,
	PAI_A9,		// 'A',9,
	PAI_B3,		// 'B',3,
	PAI_B4,		// 'B',4,
	PAI_E9,		// 'E',9,
	PAI_E8,		// 'E',8,
	PAI_A0,		// 'A',0,
	PAI_G9,		// 'G',9,
	PAI_G8,		// 'G',8,
	PAI_G7,		// 'G',7,
	PAI_G6,		// 'G',6,
	PAI_C4,		// 'C',4,
	PAI_B0,		// 'B',0,
	PAI_B1,		// 'B',1,
	PAI_B2,		// 'B',2,
	PAI_B6,		// 'B',6,
	PAI_B7,		// 'B',7,
	PAI_C3,		// 'C',3,
	PAI_C2,		// 'C',2,
	PAI_C1,		// 'C',1,
	PAI_E7,		// 'E',7,
	PAI_E6,		// 'E',6,
	PAI_E5,		// 'E',5,
	PAI_G15,	// 'G',15,
	PAI_E4,		// 'E',4,
	PAI_E3,		// 'E',3,
	PAI_E2,		// 'E',2,
	PAI_G13,	// 'G',13,
	PAI_G12,	// 'G',12,
	PAI_G14,	// 'G',14,
	PAI_E1,		// 'E',1,
	PAI_E0,		// 'E',0,
	PAI_A7,		// 'A',7,
	PAI_A6,		// 'A',6,
	PAI_G0,		// 'G',0,
	PAI_G1,		// 'G',1,
	PAI_F1,		// 'F',1,
	PAI_F0,		// 'F',0,
	PAI_D7,		// 'D',7,
	PAI_D6,		// 'D',6,
	PAI_D5,		// 'D',5,
	PAI_D4,		// 'D',4,
	PAI_D13,	// 'D',13,
	PAI_D12,	// 'D',12,
	PAI_D3,		// 'D',3,
	PAI_D2,		// 'D',2,
	PAI_D1,		// 'D',1,
	PAI_C14,	// 'C',14,
	PAI_C13,	// 'C',13,
	PAI_D0,		// 'D',0,
	PAI_D11,	// 'D',11,
	PAI_D10,	// 'D',10,
	PAI_D9,		// 'D',9,
	PAI_D8		// 'D',8
};



const unsigned char PinArray[] = 
{
	0,0,
	// Bottom row (right to left, with USB connector on right)
	'A',15,
	'A',14,
	'A',5,
	'A',4,
	'A',3,
	'A',2,
	'F',8,
	'F',2,
	'F',5,
	'F',4,
	'D',15,
	'D',14,
	'B',15,
	'B',14,
	'B',13,
	'B',12,
	'F',12,
	'F',13,
	'A',1,
	'B',11,
	'B',10,
	'B',9,
	'B',8,
	'A',10,
	'A',9,
	'B',3,
	'B',4,
	'E',9,
	'E',8,
	'A',0,
	'G',9,
	'G',8,
	'G',7,
	'G',6,
	'C',4,
	// Up the left hand side
	'B',0,
	'B',1,
	'B',2,
	'B',6,
	'B',7,
	// Top row (left to right with USB connector on right)
	'C',3,
	'C',2,
	'C',1,
	'E',7,
	'E',6,
	'E',5,
	'G',15,
	'E',4,
	'E',3,
	'E',2,
	'G',13,
	'G',12,
	'G',14,
	'E',1,
	'E',0,
	'A',7,
	'A',6,
	'G',0,
	'G',1,
	'F',1,
	'F',0,
	'D',7,
	'D',6,
	'D',5,
	'D',4,
	'D',13,
	'D',12,
	'D',3,
	'D',2,
	'D',1,
	'C',14,
	'C',13,
	'D',0,
	'D',11,
	'D',10,
	'D',9,
	'D',8,
	0,0
};

// This array holds indexes into the PinArray
// Each line of four pins is tested as a group.
// First two pins are outputs, second two are inputs
// First and third are jumpered, second and fourth are jumpered
// A15 is speical case, becuase it's the odd pin out
// it is handled separtely in the code
const unsigned char T2PinArray[] = 
{
	// Starting at the upper right near J2
	PAI_D8,PAI_D9,PAI_D10,PAI_D11,
	PAI_D0,PAI_C13,PAI_C14,PAI_D1,
	PAI_D2,PAI_D3,PAI_D12,PAI_D13,
	PAI_D4,PAI_D5,PAI_D6,PAI_D7,
	PAI_F0,PAI_F1,PAI_G1,PAI_G0,
	PAI_A6,PAI_A7,PAI_E0,PAI_E1,
	PAI_G14,PAI_G12,PAI_G13,PAI_E2,
	PAI_E3,PAI_E4,PAI_G15,PAI_E5,
	PAI_E6,PAI_E7,PAI_C1,PAI_C2,
	PAI_C3,PAI_B7,PAI_B6,PAI_B2,
	PAI_B1,PAI_B0,PAI_C4,PAI_G6,
	PAI_G7,PAI_G8,PAI_G9,PAI_A0,
	PAI_E8,PAI_E9,PAI_B4,PAI_B3,
	PAI_A9,PAI_A10,PAI_B8,PAI_B9,
	PAI_B10,PAI_B11,PAI_A1,PAI_F13,
	PAI_F12,PAI_B12,PAI_B13,PAI_B14,
	PAI_B15,PAI_D14,PAI_D15,PAI_F4,
	PAI_F5,PAI_F2,PAI_F8,PAI_A2,
	PAI_A3,PAI_A4,PAI_A5,PAI_A14,
	0,0,0,0
};

// T1 command will do a 'Test #1', which sets all available pins on
// UBW32 to be outputs, then turns each one on and off in turn, right
// around the whole board. If you hook and LED up to every I/O pin,
// you'll get to see them cycle. First
// we go from A15 to D8, then back again. Proves
// PIC32 part is soldered down, no opens or shorts. (Watch for missing
// LEDs or multiple LEDs coming on at the same time.)
// First parameter is ms/pin, second parameter is number of total cycles
// FORMAT: T1,<time>,<cycles><CR>
// <time> is from 10 through 1000 and is ms/pin
// <cycles> is from 1 through 100 and is total cycles
// EXAMPLE: "T1,100,2<CR>" would spend 100ms on each I/O, cycling twice
void parse_T1_packet(void)
{
	int i;
	unsigned int time_per_pin_ms;
	unsigned int total_cycles;

	unsigned char Index = 0;

	extract_number (kINT, (void*)&time_per_pin_ms, kREQUIRED);
	extract_number (kINT, (void*)&total_cycles, kREQUIRED);

	// Bail if we got a conversion error
	if (error_byte)
	{
		return;
	}

	// Set all I/O to be digital outputs
	// And all latch registers to be high (LEDs off)
	for (i = 0; i < kMAX_PORTS; i++)
	{
		*TRISPtr[i] = 0x00000000;
		*LATPtr[i]= 0xFFFFFFFF;
	}
 
	// Then loop through all pins
	for (i = 0; i < total_cycles; i++)
	{

		// Just turn each pin on, wait, then off
		// 78 total IO
		// First go around clockwise
		Index = 1;
		while (PinArray[Index*2] != 0)
		{
			*LATPtr[PinArray[Index*2]-'A'] = ~(1 << PinArray[Index*2+1]);
			T1_timer = time_per_pin_ms;
			while (T1_timer) {};
			*LATPtr[PinArray[Index*2]-'A'] = 0xFFFFFFFF;
			Index++;
		}
		// Then back again, counter-clockwise
		Index--;
		while (PinArray[Index*2] != 0)
		{
			*LATPtr[PinArray[Index*2]-'A'] = ~(1 << PinArray[Index*2+1]);
			T1_timer = time_per_pin_ms;
			while (T1_timer) {};
			*LATPtr[PinArray[Index*2]-'A'] = 0xFFFFFFFF;
			Index--;
		}
	}	

	for (i = 0; i < kMAX_PORTS; i++)
	{
		*LATPtr[i]= 0xFFFFFFFF;
	}

	// New UBW32 hardware boared v2.6 has pads for a 32KHz crystal
	// This code, if compiled in, would turn it on and make sure it is working
	// before allowing this test to complete.
	// We're going to leave it commented out because SparkFun will not be populating
	// the 32KHz crystal.
    //RtccInit();            // init the RTCC
    //while(RtccGetClkStat()!=RTCC_CLK_ON);    // wait for the SOSC to be actually running and RTCC to have its clock source
    //                        // could wait here at most 32ms
	//RtccShutdown();			// Turn it off so the pins can still be used later on.
}


// T2 command will do a 'Test #2'
// EXAMPLE: "T2<CR>"
// This command assumes a proper test jig, where certain pins are shorted together
// For example, RD8 and RD10, RD9 and RD11, and so on around the UBW32's pins
//
// Procedure:
// 1) Set every other pair of pins to be inputs, every other pair to be outputs. 
//		Example: RD8 = Out, RD9 = Out, RD10 = In, RD11 = In, etc. 
// 2) Set all outputs low
// 3) Set RD8 high, and check that RD10 is high.
// 4) Set RD8 low, and check that RD10 is low.
// 5) Repeat steps 3 and 4 for each pair of connected pins
// 6) Wait for up to 30 seconds for the user to push both PRG and USER buttons
// 7) Output a response - either "PASS", or "FAIL - "<some error> 
void parse_T2_packet(void)
{
	int i;
	unsigned char Index = 0;
	BOOL SeenProg = FALSE;
	BOOL SeenUser = FALSE;

	// Set all I/O to be digital inputs
	// And all latch registers to be low (LEDs off)
	for (i = 0; i < kMAX_PORTS; i++)
	{
		*TRISPtr[i] = 0xFFFFFFFF;
		*LATPtr[i]= 0x00000000;
	}

	// For sanity, run through all I/O, and make sure they're all high.
	Index = 1;
	while (PinArray[Index*2] != 0)
	{
		//UINT8 Prt = PinArray[Index*2]-'A';
		//UINT16 PrtVal = *PORTPtr[PinArray[Index*2]-'A'];
		//UINT8 Pin = PinArray[Index*2+1];
		//UINT16 PinVal = (1 << PinArray[Index*2+1]);
		//UINT16 Res = PrtVal & PinVal;

		//printf("Testing Prt %2d PrtVal %04X Pin %2d PinVal %04X Res %04X\n\r", Prt, PrtVal, Pin, PinVal, Res);

		if((*PORTPtr[PinArray[Index*2] - 'A'] & (1 << PinArray[Index*2+1])) == 0)
		{
			printf("Error: Stage 1: %c%d should be high but is low.\n\r", PinArray[Index*2], PinArray[Index*2+1]);
		}
		Index++;
	}

	printf("Stage 1 complete.\r\n");

	// For each set of four pins in the array we will:
	Index = 0;
	while (T2PinArray[Index] != 0)
	{
		// Set the first one to be an output, low
		SetPortPinOutput(Index, 0);
		// Make sure ONLY the third pin out of the four reads low
		if (GetPinVal(Index+1) == 0)
		{
			printf("Error: Stage 2: When %c%d is low, %c%d also went low.\n\r", 
				PinArray[T2PinArray[Index]*2], 
				PinArray[T2PinArray[Index]*2+1],
				PinArray[T2PinArray[Index+1]*2], 
				PinArray[T2PinArray[Index+1]*2+1]
			);
		}
		if (GetPinVal(Index+2) == 1)
		{
			printf("Error: Stage 2: When %c%d is low, %c%d stayed high.\n\r", 
				PinArray[T2PinArray[Index]*2], 
				PinArray[T2PinArray[Index]*2+1],
				PinArray[T2PinArray[Index+2]*2], 
				PinArray[T2PinArray[Index+2]*2+1]
			);
		}
		if (GetPinVal(Index+3) == 0)
		{
			printf("Error: Stage 2: When %c%d is low, %c%d also went low.\n\r", 
				PinArray[T2PinArray[Index]*2], 
				PinArray[T2PinArray[Index]*2+1],
				PinArray[T2PinArray[Index+3]*2], 
				PinArray[T2PinArray[Index+3]*2+1]
			);
		}

		// Set the first one to be an input again
		SetPortPinInput(Index);

		// Set the second one to be an output, low
		SetPortPinOutput(Index+1,0);
		// Make sure ONLY the fourth pin out of the four reads low
		if (GetPinVal(Index) == 0)
		{
			printf("Error: Stage 2: When %c%d is low, %c%d also went low.\n\r", 
				PinArray[T2PinArray[Index+1]*2], 
				PinArray[T2PinArray[Index+1]*2+1],
				PinArray[T2PinArray[Index]*2], 
				PinArray[T2PinArray[Index]*2+1]
			);
		}
		if (GetPinVal(Index+2) == 0)
		{
			printf("Error: Stage 2: When %c%d is low, %c%d also went low.\n\r", 
				PinArray[T2PinArray[Index+1]*2], 
				PinArray[T2PinArray[Index+1]*2+1],
				PinArray[T2PinArray[Index+2]*2], 
				PinArray[T2PinArray[Index+2]*2+1]
			);
		}
		if (GetPinVal(Index+3) == 1)
		{
			printf("Error: Stage 2: When %c%d is low, %c%d stayed high.\n\r", 
				PinArray[T2PinArray[Index+1]*2], 
				PinArray[T2PinArray[Index+1]*2+1],
				PinArray[T2PinArray[Index+3]*2], 
				PinArray[T2PinArray[Index+3]*2+1]
			);
		}

		// Set the second one to be an input
		SetPortPinInput(Index+1);
		
		// Move to next set of four pins
		Index += 4;
	}

	// Handle A15 as a special case (since it's not part of the 'groups of 4')
	// Set A5 low
	SetPortPinOutput(PAI_A5,0);
	// Make sure A15 went low
	if (GetPinVal(PAI_A15) == 1)
	{
		printf("Error: Stage 2: When A5 is low, A15 stayed high.\n\r");
	}
	// Set the second one to be an input
	SetPortPinInput(PAI_A5);

	printf("Stage 2 complete.\r\n");

	// Set everything back to be inputs now
	for (i = 0; i < kMAX_PORTS; i++)
	{
		*LATPtr[i]= 0xFFFFFFFF;
	}

	// Now wait for 10 seconds to see if the user can press both the PRG and USER
	// buttons (in any order). If they do, then we're done. If they don't and we
	// time out, then print an error.
	T1_timer = 10000;
	while (T1_timer > 0)
	{
		if (swProgram == 0)
		{
			SeenProg = TRUE;
		}
		if (swUser == 0)
		{
			SeenUser = TRUE;
		}
		if (SeenUser && SeenProg)
		{
			T1_timer = 0;
		}
		check_and_send_TX_data();
	}
	if (!SeenUser)
	{
		printf("Error: Stage 3: USER button press not seen in 10s.\n\r");
	}
	if (!SeenProg)
	{
		printf("Error: Stage 3: PRG button press not seen in 10s.\n\r");
	}

	printf("Stage 3 complete.\r\n");

	// Stage 4, blink the leds to make sure their populated correctly.
	mInitAllLEDs();

	for (i=0; i<4; i++)
	{
		mLED_1_On();
		T1_timer = 500;
		while (T1_timer > 0);
		mLED_1_Off();
		mLED_2_On();
		T1_timer = 500;
		while (T1_timer > 0);
		mLED_2_Off();
		mLED_3_On();
		T1_timer = 500;
		while (T1_timer > 0);
		mLED_3_Off();
		mLED_4_On();
		T1_timer = 500;
		while (T1_timer > 0);
		mLED_4_Off();
	}	
	printf("Stage 4 complete.\r\n");

	// New UBW32 hardware boared v2.6 has pads for a 32KHz crystal
	// This code, if compiled in, would turn it on and make sure it is working
	// before allowing this test to complete.
	// We're going to leave it commented out because SparkFun will not be populating
	// the 32KHz crystal.
    //RtccInit();            // init the RTCC
    //while(RtccGetClkStat()!=RTCC_CLK_ON);    // wait for the SOSC to be actually running and RTCC to have its clock source
    //                        // could wait here at most 32ms
	//RtccShutdown();			// Turn it off so the pins can still be used later on.
}

UINT8 GetPinVal(UINT8 Index)
{
	//UINT8 Prt = PinArray[T2PinArray[Index]*2];
	//UINT16 PrtVal = *PORTPtr[PinArray[T2PinArray[Index]*2]-'A'];
	//UINT8 Pin = PinArray[T2PinArray[Index]*2+1];
	//UINT16 PinVal = (1 << PinArray[T2PinArray[Index]*2+1]);
	//UINT16 Res = PrtVal & PinVal;

	// Limit check paramters
	if (Index > PAI_D8)
	{
		printf("GetPinVal() Parameter Error %02X\n\r", Index);
		return 0;
	}

	//printf("GetPinVal() Prt %c PrtVal %04X Pin %2d PinVal %04X Res %04X\n\r", Prt, PrtVal, Pin, PinVal, Res);

	if (*PORTPtr[PinArray[T2PinArray[Index]*2] - 'A'] & (1 << PinArray[T2PinArray[Index]*2+1]))
	{
		return 1;
	}
	else
	{
		return 0;
	}
}


// Port is 'A' through 'G', Pin is 0 through 16. State is 0 or 1.
void SetPortPinOutput(UINT8 Index, BOOL State)
{
	// Limit check paramters
	if (Index > PAI_D8)
	{
		printf("SetPortPinOuptut() Parameter Error %02X\n\r", Index);
		return;
	}

	// First set the output latch state
	if (State)
	{
		*LATPtr[PinArray[T2PinArray[Index]*2] - 'A'] |= (1 << PinArray[T2PinArray[Index]*2+1]);
	}
	else
	{
		*LATPtr[PinArray[T2PinArray[Index]*2] - 'A'] &= ~(1 << PinArray[T2PinArray[Index]*2+1]);
	}

	// Now make the pin an output
	*TRISPtr[PinArray[T2PinArray[Index]*2] - 'A'] &= ~(1 << PinArray[T2PinArray[Index]*2+1]);
}

void SetPortPinInput(UINT8 Index)
{
	// Limit check paramters
	if (Index > PAI_D8)
	{
		printf("SetPortPinInput() Parameter Error %02X\n\r", Index);
		return;
	}

	// Now make the pin an input
	*TRISPtr[PinArray[T2PinArray[Index]*2] - 'A'] |= (1 << PinArray[T2PinArray[Index]*2+1]);
}
