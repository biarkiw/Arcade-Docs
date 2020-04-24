/*
 * Stepper motor module description
 *
 * This module contains the code to run up to 8 stepper motors simultaneously
 * with different types of motion control. All 8 channels are treated
 * individually. Each channel can
 * have a maximum and minimum step rate, and an acceleration value, so that
 * normal trapezoidal motion profiles can be generated. Also, multiple
 * channels can be started together.
 *
 * In addition, optional limit switch input bits may be assigned to a
 * channel and the channel can be set to E-stop, or decel on any of them
 * going low.
 *
 * You can set a mode where the motor status are continually sent back to the
 * PC, or you can request a motor status whenever you want to know when
 * a move is complete.
 *
 * Each channel has a current position counter, which is readable and writable,
 * and is a 32 bit signed value.
 */

#include "plib.h"
#include <stdint.h>
#include "HardwareProfile.h"
#include "D32.h"
#include "Stepper.h"


/*
 * Module Defines
 */

// Defines the maximum number of stepper channels
#define MAX_STEPPERS    8

// These are read by the ISR, and written
// by the mainline code
typedef struct ChanStruct ChanType;
// This structure is the main method for the library to track information for
//  each pin.
struct ChanStruct
{
    volatile uint32_t   *StepSetPort;           // Pointer to port register (SET) for this pin
    volatile uint32_t   *StepClearPort;         // Pointer to port register (CLEAR) for this pin
    uint32_t            StepPort;               // Port number for this pin
    uint32_t            StepBit;                    // Bit of Port that this pin is on
    volatile uint32_t   *DirSetPort;            // Pointer to port register (SET) for this pin
    volatile uint32_t   *DirClearPort;          // Pointer to port register (CLEAR) for this pin
    uint32_t            DirPort;                // Port number for this pin
    uint32_t            DirBit;                 // Bit of Port that this pin is on
    uint32_t            StepRate;               // 40MHz ticks between this channel's step pulses
    uint32_t            TimeRemaining;          // 40MHz ticks until this channe's next step pulse
    uint32_t            MaxStepRate;            // 40MHz ticks between this channel's step pulses (max)
    uint32_t            MinStepRate;            // 40MHz ticks between this chnanel's step pulses (min)
    uint32_t            AccelRate;              // Acceleration rate (TBD)
};

static ChanType Chan[MAX_STEPPERS]; // Main structure for SoftPWM. Second index is Arduino Pin number, first is buffer (inactive/active)
//static ChanType * FirstChanP[2] = {NULL, NULL}; // Stores pointer to first channel
//static bool InactiveBufferReady;                // Set true when inactive buffer has new data for ISR and should become active
//static uint32_t FrameTime = SOFTPWMSERVO_DEFAULT_FRAME_TIME; // In 40MHz ticks, time of PWM 'cycle' or frame - when do all edges go high
//static uint32_t ServoFrames = SOFTPWMSERVO_DEFAULT_SERVO_FRAMES; // Number of FrameTimes between rising edges of servo pulses

// These are only touched inside the ISR
static BOOL RisingEdge;                         // True if we are about to set all active channels high
static uint32_t CurrentTime;                    // Number of 40MHz ticks into the frame we are right now
static uint32_t MS1Time;                        // Time into 1ms 'frame'
static uint32_t MS1Remainder;                   // Number of 40MHz ticks left before start of next 1ms 'frame'
static uint32_t ActiveBuffer;                   // Current buffer index (0 or 1), used to index Chan and FirstChanP, for double buffering
static ChanType * ISRFirstChanP = NULL;         // Stores ISR copy of pointer to first channel
static uint32_t ServoFrameCounter;              // When zero, set all servo channels high, then reset to ServoFrames and count frames down


/*
 * Module Variables (statics)
 */

/*
 * Public functions
 */

// sC command (for configuring stepper system
// Usage: "sC,<Channel>,<Function>,<Value1>,<Value2><CR>"
//
// <Channel> is a number from 1 to 8, and specifies which Stepper Channel the
//   rest of the parameters apply to
//
// <Function> is a number from the table below:
//   0 : Turns off <Channel>. <Value1> and <Value2> are optional and unused
//   1 : Sets <Value1><Value2> as the direction pin for <Channel>, <Value1> is
//         the port ('A' through 'G') and <Value2> is the pin (0 through 15)
//   2 : Sets <Value1><Value2> as the step pin for <Channel>, <Value1> is
//         the port ('A' through 'G') and <Value2> is the pin (0 through 15)
//   3 : Sets the maximum step rate for <Channel> to <Value1>. <Value2> is unused
//   4 : Sets the minimum step rate for <Channel> to <Value1>. <Value2> is unused
//   5 : Sets the acceleration rate for <Channel> to <Value1>. <Value2> is unused
//         Set accel rate to zero to disable accel/decel for <Channel>
//   6 : Sets the E-stop bit for <Channel>. <Value1> is
//         the port ('A' through 'G') and <Value2> is the pin (0 through 15)
//   7 : Sets Limit1 switch bit for <Channel>, <Value1> is
//         the port ('A' through 'G') and <Value2> is the pin (0 through 15)
//   8 : Sets Limit2 switch bit for <Channel>, <Value1> is
//         the port ('A' through 'G') and <Value2> is the pin (0 through 15)
//   9 : Sets <Channel>'s position counter to <Value1>. <Value2> is unused.
//
void parse_sC_packet(void)
{

}

// 'sR' command (Stepper run motor motion command)
// Usage: "sR,<Channel>,<Steps>,<Mode>,<StepRate>,<Accel><CR>"
//
// <Channel> is one of the 8 stepper motor channels, as defined using "sC" command
//
// <Steps> is a value from -2^31 to 2^31, and is the absolute position to
//   move <Channel> to.
//
// <Mode> is an integer value composed of a bit field. For each option
//   below, set the bit to a 1 or 0.
//   Bit 0:
//     0 = Relative (<Steps> is relative to current position)
//     1 = Absolute (<steps> is a new absolute position)
//   Bit 1:
//     0 = Do not report when move is complete
//     1 = Send sS status packet when move is complete
//   Bit 2:
//     0 = Do not stop if Limit1 is tripped during move
//     1 = Decel and stop if Limit1 is tripped during move
//   Bit 3:
//     0 = Do not stop if Limit2 is tripped during move
//     1 = Decel and stop if Limit2 is tripped during move
//   Bit 4:
//     0 = Stop if EStop is tripped during move
//     1 = Do not stop if EStop is tripped during move
//
// <StepRate> is an optional value. If it is not included (or is zero) then
//   the stepper channel will use its default max step rate values for the move.
//   If it is included, then it will set a new max step rate for just this move.
//
// <Accel> is an option value. If it is not included (or is zero) then the
//   channel will use its default accel value for the move. If it is included,
//   then it will be used as a new accel value, just for this move.
//
// Advanced use:
// You can chain multiple 5-tuples (<Channel>,<Steps>,<Mode>,<StepRate>,<Accel>)
// together (with commas) to form a single command with up to 8 5-tuples (one
// for each channel) which will cause them all to start at the same time.
void parse_sR_packet(void)
{

}

// 'sS' Command (Stepper Status send)
// Usage: "sS,<Time><CR>"
//
// <Time> Is the time, in ms, between sending out sS status packets.
// If <Time> is zero, then sending out sS status packets is disabled.
// Send an sS command to turn on the regular reporting of status packets.
// Once turned on, the stepper module will send a status packet every
// <Time> ms. Each status packet will be of the form:
// "sS,<Channel1Pos>,<Channel1Status>,<Channel2Pos>,<Channel2Status>...<CR>"
// Where all 8 channels are always reported.
// <ChannelXPos> is the 32 bit signed value of the current position
// <ChannelXStatus> is a status value, from the following table:
//   0 : Channel is disabled and off
//   1 : Channel is idle (on but not moving, and no limits are tripped)
//   2 : Channel is moving
//   3 : Channel is stopped because of E-stop or limit switch closure
//
// If you just send "sS<CR>" without the <Time> paramter, then an sS status
//   packet is sent back immediately.
void parse_sS_packet(void)
{


}

// 'sE' Command (Stepper Estop)
// Usaage: "sE,<Channel>,<StopMode><CR>"
// <Channel> is the stepper motor channel to be operated on
// <StopMode> is one of the following values:
//   0  : Do nothing to this channel
//   1  : Stop, using decel
//   2  : E-stop (i.e. immediately cease stepping)
// You can chain as main pairs of <Channel>,<StopMode> together as you want
// to effect how ever many channels you want to in a single command.
void parse_sE_packet(void)
{


}

/*
 * Private Functions
 */


// This function initalizes all of the data structures in the stepper
// module, and attaches the StepperISR() to a timer, running at a default
// rate. It is called from the setup functions.
void StepperInit(void)
{

    // TEMP set up a bit
//    Chan[0].StepSetPort = PORTDSET;
//    Chan[0].StepClearPort = PORTDCLR;
//    Chan[0].StepBit = 0x0002;       // bit 1
    

    // Clear out all data structures and initalize variables

    // Hook us an timer interrupt
    OpenTimer3(T3_ON | T3_SOURCE_INT | T3_PS_1_1, 0x8000);
    ConfigIntTimer3(T3_INT_ON | T3_INT_PRIOR_3);


    mPORTDSetPinsDigitalOut(BIT_0);

}


// Main ISR function - does all of the timing related work
// Return the number of 40MHz ticks before we want to be
// called again.
uint32_t StepperISR(void)
{
    mPORTDToggleBits(BIT_0);


    // Set the bit to take a step
    *(Chan[0].StepSetPort) = Chan[0].StepBit;

    // Clear the bit after the step is done
    *(Chan[0].StepClearPort) = Chan[0].StepBit;

    return 2000;
}

void __ISR(_TIMER_3_VECTOR, ipl3) Timer3Hander(void)
{
    mT3ClearIntFlag();

    WritePeriod3(ReadTimer3() + StepperISR());
}
