; Classes:
;   0 = normal
;   1 = water
;   2 = web
; Events:
;   A = frame
;   B = stat
;   C = push
;   D = transport
;   S = shot (W=stat, Z=direction, condflag=player) (zero to destroy bullet)
;   T = touch (XY=coordinates, Z=direction) (ret nonzero to allow move)
;   X = explosive
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
REGSAV	IS $10 ;x16

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
	LET A,Z
	POKE A,FACING
	; Find player
	GSXY A,1
	XOR A,A
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
	CWOT E,$0003
	JF A,1F
	; Add bullet
	SINK E,0
	JF A,2F
	LET A,%L,Z,16
	ADD A,$02800F00+_BULLET
	PTM A,0
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

; **** Player ****
	EV S,_PLAYER,OUCH

; **** Floors ****
	EV T,_EMPTY,1
	EV T,_FLOOR,1
	EV T,_FAKE,1
	EV T,_WEB,1

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
	KILM D,0
1H	TEXT E,"You already have a "
	TEXT G,C
	MESS G," key!"
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
	KILM D,0
1H	MESS G," door is locked!"
	LET S,0

; **** Gems ****
	EV T,_MAGICGEM
	GIVE H,1
	EV T,_GEM
	GIVE G,1
	GIVE S,10
	KILM D,0

	EV S,_MAGICGEM
	EV S,_GEM
	FLET S,0
	KILM C,0

; **** Stone ****
	EV T,_STONE
	GIVE Z,1
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
	KILM D,0

; **** Torch ****
	EV T,_TORCH
	GIVE T,1
	KILM D,0

; **** Money ****
	EV T,_MONEY
	GIVE C,1
	KILM D,0

; **** Energizer ****
	EV T,_ENERGIZER
	VSET E,75
	KILM D,0

; **** Pushable objects ****
	EV T,_BOULDER
	EV T,_SLIDERNS
	EV T,_SLIDEREW
	; (TODO: sound effects)
	LET S,1

; **** Harmful objects ****
	EV T,_BULLET
	EV T,_STAR
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
	LET T,%B,B,$71
	BLOC Z,%B,B,$20
	CALM S,0
	BLOC A,W
	JNZ A,0
1H	DIE D,W

	EV S,_BULLET
	DIE A,W
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
	LET S,0

; **** Breakable walls ****
	EV S,_BREAKABLE
	KILM C,0

; **** Water ****
	EV T,_WATER
	MESS E,"Your way is blocked by water."
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
	CASE A,POTION
1H	DATA "Dud","Healing","Poison","Energy"
	DATA "Reveal Walls","Extra Healing"
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
	LET S,0
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

; **** Light shape ****
	TA $E3
	; (This must be the last one)

