//
// Version 1.2  03/03/09  BPS : Fixed problem with boot_software_key_sec so it will work reliably with bootloader (forgot leading .)
// Version 1.3
// Version 1.4 05/09/09 BPS: 	Added T2 command for faster, better testing at SparkFun
//								Started splitting things into multiple files
// 08/28/2010 - Changed to use Microchip folder - from Microchip Applications Library USB
//				stack v2.7 - updated project to use unchanged MAL Microchip folder - no code changes
// Version 1.5 11/01/10 BPS: Added PM command (PWM output)
// Version 1.6 01/08/11 BPS: Added SP command to allow for software PWM from 100Khz ISR
//                      (BRIAN! Figure out what to do with 100KHz ISR - scale down when not doing PMW?
//                       maybe make PWM rate a paramter? Maybe use a separate timer for PWM ISR?)
// Version 1.6.2 09/02/11 BPS:
//                        * Updated Software_Key variable so it works with the new C32 v2.00 compiler. From now
//                          on, we will always need to use this compiler (or above) with this firmware.
//                        * Updated extract_number() to use 32-bit signed and unsigned ints (called longs) as
//                          well as floating point values (using sscanf()).
//                        * Tested with USB stack 2.9a, MPLAB 8.76, C32 v2.01
// Version 1.6.3 09/05/11 Andrew Prudil
//                        * Added CA and IA commands for configuring and reading analog inputs
// Version 1.6.3 11/30/11 BPS:
//                        * No code changes, same version, just got projects for MPLAB 8 and X, and built
//                            with MAL USB 2.9b and C32 v2.01
// Version 1.6.4 11/08/14 BPS:
//                        * Updated code around SP and CP a little, and updated docs a lot
//                        * Compiled in MPLAB X 2.25 with C32 v2.02.
// Version 1.6.5 10/03/15 BPS:
//                        * Added reset of PWM hardware to "R" command.
//                        * Compiled with MPLAB X 3.05 and C32 v2.02
// Version 1.6.6 09/29/16 BPS:
//                        * Fixed bug in IA command due to CP0 roll over between initial value and delay check. If delay = 0, then
//                          there was very small chance of command taking 107 seconds (CP0 rollover).
//
// Timer Peripheral usage list
// CoreTimer - used for main ISR, soft PWM (SP) command
// Timer0 -
// Timer1 -
// Timer2 - used for PM hardware PWM output command
// Timer3 - used for stepper module (sR command)
// Timer4 -

#include "GenericTypeDefs.h"
#include "Compiler.h"
#include "usb_config.h"
#include "USB/usb.h"
#include "USB/usb_function_cdc.h"
#include "HardwareProfile.h"
#include "D32.h"
#include "Test.h"
#include "Stepper.h"

#define bitset(var,bitno) ((var) |= (1 << (bitno)))
#define bitclr(var,bitno) ((var) &= ~(1 << (bitno)))
#define bittst(var,bitno) (((var) & (1 << (bitno)))?1:0)
#define bitinvert(var,bitno) ((var) ^ (1 << (bitno)))

#define kTX_BUF_SIZE 			(64)			// In bytes
#define kRX_BUF_SIZE			(512)			// In bytes

#define kUSART_TX_BUF_SIZE		(64)			// In bytes
#define kUSART_RX_BUF_SIZE		(64)			// In bytes


#define advance_RX_buf_out()						\
{ 													\
	g_RX_buf_out++;									\
	if (kRX_BUF_SIZE == g_RX_buf_out)				\
	{												\
		g_RX_buf_out = 0;							\
	}												\
}

#define kISR_FIFO_A_DEPTH		3
#define kISR_FIFO_D_DEPTH		3
#define kCR						0x0D
#define kLF						0x0A
#define kBS						0x08

// defines for the error_byte byte - each bit has a meaning
#define kERROR_BYTE_TX_BUF_OVERRUN			2
#define kERROR_BYTE_RX_BUFFER_OVERRUN		3
#define kERROR_BYTE_MISSING_PARAMETER		4
#define kERROR_BYTE_PRINTED_ERROR			5			// We've already printed out an error
#define kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT	6
#define kERROR_BYTE_EXTRA_CHARACTERS 		7
#define kERROR_BYTE_UNKNOWN_COMMAND			8			// Part of command parser, not error handler

// Let compile time pre-processor calculate the CORE_TICK_PERIOD
#define SYS_FREQ 				(80000000L)
#define TOGGLES_PER_SEC			100000
#define CORE_TICK_RATE	       (SYS_FREQ/2/TOGGLES_PER_SEC)

// Constants for SP command
#define SP_MAX_CHANNEL_NUMBER	(65)
#define SP_ISR_PWM_ROLLOVER		(4095)

// Number of times the core timer ISR needs to fire to decriment T1_timer
#define T1_CORE_MULTIPLIER_RELOAD (100)

static char USB_In_Buffer[64];

// Local variables for this file (statics)
static WORD old_swUser;
static WORD old_swProgram;

// This byte has each of its bits used as a seperate error flag
BYTE error_byte;

// ROM strings
const char st_OK[] = {"OK\r\n"};
const char st_LFCR[] = {"\r\n"};
const char st_version[] = {"UBW32 Version 1.6.6"};

const char ErrorStrings[8][40] =
{
	"!0 \r\n",								// Unused as of yet
	"!1 \r\n",								// Unused as of yet
	"!2 Err: TX Buffer overrun\r\n",		// kERROR_BYTE_TX_BUF_OVERRUN
	"!3 Err: RX Buffer overrun\r\n",		// kERROR_BYTE_RX_BUFFER_OVERRUN
	"!4 Err: Missing parameter(s)\r\n",		// kERROR_BYTE_MISSING_PARAMETER
	"",										// kERROR_BYTE_PRINTED_ERROR (something already printed)
	"!6 Err: Invalid paramter value\r\n",	// kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT
	"!7 Err: Extra parmater\r\n"			// kERROR_BYTE_EXTRA_CHARACTERS
};

// USB Transmit buffer for packets (back to PC)
unsigned char g_TX_buf[kTX_BUF_SIZE];
// USB Receiving buffer for commands as they come from PC
unsigned char g_RX_buf[kRX_BUF_SIZE];

// USART Receiving buffer for data coming from the USART
unsigned char g_USART_RX_buf[kUSART_RX_BUF_SIZE];

// USART Transmit buffer for data going to the USART
unsigned char g_USART_TX_buf[kUSART_TX_BUF_SIZE];

// Pointers to USB transmit (back to PC) buffer
unsigned char g_TX_buf_in;
unsigned char g_TX_buf_out;
unsigned char g_TX_buf_length;

// Pointers to USB receive (from PC) buffer
unsigned int g_RX_buf_in;
unsigned int g_RX_buf_out;

// In and out pointers to our USART input buffer
unsigned char g_USART_RX_buf_in;
unsigned char g_USART_RX_buf_out;

// In and out pointers to our USART output buffer
unsigned char g_USART_TX_buf_in;
unsigned char g_USART_TX_buf_out;

// Normally set to TRUE. Able to set FALSE to not send "OK" message after packet recepetion
BOOL	g_ack_enable;

// Normally set to TRUE. Set to false to disable echoing of all data sent to UBW
BOOL	g_echo_enable;

// Used in T1 command to time the LEDs. In ms.
volatile unsigned int T1_timer;
volatile unsigned int T1_Core_Multiplier;

// State of each hardware PWM channel - on=true, off=false
BOOL PWMChannelOn[5] = {FALSE,FALSE,FALSE,FALSE,FALSE};

volatile unsigned int * const ROM LATPtr[kMAX_PORTS] =
{
	&LATA,
	&LATB,
	&LATC,
	&LATD,
	&LATE,
	&LATF,
	&LATG
};

volatile unsigned int * const ROM LATSetPtr[kMAX_PORTS] =
{
	&LATASET,
	&LATBSET,
	&LATCSET,
	&LATDSET,
	&LATESET,
	&LATFSET,
	&LATGSET
};

volatile unsigned int * const ROM LATClrPtr[kMAX_PORTS] =
{
	&LATACLR,
	&LATBCLR,
	&LATCCLR,
	&LATDCLR,
	&LATECLR,
	&LATFCLR,
	&LATGCLR
};

volatile unsigned int * const ROM PORTPtr[kMAX_PORTS] =
{
	&PORTA,
	&PORTB,
	&PORTC,
	&PORTD,
	&PORTE,
	&PORTF,
	&PORTG
};
volatile unsigned int * const ROM TRISPtr[kMAX_PORTS] =
{
	&TRISA,
	&TRISB,
	&TRISC,
	&TRISD,
	&TRISE,
	&TRISF,
	&TRISG
};
volatile unsigned int * const ROM ODCPtr[kMAX_PORTS] =
{
	&ODCA,
	&ODCB,
	&ODCC,
	&ODCD,
	&ODCE,
	&ODCF,
	&ODCG
};
volatile unsigned int * const ROM ADC1BUFPtr[16] =
{
	&ADC1BUF0,
	&ADC1BUF1,
	&ADC1BUF2,
	&ADC1BUF3,
	&ADC1BUF4,
	&ADC1BUF5,
	&ADC1BUF6,
	&ADC1BUF7,
	&ADC1BUF8,
	&ADC1BUF9,
	&ADC1BUFA,
	&ADC1BUFB,
	&ADC1BUFC,
	&ADC1BUFD,
	&ADC1BUFE,
	&ADC1BUFF,
};
// SP/PC (software PWM) command variables
unsigned int SP_PWM_Values[SP_MAX_CHANNEL_NUMBER];          // Each PWM channel's PWM value
BOOL SP_PWM_Enables[SP_MAX_CHANNEL_NUMBER];                 // True if this channel is turned on
unsigned int SP_Pin_Map[SP_MAX_CHANNEL_NUMBER];             // LSB is bit in port, second byte is port's address offset
unsigned int SP_PWM_Values_Buffer[SP_MAX_CHANNEL_NUMBER];   // Holds PWM value for glitchless updating
unsigned int SP_ISR_Counter;                                //
BOOL SP_Command_Running;                                    //
BOOL SP_Command_Running_Toggle;                             //
unsigned int SP_Rollover_Value;                             //
unsigned int SP_Max_Channel_Used;                           //

// This was the way that the C32 v1.12 and before compiler needed things
//unsigned int __attribute__((section(".boot_software_key_sec,\"aw\",@nobits#"))) SoftwareKey;
// Updated this line for C32 v2.00 and above
//unsigned int SoftwareKey __attribute__((persistent,section("boot_software_key"),address(0xA0000000)));
//unsigned int SoftwareKey __attribute__((persistent));
// Total, total hack, to get around bug in C32 v2.01
unsigned int * SoftwareKey = NULL;

/** P R I V A T E  P R O T O T Y P E S ***************************************/
void parse_packet (void);		// Take a full packet and dispatch it to the right function
ExtractReturnType extract_number(
	ExtractType Type,
	void * ReturnValue,
	unsigned char Required
); 								// Pull a number paramter out of the packet
signed char extract_digit (signed long * acc, unsigned char digits); // Pull a character out of the packet
void PrintErrors (void);		// Prints out any errors in error_byte
void parse_R_packet (void);		// R for resetting UBW
void parse_BL_packet (void);	// BL for entering into bootloader
void parse_C_packet (void);		// C for configuring I/O and analog pins
void parse_CA_packet (void);	// CA for configuring I/O and analog pins
void parse_CX_packet (void); 	// CX For configuring serial port
void parse_O_packet (void);		// O for output digital to pins
void parse_I_packet (void);		// I for input digital from pins
void parse_IA_packet (void);	// IA for analog input from pins
void parse_V_packet (void);		// V for printing version
void parse_A_packet (void);		// A for requesting analog inputs
void parse_T_packet (void);		// T for setting up timed I/O (digital or analog)
void parse_PI_packet (void);	// PI for reading a single pin
void parse_PO_packet (void);	// PO for setting a single pin state
void parse_PD_packet (void);	// PD for setting a pin's direction
void parse_MR_packet (void);	// MR for Memory Read
void parse_MW_packet (void); 	// MW for Memory Write
void parse_TX_packet (void);	// TX for transmitting serial
void parse_RX_packet (void);	// RX for receiving serial
void parse_RC_packet (void);	// RC is for outputing RC servo pulses
void parse_BO_packet (void);	// BO sends data to fast parallel output
void parse_BC_packet (void);	// BC configures fast parallel outputs
void parse_BS_packet (void);	// BS sends binary data to fast parallel output
void parse_CU_packet (void);	// CU configures UBW (system wide parameters)
void parse_SS_packet (void);	// SS Send SPI
void parse_RS_packet (void);	// RS Receive SPI
void parse_CS_packet (void);	// CS Configure SPI
void parse_SI_packet (void);	// SI Send I2C
void parse_RI_packet (void);	// RI Receive I2C
void parse_CI_packet (void);	// CI Configure I2C
void parse_TP_packet (void);	// TP Toggle Pin
void parse_SL_packet (void);	// SL Set Latch
void parse_TO_packet (void);	// TO porT Output
void parse_TI_packet (void);	// TI porT Input
void parse_PW_packet (void);	// PW PWm on any pin
void parse_SC_packet (void);	// SC Serial Configure (serial shift)
void parse_SO_packet (void);	// SO Serial Output (serial shift)
void parse_EC_packet (void);	// EC stEpper Configure
void parse_ES_packet (void);	// SS stEpper Step
void parse_T1_packet (void);	// T1 for Test number 1 (cycle all I/Os)
void parse_T2_packet (void);	// T2 for Test number 1 (cycle all I/Os)
void parse_PM_packet (void);	// PM for PWM output
void parse_SP_packet (void);	// SP for Software PWM (ISR based)
void parse_PC_packet (void);	// PC for PWM Configure (software ISR based)
void StartWrite(void);			// Sets up write enable in hardware for EEPROM
unsigned char ReadEE(unsigned char Address);	// Reads a byte of EEPROM
void WriteEE(unsigned char Address, unsigned char Data);	// Writes a  byte of EEPROM
void check_and_send_TX_data (void); // See if there is any data to send to PC, and if so, do it
void PrintAck (void);			// Print "OK" after packet is parsed
void _mon_putc (char c);		// Our USB based stream character printer
unsigned char CheckLatchingInput (unsigned char PortIndex, unsigned char LatchingClearMask); // Handles the latching of inputs
void Delay_us(WORD us); // ~1us delay
void Delay_ms(unsigned int ms); //~1ms delay

typedef struct
{
	unsigned char Name1;
	unsigned char Name2;
	void (*CommandPtr)(void);
} CommandListType;

const CommandListType CommandList[] =
{
	{'R',0x00,	&parse_R_packet},
	{'C',0x00,	&parse_C_packet},
	{'C','A', 	&parse_CA_packet},
//	{'C','X', 	&parse_CX_packet},
	{'O',0x00, 	&parse_O_packet},
	{'I',0x00, 	&parse_I_packet},
	{'I','A', 	&parse_IA_packet},
	{'V',0x00,	&parse_V_packet},
//	{'A',0x00,	&parse_A_packet},
//	{'T',0x00,	&parse_T_packet},
	{'P','I',	&parse_PI_packet},
	{'P','O',	&parse_PO_packet},
	{'P','D',	&parse_PD_packet},
//	{'M','R',	&parse_MR_packet},
//	{'M','W',	&parse_MW_packet},
//	{'T','X',	&parse_TX_packet},
//	{'R','X',	&parse_RX_packet},
//	{'R','C',	&parse_RC_packet},
//	{'B','O',	&parse_BO_packet},
//	{'B','C',	&parse_BC_packet},
//	{'B','S',	&parse_BS_packet},
	{'C','U',	&parse_CU_packet},
//	{'S','S',	&parse_SS_packet},
//	{'R','S',	&parse_RS_packet},
//	{'C','I',	&parse_CS_packet},
//	{'S','I',	&parse_SI_packet},
//	{'R','I',	&parse_RI_packet},
//	{'C','I',	&parse_CI_packet},
//	{'T','P',	&parse_TP_packet},
//	{'S','L',	&parse_SL_packet},
	{'T','O',	&parse_TO_packet},
	{'T','I',	&parse_TI_packet},
//	{'P','W',	&parse_PW_packet},
//	{'S','C',	&parse_SC_packet},
//	{'S','O',	&parse_SO_packet},
//	{'E','C',	&parse_EC_packet},
//	{'E','S',	&parse_ES_packet},
	{'B','L',	&parse_BL_packet},
	{'T','1',	&parse_T1_packet},
	{'T','2',	&parse_T2_packet},
	{'P','M',   &parse_PM_packet},
	{'S','P',	&parse_SP_packet},
	{'P','C',	&parse_PC_packet},
	{'s','C',	&parse_sC_packet},
    {'s','R',   &parse_sR_packet},
    {'s','S',   &parse_sS_packet},
    {'s','E',   &parse_sE_packet},
	{0x00,0x00,	NULL}
};

void Delay_us(WORD us)
{
 DWORD start = _CP0_GET_COUNT();
 DWORD end = start + SYS_FREQ / 1000000 / 2 * us;
 if (end > start) while (_CP0_GET_COUNT() < end);
 else while (_CP0_GET_COUNT() > start || _CP0_GET_COUNT() < end);
}

void Delay_ms(unsigned int ms)
{
	unsigned int i;
	for(i=0; i<ms; i++)
  	Delay_us(1000);
}

void __ISR(_CORE_TIMER_VECTOR, ipl2) CoreTimerHandler(void)
{
	static BOOL SP_Running_State = FALSE;

    // clear the interrupt flag
    mCTClearIntFlag();

	T1_Core_Multiplier--;
	if (T1_Core_Multiplier == 0)
	{
		if (T1_timer)
		{
			T1_timer--;
		}
		T1_Core_Multiplier = T1_CORE_MULTIPLIER_RELOAD;
	}

	// For Software PWM command
	// We incriment our counter every ISR
	// Then for each of our active PWM channels, we see if the counter is greater than the counter
	// If so, we set that channel low.
	// When the counter wraps around to zero, then we set all of our outputs high.
	if (SP_Running_State == FALSE && SP_Command_Running == TRUE)
	{
		SP_ISR_Counter = SP_Rollover_Value - 1;
		SP_Running_State = TRUE;
	}
	if (SP_Running_State)
	{
		int i;

		SP_ISR_Counter++;

		// Reset counter to zero on reaching max, and set all outputs high
		if (SP_ISR_Counter == SP_Rollover_Value)
		{
			// Do we need to stop doing PWM?
			if (SP_Command_Running == FALSE)
			{
				// Make sure we don't get to this part of the ISR again
				SP_Running_State = FALSE;

				// Set all of our outputs low
				for (i=0; i <= SP_Max_Channel_Used; i++)
				{
					// But only if the channel is enabled
					if (SP_PWM_Enables[i])
					{
						*LATClrPtr[(SP_Pin_Map[i]>>8)] = 1 << (SP_Pin_Map[i] & 0xFF);
					}
				}
			}
			else
			{
				// Set all of our outputs high
				for (i=0; i <= SP_Max_Channel_Used; i++)
				{
					// First copy over the buffered PWM value for this channel (for glitchless updating)
					SP_PWM_Values[i] = SP_PWM_Values_Buffer[i];
					// But  only if the channel is enabled
					if (SP_PWM_Enables[i])
					{
						// If this channel is supposed to be low, then start him out that way
						if (SP_PWM_Values[i] == 0)
						{
							*LATClrPtr[(SP_Pin_Map[i]>>8)] = 1 << (SP_Pin_Map[i] & 0xFF);
						}
						// Otherwise set him high
						else
						{
							*LATSetPtr[(SP_Pin_Map[i]>>8)] = 1 << (SP_Pin_Map[i] & 0xFF);
						}
					}
				}
			}

			SP_ISR_Counter = 0;
		}
		else
		// If we're not at the rollover, check to see if any of the channels needs to be set low
		{
			for (i=0; i <= SP_Max_Channel_Used; i++)
			{
				if (SP_PWM_Enables[i])
				{
					// Does this channel need to go low?
					if (SP_ISR_Counter == SP_PWM_Values[i])
					{
						// Yup.
						*LATClrPtr[SP_Pin_Map[i]>>8] = 1 << (SP_Pin_Map[i] & 0xFF);
					}
				}
			}
		}
	}

    // update the period
    UpdateCoreTimer(CORE_TICK_RATE);
}


/* Switch routines */
BOOL SwitchUserIsPressed(void)
{
    if(swUser != old_swUser)
    {
        old_swUser = swUser;                  // Save new value
        if(swUser == 0)                    // If pressed
            return TRUE;                // Was pressed
    }//end if
    return FALSE;                       // Was not pressed
}

BOOL SwitchProgramIsPressed(void)
{
    if(swProgram != old_swProgram)
    {
        old_swProgram = swProgram;                  // Save new value
        if(swProgram == 0)                    // If pressed
            return TRUE;                // Was pressed
    }//end if
    return FALSE;                       // Was not pressed
}


/** D E C L A R A T I O N S **************************************************/

/******************************************************************************
 * Function:        void UserInit(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        This routine should take care of all of the demo code
 *                  initialization that is required.
 *
 * Note:
 *
 *****************************************************************************/
void UserInit(void)
{
	char i;

	// Loop through each I/O register, setting them all to digital inputs
	// and making none of them open drain and turning off all pullups and
	// setting all of the latches to zero. We have PORTA through PORTG on
	// this chip. That's 7 total.
	for (i = 0; i < 7; i++)
	{
		*LATPtr[i] = 0x0000;
		*TRISPtr[i] = 0xFFFF;
		*ODCPtr[i] = 0x0000;
	}

// Just for debug
//TRISCbits.TRISC13 = 0;


    //Initialize all of the LED pins
	mInitAllLEDs();

    // Shut down any hardware PWM that's currently going on
    PWMChannelOn[0] = FALSE;
    PWMChannelOn[1] = FALSE;
    PWMChannelOn[2] = FALSE;
    PWMChannelOn[3] = FALSE;
    PWMChannelOn[4] = FALSE;

    CloseOC1();
    PORTDbits.RD0 = 0;
    CloseOC2();
    PORTDbits.RD1 = 0;
    CloseOC3();
    PORTDbits.RD2 = 0;
    CloseOC4();
    PORTDbits.RD3 = 0;
    CloseOC5();
    PORTDbits.RD4 = 0;
    
	// Start off always using "OK" acknolege.
	g_ack_enable = TRUE;

	// Start off always echoing all data sent to us
	g_echo_enable = TRUE;

    // Initialize USB TX and RX buffer management
    g_RX_buf_in = 0;
    g_RX_buf_out = 0;
	g_TX_buf_in = 0;
	g_TX_buf_out = 0;
	g_TX_buf_length = 0;

	// And the USART TX and RX buffer management
	g_USART_RX_buf_in = 0;
	g_USART_RX_buf_out = 0;
	g_USART_TX_buf_in = 0;
	g_USART_TX_buf_out = 0;

	// Initialize the SP command variables
	SP_Command_Running = FALSE;
	SP_Rollover_Value = 4096;		// Start out with 12 bit resolution
	for (i=0; i < SP_MAX_CHANNEL_NUMBER; i++)
	{
		SP_PWM_Values[i] = 0;
		SP_Pin_Map[i] = 0;
		SP_PWM_Enables[i] = FALSE;
		SP_PWM_Values_Buffer[i] = 0;
	}
	SP_ISR_Counter = 0;

//    StepperInit();

	// Start the core timer off on the right foot
	T1_Core_Multiplier = T1_CORE_MULTIPLIER_RELOAD;

	// Open up the core timer at our 1ms rate
	OpenCoreTimer(CORE_TICK_RATE);

    // set up the core timer interrupt with a priority of 2 and zero sub-priority
	mConfigIntCoreTimer((CT_INT_ON | CT_INT_PRIOR_2 | CT_INT_SUB_PRIOR_0));

    // enable multi-vector interrupts
	INTEnableSystemMultiVectoredInt();

}//end UserInit

/********************************************************************
 * Function:        void ProcessIO(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        This function is a place holder for other user
 *                  routines. It is a mixture of both USB and
 *                  non-USB tasks.
 *
 * Note:            None
 *******************************************************************/
void ProcessIO(void)
{
	static BOOL in_cr = FALSE;
	static char last_fifo_size;
    unsigned char tst_char;
	unsigned char RXBufInTemp;
	BOOL	got_full_packet = FALSE;
	cdc_rx_len = 0;
	BYTE numBytesRead;

    //Blink the LEDs according to the USB device status
    BlinkUSBStatus();

    // User Application USB tasks
    if((USBDeviceState < CONFIGURED_STATE)||(USBSuspendControl==1)) return;

	// Pull in some new data if there is new data to pull in
	numBytesRead = getsUSBUSART(USB_In_Buffer,64);

	if(numBytesRead != 0)
	{
		// Copy data from USB buffer to our buffer
		for(cdc_rx_len = 0; cdc_rx_len < numBytesRead; cdc_rx_len++)
		{
			// Check to see if we are in a CR/LF situation
			tst_char = USB_In_Buffer[cdc_rx_len];
			if (g_echo_enable)
			{
				_mon_putc(tst_char);
				if (kCR == tst_char)
				{
					_mon_putc(kLF);
				}
				if (kBS == tst_char)
				{
					_mon_putc(' ');
					_mon_putc(kBS);
				}
			}
			if (
				!in_cr
				&&
				(
					kCR == tst_char
					||
					kLF == tst_char
				)
			)
			{
				in_cr = TRUE;
				g_RX_buf[g_RX_buf_in] = kCR;
				g_RX_buf_in++;

				// At this point, we know we have a full packet
				// of information from the PC to parse
				got_full_packet = TRUE;
			}
			else if (
				tst_char != kCR
				&&
				tst_char != kLF
			)
			{
				in_cr = FALSE;
				if (kBS == tst_char)
				{
					// Check to see that we're not already at the beginning
					if (g_RX_buf_in != g_RX_buf_out)
					{
						// If we have backspace, then handle that here
						// Then decriment the input pointer
						if (g_RX_buf_in > 0)
						{
							g_RX_buf_in--;
						}
						else
						{
							g_RX_buf_in = kRX_BUF_SIZE - 1;
						}
					}
					continue;
				}
				else
				{
					// Only add a byte if it is not a CR or LF or BS
					g_RX_buf[g_RX_buf_in] = tst_char;
					g_RX_buf_in++;
				}
			}
			else
			{
				continue;
			}
			// Check for buffer wraparound
			if (kRX_BUF_SIZE == g_RX_buf_in)
			{
				g_RX_buf_in = 0;
			}
			// If we hit the out pointer, then this is bad.
			if (g_RX_buf_in == g_RX_buf_out)
			{
				bitset (error_byte, kERROR_BYTE_RX_BUFFER_OVERRUN);
				break;
			}
			// Now, if we've gotten a full command (user send <CR>) then
			// go call the code that deals with that command, and then
			// keep parsing. (This allows multiple small commands per packet)
			if (got_full_packet)
			{
				parse_packet ();
				got_full_packet = FALSE;
			}
			PrintErrors ();
		}
	}

	PrintErrors();

	// Go send any data that needs sending to PC
	check_and_send_TX_data ();
}

// This routine checks to see if any of the bits in error_byte
// are set, and if so, prints out the corresponding error string.
void PrintErrors (void)
{
	unsigned char Bit;

	// Check for any errors logged in error_byte that need to be sent out
	if (error_byte)
	{
		for (Bit = 0; Bit < 8; Bit++)
		{
			if (bittst (error_byte, Bit))
			{
				printf (ErrorStrings[Bit]);
			}
		}
		error_byte = 0;
	}
}

// This is our replacement for the standard putc routine
// This enables printf() and all related functions to print to
// the USB output (i.e. to the PC) buffer
void _mon_putc (char c)
{
	// Only add chars to USB buffer if it's configured!
    if((USBDeviceState < CONFIGURED_STATE)||(USBSuspendControl==1))
	{
		return;
	}

	// We need to check to see if adding this character will
	// cause us to become overfull.
	// We want to sit and just process USB tasks if our buffer
	// is full.
	if (g_TX_buf_length >= (kTX_BUF_SIZE - 2))
	{
		while (g_TX_buf_length > 0)
		{
			// In this case, we want to puase for a moment, send out these
			// characers to the PC over USB, and then clear out out buffer.
			check_and_send_TX_data();
		}
	}

	// Copy the character into the output buffer
	g_TX_buf[g_TX_buf_in] = c;
	g_TX_buf_in++;
	g_TX_buf_length++;

	// Check for wrap around
	if (kTX_BUF_SIZE == g_TX_buf_in)
	{
		g_TX_buf_in = 0;
	}

	// Also check to see if we bumped up against our output pointer
	if (g_TX_buf_in == g_TX_buf_out)
	{
		bitset (error_byte, kERROR_BYTE_TX_BUF_OVERRUN);
		g_TX_buf_in = 0;
		g_TX_buf_out = 0;
		g_TX_buf_length = 0;
	}
	return;
}

// In this function, we check to see it is OK to transmit. If so
// we see if there is any data to transmit to PC. If so, we schedule
// it for sending.
void check_and_send_TX_data (void)
{
	char temp;
	unsigned char i;
	char TempBuf[64];

	// Only send if there's something there to send
	if (g_TX_buf_length != 0)
	{
		// We're going to sit and spin and wait until
		// can transmit
		while (!mUSBUSARTIsTxTrfReady ())
		{
			USBDeviceTasks();
			CDCTxService();
		}

		// Now copy over all of the FIFO bytes into our temp buffer
		for (i = 0; i < g_TX_buf_length; i++)
		{
			TempBuf[i] = g_TX_buf[g_TX_buf_out];
			g_TX_buf_out++;
			if (g_TX_buf_out == kTX_BUF_SIZE)
			{
				g_TX_buf_out = 0;
			}
		}

		putUSBUSART (TempBuf, g_TX_buf_length);
		g_TX_buf_length = 0;
		g_TX_buf_out = g_TX_buf_in;

		CDCTxService();
		USBDeviceTasks();
	}
}


// Look at the new packet, see what command it is, and
// route it appropriately. We come in knowing that
// our packet is in g_RX_buf[], and that the beginning
// of the packet is at g_RX_buf_out, and the end (CR) is at
// g_RX_buf_in. Note that because of buffer wrapping,
// g_RX_buf_in may be less than g_RX_buf_out.
void parse_packet(void)
{
	unsigned char	CommandNumber = 0;
	unsigned char	CmdName1 = 0;
	unsigned char	CmdName2 = 0;

	// Always grab the first character (which is the first byte of the command)
	CmdName1 = toupper (g_RX_buf[g_RX_buf_out]);
	if (kCR == CmdName1)
	{
		goto parse_packet_end;
	}
	advance_RX_buf_out();

	// Only grab second one if it is not a comma or CR
	if (g_RX_buf[g_RX_buf_out] != ',' && g_RX_buf[g_RX_buf_out] != kCR)
	{
		CmdName2 = toupper (g_RX_buf[g_RX_buf_out]);
		advance_RX_buf_out();
	}

	// Now loop through the array of commands, trying to find
	// a match based upon the two (or one) first characters of the command
	while (CommandList[CommandNumber].CommandPtr != NULL && CommandNumber < 250)
	{
		// If the two name characters match, then call the command to do the work
		if (
			(CmdName1 == CommandList[CommandNumber].Name1)
			&&
			(CmdName2 == CommandList[CommandNumber].Name2)
		)
		{
			CommandList[CommandNumber].CommandPtr();
			goto parse_packet_end;
		}
		else
		{
			CommandNumber++;
		}
	}

	// If we get here then we did not find a match for our command characters
	if (0 == CmdName2)
	{
		// Send back 'unknown command' error
		printf (
			 (const char *)"!8 Err: Unknown command '%c:%2X'\r\n"
			,CmdName1
			,CmdName1
		);
	}
	else
	{
		// Send back 'unknown command' error
		printf (
			 (const char *)"!8 Err: Unknown command '%c%c:%2X%2X'\r\n"
			,CmdName1
			,CmdName2
			,CmdName1
			,CmdName2
		);
	}
	// Set the error bit so that other routines knew there was an error
	bitset (error_byte, kERROR_BYTE_PRINTED_ERROR);

parse_packet_end:
	// Double check that our output pointer is now at the ending <CR>
	// If it is not, this indicates that there were extra characters that
	// the command parsing routine didn't eat. This would be an error and needs
	// to be reported. (Ignore for Reset command because FIFO pointers get cleared.)
	if (
		(g_RX_buf[g_RX_buf_out] != kCR)
		&&
		(0 == error_byte)
		&&
		(
			('R' != CmdName1)
			||
			(0 != CmdName2)
		)
	)
	{
		bitset (error_byte, kERROR_BYTE_EXTRA_CHARACTERS);
	}

	// Clean up by skipping over any bytes we haven't eaten
	// This is safe since we parse each packet as we get a <CR>
	// (i.e. g_RX_buf_in doesn't move while we are in this routine)
	g_RX_buf_out = g_RX_buf_in;

	// Always try to print out an OK packet here. If there was an
	// error, nothing will print out.
	PrintAck();
}

// Print out the positive acknoledgement that the packet was received
// if we have acks turned on.
void PrintAck(void)
{
	if (g_ack_enable && !error_byte)
	{
		printf ((const char *)st_OK);
	}
}

// Return all I/Os to their default power-on values
void parse_R_packet(void)
{
	UserInit();
}

// Restarts UBW32 with a software reset, which should launch bootloader
// without user having to press PRG button
void parse_BL_packet(void)
{
    unsigned int dma_status;
    unsigned int int_status;

	// Kill USB so that we always re-enumerate when we hit the bootloader
	USBModuleDisable();

	// Set the software key
	SoftwareKey = (unsigned int *)0xA0000000;
	*SoftwareKey = 0x12345678;

	// TEMP : For reset testing
	/* The following code illustrates a software Reset */
	/* perform a system unlock sequence */
    mSYSTEMUnlock(int_status, dma_status);

	/* set SWRST bit to arm reset */
    RSWRSTSET = 1;

    /* read RSWRST register to trigger reset */
    volatile int* p = &RSWRST;
    *p;

    /* prevent any unwanted code execution until reset occurs*/
    while(1);
}

// CU is "Configure UBW32" and controls system-wide configruation values
// "CU,<parameter_number>,<paramter_value><CR>"
// <paramter_number>	<parameter_value>
// 1					{1|0} turns on or off the 'ack' ("OK" at end of packets)
// 2					{1|0} turns on or off the echoing of all data sent to the UBW
void parse_CU_packet(void)
{
	unsigned char parameter_number;
	signed int parameter_value;

	extract_number (kUCHAR, (void*)&parameter_number, kREQUIRED);
	extract_number (kINT, (void*)&parameter_value, kREQUIRED);

	// Bail if we got a conversion error
	if (error_byte)
	{
		return;
	}

	switch (parameter_number)
	{
		case 1:
			if (0 == parameter_value || 1 == parameter_value)
			{
				g_ack_enable = parameter_value;
			}
			else
			{
				bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
			}
			break;
		case 2:
			if (0 == parameter_value || 1 == parameter_value)
			{
				g_echo_enable = parameter_value;
			}
			else
			{
				bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
			}
			break;

		default:
			bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
	}
}



// "TP" packet
// The TP command stands for Toggle Pin. You specify a port and a pin, and it
// toggles the pin. You can use the CU command to set up different amounts of
// time for it to toggle. This function will block until the toggle is done.
// Note that we read the LATch register, not the PORT register when determining
// what the actual inital state of the pin is. This should never be a problem
// but is something to keep in mind. (i.e. we don't read the state that the
// input buffer sees, we read the state that the output latch is trying to output.)
// Obviously make the pin an output before you use this or very little will happen.
// FORMAT: TP,<port>,<pin><CR>
// <port> is {A|B|C|D|E}
// <pin> is from 0 through 7
// EXAMPLE: "TP,B,2<CR>" to toggle PortB pin 2.
void parse_TP_packet(void)
{
//	unsigned char Port;
//	unsigned char Pin;
//	unsigned char Temp;
//	near unsigned char * Tempp;
//
//	extract_number (kUCASE_ASCII_CHAR, (void*)&Port, kREQUIRED);
//	extract_number (kUCHAR, (void*)&Pin, kREQUIRED);
//
//	// Bail if we got a conversion error
//	if (error_byte)
//	{
//		return;
//	}
//
//	// Limit check the parameters
//	if (Pin > 7 || Port < 'A' || Port > ('A' + gMaxPorts))
//	{
//		bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
//		return;
//	}
//	Tempp = Latchp + (Port - 'A');	// Generate pointer to our latch register
//	Temp = *Tempp;			// Save off inital value
//	*Tempp = *Tempp ^ (1 << Pin);
//	Delay10TCYx(g_TP_delay);
//	*Tempp = Temp;
}

// SL Packet
// For "Setting Latching inputs" on digital inputs
// "SL,<PortA>,[PortB],[PortC],[PortD],[PortE]<CR>"
void parse_SL_packet(void)
{
//	unsigned char Port = 0;
//
//	// Read in up to 5 bytes from the command, and put them
//	// in the latch enable array
//	while (Port < gMaxPorts)
//	{
//		extract_number (kUCHAR, (void*)&g_latching_enable[Port], Port);
//		Port++;
//	}
//	if (error_byte)
//	{
//		return;
//	}
//
//	// We need to update the latching_state array
//	// and we need to clear out the triggered array
//	for (Port = 0; Port < gMaxPorts; Port++)
//	{
//		g_latching_state[Port] = Portp[Port];
//		g_latching_triggered[Port] = 0;
//	}
}

// porT Output "TO" command
// "TO,<Port>,<Value><CR>"
// Just writes <Value> to <Port>
void parse_TO_packet(void)
{
	unsigned char Port;
	unsigned int Value;

	extract_number (kUCASE_ASCII_CHAR, (void*)&Port, kREQUIRED);
	extract_number (kUINT, (void*)&Value, kREQUIRED);

	// Bail if we got a conversion error
	if (error_byte)
	{
		return;
	}

	// Limit check the parameters
	if (Port < 'A' || Port > ('A' + kMAX_PORTS - 1))
	{
		bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
		return;
	}
	*LATPtr[Port - 'A'] = Value;
}

// porT Input "TI" command
// "TI,<Port><CR>"
// Just reads <Port> and returns its value as a
// "TI,<Value>" packet (<Value> is 16-bit decimal integer)
void parse_TI_packet(void)
{
	unsigned char Port;
	unsigned int Value;

	extract_number (kUCASE_ASCII_CHAR, (void*)&Port, kREQUIRED);

	// Bail if we got a conversion error
	if (error_byte)
	{
		return;
	}

	// Limit check the parameters
	if (Port < 'A' || Port > ('A' + kMAX_PORTS - 1))
	{
		bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
		return;
	}

	// Note that we want to clear out all latching triggered bits for
	// the byte that we're reading out.
//	Value = CheckLatchingInput(Port - 'A', 0xFF);
	Value = *PORTPtr[Port - 'A'];

	// Now send back the TI packet
	printf (
		(const char *)"TI,%05u\r\n"
		,Value
	);
}

// What we do here is to use the raw port value from the input pins
// if the latching_enable bit for that I/O pin is clear, and
// use the value that is in latching_state if the latching_enable
// bit is set.
// We also clear out the latching triggered bits that we are 'reading'
// i.e. any bit set in the LatchingClearMask will have it's g_latching_triggered
// bit cleared, indicating that we have 'read' that bit.
unsigned char CheckLatchingInput (unsigned char PortIndex, unsigned char LatchingClearMask)
{
//	g_latching_triggered[PortIndex] = g_latching_triggered[PortIndex] & ~LatchingClearMask;
//	return (
//		(Portp[PortIndex] & ~g_latching_enable[PortIndex])
//		|
//		(g_latching_state[PortIndex] & g_latching_enable[PortIndex])
//	);
}

// PW is for PWM'n a pin output
// "PW,<port>,<pin>,<cutoff><CR>"
// <port> is "A" through "E" and indicates the port
// <pin> is a number between 0 and 7 and indicates which pin to PWM
// <value> is between 0 and 255
void parse_PW_packet(void)
{
//	unsigned char Port;
//	unsigned char Value;
//	unsigned char Pin;
//
//	extract_number (kUCASE_ASCII_CHAR, (void*)&Port, kREQUIRED);
//	extract_number (kUCHAR, (void*)&Pin, kREQUIRED);
//	extract_number (kUCHAR, (void*)&Value, kREQUIRED);
//
//	// Bail if we got a conversion error
//	if (error_byte)
//	{
//		return;
//	}
//
//	// Limit check the parameters
//	if (Port < 'A' || Port > ('A' + gMaxPorts) || Pin > 7)
//	{
//		bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
//		return;
//	}
//
//
//#ifdef BRIAN_PWM
//    // setup the channel inits
//	// If value == 0, then we clear our bit in PWM_enable,
//	// otherwise we set it. (Turns PWM on/off for that pin)
//	Port -= 'A';
//	if (Value)
//	{
//		PWM_enable[Port] = bitset(PWM_enable[Port],Pin);
//	}
//	else
//	{
//		PWM_enable[Port] = bitclr(PWM_enable[Port],Pin);
//	}
//
//	// Store the PWM level
//	PWM_values[Port][Pin] = Value;
//
//	// If we are turning this PWM pin on, then make sure to
//	// start up the interrupt
//    if (Value)
//    {
//        T1CONbits.TMR1ON = 1;
//		PIE1bits.TMR1IE = 1;
//    }
//#endif
//#ifdef GILES_PWM
//
//
//#endif
}

// SC - Serial Configure (for use with SO command below)
// "SC,<DataPort>,<DataPin>,<DataState>,<ClockPort>,<ClockPin>,<ClockState>,<LatchPort>,<LatchPin>,<LatchState><CR>"
// The SC command requires data, clock and latch pins. This commands set ups
// which port and which pin will be used for each of these three, and
// their inital states.
void parse_SC_packet(void)
{
//	unsigned char SO_DataState, SO_ClockState, SO_LatchState;
//
//	extract_number (kUCASE_ASCII_CHAR, (void*)&gSO_DataPort, kREQUIRED);
//	extract_number (kUCHAR, (void*)&gSO_DataPin, kREQUIRED);
//	extract_number (kUCHAR, (void*)&SO_DataState, kREQUIRED);
//	extract_number (kUCASE_ASCII_CHAR, (void*)&gSO_ClockPort, kREQUIRED);
//	extract_number (kUCHAR, (void*)&gSO_ClockPin, kREQUIRED);
//	extract_number (kUCHAR, (void*)&SO_ClockState, kREQUIRED);
//	extract_number (kUCASE_ASCII_CHAR, (void*)&gSO_LatchPort, kREQUIRED);
//	extract_number (kUCHAR, (void*)&gSO_LatchPin, kREQUIRED);
//	extract_number (kUCHAR, (void*)&SO_LatchState, kREQUIRED);
//
//	// Bail if we got a conversion error
//	if (error_byte)
//	{
//		return;
//	}
//
//	// Check for invalid parameters
//	if (
//		(gSO_DataPort < 'A' || gSO_DataPort > 'A' + gMaxPorts)
//		||
//		(gSO_ClockPort < 'A' || gSO_ClockPort > 'A' + gMaxPorts)
//		||
//		(gSO_ClockPort < 'A' || gSO_ClockPort > 'A' + gMaxPorts)
//		||
//		(gSO_DataPin > 7 || gSO_ClockPin > 7 || gSO_LatchPin > 7)
//		||
//		(SO_DataState > 1 || SO_ClockState > 1 || SO_LatchState > 1)
//	)
//	{
//		bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
//		return;
//	}
//	// Convert the port letters to port numbers
//	gSO_DataPort -= 'A';
//	gSO_ClockPort -= 'A';
//	gSO_LatchPort -= 'A';
//
//	// Set up each of the three pins' data directions
//	Trisp[gSO_DataPort] = bitclr(Trisp[gSO_DataPort], gSO_DataPin);
//	Trisp[gSO_ClockPort] = bitclr(Trisp[gSO_ClockPort], gSO_ClockPin);
//	Trisp[gSO_LatchPort] = bitclr(Trisp[gSO_LatchPort], gSO_LatchPin);
//	// And inital states
//	Latchp[gSO_DataPort] = bitclr(Latchp[gSO_DataPort], gSO_DataPin) | (SO_DataState << gSO_DataPin);
//	Latchp[gSO_ClockPort] = bitclr(Latchp[gSO_ClockPort], gSO_ClockPin) | (SO_ClockState << gSO_ClockPin);
//	Latchp[gSO_LatchPort] = bitclr(Latchp[gSO_LatchPort], gSO_LatchPin) | (SO_LatchState << gSO_LatchPin);
}

// SO - Serial Output
// "SO,<Latch>,<Data1>,[Data2],[Data3]...<CR>"
// The SO command takes <Data1> and shifts it out (LSB first) of the data
// pin defined in the SC command, toggling the clock bit inbetween each
// bit. It then does the same for all following
// [Data] bytes. Then, if <Latch> is 1, it toggles the latch bit.
void parse_SO_packet(void)
{
//	unsigned char Latch;
//	unsigned char Data;
//	unsigned char BitCtr;
//
//	extract_number (kUCHAR, (void*)&Latch, kREQUIRED);
//
//	// Bail if we got a conversion error
//	if (error_byte)
//	{
//		return;
//	}
//
//	// Check for invalid data
//	if (Latch > 1)
//	{
//		bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
//		return;
//	}
//
//	while (!extract_number (kUCHAR, (void*)&Data, kOPTIONAL))
//	{
//		// Now, shift out the byte one bit at a time
//		for (BitCtr = 0; BitCtr < 8; BitCtr++)
//		{
//			// First set the data bit
//			if (bittst(Data,BitCtr))
//			{
//				Latchp[gSO_DataPort] = bitset(Latchp[gSO_DataPort],gSO_DataPin);
//			}
//			else
//			{
//				Latchp[gSO_DataPort] = bitclr(Latchp[gSO_DataPort],gSO_DataPin);
//			}
//
//			// Then toggle the clock bit
//			Latchp[gSO_ClockPort] = bitinvert(Latchp[gSO_ClockPort],gSO_ClockPin);
//			Latchp[gSO_ClockPort] = bitinvert(Latchp[gSO_ClockPort],gSO_ClockPin);
//		}
//	}
//
//	// If we are supposed to latch at the end, then do that here
//	if (Latch)
//	{
//		Latchp[gSO_LatchPort] = bitinvert(Latchp[gSO_LatchPort],gSO_LatchPin);
//		Latchp[gSO_LatchPort] = bitinvert(Latchp[gSO_LatchPort],gSO_LatchPin);
//	}
}

void parse_EC_packet(void)
{
}

void parse_ES_packet(void)
{
}

// "T" Packet
// Causes PIC to sample digital or analog inputs at a regular interval and send
// I (or A) packets back at that interval.
// Send T,0,0<CR> to stop I (or A) packets
// FORMAT: T,<TIME_BETWEEN_UPDATES_IN_MS>,<MODE><CR>
// <MODE> is 0 for digital (I packets) and 1 for analog (A packets)
// EXAMPLE: "T,4000,0<CR>" to send an I packet back every 4 seconds.
// EXAMPLE: "T,2000,1<CR>" to send an A packet back every 2 seconds.
void parse_T_packet(void)
{
//	unsigned int value;
//	unsigned char mode = 0;
//
//	// Extract the <TIME_BETWEEN_UPDATES_IN_MS> value
//	extract_number (kUINT, (void*)&time_between_updates, kREQUIRED);
//	// Extract the <MODE> value
//	extract_number (kUCHAR, (void*)&mode, kREQUIRED);
//
//	// Bail if we got a conversion error
//	if (error_byte)
//	{
//		return;
//	}
//
//	// Now start up the timer at the right rate or shut
//	// it down.
//	if (0 == mode)
//	{
//		if (0 == time_between_updates)
//		{
//			// Turn off sending of I packets.
//			ISR_D_RepeatRate = 0;
//		}
//		else
//		{
//			T2CONbits.TMR2ON = 1;
//
//			// Eventually gaurd this section from interrupts
//			ISR_D_RepeatRate = time_between_updates;
//		}
//	}
//	else
//	{
//		if (0 == time_between_updates)
//		{
//			// Turn off sending of A packets.
//			ISR_A_RepeatRate = 0;
//		}
//		else
//		{
//			T2CONbits.TMR2ON = 1;
//
//			// Eventually gaurd this section from interrupts
//			ISR_A_RepeatRate = time_between_updates;
//		}
//	}
}


// FORMAT: C,<dirA>,<dirB>,<dirC>,<dirD>,<dirE>,<dirF>,<dirG><CR>
// EXAMPLE: "C,255,0,4,0,0,0<CR>"
// <dirX> is the 16-bit value sent to the Data Direction (DDR i.e. TRIS) regsiter for
// each port. A 1 in a bit location means input, a 0 means output.
void parse_C_packet(void)
{
	unsigned char Port = 0;
	unsigned int Value;

	// Extract each of the seven values.
	while (Port < kMAX_PORTS)
	{
		extract_number (kUINT, &Value, kOPTIONAL);
		// Bail if we got a conversion error
		if (error_byte)
		{
			return;
		}

		*TRISPtr[Port] = Value;
		Port++;
	}
}

// FORMAT: CA,<analogB><CR>
// EXAMPLE: "C,12<CR>"
// <analogB> is the 16-bit value sent to configure each bit on port B to analog in
// A 1 in a bit location means anlog input, a 0 means digital input.
void parse_CA_packet(void)
{
	unsigned int Value;

	extract_number (kUINT, &Value, kOPTIONAL);
	// Bail if we got a conversion error
	if (error_byte)
	{
		return;
	}
	// configure port for input
	TRISBSET = Value;

	// configure analog port
    //clear all previous configuration
	AD1CON1CLR = 0xFFFF;
	AD1CON2CLR = 0xFFFF;
	AD1CON3CLR = 0xFFFF;
	AD1PCFGCLR = 0xFFFF;

	// NOTE: in AD1PCFG a 0 means analog therefore we must invert the bits
	// this is done using the '~' (compliment) operator
	AD1PCFGSET = ~Value;

	AD1CON1SET = 0x0004; //set automatic sampling mode

	//configure format
	AD1CON1SET = 0x0400; //set to 16-bit result
	AD1CON1SET = 0x0000; // 0x0000 integer
						 // 0x0100 signed integer
						 // 0x0200 fractional
						 // 0x0300 would be signed fractional

	//set reference voltages bits 15:13
	AD1CON2 = 0x0000; //Vref from AVDD and AVSS
					  //not sure how to set it to anything else even if I wanted to

	//turn on scan mode for all analog inputs by setting bit10
	AD1CON2 = 0x0400;
	//now set AD1CSSL register to 1 for each bit to include in scan
	AD1CSSL = Value;

	//want as many spots in results buffer as analog inputs therefore must calculate
	char num_analog_pins=0;
	unsigned int Value_copy;
	unsigned int i;
	Value_copy = Value;
	for (i=0;i<=15;i++){
		num_analog_pins = num_analog_pins + (0x0001 & Value_copy);
		Value_copy = Value_copy>>1;
	}
	//now set buffer to that size AD1CON2<5:2>
	AD1CON2SET = (num_analog_pins-1)<<2;

	//buffer-fill mode
	AD1CON2SET = 0x0000; //use only 1 buffer
						// 0x0002 to use two buffers

	//Select ADC conversion clock source internal
	AD1CON3SET = 0x8000;

	//ADC clock control
	AD1CON1SET = 0x00E0; //internal RC ossilator
	//ADC aqusition period
	AD1CON3SET = 0x00FF; //maximum acquistion period

	//clear the interup flag
	IFS1CLR = 0x0002;


	if(num_analog_pins){
		//turn on the ADC module
		AD1CON1SET = 0x8000;
	}
	else {
		//turn off the ADC module
		AD1CON1CLR = 0x8000;
	}
}

// Outputs values to the ports pins that are set up as outputs.
// Example "O,121,224,002<CR>"
void parse_O_packet(void)
{
	unsigned int Port = 0;

	// Extract each of the five values.
	while (Port < kMAX_PORTS)
	{
		extract_number (kUINT, (void*)LATPtr[Port], Port);
		Port++;
	}
	// Bail if we got a conversion error
	if (error_byte)
	{
		return;
	}
}

// Read in the three I/O ports (A,B,C) and create
// a packet to send back with all of values.
// Example: "I,143,221,010<CR>"
// Remember that on UBW 28 pin boards, we only have
// Port A bits 0 through 5
// Port B bits 0 through 7
// Port C bits 0,1,2 and 6,7
// And that Port C bits 0,1,2 are used for
// 		User1 LED, User2 LED and Program switch respectively.
// The rest will be read in as zeros.
void parse_I_packet(void)
{
	printf (
		(const char *)"I,%05i,%05i,%05i,%05i,%05i,%05i,%05i",
		*PORTPtr[0],
		*PORTPtr[1],
		*PORTPtr[2],
		*PORTPtr[3],
		*PORTPtr[4],
		*PORTPtr[5],
		*PORTPtr[6]
	);
	printf ((const char *)st_LFCR);
}

// Read in the analog pins send back with all of values.
// Format: IA,<pins>,<delay>,<count><CR>
// Example: "IA,5,100,150<CR>"
// would read pins 0 and 2  150 times with a 100ms delay between reads
void parse_IA_packet(void)
{
 	DWORD start_cpu_time;
 	DWORD end_cpu_time;
	unsigned int pins;
	unsigned long delay_us;
	unsigned int count;
	int mask;
	int buff_index;
	int i;

	extract_number (kUINT, &pins, kREQUIRED);
	extract_number (kULONG, &delay_us, kREQUIRED);
	extract_number (kUINT, &count, kREQUIRED);
	// Bail if we got a conversion error
	if (error_byte)
	{
		return;
	}

	printf ((const char *)"IA");

	//only read pins that are analog
	pins = pins & ~AD1PCFG;

    // We only generate output (and sample analogs) if some pins are in analog mode
	if(pins)
	{
        //start sampling
        AD1CON1SET = 0x0004;

        //start_output
        while(count != 0)
        {
            //wait till conversion is done
            while(!(AD1CON1 & 0x0001)) { }

            //clear the interrupt flag
            IFS1CLR = 0x0002;

            buff_index = 0;
            mask = 0x0001;
            //output result
            for (i = 0; i < 16; i++)
            {
                //if wanted that pin output then output
                if (mask & pins)
                { 
                    printf ((const char *)",%01i",*ADC1BUFPtr[buff_index]);
                }
                //if pin is analog then increment buffer index
                if (mask & ~AD1PCFG)
                { 
                    buff_index++;
                }
                mask = mask << 1;
            }
            count--;
            
            // Sit and spin for delay_us if it's not zero
            if (delay_us)
            {
                // Get cpu counter value for delay
                start_cpu_time = _CP0_GET_COUNT();

                // Calculate end value of cpu counter
                end_cpu_time = start_cpu_time + SYS_FREQ / 2000000 * delay_us;

                // if counter wont roll-over
                if (end_cpu_time >= start_cpu_time)
                {
                    while (_CP0_GET_COUNT() < end_cpu_time);
                }
                else { //counter will roll over
                    while (_CP0_GET_COUNT() > start_cpu_time || _CP0_GET_COUNT() < end_cpu_time);
                }
            }
        }

        //stop sampling
        AD1CON1CLR = 0x0004;
    }
    
	//We are done
	printf ((const char *)st_LFCR);
}

// All we do here is just print out our version number
void parse_V_packet(void)
{
	// Print out the normal version string
	printf ((const char *)st_version);
	printf ((const char *)st_LFCR);

	// And for versions 1.5.0 and above, we print out our 'serial' number as well
//	printf (
//		(const char *)" SN: %c%c%c%c\r\n",
//		sd003.string[0],
//		sd003.string[1],
//		sd003.string[2],
//		sd003.string[3]
//	);
}

// A is for read Analog inputs
// Just print out the last analog values for each of the
// enabled channels. The number of value returned in the
// A packet depend upon the number of analog inputs enabled.
// The user can enabled any number of analog inputs between
// 0 and 12. (none enabled, through all 12 analog inputs enabled).
// Returned packet will look like "A,0,0,0,0,0,0<CR>" if
// six analog inputs are enabled but they are all
// grounded. Note that each one is a 10 bit
// value, where 0 means the intput was at ground, and
// 1024 means it was at +5 V. (Or whatever the USB +5
// pin is at.)
void parse_A_packet(void)
{
//	char channel = 0;
//
//	// Put the beginning of the packet in place
//	printf ((const char *)"A");
//
//	// Now add each analog value
//	for (channel = 0; channel < AnalogEnable; channel++)
//	{
//		printf(
//			(const char *)",%04u"
//			,ISR_A_FIFO[channel][ISR_A_FIFO_out]
//		);
//	}
//
//	// Add LF/CR and terminating zero.
//	printf ((const char *)st_LFCR);
}

// MW is for Memory Write
// "MW,<location>,<value><CR>"
// <location> is a decimal value between 0 and 4096 indicating the RAM address to write to
// <value> is a decimal value between 0 and 255 that is the value to write
// If <location> is between 8192 and 8447, then we are writing to EEPROM instead of RAM
void parse_MW_packet(void)
{
//	unsigned int location;
//	unsigned char value;
//
//	extract_number (kUINT, (void*)&location, kREQUIRED);
//	extract_number (kUCHAR, (void*)&value, kREQUIRED);
//
//	// Bail if we got a conversion error
//	if (error_byte)
//	{
//		return;
//	}
//
//	if (location >= 8192 && location <= 8447)
//	{
//		WriteEE ((unsigned char)(location - 8192), value);
//	}
//	// Limit check the address and write the byte in
//	else if (location < 4096)
//	{
//		*((unsigned char *)location) = value;
//	}
//	// This is the error case.
//	else
//	{
//		bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
//		return;
//	}
}


// MR is for Memory Read
// "MW,<location><CR>"
// <location> is a decimal value between 0 and 4096 indicating the RAM address to read from
// The UBW will then send a "MR,<value><CR>" packet back to the PC
// where <value> is the byte value read from the address
// For values between 8192 and 8447, we need to read EEPROM instead of RAM
void parse_MR_packet(void)
{
//	unsigned int location;
//	unsigned char value;
//
//	extract_number (kUINT, (void*)&location, kREQUIRED);
//
//	// Bail if we got a conversion error
//	if (error_byte)
//	{
//		return;
//	}
//
//	if (location >= 8192 && location <= 8447)
//	{
//		value = ReadEE ((unsigned char)(location - 8192));
//	}
//	// Limit check the address and write the byte in
//	else if (location < 4096)
//	{
//		value = *((unsigned char *)location);
//	}
//	// This is the error case.
//	else
//	{
//		bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
//		return;
//	}
//
//	// Now send back the MR packet
//	printf (
//		(const char *)"MR,%03u\r\n"
//		,value
//	);
}

// PD is for Pin Direction
// "PD,<port>,<pin>,<direction><CR>"
// <port> is "A", "B", "C" and indicates the port
// <pin> is a number between 0 and 7 and indicates which pin to change direction on
// <direction> is "1" for input, "0" for output
void parse_PD_packet(void)
{
	unsigned char Port;
	unsigned char Pin;
	unsigned char Direction;

	extract_number (kUCASE_ASCII_CHAR, (void*)&Port, kREQUIRED);
	extract_number (kUCHAR, (void*)&Pin, kREQUIRED);
	extract_number (kUCHAR, (void*)&Direction, kREQUIRED);

	// Bail if we got a conversion error
	if (error_byte)
	{
		return;
	}

	// Limit check the parameters
	if (Direction > 1 || Pin > 31 || Port < 'A' || Port > ('A' + kMAX_PORTS - 1))
	{
		bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
		return;
	}

	if (0 == Direction)
	{
		bitclr (*TRISPtr[Port - 'A'], Pin);
	}
	else
	{
		bitset (*TRISPtr[Port - 'A'], Pin);
	}
}

// PI is for Pin Input
// "PI,<port>,<pin><CR>"
// <port> is "A", "B", "C" and indicates the port
// <pin> is a number between 0 and 7 and indicates which pin to read
// The command returns a "PI,<value><CR>" packet,
// where <value> is the value (0 or 1 for digital, 0 to 1024 for Analog)
// value for that pin.
void parse_PI_packet(void)
{
	unsigned char Port;
	unsigned char Pin;
	unsigned char Value = 0;

	extract_number (kUCASE_ASCII_CHAR, (void*)&Port, kREQUIRED);
	extract_number (kUCHAR, (void*)&Pin, kREQUIRED);

	// Bail if we got a conversion error
	if (error_byte)
	{
		return;
	}

	// Limit check the parameters
	if (Pin > 31 || Port < 'A' || Port > ('A' + kMAX_PORTS - 1))
	{
		bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
		return;
	}

	// Then test the bit in question based upon port
//	Value = bittst (CheckLatchingInput(Port - 'A', 1 << Pin), Pin);
	Value = bittst (*PORTPtr[Port - 'A'], Pin);

	// Now send back our response
	printf(
		 (const char *)"PI,%u\r\n"
		,Value
	);
}

// PO is for Pin Output
// "PO,<port>,<pin>,<value><CR>"
// <port> is "A", "B", "C" and indicates the port
// <pin> is a number between 0 and 7 and indicates which pin to write out the value to
// <value> is "1" or "0" and indicates the state to change the pin to
void parse_PO_packet(void)
{
	unsigned char Port;
	unsigned char Pin;
	unsigned char Value;

	extract_number (kUCASE_ASCII_CHAR, (void*)&Port, kREQUIRED);
	extract_number (kUCHAR, (void*)&Pin, kREQUIRED);
	extract_number (kUCHAR, (void*)&Value, kREQUIRED);

	// Bail if we got a conversion error
	if (error_byte)
	{
		return;
	}

	// Limit check the parameters
	if (Value > 1 || Pin > 31 || Port < 'A' || Port > ('A' + kMAX_PORTS - 1))
	{
		bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
		return;
	}

	if (0 == Value)
	{
		bitclr (*LATPtr[Port - 'A'], Pin);
	}
	else
	{
		bitset (*LATPtr[Port - 'A'], Pin);
	}
}

// TX is for Serial Transmit
// "TX,<data_length>,<variable_length_data><CR>"
// <data_length> is a count of the number of bytes in the <variable_length_data> field.
// It must never be larger than the number of bytes that are currently free in the
// software TX buffer or some data will get lost.
// <variable_length_data> are the bytes that you want the UBW to send. It will store them
// in its software TX buffer until there is time to send them out the TX pin.
// If you send in "0" for a <data_length" (and thus nothing for <variable_length_data>
// then the UBW will send back a "TX,<free_buffer_space><CR>" packet,
// where <free_buffer_space> is the number of bytes currently available in the
// software TX buffer.
void parse_TX_packet(void)
{
}

// RX is for Serial Receive
// "RX,<length_request><CR>"
// <length_request> is the maximum number of characters that you want the UBW to send
// back to you in the RX packet. If you use "0" for <length_request> then the UBW
// will just send you the current number of bytes in it's RX buffer, and if
// there have been any buffer overruns since the last time a <length_request> of
// "0" was received by the UBW.
// This command will send back a "RX,<length>,<variable_length_data><CR>"
// or "RX,<buffer_fullness>,<status><CR>" packet depending upon if you send
// "0" or something else for <length_request>
// <length> in the returning RX packet is a count of the number of bytes
// in the <variable_length_data> field. It will never be more than the
// <length_request> you sent in.
// <variable_length_data> is the data (in raw form - byte for byte what was received -
// i.e. not translated in any way, into ASCII values or anything else) that the UBW
// received. This may include <CR>s and NULLs among any other bytes, so make sure
// your PC application treates the RX packet coming back from the UBW in a speical way
// so as not to screw up normal packet processing if any special caracters are received.
// <buffer_fullness> is a valule between 0 and MAX_SERIAL_RX_BUFFER_SIZE that records
// the total number of bytes, at that point in time, that the UBW is holding, waiting
// to pass on to the PC.
// <status> has several bits.
//	Bit 0 = Software RX Buffer Overrun (1 means software RX buffer (on RX pin)
//		has been overrun and data has been lost) This will happen if you don't
//		read the data out of the UWB often enough and the data is coming in too fast.
//	Bit 1 = Software TX Buffer Overrun (1 means software TX buffer (on TX pin)
//		as been overrun and data hs been lost. This will happen if you send too much
//		data to the UBW and you have the serial port set to a low baud rate.
void parse_RX_packet(void)
{
}

// CX is for setting up serial port parameters
// TBD
void parse_CX_packet(void)
{
}

// RC is for outputting RC servo pulses on a pin
// "RC,<port>,<pin>,<value><CR>"
// <port> is "A", "B", "C" and indicates the port
// <pin> is a number between 0 and 7 and indicates which pin to output the new value on
// <value> is an unsigned 16 bit number between 0 and 11890.
// If <value> is "0" then the RC output on that pin is disabled.
// Otherwise <value> = 1 means 1ms pulse, <value> = 11890 means 2ms pulse,
// any value inbetween means proportional pulse values between those two
// Note: The pin used for RC output must be set as an output, or not much will happen.
// The RC command will continue to send out pulses at the last set value on
// each pin that has RC output with a repition rate of 1 pulse about every 19ms.
// If you have RC output enabled on a pin, outputting a digital value to that pin
// will be overwritten the next time the RC pulses. Make sure to turn off the RC
// output if you want to use the pin for something else.
void parse_RC_packet(void)
{
//	unsigned char port;
//	unsigned char pin;
//	unsigned int value;
//
//	port = extract_number (kUCASE_ASCII_CHAR, (void*)&port, kREQUIRED);
//	pin = extract_number (kUCHAR, (void*)&pin, kREQUIRED);
//	value = extract_number (kUINT, (void*)&value, kREQUIRED);
//
//	// Bail if we got a conversion error
//	if (error_byte)
//	{
//		return;
//	}
//
//	// Max value user can input. (min is zero)
//	if (value > 11890)
//	{
//		bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
//		return;
//	}
//
//	// Now get Value in the form that TMR0 needs it
//	// TMR0 needs to get filled with values from 65490 (1ms) to 53600 (2ms)
//	if (value != 0)
//	{
//		value = (65535 - (value + 45));
//	}
//
//	if (pin > 7)
//	{
//		bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
//		return;
//	}
//	if ('A' == port)
//	{
//		port = 0;
//	}
//	else if ('B' == port)
//	{
//		port = 8;
//	}
//	else if ('C' == port)
//	{
//		port = 16;
//	}
//	else
//	{
//		bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
//		return;
//	}
//
//	// Store the new RC time value
//	g_RC_value[pin + port] = value;
//	// Only set this state if we are off - if we are already running on
//	// this pin, then the new value will be picked up next time around (19ms)
//	if (kOFF == g_RC_state[pin + port])
//	{
//		g_RC_state[pin + port] = kWAITING;
//	}
}

// BC is for Bulk Configure
// BC,<port A init>,<waitmask>,<wait delay>,<strobemask>,<strobe delay><CR>
// This command sets up the mask and strobe bits on port A for the
// BO (Bulk Output) command below. Also suck in wait delay, strobe delay, etc.
void parse_BC_packet(void)
{
//	unsigned char BO_init;
//	unsigned char BO_strobe_mask;
//	unsigned char BO_wait_mask;
//	unsigned char BO_wait_delay;
//	unsigned char BO_strobe_delay;
//
//	extract_number (kUCHAR, (void*)&BO_init, kREQUIRED);
//	extract_number (kUCHAR, (void*)&BO_wait_mask, kREQUIRED);
//	extract_number (kUCHAR, (void*)&BO_wait_delay, kREQUIRED);
//	extract_number (kUCHAR, (void*)&BO_strobe_mask, kREQUIRED);
//	extract_number (kUCHAR, (void*)&BO_strobe_delay, kREQUIRED);
//
//	// Bail if we got a conversion error
//	if (error_byte)
//	{
//		return;
//	}
//
//	// Copy over values to their gloabls
//	g_BO_init = BO_init;
//	g_BO_wait_mask = BO_wait_mask;
//	g_BO_strobe_mask = BO_strobe_mask;
//	g_BO_wait_delay = BO_wait_delay;
//	g_BO_strobe_delay = BO_strobe_delay;
//	// And initalize Port A
//	LATA = g_BO_init;
}

// Bulk Output (BO)
// BO,4AF2C124<CR>
// After the inital comma, pull in hex values and spit them out to port A
// Note that the procedure here is as follows:
//	1) Write new value to PortB
//	2) Assert <strobemask>
//	3) Wait for <strobdelay> (if not zero)
//	4) Deassert <strobemask>
//	5) Wait for <waitmask> to be asserted
//	6) Wait for <waitmask> to be deasserted
//	7) If 5) or 6) takes longer than <waitdelay> then just move on to next byte
//	Repeat for each byte
void parse_BO_packet(void)
{
//	unsigned char BO_data_byte;
//	unsigned char new_port_A_value;
//	unsigned char tmp;
//	unsigned char wait_count = 0;
//
//	// Check for comma where ptr points
//	if (g_RX_buf[g_RX_buf_out] != ',')
//	{
//		printf ((const char *)"!5 Err: Need comma next, found: '%c'\r\n", g_RX_buf[g_RX_buf_out]);
//		bitset (error_byte, kERROR_BYTE_PRINTED_ERROR);
//		return;
//	}
//
//	// Move to the next character
//	advance_RX_buf_out ();
//
//	// Make sure Port A is correct
//	LATA = g_BO_init;
//	new_port_A_value = ((~LATA & g_BO_strobe_mask)) | (LATA & ~g_BO_strobe_mask);
//
//	while (g_RX_buf[g_RX_buf_out] != 13)
//	{
//		// Pull in a nibble from the input buffer
//		tmp = toupper (g_RX_buf[g_RX_buf_out]);
//		if (tmp >= '0' && tmp <= '9')
//		{
//			tmp -= '0';
//		}
//		else if (tmp >= 'A' && tmp <= 'F')
//		{
//			tmp -= 55;
//		}
//		else
//		{
//			bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
//			return;
//		}
//		BO_data_byte = tmp << 4;
//		advance_RX_buf_out ();
//
//		// Check for CR next
//		if (kCR == g_RX_buf[g_RX_buf_out])
//		{
//			bitset (error_byte, kERROR_BYTE_MISSING_PARAMETER);
//			return;
//		}
//
//		tmp =  toupper (g_RX_buf[g_RX_buf_out]);
//		if (tmp >= '0' && tmp <= '9')
//		{
//			tmp -= '0';
//		}
//		else if (tmp >= 'A' && tmp <= 'F')
//		{
//			tmp -= 55;
//		}
//		else
//		{
//			bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
//			return;
//		}
//		BO_data_byte = BO_data_byte + tmp;
//		advance_RX_buf_out ();
//
//		// Output the byte on Port B
//		LATB = BO_data_byte;
//
//		// And strobe the Port A bits that we're supposed to
//		LATA = new_port_A_value;
//		if (g_BO_strobe_delay)
//		{
//			Delay10TCYx (g_BO_strobe_delay);
//		}
//		LATA = g_BO_init;
//
//		if (g_BO_wait_delay)
//		{
//			// Now we spin on the wait bit specified in WaitMask
//			// (Used for Busy Bits) We also have to wait here
//			// for a maximum of g_BO_wait_delay, which is in 10 clock units
//			// First we wait for the wait mask to become asserted
//
//			// Set the wait counter to the number of delays we want
//			wait_count = g_BO_wait_delay;
//			while (
//				((g_BO_init & g_BO_wait_mask) == (PORTA & g_BO_wait_mask))
//				&&
//				(wait_count != 0)
//			)
//			{
//				Delay1TCY ();
//				Delay1TCY ();
//				Delay1TCY ();
//				Delay1TCY ();
//				Delay1TCY ();
//				wait_count--;
//			}
//
//			// Set the wait counter to the number of delays we want
//			wait_count = g_BO_wait_delay;
//			// Then we wait for the wait mask to become de-asserted
//			while (
//				((g_BO_init & g_BO_wait_mask) != (PORTA & g_BO_wait_mask))
//				&&
//				(wait_count != 0)
//			)
//			{
//				Delay1TCY ();
//				Delay1TCY ();
//				Delay1TCY ();
//				Delay1TCY ();
//				Delay1TCY ();
//				wait_count--;
//			}
//		}
//	}
}

// Bulk Stream (BS) (he he, couldn't think of a better name)
// BS,<count>,<binary_data><CR>
// This command is extremely similar to the BO command
// except that instead of ASCII HEX values, it actually
// takes raw binary data.
// So in order for the UBW to know when the end of the stream
// is, we need to have a <count> of bytes.
// <count> represents the number of bytes after the second comma
// that will be the actual binary data to be streamed out port B.
// Then, <binary_data> must be exactly that length.
// <count> must be between 1 and 56 (currently - in the future
// it would be nice to extend the upper limit)
// The UBW will pull in one byte at a time within the <binary_data>
// section and output it to PORTB exactly as the BO command does.
// It will do this for <count> bytes. It will then pull in another
// byte (which must be a carrige return) and be done.
// The whole point of this command is to improve data throughput
// from the PC to the UBW. This form of data is also more efficient
// for the UBW to process.
void parse_BS_packet(void)
{
//	unsigned char BO_data_byte;
//	unsigned char new_port_A_value;
//	unsigned char tmp;
//	unsigned char wait_count = 0;
//	unsigned char byte_count = 0;
//
//	// Get byte_count
//	extract_number (kUCHAR, (void*)&byte_count, kREQUIRED);
//
//	// Limit check it
//	if (0 == byte_count || byte_count > 56)
//	{
//		bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
//		return;
//	}
//
//	// Check for comma where ptr points
//	if (g_RX_buf[g_RX_buf_out] != ',')
//	{
//		printf ((const char *)"!5 Err: Need comma next, found: '%c'\r\n", g_RX_buf[g_RX_buf_out]);
//		bitset (error_byte, kERROR_BYTE_PRINTED_ERROR);
//		return;
//	}
//
//	// Move to the next character
//	advance_RX_buf_out ();
//
//	// Make sure Port A is correct
//	LATA = g_BO_init;
//	new_port_A_value = ((~LATA & g_BO_strobe_mask)) | (LATA & ~g_BO_strobe_mask);
//
//	while (byte_count != 0)
//	{
//		// Pull in a single byte from input buffer
//		BO_data_byte = g_RX_buf[g_RX_buf_out];
//		advance_RX_buf_out ();
//
//		// Count this byte
//		byte_count--;
//
//		// Output the byte on Port B
//		LATB = BO_data_byte;
//
//		// And strobe the Port A bits that we're supposed to
//		LATA = new_port_A_value;
//		if (g_BO_strobe_delay)
//		{
//			Delay10TCYx (g_BO_strobe_delay);
//		}
//		LATA = g_BO_init;
//
//		if (g_BO_wait_delay)
//		{
//			// Now we spin on the wait bit specified in WaitMask
//			// (Used for Busy Bits) We also have to wait here
//			// for a maximum of g_BO_wait_delay, which is in 10 clock units
//			// First we wait for the wait mask to become asserted
//
//			// Set the wait counter to the number of delays we want
//			wait_count = g_BO_wait_delay;
//			while (
//				((g_BO_init & g_BO_wait_mask) == (PORTA & g_BO_wait_mask))
//				&&
//				(wait_count != 0)
//			)
//			{
//				Delay1TCY ();
//				Delay1TCY ();
//				Delay1TCY ();
//				Delay1TCY ();
//				Delay1TCY ();
//				wait_count--;
//			}
//
//			// Set the wait counter to the number of delays we want
//			wait_count = g_BO_wait_delay;
//			// Then we wait for the wait mask to become de-asserted
//			while (
//				((g_BO_init & g_BO_wait_mask) != (PORTA & g_BO_wait_mask))
//				&&
//				(wait_count != 0)
//			)
//			{
//				Delay1TCY ();
//				Delay1TCY ();
//				Delay1TCY ();
//				Delay1TCY ();
//				Delay1TCY ();
//				wait_count--;
//			}
//		}
//	}
}

// SS Send SPI
void parse_SS_packet (void)
{
}

// RS Receive SPI
void parse_RS_packet (void)
{
}

// CS Configure SPI
void parse_CS_packet (void)
{
}

// SI Send I2C
void parse_SI_packet (void)
{
}

// RI Receive I2C
void parse_RI_packet (void)
{
}

// CI Configure I2C
void parse_CI_packet (void)
{
}

void StartWrite(void)
{
//	EECON2 = 0x55;
//	EECON2 = 0xAA;
//	EECON1bits.WR = 1;
}

unsigned char ReadEE(unsigned char Address)
{
//	EECON1 = 0x00;
//	EEADR = Address;
//	EECON1bits.RD = 1;
//	return (EEDATA);
}

void WriteEE(unsigned char Address, unsigned char Data)
{
//	EEADR = Address;
//	EEDATA = Data;
//	EECON1 = 0b00000100;    //Setup writes: EEPGD=0,WREN=1
//	StartWrite();
//	while( EECON1bits.WR);       //Wait till WR bit is clear, hopefully not long enough to kill USB
}

// PM command (PWM output)
// Sets a PWM value for any of the 5 PWM channels
// Usage: PM,<Channel>,<DutyCycle><CR>
// <Channel> is required and is 1 through 5
// <DutyCycle> is required and is 0 through 65535
// PWM frequency is 1220Hz (80MHz/0x10000)

void parse_PM_packet (void)
{
	unsigned char Channel;
	unsigned int DutyCycle;
	static BOOL TimerRunning = FALSE;

	UINT32 Temp1, Temp2, Temp3, Temp4;

	extract_number (kUCHAR, (void*)&Channel, kREQUIRED);
	extract_number (kUINT, (void*)&DutyCycle, kREQUIRED);

	Temp1 = DutyCycle;

	// Bail if we got a conversion error
	if (error_byte)
	{
		return;
	}

	// Limit check the parameters
	if (Channel > 5 || Channel == 0)
	{
		bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
		return;
	}

	// Start up our timer if it's not already running
	if (TimerRunning == FALSE)
	{
	    OpenTimer2(T2_ON, 0xFFFE);
	    TimerRunning = TRUE;
	}

	// Handle the special case of the PWM = 0%
	if (DutyCycle == 0)
	{
		PWMChannelOn[Channel - 1] = FALSE;

		if (Channel == 1) {
			CloseOC1();
			PORTDbits.RD0 = 0;
		};
		if (Channel == 2) {
			CloseOC2();
			PORTDbits.RD1 = 0;
		};
		if (Channel == 3) {
			CloseOC3();
			PORTDbits.RD2 = 0;
		};
		if (Channel == 4) {
			CloseOC4();
			PORTDbits.RD3 = 0;
		};
		if (Channel == 5) {
			CloseOC5();
			PORTDbits.RD4 = 0;
		};
		return;
	}

#if 0
	// Handle the special case of the PWM = 100%
	if (DutyCycle == 65535)
	{
		PWMChannelOn[Channel - 1] = TRUE;

		if (Channel == 1) {
			CloseOC1();
			PORTDbits.RD0 = 1;
		};
		if (Channel == 2) {
			CloseOC2();
			PORTDbits.RD1 = 1;
		};
		if (Channel == 3) {
			CloseOC3();
			PORTDbits.RD2 = 1;
		};
		if (Channel == 4) {
			CloseOC4();
			PORTDbits.RD3 = 1;
		};
		if (Channel == 5) {
			CloseOC5();
			PORTDbits.RD4 = 1;
		};
		return;
	}

#endif

	// If this channel is already on, then update the OCxRS register
    if (PWMChannelOn[Channel-1])
    {
		if (Channel == 1) {
			SetDCOC1PWM(DutyCycle);
		};
		if (Channel == 2) {
			SetDCOC2PWM(DutyCycle);
		};
		if (Channel == 3) {
			SetDCOC3PWM(DutyCycle);
		};
		if (Channel == 4) {
			SetDCOC4PWM(DutyCycle);
		};
		if (Channel == 5) {
			SetDCOC5PWM(DutyCycle);
		};
		return;
	}
	else
	{
		// Otherwise, start up the OC channel

	    /* Enable OC | 16 bit Mode  | Timer2 is selected | Continuous O/P   | OC Pin High , S Compare value, Compare value*/
	    if (Channel == 1)
	    {
		    OpenOC1( OC_ON | OC_TIMER_MODE16 | OC_TIMER2_SRC | OC_PWM_FAULT_PIN_DISABLE , DutyCycle, 0 );
		}
	    if (Channel == 2)
	    {
		    OpenOC2( OC_ON | OC_TIMER_MODE16 | OC_TIMER2_SRC | OC_PWM_FAULT_PIN_DISABLE , DutyCycle, 0 );
		}
	    if (Channel == 3)
	    {
		    OpenOC3( OC_ON | OC_TIMER_MODE16 | OC_TIMER2_SRC | OC_PWM_FAULT_PIN_DISABLE , DutyCycle, 0 );
		}
	    if (Channel == 4)
	    {
		    OpenOC4( OC_ON | OC_TIMER_MODE16 | OC_TIMER2_SRC | OC_PWM_FAULT_PIN_DISABLE , DutyCycle, 0 );
		}
	    if (Channel == 5)
	    {
		    OpenOC5( OC_ON | OC_TIMER_MODE16 | OC_TIMER2_SRC | OC_PWM_FAULT_PIN_DISABLE , DutyCycle, 0 );
		}

		PWMChannelOn[Channel - 1] = TRUE;
	}
}


// SP command (Software PWM output)
// Sets a PWM value for any of 18 PWM channels
// Usage: SP,<Channel1>,<DutyCycle1>,<Channe2>,<DutyCycle2>,...,<Channel18>,<DutyCycle18><CR>
// <Channel> is required
// <DutyCycle1> is required and is 0 through 4096
// But <Channel2> through <Channel18> are optional. Note that you must have pairs of values - you can't
// skip a channel number and have a lot of PWM values, for example.
//
// Example:
// SP,4,2899,7,1955,2,0
// Would set channel 4 to 2899, channel 7 to 1955 and channel 5 to 0.
// Setting a PWM value of a channel to 0 turns the PWM of that channel off, and allows normal digitial I/O on that
// pin.
// PWM value of 4095 will be %100 on time.
// The repititon rate (PWM rate) is
void parse_SP_packet (void)
{
	unsigned char Channel;
	unsigned int DutyCycle;
	unsigned int i;
	ExtractReturnType Result = kEXTRACT_OK;
	BOOL Flag;

	extract_number (kUCHAR, (void*)&Channel, kREQUIRED);

	// We want to allow multiple pairs of Channel,PWM_Value in a single command
	while(Result == kEXTRACT_OK)
	{
		extract_number (kUINT, (void*)&DutyCycle, kREQUIRED);

		// Bail if we got a conversion error
		if (error_byte)
		{
			return;
		}

		// Limit check the parameters
		if (Channel > SP_MAX_CHANNEL_NUMBER)
		{
			bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
			return;
		}

		// Set the channel's PWM value
		SP_PWM_Values_Buffer[Channel] = DutyCycle;

        // If the value is zero, shut down the channel
        if (DutyCycle == 0)
        {
            SP_PWM_Enables[Channel] = FALSE;
        }
        else
        {
            // Make sure to set this channel as being enabled
            SP_PWM_Enables[Channel] = TRUE;
        }

		Result = extract_number (kUCHAR, (void*)&Channel, kOPTIONAL);
	}

	// Any time we set a PWM value, make sure to turn on Sofware PWM
	SP_Command_Running = TRUE;
}

// PC command (PWM Configure)
// Sets options for the SP command
// Usage: PC,<Command>,<Value1>,<Value2>,<Value3><CR>
// <Command> can be one of the following, and is required (<Value> may or may not be required depending on the command)
// Command = 0, Value1 = Roll Over Value, 32 bit unsigned int, units = ISR ticks
// Command = 1, Value1 = ISR Rate, 32 bit unsigned int
// Command = 2, Value1 = PWM Channel, Value2 = Port (use 'A' through 'G'), Value3 = pin (0 through 15)
// Command = 3, Value1 = anything, print out current SP command paramters
// Command = 4, Value1 = MaxUsedChannel, 8-bit unsigned value from 0 to 64
// Command = 5, Value1 = SoftwarePWMState (0 = Off/1 = On) - use to turn off software PWM
void parse_PC_packet (void)
{
	unsigned char Command = 0;
	unsigned int Value1 = 0;
	unsigned char Value2 = 0;
	unsigned char Value3 = 0;
	unsigned int i;

	extract_number (kUCHAR, (void*)&Command, kREQUIRED);
	extract_number (kUINT, (void*)&Value1, kREQUIRED);
	extract_number (kUCASE_ASCII_CHAR, (void*)&Value2, kOPTIONAL);
	extract_number (kUCHAR, (void*)&Value3, kOPTIONAL);

	// Bail if we got a conversion error
	if (error_byte)
	{
		return;
	}

	// Limit check the parameters
	if (Command > 5)
	{
		bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
		return;
	}

	// Exceute the command the user has specified
	switch (Command)
	{
	case 0:
		SP_Rollover_Value = Value1;
		break;

	case 1:
		// HMM, not quite sure how to do this yet
		break;

	case 2:
		// Limit check the parameters
		if (Value1 > SP_MAX_CHANNEL_NUMBER)
		{
			bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
			return;
		}

		if ((Value2 > 'G' || Value2 < 'A'))
		{
			bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
			return;
		}

		if (Value3 > 15)
		{
			bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
			return;
		}

        // Now compute the proper value to put in the pin map
        SP_Pin_Map[Value1] = ((Value2 - 'A') << 8) + Value3;
        SP_PWM_Enables[Value1] = TRUE;
		break;

	case 3:
		// Print out all SP variables
        printf("Software PWM values:\n\r");
		printf("Roll Over Value: %5d\n\r", SP_Rollover_Value);
        printf("PWM Frequency: %5d\n\r", CORE_TICK_RATE);
        printf("Max Used Channel: %2d\n\r", SP_Max_Channel_Used);
        printf("Software PWM Enable %d\n\r", SP_Command_Running);
        printf("Channel, Duty Cycle, Port, Pin, Enabled\n\r");
        for (i=0; i < SP_Max_Channel_Used; i++)
		{
			printf("%02d: %05d %c %02d %1d\n\r", i, SP_PWM_Values[i], ((SP_Pin_Map[i] >> 8) & 0xF) + 'A', (SP_Pin_Map[i] & 0xF), SP_PWM_Enables[i]);
		}
		break;

    case 4:
        if (Value1 > SP_MAX_CHANNEL_NUMBER)
        {
            bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
        }
        SP_Max_Channel_Used = Value1;
        break;

	case 5:
		// Turn on/off software PWM feature
		if (Value1)
		{
			SP_Command_Running = TRUE;
		}
		else
		{
			SP_Command_Running = FALSE;
		}
		break;

	default:

		break;
	}
}


// Look at the string pointed to by ptr
// There should be a comma where ptr points to upon entry.
// If not, throw a comma error.
// If so, then look for up to three bytes after the
// comma for numbers, and put them all into one
// byte (0-255). If the number is greater than 255, then
// thow a range error.
// Advance the pointer to the byte after the last number
// and return.
ExtractReturnType extract_number(
	ExtractType Type,
	void * ReturnValue,
	unsigned char Required
)
{
	unsigned long ULAccumulator;
	signed long Accumulator;
	BOOL Negative = FALSE;
	UINT8 FloatBuf[20];
	int i;

	// Check to see if we're already at the end
	if (kCR == g_RX_buf[g_RX_buf_out])
	{
		if (0 == Required)
		{
			bitset (error_byte, kERROR_BYTE_MISSING_PARAMETER);
		}
		return (kEXTRACT_MISSING_PARAMETER);
	}

	// Check for comma where ptr points
	if (g_RX_buf[g_RX_buf_out] != ',')
	{
		if (0 == Required)
		{
			printf ((const char *)"!5 Err: Need comma next, found: '%c'\r\n", g_RX_buf[g_RX_buf_out]);
			bitset (error_byte, kERROR_BYTE_PRINTED_ERROR);
		}
		return (kEXTRACT_COMMA_MISSING);
	}

	// Move to the next character
	advance_RX_buf_out ();

	// Check for end of command
	if (kCR == g_RX_buf[g_RX_buf_out])
	{
		if (0 == Required)
		{
			bitset (error_byte, kERROR_BYTE_MISSING_PARAMETER);
		}
		return (kEXTRACT_MISSING_PARAMETER);
	}

	if (kFLOAT == Type)
	{
		i = 0;
		// We're going to cheat, and use sscanf here
		// First copy into a a buffer that sscanf can use
		while (
			kCR != g_RX_buf[g_RX_buf_out]
			&&
			',' != g_RX_buf[g_RX_buf_out]
			&&
			i < 20
		)
		{
			FloatBuf[i] = g_RX_buf[g_RX_buf_out];
			i++;
			advance_RX_buf_out();
		}

		if (sscanf(FloatBuf, "%f", ReturnValue))
		{
			return (kEXTRACT_OK);
		}
		else
		{
			return (kEXTRACT_PARAMETER_OUTSIDE_LIMIT);
		}
	}

	// Now check for a sign character if we're not looking for ASCII chars
	if (
		('-' == g_RX_buf[g_RX_buf_out])
		&&
		(
			(kASCII_CHAR != Type)
			&&
			(kUCASE_ASCII_CHAR != Type)
		)
	)
	{
		// It's an error if we see a negative sign on an unsigned value
		if (
			(kUCHAR == Type)
			||
			(kUINT == Type)
			||
			(kULONG == Type)
		)
		{
			bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
			return (kEXTRACT_PARAMETER_OUTSIDE_LIMIT);
		}
		else
		{
			Negative = TRUE;
			// Move to the next character
			advance_RX_buf_out ();
		}
	}

	// If we need to get a digit, go do that
	if (
		(kASCII_CHAR != Type)
		&&
		(kUCASE_ASCII_CHAR != Type)
	)
	{
		extract_digit(&ULAccumulator, 10);
	}
	else
	{
		// Otherwise just copy the byte
		ULAccumulator = g_RX_buf[g_RX_buf_out];

		// Force uppercase if that's what type we have
		if (kUCASE_ASCII_CHAR == Type)
		{
			ULAccumulator = toupper (ULAccumulator);
		}

		// Move to the next character
		advance_RX_buf_out ();
	}

	// Range check absolute values
	if (Negative)
	{
		if (
			(
				kCHAR == Type
				&&
				(ULAccumulator > (unsigned long)128)
			)
			||
			(
				kINT == Type
				&&
				(ULAccumulator > (unsigned long)32768)
			)
			||
			(
				kLONG == Type
				&&
				(ULAccumulator > (unsigned long)0x80000000L)
			)
		)
		{
			bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
			return (kEXTRACT_PARAMETER_OUTSIDE_LIMIT);
		}

		Accumulator = ULAccumulator;
		// Then apply the negative if that's the right thing to do
		if (Negative)
		{
			Accumulator = -Accumulator;
		}
	}
	else
	{
		if (
			(
				kCHAR == Type
				&&
				(ULAccumulator > (unsigned long)127)
			)
			||
			(
				kUCHAR == Type
				&&
				(ULAccumulator > (unsigned long)255)
			)
			||
			(
				kINT == Type
				&&
				(ULAccumulator > (unsigned long)32767)
			)
			||
			(
				kUINT == Type
				&&
				(ULAccumulator > (unsigned long)65535)
			)
			||
			(
				kLONG == Type
				&&
				(ULAccumulator > (unsigned long)0x7FFFFFFFL)
			)
		)
		{
			bitset (error_byte, kERROR_BYTE_PARAMETER_OUTSIDE_LIMIT);
			return (kEXTRACT_PARAMETER_OUTSIDE_LIMIT);
		}

		if (kULONG != Type)
		{
			Accumulator = ULAccumulator;
		}
	}

	// If all went well, then copy the result
	switch (Type)
	{
		case kCHAR:
			*(signed char *)ReturnValue = (signed char)Accumulator;
			break;
		case kUCHAR:
		case kASCII_CHAR:
		case kUCASE_ASCII_CHAR:
			*(unsigned char *)ReturnValue = (unsigned char)Accumulator;
			break;
		case kINT:
			*(signed int *)ReturnValue = (signed int)Accumulator;
			break;
		case kUINT:
			*(unsigned int *)ReturnValue = (unsigned int)Accumulator;
			break;
		case kLONG:
			*(signed long *)ReturnValue = Accumulator;
			break;
		case kULONG:
			*(unsigned long *)ReturnValue = ULAccumulator;
			break;
		default:
			return (kEXTRACT_INVALID_TYPE);
	}
	return(kEXTRACT_OK);
}

// Loop 'digits' number of times, looking at the
// byte in input_buffer index *ptr, and if it is
// a digit, adding it to acc. Take care of
// powers of ten as well. If you hit a non-numerical
// char, then return FALSE, otherwise return TRUE.
// Store result as you go in *acc.
signed char extract_digit(signed long * acc,	unsigned char digits)
{
	unsigned char val;
	unsigned char digit_cnt;

	*acc = 0;

	for (digit_cnt = 0; digit_cnt < digits; digit_cnt++)
	{
		val = g_RX_buf[g_RX_buf_out];
		if ((val >= 48) && (val <= 57))
		{
			*acc = (*acc * 10) + (val - 48);
			// Move to the next character
			advance_RX_buf_out ();
		}
		else
		{
			return (FALSE);
		}
	}
	return (TRUE);
}

// For debugging, this command will spit out a bunch of values.
void print_status(void)
{
//	printf(
//		(const char *)"Status=%i\r\n"
//		,ISR_D_FIFO_length
//	);
}

