#ifndef HARDWARE_PROFILE_FUBARDUINO_H
#define HARDWARE_PROFILE_FUBARDUINO_H

    /*******************************************************************/
    /******** USB stack hardware selection options *********************/
    /*******************************************************************/
    //This section is the set of definitions required by the MCHPFSUSB
    //  framework.  These definitions tell the firmware what mode it is
    //  running in, and where it can find the results to some information
    //  that the stack needs.
    //These definitions are required by every application developed with
    //  this revision of the MCHPFSUSB framework.  Please review each
    //  option carefully and determine which options are desired/required
    //  for your application.

    //#define USE_SELF_POWER_SENSE_IO
    #define tris_self_power     TRISAbits.TRISA2    // Input
    #define self_power          1

    //#define USE_USB_BUS_SENSE_IO
    #define tris_usb_bus_sense  TRISBbits.TRISB5    // Input
    #define USB_BUS_SENSE       1

    /*******************************************************************/
    /*******************************************************************/
    /*******************************************************************/
    /******** Application specific definitions *************************/
    /*******************************************************************/
    /*******************************************************************/
    /*******************************************************************/

    /** Board definition ***********************************************/
    //These defintions will tell the main() function which board is
    //  currently selected.  This will allow the application to add
    //  the correct configuration bits as wells use the correct
    //  initialization functions for the board.  These defitions are only
    //  required in the stack provided demos.  They are not required in
    //  final application design.
    #define DEMO_BOARD FUBARDUINO

    /** LED ************************************************************/
    #define mInitAllLEDs()      LATE |= 0x0020; TRISE &= 0xFFDF;

    #define mLED_1              LATEbits.LATE5

    #define mGetLED_1()         mLED_1

    #define mLED_1_On()         mLED_1 = 0;
    #define mLED_2_On()

    #define mLED_1_Off()        mLED_1 = 1;
    #define mLED_2_Off()

    #define mLED_1_Toggle()     mLED_1 = !mLED_1;
    #define mLED_2_Toggle()     

    /** SWITCH *********************************************************/
    #define mInitSwitch2()  TRISEbits.TRISE7=1;
    #define mInitAllSwitches() mInitSwitch2();
    #define sw2             PORTEbits.RE7
#endif  //HARDWARE_PROFILE_UBW32_H

