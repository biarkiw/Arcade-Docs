
#ifndef D32_H
#define D32_H

#define kMAX_PORTS			(7)

// Enum for extract_num() function parameter
typedef enum {
	 kCHAR
	,kUCHAR
	,kINT
	,kUINT
	,kASCII_CHAR
	,kUCASE_ASCII_CHAR
	,kLONG
	,kULONG
	,kFLOAT
} ExtractType;

typedef enum {
	 kEXTRACT_OK = 0
	,kEXTRACT_PARAMETER_OUTSIDE_LIMIT
	,kEXTRACT_COMMA_MISSING
	,kEXTRACT_MISSING_PARAMETER
	,kEXTRACT_INVALID_TYPE
} ExtractReturnType;

#define kREQUIRED	FALSE
#define kOPTIONAL	TRUE


extern BYTE error_byte;
// Used in T1 command to time the LEDs. In ms.
extern volatile unsigned int T1_timer;

extern volatile unsigned int * const LATPtr[kMAX_PORTS];
extern volatile unsigned int * const PORTPtr[kMAX_PORTS];
extern volatile unsigned int * const TRISPtr[kMAX_PORTS];
extern volatile unsigned int * const ODCPtr[kMAX_PORTS];
extern void UserInit(void);
extern void ProcessIO(void);

#endif

