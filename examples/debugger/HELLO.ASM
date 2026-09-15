*
* HELLO.ASM -- a symbolic-debugger demo for swtpcsim.
*
* Prints "HELLO, WORLD" to the console through the SWTPC MP-S serial
* board (a 6850 ACIA), then WAIs -- which halts the processor and hands
* the machine back to the monitor. Assembled at $0100. Small on purpose:
* every label, every EQU and every kind of operand is here to be looked
* at under the debugger.
*
ACIAS   EQU     $8004           MP-S 6850 status/control register
ACIAD   EQU     $8005           MP-S 6850 data register
TDRE    EQU     $02             status bit: transmit data register empty
CR      EQU     $0D
LF      EQU     $0A
*
        ORG     $0100
*
START   LDS     #STACK          set up a stack
        LDX     #MSG            X -> the string
LOOP    LDAA    0,X             fetch the next character
        BEQ     DONE            zero terminator? -- finished
        BSR     PUTC            no -- send it
        INX                     step to the next character
        BRA     LOOP
DONE    WAI                     halt; the debugger takes over
*
* PUTC -- send the character in A, waiting for the transmitter first.
*
PUTC    PSHA
PWAIT   LDAB    ACIAS           read the ACIA status
        ANDB    #TDRE           transmitter ready?
        BEQ     PWAIT           no -- keep polling
        PULA
        STAA    ACIAD           yes -- send the byte
        RTS
*
MSG     FCC     /HELLO, WORLD/
        FCB     CR,LF,0
        RMB     16              room for the stack to grow down into
STACK   EQU     *               top of stack (grows downward from here)
*
        END     START
