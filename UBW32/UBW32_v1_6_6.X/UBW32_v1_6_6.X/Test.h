#ifndef TEST_H
#define TEST_H

void parse_T1_packet(void);
void parse_T2_packet(void);

// Take a port letter, a pin number, and make that I/O pin
// an output, setting it to be State.
void SetPortPinOutput(UINT8 Index, BOOL State);

// Reads the value of an I/O pin
UINT8 GetPinVal(UINT8 Index);

// Set Port/Pin to be an input
void SetPortPinInput(UINT8 Index);

#endif
