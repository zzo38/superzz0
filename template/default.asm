; Classes:
;   0 = normal
;   1 = water
;   2 = web
;   3 = player only
;   4 = ice
; Events:
;   A = frame
;   B = stat
;   C = push
;   D = transport
;   S = shot (W=stat, Z=direction, condflag=player) (zero to destroy bullet)
;   T = touch (XY=coordinates, Z=direction) (ret nonzero to allow move)
;   U = under player
;   X = explosive (ret nonzero to destroy object)
; Element attributes:
;   C = creature
;   D = damaged by stars
; Status variables:
;   A = ammo
;   C = money
;   D = keys
;   E = energy
;   G = gems
;   H = health
;   S = score
;   T = torches
;   W = light
;   X = time
;   Z = stones
; Player misc:
;   Misc1 = Max shots
;   Misc2 = (unused)
;   Misc3 = (unused)
; Board flags:
;   bit0 = Restart if zapped
;   UserData = Time limit (0=none)
; Stat uses:
;   1 = player
;   2 = bullets/stars

; **** Global variables ****
INITPX	IS $00
INITPY	IS $01
FACING	IS $02
FOREST	IS $03
NWAITS	IS $04
REGSAV	IS $10 ;x16

; **** Global parameters ****
	TA $ED,1

; **** Keyboard handler ****
	TA $E0
	LET A,%B,W,$70
	CASE A,KEYS
KEYS	FILL $80

	; Arrows
	TA KEYS+24
	TA KEYS+25
	TA KEYS+26
	TA KEYS+27
	TA KEYS+'2'
	TA KEYS+'4'
	TA KEYS+'6'
	TA KEYS+'8'
	; Do nothing if no direction is pushed
	INC T,Z
	FLET S,0
	; Save direction
MOVEPL	LET A,Z
	POKE A,FACING
	; Find player
	GSXY A,1
MOVEPL1	XOR A,A
	DIR A,Z
	JF A,1F
	; Hit a tile within the board
	CALM T,0
	LET T,W
	JF A,0
	; Move player
	LET A,Z
	LET B,1
	SMOV B,$0050
	LET S,0
	; Hit an edge
1H	EXIT C,Z
	JF C,0
	WARP C,1F
	LET S,0
	; After warped due to a board edge
1H	LET B,Z
	LET C,X
	LET D,Y
	JEV B,1F
	; North/south
	LET D,0
	EQ B,1
	TLET D,%H,,-1
	GOTO A,2F
	; East/west
1H	LET C,%B,B,$11
	MUL C,%W,,-1
	; Move player to correct position on new board
2H	PICK A,1
1H	SWPC X,X
	SWPD Y,Y
	DROP A,1
	; If it cannot be placed there, put it back where it was instead
	JF F,1B
	GOTO A,ENTER

	; Shift+arrows
	TA KEYS+16
	TA KEYS+17
	TA KEYS+30
	TA KEYS+31
	; Shoot
	GM1 C,1
	JNZ C,1F
	MESS E,"You can't shoot in this place!"
	LET S,0
	; Check number of existing player bullets
2H	DATA $0002,_BULLET,$FFFF,$7FFF,$FFFF
1H	COUN A,2B
	LESS A,C
	JF A,0
	; Check ammo
1H	TAKE A,1
	JT A,1F
	MESS E,"You don't have any ammo!"
	LET S,0
	; Do shoot
1H	GSXY A,1
	FORW E,Z
	XOR E,E
	CWOT E,$0017
	JF A,1F
	; Add bullet
	SINK E,0
	JF A,2F
	LET A,%L,Z,16
	ADD A,$02800F00+_BULLET
	PTM A,0
	SFX A,"@22C'C<C"
	LET S,0
	; Close range shot
1H	GTMK B,0
	LET B,%E,B,#S
	JZ B,2F
	LET W,0
	EQ A,A
	GOTO B,B
	; Can't shoot; give back ammo
2H	GIVE A,1
	LET S,0

	; Hint
	TA KEYS+'H'
	TA KEYS+'h'
	LET A,0
	SEND A,"HINT"
	LET S,0

	; Torch light
	TA KEYS+'T'
	TA KEYS+'t'
	ROB W,0
	JT A,0
	BFLG G,0
	JF A,0
	TAKE T,1
	JT A,1F
	MESS E,"You don't have any torches!"
	LET S,0
1H	LITE C,5
	GIVE W,200
	LET S,0

	; Coordinates of player
	TA KEYS+'|'
	TEXT E,0
	TEXT D,%XP,,0
	TEXT C,$20
	MESS D,%YP,,0
	LET S,0

; **** Frame ****
	TA $E1
	; Enter starting board
	LET A,1F
	POKE A,$E1
	GOTO A,ENTER
	; Check health
1H	ROB H,0
	JF H,GAMOVER
	; Check energy
	LET A,$1F
	ROB E,1
	JF E,1F
	INC A,%R,,$F0
1H	GSXY B,1
	PTMC A,0
	; Check light
	TAKE W,1
	JF A,1F
	ROB W,0
	JT A,1F
	LITE A,0
	; Check time limit
1H	CBT A,$EC
	JF A,0
	GBU A,0
	JZ A,0
	ROB X,1
	TLET S,0
	GOTO A,OUCH1

; **** Miscellaneous subroutines ****

	; Game over
GAMOVER	GSXY A,1
	LET B,0
	PTMK B,0
	PSPD B,1
	SFX A,"@82<S.CD#GC'GA#>DGFG#>CFO1Q.CX"
	LET A,1F
	POKE A,$E0
	POKE A,$E1
1H	MESS E,"*** Game Over ***"
	LET S,0

	; Always needed after entering any board
ENTER	MESS E,0
	GSXY A,1
	LET A,X
	POKE A,INITPX
	LET A,Y
	POKE A,INITPY
	LET A,0
	SEND A,"ENTER"
	GBU A,0
	VSET X,A
	LET S,0

	; Damage player
OUCH	ROB E,0
	JT E,0
OUCH1	ROB H,10
	MESS E,"Ouch!"
	SFX A,"@30Z<<CC'C#D#'X"
	; Restart if zapped
	BFLG A,0
	JF A,0
	PICK A,1
	LET X,%I,,INITPX
	LET Y,%I,,INITPY
	DROP A,1
	JT A,0
	GSXY B,1
	DROP A,1
	GBU A,0
	VSET X,A
	LET S,0

	; Move creature, but die and ouch if player
	; A = the direction of movement
	; B = move flags
	; C = temporary
	; W = stat
	; X,Y = coordinates
	; Return = 0 if not moved, 1 if OK, -1 if hurt player
MOVCRE	FORW A,A
	JF A,0
	GTMK C,0
	EQ C,_PLAYER
	JF C,1F
	CALL C,OUCH
	DIE B,W
1H	LET C,W
	SMOV C,B
	JF A,0
	JT A,1

	; Shoot (non-player bullets)
	; H = temporary use
	; X,Y = coordinates
	; Z = direction
SHOOT	FORW H,Z
	XOR H,H
	CWOT H,$0017
	JF A,1F
	; Add bullet
	SINK H,0
	JF H,0
	LET H,%L,Z,16
	ADD H,$02000F00+_BULLET
	PTM H,0
	LET S,0
	; Close range shot
1H	GTMK H,0
	LET W,0
	LET T,0
	EJMP H,0

; **** Player ****
	EV S,_PLAYER,OUCH
	EV X,_PLAYER,OUCH

	EV B,_PLAYER
	GTUK A,0
	EJMP U,A

; **** Floors ****
	EV T,_EMPTY,1
	EV T,_FLOOR,1
	EV T,_FAKE,1
	EV T,_WEB,1
	EV T,_ICE,1

; **** Keys/doors ****
	EV T,_KEY
	GTMC C,0
	AND C,7
	BGIV D,C
	PEER C,2F
	JF D,1F
	TEXT E,"You now have the "
	TEXT G,C
	MESS G," key."
	SFX A,"@22T>CEGCEGCEG>SC"
	KILM D,0
1H	TEXT E,"You already have a "
	TEXT G,C
	MESS G," key!"
	SFX A,"@22SC<C"
	LET S,0
2H	DATA "black","blue","green","cyan","red","purple","yellow","white"
	EV T,_DOOR
	GTMC C,0
	LET C,%B,C,$34
	BTAK D,C
	TEXT E,"The "
	PEER C,2B
	TEXT G,C
	JF D,1F
	MESS G," door is now open."
	SFX A,"@22TCGBCGB>IC"
	KILM D,0
1H	MESS G," door is locked!"
	SFX A,"@22T<<GC"
	LET S,0

; **** Gems ****
	EV T,_MAGICGEM
	GIVE H,1
	EV T,_GEM
	GIVE G,1
	GIVE S,10
	SFX A,"@20TC'GEC"
	KILM D,0

	EV S,_MAGICGEM
	EV S,_GEM
	FLET S,0
	SFX A,"@10Z.<C"
	KILM C,0

; **** Stone ****
	EV T,_STONE
	GIVE Z,1
	SFX A,"@23ZK4K6K8K10K12K14K16K20K24K28K32"
	KILM D,0

	EV A,_STONE
	NEG A,%R,,26
	PTMP A,0
	LET A,%R,,7
	ADD A,9
	PTMC A,0
	LET S,0

; **** Ammo ****
	EV T,_AMMO
	GIVE A,5
	SFX A,"@20TCC#D"
	KILM D,0

; **** Torch ****
	EV T,_TORCH
	GIVE T,1
	SFX A,"@20TCASE"
	KILM D,0

; **** Money ****
	EV T,_MONEY
	GIVE C,1
	SFX A,"@20ZATA"
	KILM D,0

; **** Energizer ****
	EV T,_ENERGIZER
	VSET E,75
	KILM D,0

; **** Pushable objects ****
	EV T,_BOULDER
	EV T,_SLIDERNS
	EV T,_SLIDEREW
	SFX A,"@12T<<F"
	LET S,1

; **** Harmful objects ****
	EV T,_BULLET
	EV T,_STAR
	EV T,_LION
	EV T,_TIGER
	EV T,_BEAR
	EV T,_SHARK
	EV T,_RUNNER
	EV T,_HEAD
	EV T,_SEGMENT
	CALL W,OUCH
	KILM D,0

; **** Bullet ****
; Parameter:
;   bit1-bit0 = Direction
;   bit7 = Type (clear=object, set=player)
	EV B,_BULLET
	LET A,W
	LET B,Z
	SMOV A,$0001
	JT A,0
	FORW H,Z
	JF A,1F
	LET T,%B,B,$17
	BLOC Z,%B,B,$20
	CALM S,0
	BLOC A,W
	JNZ A,0
1H	DIE D,W

	EV S,_BULLET
	DIE A,W
	SFX A,"@10Z.<C"
	KILM C,0

; **** Ricochet ****
	EV S,_RICOCHET
	GSXY A,W
	JF A,OUCH
	GTMP A,0
	XOR A,2
	PTMP A,0
	LET S,1

; **** Invisible walls ****
	EV T,_INVISIBLE
	MESS E,"You are blocked by an invisible wall!"
	LET A,_NORMAL
	PTMK A,0
	SFX A,"@20<<DC"
	LET S,0

; **** Breakable walls ****
	EV S,_BREAKABLE
	SFX A,"@10Z.<C"
	KILM C,0

; **** Water ****
	EV T,_WATER
	MESS E,"Your way is blocked by water."
	SFX A,"@20T>C>C"
	LET S,0

; **** Transporter ****
	EV T,_TRANSPORTER,1
	EV D,_TRANSPORTER
	REGS B,REGSAV
	; Check direction
	GTMP A,0
	EQ A,W
	JF A,0
	; Check target coordinates
	LET B,Z
	JNZ B,1F
	; First try through transporter
3H	FORW A,W
2H	REGL B,REGSAV
	FLET S,0
	PACK S,0
	; Not first try
1H	UNPC B,B
	LET A,%,W,2
	AND A,3
	LET Z,A
1H	FORW A,W
	JF A,2B
	; Check if transporter matches
	GTMK A,0
	EQ A,_TRANSPORTER
	JF A,1B
	GTMP A,0
	EQ A,Z
	JT A,3B
	GOTO A,1B

	TA $C8
	SFX A,"@24TCD'EF#'G#>A#CD'"
	LET S,0

; **** Potion ****
; Parameter: effect of potion (0=none)
	EV A,_POTION
	GTMC A,0
	XOR A,8
	PTMC A,0
	LET S,0

	EV T,_POTION
	GTMP A,0
	GIVE S,5
	KILM A,0
	TEXT E,"\xB0\xB1\xB2 "
	LET B,A
	PEER B,1F
	TEXT G,B
	MESS G," \xB2\xB1\xB0"
	SFX A,"@24Z.U4U5U6U7U8U9U10U12U14U16U18U20U18U16U14U12U10U9U8U7U6U5U4U3"
	CASE A,POTION
1H	DATA "Dud","Healing","Poison","Energy"
	DATA "Reveal Walls","Extra Healing","Time"
POTION	FILL $10,1

	TA POTION+1 ; Healing
	GIVE H,10
	LET S,1

	TA POTION+2 ; Poison
	ROB H,10
	LET S,1

	TA POTION+3 ; Energy
	GIVE E,70
	LET S,1

	TA POTION+4 ; Reveal Walls
	CHA A,1F
	LET S,1
1H	DATA $0003,_INVISIBLE,$FFFF,$FFFF,$FFFF,_NORMAL,$FF00,$FF00,$FF00

	TA POTION+5 ; Extra Healing
	GIVE H,50
	LET S,1

	TA POTION+6 ; Time
	GIVE T,25
	LET S,1

; **** Stars ****
; Parameter: duration
	EV B,_STAR
	GTMP A,0
	LOOP A,1F
	DIE C,W
1H	PTMP A,0
	GTMC A,0
	ICG A,15
	TLET A,9
	PTMC A,0
	SEEK A,1
	FORW B,A
	TMAT D,0
	JT A,1F
	TMAT C,0
	JT A,0
	LET B,W
	SMOV B,$0010
	LET S,0
1H	LET T,0
	LET Z,A
	DIE A,W
	CALM S,0
	LET S,0

; **** Object ****
	EV B,_OBJECT
	LET A,W
	RUN A,1
	DEC A,%I,,NWAITS
	JNEG A,0
	PSD A,W
	SPOK A,NWAITS
	LET S,1
	EV S,_OBJECT
	SIM A,0
	SEND A,"SHOT"
	LET S,0
	EV T,_OBJECT
	SIM A,0
	SEND A,"TOUCH"
	LET S,0
	EV X,_OBJECT
	SIM A,0
	SEND A,"BOMBED"
	LET S,0

; **** Scroll ****
	EV A,_SCROLL
	GTMC A,0
	ICG A,15
	TLET A,9
	PTMC A,0
	LET S,0

	EV T,_SCROLL
	SIM A,0
	RUN A,0
	KILM C,0

; **** Checkpoint ****
	EV T,_CHECKPOINT
	LET A,X
	POKE A,INITPX
	LET A,Y
	POKE A,INITPY
	GBU A,0
	VSET X,A
	KILM D,0

; **** Passage ****
	EV T,_PASSAGE
	GTMP A,0
	GTMC B,0
	LET Z,B
	WARP A,1F
	SFX A,"@30CEGC#FG#DF#AD#GA#EG#>C"
	LET S,0
1H	DEC A,0
1H	SCAN A,_PASSAGE
	JF A,ENTER
	GTMC C,A
	XOR C,Z
	JNZ C,1B
	LET B,1
	TELE B,A
	GOTO A,ENTER

; **** Ice ****
	EV U,_ICE
	LET Z,%I,,FACING
	GOTO A,MOVEPL1

; **** Forest ****
	EV T,_FOREST
	LET A,_FLOOR
	PTMK A,0
	GTMC A,0
	LSH A,4
	OR A,%UR,A,8
	PTMC A,0
	; Sound effect
	INC A,%I,,FOREST
	AND A,7
	POKE A,FOREST
	PEER A,1F
	SFX A,A
	LET S,1
1H	DATA "@18>F","@18>C","@18>G","@18>>C"
	DATA "@18>F#","@18>C#","@18>G#","@18>>C#"

; **** Slime ****
; Parameter:
;   bit7-bit4 = Current delay
;   bit3-bit0 = Speed
	EV T,_SLIME
	LET A,_BREAKABLE
	PTMK A,0
	SFX A,"@20<CD#"
	LET S,0

	EV A,_SLIME
	; Alternate odd/even frames
	PEEK A,$EC
	ADD A,X
	ADD A,Y
	JOD A,0
	; Check delay
	GTMP A,0
	LESS A,$10
	JF A,1F
	; Do spread
	MUL A,$11
	GTMK E,0
	GTMC D,0
	LET C,_BREAKABLE
	PTMK C,0
	XOR C,C
	CALL C,2F
2H	CALL C,2F
2H	FORW C,C
	GTMK B,0
	JNZ B,2F
	PTMC D,0
	PTMP A,0
	PTMK E,0
2H	INC S,C
	; Decrement
1H	SUB A,$10
	PTMP A,0
	LET S,0

; **** Pusher ****
; Parameter:
;   bit1-bit0 = Direction
	EV B,_PUSHER
	LET A,W
	LET B,Z
	SMOV A,$0011
	FLET S,0
	SFX A,"@12T<<F"
	LET S,0

; **** Creatures (General) ****
	EV S,_RUNNER
	EV S,_LION
	EV S,_TIGER
	EV S,_BEAR
	EV S,_SHARK
	EV S,_HEAD
	EV S,_SEGMENT
	FLET S,0
	GIVE S,1
	SFX A,"@20O4CO1CO5CO3C"
	KILM C,0

; **** Runner ****
; Parameter:
;   bit1-bit0 = Direction
	EV B,_RUNNER
	LET A,Z
	LET B,$0050
	CALL C,MOVCRE
	JNEG C,1
	JNZ C,0
	INC A,1
	XOR A,Z
	PTMP A,0
	LET S,0

; **** Lion, Tiger, Shark ****
; Parameter:
;   bit3-bit0 = Intelligence
;   bit7-bit4 = Firing rate (Tiger only)
; User bit: 0=bullets, 1=stars (Tiger only)
	EV B,_TIGER
	; Check firing rate
	LET B,%B,Z,$44
	GRTR B,%R,,16
	JF B,1F
	; Do shoot
	SEEK A,1
	LET Z,A
	CALL A,SHOOT
	; fall through to Lion moving subroutine

	EV B,_LION
	EV B,_SHARK
1H	LET B,Z
	AND B,$0F
	SEEK A,1
	GRTR C,%R,,16
	TLET A,%R,,4
	LET B,0
	CALL C,MOVCRE
	LET S,0

; **** Bear ****
; Parameter: Range (0-255)
	EV B,_BEAR
	ABS A,%XP,X,0
	ABS B,%YP,Y,0
	ADD A,B
	GRTR A,Z
	JT A,0
	SEEK A,1
	LET Z,A
	LET B,0
	CALL A,MOVCRE
	JNZ A,0
	FORW A,Z
	GTMK A,0
	EQ A,_BREAKABLE
	JF A,0
	KILM A,0
	SFX A,"@20O4CO1CO5CO3C"
	DIE C,W

; **** Centipedes ****
; Parameter: Direction (0-3)
; Misc1: Intelligence (0-128)
; Misc2: Deviance (0-128)
; Instruction pointer: Sequence number
; User bit:
;   clear = Follower is next
;   set = Follower is previous
; Lock bit: Initialized

	; Initialization
CENINI	LET A,%UR,W,16
	PIP A,W
	LET A,$80
	LOCK A,W
	LET S,0

	; Subroutine for trying to move a centipede.
	; Does not cause segments to follow, but does update parameter.
	; Return value is 1 if successful, 0 if not successful, -1 if ouch.
	; Input: W=stat, XY=coordinates, Z=direction.
	; Output: D=direction.
	; Clobbers: B.
CENMOV	LET D,Z
	AND D,3
	FORW B,D
	JF B,0
	GTMK B,0
	EQ B,_PLAYER
	JT A,1F
	LET B,W
	SMOV B,$0003
	JF B,0
	SIXY W,W
	PTMP D,W
	LET S,1
	; Hurt player
1H	CALL A,OUCH
	DIE B,W

	EV B,_HEAD
	LAY A,W
	BTST A,7
	JF A,CENINI
	; (A = layer/lock bits of head)
	; Check if it should change direction to follow player
	GM1 B,W
	GRTR B,%R,,128
	JF B,1F
	LET T,%XP,X,0
	TLET T,%YP,Y,0
	JT B,1F
	SEEK D,1
	GOTO D,2F
	; Check if it should change direction at random
1H	GM2 B,W
	GRTR B,%R,,128
	JF B,1F
	LET D,%R,,4
	; Do change direction to D
2H	PTMP D,0
	LET Z,D
	; Try to move
1H	CALL B,CENMOV
	JNEG B,0
	JNZ B,2F
	; Can't move; try to change direction CW or CCW
	INC Z,Z
	CALL B,CENMOV
	JNEG B,0
	JNZ B,2F
	DEC Z,Z
	DEC Z,Z
	CALL B,CENMOV
	JNEG B,0
	JZ B,3F
	; Moved successfully; other segments should follow
	; (D = direction)
	; (Now (X,Y) are the old coordinates of the head)
2H	BTST A,6
	FLET C,$10000
	TLET C,-$10000
	; (C = +1 or -1 offset to follower)
	LET E,W
	; (E = stat index to work with)
	PACK Z,0
	; (Z = packed coordinates to move segment to)
	; Find follower
1H	GIP G,E
	ADD E,C
	GIP H,E
	; Check if sequence is broken
	ASUB G,H
	EQ G,1
	JF G,0
	; Check if the follower is valid
	GSXY B,E
	JF B,0
	URSH B,8
	EQ B,A
	JF B,0
	GTMK B,0
	EQ B,_SEGMENT
	JF B,0
	; Move follower
	PICK B,E
	JF B,0
	PACK F,Z
	UNPC F,Z
	DROP B,E
	LET Z,F
	; Update direction
	GTMP G,0
	PTMP D,0
	LET D,G
	; Continue
	GOTO F,1B
	; Can't go either way, so reverse direction instead
	;  Directions must be reversed as follows:
	;    >>v             <<<
	;      v   becomes     ^
	;      v               ^
3H	BTST A,6
	FLET C,$10000
	TLET C,-$10000
	LET E,W
	; (E = stat to work with)
	; (W = old head)
	; Change head to segment
	LET B,_SEGMENT
	PTMK B,0
	; Find end of tail
1H	GIP G,E
	ADD E,C
	GIP H,E
	; Check if sequence is broken
	ASUB G,H
	EQ G,1
	JF G,1F
	; Check if follower is valid
	GSXY B,E
	JF B,1F
	URSH B,8
	EQ B,A
	JF B,1F
	GTMK B,0
	EQ B,_SEGMENT
	JT B,1B
	; Found end of tail
1H	SUB E,C
	XOR A,$40
	; Make new head and remember direction
	GSXY B,E
	GTMP D,0
	XOR D,2
	; (D = direction)
	LET B,_HEAD
	PTMK B,0
	; Go back the other way
1H	LOCK A,E
	GTMP F,0
	PTMP D,0
	EQ E,W
	JT E,0
	SUB E,C
	GSXY B,E
	LET D,F
	XOR D,2
	GOTO A,1B

	EV B,_SEGMENT
	LAY A,W
	BTST A,7
	JF A,CENINI
	; Check forward or backward
	LET B,-$10000
	BTST A,6
	TLET B,$10000
	ADD B,W
	; Check that there is another segment/head
	LAY E,B
	JF E,1F
	; If the other one is not initialized, wait until it is
	BTST E,7
	JF E,0
	; If chain has not been broken, wait until it is broken
	GIP C,W
	GIP D,B
	ASUB C,D
	EQ C,1
	JT C,0
	; Chain is broken; change this segment into a head
1H	LET A,_HEAD
	PTMK A,0
	LET S,0

; **** Script commands ****

	; #CHAR <number>
	COM "CHAR"
	PARN A,W
	JF A,0
	PTSP A,W
	LET S,0

	; #SHOOT <direction>
	COM "SHOOT"
	PARD A,W
	JF A,0
	JNEG A,1
	LET Z,A
	CALL A,SHOOT
	LET S,1

	; #WAIT <number>
	COM "WAIT"
	PARN A,W
	JF A,0
	POKE A,NWAITS
	LET S,1

	; #WAITFOR <condition>
	COM "WAITFOR"
	PARC A,W
	JT A,0
	SPOK B,NWAITS
	LET S,$18

	; #THROWSTAR <direction> [<duration>]
	COM "THROWSTAR"
	PARD A,W
	JF A,0
	JNEG A,1
	FORW A,A
	PARN B,W
	FLET B,127
	GTMK C,0
	EMAT D,C
	JT C,1F
	CWOE C,$0017
	JF C,1
	SINK C,0
	JF C,1
	LSH B,16
	ADD B,$02000F00+_STAR
	PTM B,0
	LET S,1
1H	CALM S,0
	LET S,1

; **** Light shape ****
	TA $E3
	ASS @,65487
; (This must be the last one, other than the editor data.)

; **** Editor menus ****

	ED1 1
	ED 1,"Items:"
	ED 'G',"Gem",_GEM,$0000
	ED 'M',"Magic Gem",_MAGICGEM,$0000
	ED 'O',"Money",_MONEY,$030E
	ED 'A',"Ammo",_AMMO,$0303
	ED 'T',"Torch",_TORCH,$0306
	ED 'K',"Key",_KEY,$0000
	ED 'D',"Door",_DOOR,$040F
	ED 'Z',"Stone",_STONE,$0000
	ED 'E',"Energizer",_ENERGIZER,$0307
	ED 'P',"Potion",_POTION,$0200
	ED 'S',"Scroll",_SCROLL,$0800
	ED 'Q',"Checkpoint",_CHECKPOINT,$0309
	ED 2

	ED1 2
	ED 1,"Creatures:"
	ED 'L',"Lion",E_LION,_LION+$8600
	ED 'T',"Tiger",E_LION,_TIGER+$8600
	ED 'B',"Bear",E_LION,_BEAR+$8600
	ED 'K',"Shark",E_LION,_SHARK+$8600
	ED 'R',"Runner",E_RUNN,$8200
	ED 'V',"Slime",_SLIME,$0000
	ED 1,"Centipedes:"
	ED 'H',"Head",E_CENT,_HEAD+$8200
	ED 'S',"Segment",E_CENT,_SEGMENT+$8200
	ED 2

	ED1 3
	ED 1,"Terrains:"
	ED 'W',"Water",_WATER,$0319
	ED 'F',"Forest",_FOREST,$0320
	ED 'I',"Ice",_ICE,$0331
	ED 'X',"Web",_WEB,$0000
	ED 1,"Walls:"
	ED 'S',"Solid",_SOLID,$0000
	ED 'N',"Normal",_NORMAL,$0000
	ED 'L',"Line",_LINE,$0000
	ED 'B',"Breakable",_BREAKABLE,$0000
	ED 'T',"Text",_TEXT,$0000
	ED 'V',"Invisible",_INVISIBLE,$0000
	ED 'R',"Ricochet",_RICOCHET,$030A
	ED 1,"Floors:"
	ED 'E',"Empty",_EMPTY,$0300
	ED 'O',"Floor",_FLOOR,$0000
	ED 'K',"Fake",_FAKE,$0000
	ED 2

	ED1 5
	ED 1,"Projectiles:"
	ED 'B',"Bullet",_BULLET+$0200,$010F
	ED 'S',"Star",_STAR+$0200,$010F
	ED 1,"Special:"
	ED 'E',"Empty",_EMPTY,$0300
	ED 'Z',"Player",_PLAYER+$0100,$031F
	ED 'O',"Object",_OBJECT,$0800
	ED 2

	ED1 6
	ED 0,_TEXT,$1000

; **** Parameter edit ****

	ED0 _TEXT,$0100

	ED0 _OBJECT
	ED 'C'
	ED0 _SCROLL
	ED 1,$0200
	ED 'E'
	ED 0

	ED0 _BULLET
	ED 'H',"Bullet"
	ED 'O',$0010
	ED2 'O',"~East",0
	ED2 'O',"~North",1
	ED2 'O',"~West",2
	ED2 'O',"~South",3
	ED 'H',0
	ED2 'B',"~Player bullet",$0007
	ED 0

	ED0 _STAR
	ED 1,$007F
	ED 'H',"Star"
	ED2 'N',"~Duration: ",$0070,0,255
	ED 0

	ED0 _POTION
	ED 'H',"Potion"
	ED 'O',$0070
	ED2 'O',"~Dud",0
	ED2 'O',"~Healing",1
	ED2 'O',"~Poison",2
	ED2 'O',"~Energy",3
	ED2 'O',"Re~veal Walls",4
	ED2 'O',"E~xtra Healing",5
	ED2 'O',"~Time",6
	ED 0

E_LION	ED3 _LION,$0C,_TIGER,$0B,_BEAR,$06,_SHARK,$07
	ED '=',"K-MPaT"
	ED '@',"_2",2
	ED 'P',$FFFF,$4800
	ED 0

E_RUNN	ED '=',"-MP"
	ED '@',"_1",1
	ED 'P',_RUNNER,$5800
	ED 0

	ED0 _SLIME
	ED 1,$0077
	ED 'H',"Slime"
	ED2 'N',"~Speed: ",$0030,0,15
	ED2 'N',"~Delay: ",$0034,0,15
	ED 0

	ED0 _LION
	ED0 _SHARK
	ED 1,$0008
	ED 'H',"Creature"
	ED2 'N',"~Intelligence: ",$0030,0,15
	ED 0

	ED0 _TIGER
	ED 1,$0088
	ED 'H',"Creature"
	ED2 'N',"~Intelligence: ",$0030,0,15
	ED2 'N',"~Firing rate: ",$0034,0,15
	ED2 'B',"~Stars",$2204
	ED 0

	ED0 _BEAR
	ED 1,$0006
	ED 'H',"Bear"
	ED2 'N',"~Range: ",$0030,0,255
	ED 0

	ED0 _TRANSPORTER
	ED0 _PUSHER
	ED0 _RUNNER
	ED 'H',"Direction:"
	ED 'O',$0010
	ED2 'O',"~East",0
	ED2 'O',"~North",1
	ED2 'O',"~West",2
	ED2 'O',"~South",3
	ED 0

E_CENT	ED '=',"K-MP"
	ED '@',"_C",2
	ED 'P',$FFFF,$4800
	ED 0

	ED0 _HEAD
	ED0 _SEGMENT
	ED 'H',"Direction:"
	ED 'O',$0010
	ED2 'O',"~East",0
	ED2 'O',"~North",1
	ED2 'O',"~West",2
	ED2 'O',"~South",3
	ED 'H',0
	ED2 'N',"~Intelligence: ",$1170,0,128
	ED2 'N',"~Deviance: ",$1270,0,128
	ED 0

; **** Editor board info ****
	ED1 32,"Restart if zapped"
	ED1 36,"Time limit: "
	ED1 37,"Max shots: "

	; Text editor
	ED1 64,60

