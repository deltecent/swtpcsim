*--------------------------------------------------------------
*
*  EDSKDRV.ASM
*
*    This is an enhanced disk driver for FLEX 2 or 3 running
*    on the SWTPC 6800. The driver supports the original DC-x
*    disk controllers from SWTPC as well as the FD-2 from
*    Peripheral Technologies. The driver provides the following
*    improvments:
*
*	- 20% to 50% faster disk operations
*	- Support for double sided drives
*	- Support for fast stepping drives
*	- Honors head load timing when switching between drives
*	- Honors motor start-up time and overlays this timeout
*	  with the initial seek operation to improve efficiency
*	- Honors side select timing
*	- Honors trim erase timing
*	- Single driver works with both FLEX 2.0 and 3.0
*
*     The speed improvement was achieved by turning on the E
*     bit in read/write commands only when necessary. This,
*     in turn, allows FLEX to work within its designed sector
*     interleave instead of slipping a full revolution with
*     every sector I/O.
*
*     For FLEX 2 vs FLEX 3, see the END directive note at the
*     end of this file.
*
*     The step rate for seeking (as defined for the 1771) is
*     stored at $BEB3. The default value is $03 which gives a
*     40ms step interval (30ms with 2797). A value of $00 can
*     be placed here for a 12ms/6ms step interval. The STEPFAST
*     utility does this and can be placed in the STARTUP.TXT
*     file if desired.
*
*    Ver     Date           Notes
*    ---   --------     ---------------------------------------
*    1.0   12/14/22	Original, Mike Douglas
*
*    1.1   12/28/20	Add 1ms post-write delay to allow trim
*			erase to complete. Optimize motor start
*			timing for slow versus fast stepping
*			drives.
*
*--------------------------------------------------------------

* Disk controller equates

DRQ	EQU	$02		DRQ BIT MASK
BUSY	EQU	$01		BUSY MASK
HEADMSK	EQU	$20		HEAD LOADED STATUS BIT
NOTRDY	EQU	$80		1=NOT READY (MOTOR OFF)
SIDEMSK	EQU	$40		1=TOP, 0=BOTTOM
RDMSK	EQU	$1C		READ ERROR MASK
VERMSK	EQU	$18		VERIFY ERROR MASK
WTMSK	EQU	$5C		WRITE ERROR MASK

RDCMND	EQU	$88		READ COMMAND (E BIT OFF)
WTCMND	EQU	$A8		WRITE COMMAND (E BIT OFF)
RSCMND	EQU	$0B		RESTORE COMMAND
SKCMND	EQU	$18		SEEK COMMAND (LOAD HEAD)
HDUNLD	EQU	$10		SEEK TO UNLOAD HEAD
RSTCMD	EQU	$D0		FORCE INTERRUPT (RESET 1771)

DRVREG	EQU	$8014		FDC DRIVE SELECT REG
COMREG	EQU	$8018		FDC COMMAND REG
TRKREG	EQU	$8019		FDC TRACK REG
SECREG	EQU	$801A		FDC SECTOR REG
DATREG	EQU	$801B		FDC DATA REG

* FLEX equates

PRCNT	EQU	$AC34		FLAG NON ZERO IF SPOOLING

* Driver jump table

	ORG	$BE80
	JMP	READ		READ SECTOR
	JMP	WRITE		WRITE SECTOR
	JMP	VERIFY		VERIFY
	JMP	RESTOR		RESTORE
	JMP	DRVSEL		DRIVE SELECT
	JMP	CHECK		DRIVE READY
	JMP	CHECK		QUICK DRIVE READY
	JMP	EXIT0		COLD START INIT
	JMP	EXIT0		WARM START INIT
	JMP	EXIT0		SEEK TO TRACK

* FLEX 2.0 Console and Timer address table. For FLEX 3.0, see
*    the FLEX3CON.ASM file instead.

	ORG	$BEA3
INVEC	FDB	$E1AC		INPUT CHARACTER ROUTINE
OUTVEC	FDB	$E1D1		OUTPUT CHARACTER ROUTINE
ACIA	FDB	$8004		BASE OF ACIA
TIMER	FDB	$8012		TIMER BOARD BASE
IRQ	FDB	$A000		IRQ VECTOR
SWI	FDB	$A012		SWI VECTOR
MON	FDB	$E0D0		MONITOR ENTRY ADDRESS
PCV	FDB	$A048		MONITOR PC LOCATION

STEPR8	FCB	3		MUST REMAIN AT $BEB3 (STEP RATE
*				BITS CAN BE PATCHED EXTERNALLY)

* Data definitions

	ORG	$BE10		SAFE START OF DRIVER SPACE
EBIT	FCB	0		CURRENT E-BIT STATE
CURSIDE	FCB	0		0=BOTTOM, $40=TOP
MOTOFF	FCB	1		0=MOTOR ON, ELSE MOTOR WAS OFF
XTEMP	FDB	0
CURDRV	FCB	4		CURRENT DRIVE (4 IS INVALID)
LSTTRK	FCB	0,0,0,0,0	TRACK FOR EACH DRIVE PLUS GUARD
SEEKLEN	FCB	0		LENGTH IN TRACKS OF THE SEEK
STPTIME	FCB	0		STEP TIME IN 6MS TICS

*--------------------------------------------------------------
* DRVSEL - Select the drive in the FCB pointed to by X.
*    This call starts the motor and sets the MOTOFF flag to
*    record whether the motor was already running or not. If 
*    a drive change occurs, the head is unloaded to ensure the
*    head load timer restarts with the new drive selection.
*--------------------------------------------------------------
DRVSEL	BSR	CHECK		CHECK FOR VALID DRIVE
	BCS	SELRTS		ERROR, EXIT

	LDAB	COMREG		START MOTOR AND ALREADY ON?
	ANDB	#NOTRDY		NOT READY BIT TRUE IF MOTOR OFF
	STAB	MOTOFF		MOTOFF <> 0 IF MOTOR WAS OFF

	CMPA	CURDRV		DIFFERENT DRIVE SELECTED?
	BEQ	SAMEDRV		NO
	
	LDAA	TRKREG		DO A NULL SEEK TO UNLOAD HEAD
	STAA	DATREG
	LDAA	#HDUNLD		SEEK WITH HEAD UNLOAD
	JSR	SEEKCMD		ISSUE THE SEEK COMMAND

	LDAA	3,X		A=NEW DRIVE
	LDX	#LSTTRK		X->SAVED TRACK TABLE
	LDAB	CURDRV		B=CURRENT DRIVE (BEFORE SWITCH)
	STAB	SAVETRK+1	SET AS INDEX INTO TABLE
	LDAB	TRKREG		SAVE CURRENT TRACK IN TABLE
SAVETRK	STAB	0,X

	STAA	CURDRV		SAVE NEW CURRENT DRIVE
	STAA	LOADTRK+1	USE AS INDEX INTO TRACK TABLE
LOADTRK	LDAB	0,X		GET TRACK FOR NEW DRIVE
	STAB	TRKREG		SET IN THE 1771

SAMEDRV	ORAA	CURSIDE		INCLUDE CURRENT BOTTOM/TOP BIT
	STAA	DRVREG		SELECT THE DRIVE
	CLC			NO ERROR
SELRTS	RTS

*--------------------------------------------------------------
* CHECK - Check if the drive in the FCB pointed is ready
*   Returns:
*     Zero true, carry clear if drive ready
*     Zero false, carry set if drive not ready
*--------------------------------------------------------------
CHECK	LDAA	3,X		GET DRIVE NUMBER FROM FCB
	CMPA	#3		ALLOW 0-3
	BHI	CHKBAD		INVALID DRIVE NUMBER
	CLRB			ELSE, CARRY CLEAR, ZERO TRUE
	RTS
	
CHKBAD	LDAB	#$80		SHOW NOT READY
	SEC
	RTS

**************************************************************
* NOTE: JUMP TABLE AND FLEX 2.0 CONSOLE/TIMER PARAMS ARE
*    HERE IN MEMORY
**************************************************************

 	ORG	STEPR8+1	RESUME PAST JUMP TABLE
*--------------------------------------------------------------
* READ - Read sector with track in A, sector in B, X->buffer
*--------------------------------------------------------------
READ	JSR	SEEK		SEEK TRACK, SET SECTOR
	LDAA	#RDCMND
	BSR	IOCMD		ISSUE THE READ COMMAND

READ2	LDAA	COMREG		GET STATUS
	BITA	#DRQ		DATA PRESENT?
	BNE	READ3		YES
	BITA	#BUSY		LOOP WHILE STILL BUSY
	BNE	READ2

	TAB			ERROR IF NOT BUSY
	BRA	READ4		REPORT ERROR

READ3	LDAA	DATREG		STORE BYTE JUST READ
	STAA	0,X
	INX
	DECB			BYTE COUNTER
	BNE	READ2

	JSR	WAIT		WAIT UNTIL 1771 IS DONE

READ4	BITB	#RDMSK		RETURN ANY ERRORS
	CLI			ENABLE INTURRUPTS
	RTS

*--------------------------------------------------------------
* WRITE - Write sector with track in A, sector in B, X->buffer
*--------------------------------------------------------------
WRITE	JSR	SEEK		SEEK TO TRACK, SET SECTOR
	LDAA	#WTCMND
	BSR	IOCMD		ISSUE THE WRITE

WRITE2	LDAA	COMREG		GET 1771 STATUS
	BITA	#DRQ		READY FOR DATA?
	BNE	WRITE3		YES
	BITA	#BUSY		STILL BUSY?
	BNE	WRITE2		YES

	TAB			ERROR IF NOT
	BRA	WRITE4

WRITE3	LDAA	0,X		WRITE NEXT DATA BYTE
	STAA	DATREG
	INX
	DECB			BYTE COUNTER
	BNE	WRITE2

	JSR	WAIT		WAIT UNTIL 1771 IS FINISHED
	
WRITE4	LDAA	#1000/6		DELAY 1MS FOR TRIM ERASE
TRIMDLY	DECA
	BNE	TRIMDLY
	
	BITB	#WTMSK		RETURN ANY ERRORS
	CLI
	RTS

*--------------------------------------------------------------
* IOCMD - Issue the read or write command in A.
*    B is returned with the sector length for the transfer
*    (256 bytes). Swap out the spooler if required.
*--------------------------------------------------------------
IOCMD	TST	PRCNT		ARE WE SPOOLING?
	BEQ	IOCMD1		NO
	SWI			YES, TURN OFF SPOOLER

IOCMD1	ORAA	EBIT		INCLUDE POSSIBLE E BIT
	CLR	EBIT		ASSUME NOT NEEDED NEXT TIME
	SEI			DISABLE INTERRUPTS
	STAA	COMREG		ISSUE THE COMMAND
	JSR	DELAY		GIVE 1771 TIME FOR VALID STATUS
EXIT0	CLRB			SET SECTOR LENGTH TO 256
	RTS

*--------------------------------------------------------------
* VERIFY - Verify the sector just written. Interrupts are
*    not masked so a spooler swap is not required.
*--------------------------------------------------------------
VERIFY	LDAA	#RDCMND		READ SECTOR TO VERIFY 
	STAA	COMREG
	JSR	WAIT		WAIT FOR READ TO FINISH
	BITB	#VERMSK		TEST FOR ERRORS
	RTS

*--------------------------------------------------------------
* RESTORE - Select drive and restore to track zero.
*   Interrupts are not masked so a spooler swap is not
*   required.
*
*   Returns:
*     Zero false, carry clear if write protected
*     Zero false, carry set for invalid drive
*     Zero true, carry clear otherwise
*--------------------------------------------------------------
RESTOR	STX	XTEMP		SAVE INDEX
	JSR	DRVSEL		DO SELECT
	LDX	XTEMP
	BCS	RESTR2		SELECT ERROR

	LDAA	#RSCMND		RESTORE TO ZERO COMMAND
	JSR	SEEKCMD		ISSUE A SEEK COMMAND
	BITB	#$40		WRITE PROTECTED?
	BEQ	RESTR1		NO
	LDAB	#$0B		ELSE, CODE FOR WRITE PROTECTED
RESTR1	CLC
RESTR2	RTS

*--------------------------------------------------------------
* SEEK - Seek to the track specified in A, sector in B.
*    A seek is issued even if the track does not change as
*    the seek is used to load the head. The 1771 sector
*    register is set to the value in B. The side select
*    output is set based on the sector number. The side
*    select delay of 100us is inherent in the processing
*    here. If the motor was just started, the motor start
*    timeout is completed here and overlapped with the
*    seek time.
*--------------------------------------------------------------
SEEK	PSHA			PRESERVE THE TRACK NUMBER
	STAB	SECREG		SET SECTOR IN THE 1771
	CLRA			ASSUME SIDE 0
	CMPB	#10		SIDE 0 OR 1?
	BLS	SETSIDE		SIDE 0
	LDAA	#SIDEMSK	ELSE, SET SIDE 1

SETSIDE	STAA	CURSIDE		SAVE CURRENT SIDE BIT
	ORAA	CURDRV		INCLUDE DRIVE NUMBER
	STAA	DRVREG		SET IN DRIVE SELECT REGISTER

 	LDAA	#RSTCMD		FORCE TYPE I STATUS WITH A
	STAA	COMREG		..."FORCE INTERRUPT" COMMAND
	JSR	WAIT
	ANDB	#HEADMSK	IS HEAD LOADED?
	BNE	SEEK2		YES

	LDAA	#$04		SET E BIT FOR NEXT DISK I/O
	STAA	EBIT		...TO WAIT ON HEAD LOAD
 
SEEK2	PULA			RESTORE A=TRACK NUMBER
	STAA	DATREG		SET DESIRED TRACK
	SUBA	TRKREG		COMPUTE SEEK LENGTH
	BHS	SEEK3		NOT NEGATIVE
	NEGA			CONVERT TO POSITIVE
SEEK3	STAA	SEEKLEN

	LDAA	#SKCMND		ALWAYS SEEK TO LOAD THE HEAD
	JSR	SEEKCMD		ISSUE THE COMMAND AND WAIT

* Complete motor startup delay if needed. Subtract the time
*    already spent seeking. A one second timeout and 30ms
*    step time is used for slow seeking drives. A 0.5 sec
*    timeout and 6ms step time is used for fast seeking
*    drives. The step times represent the "worst case" times
*    that could occur if a 179x or 2797 controller is used.

	LDAA	MOTOFF		WAS MOTOR PREVIOUSLY OFF?
	BEQ	NOWAIT		NO MOTOR WAIT REQUIRED
		
	CLR	MOTOFF
	LDAA	SEEKLEN		A=LENGTH OF SEEK IN TRACKS
	TST	STEPR8		FAST STEPPING DRIVE?
	BEQ	FASTSTP		YES

	LDAB	#30/6		ELSE, USE 30MS STEP TIME
	STAB	STPTIME
	LDAB	#1000/6		B=1 SEC IN 6MS TICS
	BRA	SUBSEEK		GO SUBTRACT SEEK TIME
	
FASTSTP	LDAB	#6/6		USE 6MS STEP
	STAB	STPTIME	
	LDAB	#500/6		B=0.5 SEC IN 6MS TICS
	BRA	SUBSEEK		GO SUBTRACT SEEK TIME

SUBLOOP	SUBB	STPTIME		DECREMENT BY 6 OR 30MS PER STEP
	BLS	NOWAIT		SEEK TIME > MOTOR START TIME
SUBSEEK	DECA			DECREMENT TRACK STEP COUNT
	BPL	SUBLOOP
*				B=#6MS TICS TO DELAY
MOTDLY	LDAA	#6000/24	A=#24US TICS FOR 6MS

DLY6MS	INX			(4) 24US PER ITERATION
	DEX			(4)
	INX			(4)
	DEX			(4)
	NOP			(2)
	DECA			(2)
	BNE	DLY6MS		(4)

	DECB			6MS TIC COUNTER
	BNE	MOTDLY

NOWAIT	LDAA	SEEKLEN		WAS A SEEK REQUIRED?
	BEQ	SEEKRTS		NO
	LDAA	#$04		TURN ON EBIT TO FORCE 20MS
	STAA	EBIT		...FOR HEAD SETTLE TIME
SEEKRTS	RTS

*--------------------------------------------------------------
* SEEKCMD - Issue the seek command in A and wait for the seek
*    to complete.
*--------------------------------------------------------------
SEEKCMD	ORAA	STEPR8		INCLUDE STEP RATE BITS
	STAA	COMREG		ISSUE THE SEEEK
*				FALL INTO WAIT TO FINISH

*--------------------------------------------------------------
* WAIT - Wait for the current command to complete
*--------------------------------------------------------------
WAIT	JSR	DELAY		WAIT TO ENSURE 1771 STATUS

WAIT2	LDAB	COMREG		WAIT UNTIL NOT BUSY
	BITB	#BUSY
	BNE	WAIT2
	RTS

*--------------------------------------------------------------
* DELAY - Delay 56us
*--------------------------------------------------------------
DELAY	JSR	DELAY1
DELAY1	JSR	DELAY2
DELAY2	RTS

*--------------------------------------------------------------
* Use "END $AD00" when assembling for FLEX 2 and append
* EDSKDRV.BIN last when making FLEX2-ED.SYS
*
* Use "END" alone (not even a trailing comment) for FLEX 3 and
* append FLEX3CON.BIN last in the list of files when making
* FLEX3-ED.SYS
*
* This entry address syntax only works if assembling under FLEX
* using ASMB. If assembled on a PC and uploaded as an S19 file,
* the $AD00 entry address must be specified when saving the
* last .BIN file that will be specified in the APPEND statement.
*--------------------------------------------------------------
	END	$AD00		UNCOMMENT FOR FLEX 2.0
*	END

