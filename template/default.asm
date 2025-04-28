; Classes:
;   0 = normal
;   1 = water
;   2 = web
;   3 = player only
;   4 = ice
;   5 = fire
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
;   B = burns
;   C = creature
;   D = damaged by stars
;   H = cannot be duplicated
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
; Predefined stat names (not necessarily present on all boards):
;   _1 = Creatures with speed 1 (Runner)
;   _2 = Creatures with speed 2 (Lion, Tiger, Bear, Shark)
;   _3 = Speed 3 (Conveyor)
;   _4 = Creatures with speed 4 (Pusher)
;   _6 = Speed 6 (Bomb)
;   _C = Centipedes

; **** Global variables ****
INITPX	IS $00
INITPY	IS $01
FACING	IS $02
FOREST	IS $03
NWAITS	IS $04
REGSAV	IS $10 ;x16

; **** Global parameters ****
	TA $C4,$0010
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
	CWOT E,$0037
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
1H	LITE C,6
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
	SFX A,"@9T<C<C"
	; Check time limit
1H	CBT A,$EC
	JF A,0
	GBU A,0
	JZ A,0
	ROB X,1
	TLET S,0
	GIVE X,A
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
	; (clobbers: A, B)
OUCH	ROB E,0
	JT E,0
OUCH1	ROB H,10
	MESS E,"Ouch!"
	SFX A,"@30Z<<CC'C#D#'X"
	; Restart if zapped
REZAP	BFLG A,0
	JF A,0
	PICK A,1
	LET X,%I,,INITPX
	LET Y,%I,,INITPY
	DROP A,1
	JT A,1F
	GSXY B,1
	DROP A,1
	LET S,0
1H	SFX A,"@32T<CD#GC'<C"
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
	CWOT H,$0037
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
	EJMP S,H

	; Spit fire
	; H = temporary use
	; X,Y = coordinates
	; Z = direction
SPFIRE	FORW H,Z
	XOR H,H
	CWOT H,$0031
	JF A,1F
	; Add projectile
	SINK H,0
	JF H,0
	LET H,%L,Z,16
	ADD H,$02000C00+_SPITFIRE
	PTM H,0
	LET S,0
	; Check if player
1H	GTMK H,0
	EQ H,_PLAYER
	JT H,OUCH
	; Check if burnable
1H	EMAT B,H
	JF B,0
	KILM A,0
	KILM A,0
	LET A,_FIRE+$4E00
	PTM A,0
	LET S,0

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
	EV T,_FIRE,1
	EV T,_OPENGATE,1

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

; **** Heart ****
	EV T,_HEART
	GIVE H,5
	SFX A,"@20TK4K5K6K12K20"
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

; **** Indirect Push ****
	EV C,_INDIRECTPUSH
	BACK A,W
	GTMK A,0
	EQ A,_PLAYER
	LET S,%OF,,1

; **** Rock ****
	EV T,_ROCK
	XOR A,A
	LET B,Z
	MOVE A,$0041
	JF A,0
	SFX A,"@12T<<F"
	GTUK A,A
	EQ A,_WATER
	JF A,1
	FLOA A,0
	FLOA A,0
	SFX A,"@13TF#C<F#C<F#"
	LET S,1

; **** Spike ****
	EV T,_SPIKE,OUCH

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
	EV T,_MOUSE
	EV T,_SNAKE
	EV T,_SPIDER
	EV T,_BIRD
	EV T,_LUMBERJACK
	EV T,_LANDMINE
	EV T,_SPITFIRE
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

; **** Spit Fire ****
; Parameter:
;   bit1-bit0 = Direction
	EV B,_SPITFIRE
	LET A,W
	LET B,Z
	GTMC C,0
	XOR C,8
	PTMC C,0
	SMOV A,$0001
	JT A,0
	FORW H,Z
	JF A,1F
	GTMK A,0
	EQ A,_PLAYER
	JT A,2F
	EMAT B,A
	JF B,1F
	KILM A,0
	KILM A,0
	LET A,_FIRE+$4E00
	PTM A,0
	DIE D,W
2H	CALL A,OUCH
1H	DIE D,W

; **** Ricochet ****
	EV S,_RICOCHET
	GSXY A,W
	JF A,1
	GTMP A,0
	XOR A,2
	PTMP A,0
	SFX A,"@10T.`"
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
	DATA "Reveal Walls","Extra Healing","Time","Avalanche"
	DATA "Destroy Creatures","Explode Bombs"
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

	TA POTION+7 ; Avalanche
	LET A,%W,,0
	MUL A,%H,,0
	INC B,A
	DIV B,20
1H	INC C,%R,A,0
	CWOT C,$0015
	JF C,2F
	SINK D,C
	LET D,$00000700+_ROCK
	PTM D,C
2H	LOOP B,1B
	LET S,1

	TA POTION+8 ; Destroy Creatures
	CHA A,1F
	LET S,1
1H	DATA $0023,$FB04,$FFFF,$FFFF,$FFFF,_DESTROYED,0,0,0,0

	TA POTION+9 ; Explode Bombs
	CHA A,2F
	CHA A,1F
	LET S,1
1H	DATA $8003,_BOMB,$FFFF,$FFFF,$FFFF,_LITBOMB,$FF00,$0001,$FF00
2H	DATA $8003,_LITBOMB,$FFFF,$FFFF,$FFFF,_LITBOMB,$FF00,$0001,$FF00

; **** Destroyed tiles ****
; This is used when other objects are changed to this in order to destroy them.
	EV A,_DESTROYED
	FLOA A,0
	LET S,0

; **** Chest ****
; Parameter:
;   bit7-bit4 = Kind
;   bit3-bit0 = Amount
	EV X,_CHEST,1
	EV T,_CHEST
	GTMP A,0
	DEC B,%UR,A,4
	JNEG B,0
	AND A,$0F
	PTMP A,0
	SFX A,"@29T<D>DA>DX"
	TEXT E,"Inside the chest you find "
	CASE B,CHEST

CHEST	FILL $0F,0

	TA CHEST+1-1 ; Ammo (x5)
	MUL A,5
	TEXT D,A
	MESS G," pieces of ammunition."
	GIVE A,A
	LET S,0

	TA CHEST+2-1 ; Money (x5)
	MUL A,5
	TEXT D,A
	MESS G," pieces of money."
	GIVE C,A
	LET S,0

	TA CHEST+3-1 ; Gems (x5)
	MUL A,5
	TEXT D,A
	MESS G," gems."
	GIVE G,A
	GIVE S,A
	LET S,0

	TA CHEST+4-1 ; Stones
	TEXT D,A
	MESS G," stones."
	GIVE Z,A
	LET S,0

	TA CHEST+5-1 ; Torches
	TEXT D,A
	MESS G," torches."
	GIVE T,A
	LET S,0

	TA CHEST+6-1 ; Trap
	CALL A,OUCH
	MESS G," It is a trap!"
	LET S,0

; **** Pouch ****
; Parameter:
;   bit7-bit4 = Money (5x)
;   bit3-bit0 = Gems (5x)
	EV T,_POUCH
	GTMP B,0
	SFX A,"@29Z.CGEC'G>ECGEC'G>EC"
	TEXT E,"This pouch contains "
	LET A,%B,B,$40
	MUL A,5
	GIVE G,A
	GIVE S,A
	TEXT D,A
	TEXT G," gems and "
	LET A,%B,B,$44
	MUL A,5
	GIVE C,A
	GIVE S,A
	TEXT D,A
	MESS G," coins."
	KILM D,0

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
	TA $C7
	LET A,W
	RUN A,1
	LAY A,W
	BTST A,4
	JF A,1F
	RSH A,2
	LET B,W
	SMOV B,0
	JT A,1F
	SEND B,"THUD"
1H	DEC A,%I,,NWAITS
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

; **** Sensor ****
	EV T,_SENSOR,1
	TA $C6
	DEC T,W
	TLET S,0
	LETL A,W
	BTST A,3
	TLET S,1
	SIM A,0
	FLET S,1
	RUN A,0
	LET S,1

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

; **** Fire ****
	EV A,_FIRE
	PEEK A,$EC
	ADD A,X
	ADD A,Y
	JOD A,0
	LET A,%R,,4
	JZ A,0
	INC A,Z
	MOD A,3
	PTMP A,0
	LET S,0

	EV U,_FIRE
	ROB E,0
	JT E,0
	ROB H,5
	MESS E,"Ouch!"
	SFX A,"@30Z<C<D#GC"
	GOTO A,REZAP

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
	EV S,_MOUSE
	EV S,_SNAKE
	EV S,_SPIDER
	EV S,_BIRD
	EV S,_LUMBERJACK
	FLET S,0
	GIVE S,1
	SFX A,"@24O4CO1CO5CO3C"
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

; **** Snake ****
; Parameter:
;   bit1-bit0 = Direction
;   bit7-bit4 = Intelligence
	EV B,_SNAKE
	LET A,Z
	XOR B,B
	CALL C,MOVCRE
	JNZ C,0
	SEEK C,1
	RSH A,4
	GRTR A,%R,,16
	TLET C,%R,,4
	LSH A,4
	ADD A,C
	PTMP A,0
	LET S,0

; **** Lion, Tiger, Shark, Mouse, Spider, Bird, Lumberjack ****
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
	EV B,_BIRD
	EV B,_SPIDER
1H	LET B,Z
	AND B,$0F
	SEEK A,1
2H	GRTR B,%R,,16
	TLET A,%R,,4
	LET B,0
	CALL C,MOVCRE
	LET S,0

	EV B,_MOUSE
	LET B,Z
	SEEK A,1
	XOR A,2
	GOTO A,2B

	EV B,_LUMBERJACK
	CALL C,1B
	JNZ C,0
	FORW C,A
	GTMK D,0
	EQ D,_TREE
	JT D,1F
	EQ D,_FOREST
	JF D,0
1H	SFX A,"@18&"
	LET D,_FLOOR
	PTMK D,0
	INC D,1
	PTMC D,0
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

; **** Dragon ****
; Parameter:
;   bit2-bit0 = Hit points
;   bit5-bit3 = Firing rate
;   bit7 = Random movement (1/8 of the time)

	EV T,_DRAGON,OUCH

	EV S,_DRAGON
	JF A,0
	EV X,_DRAGON
	GIVE S,1
	GTMP A,0
	LET B,A
	AND B,$07
	LOOP B,1F
	SFX A,"@24O4CO1CO5CO3C"
	KILM C,0
1H	DEC A,A
	PTMP A,0
	SFX A,"@24O4CO1CC#"
	LET S,0

	EV B,_DRAGON
	SEEK A,1
	LET T,%B,Z,$17
	JF A,1F
	LET T,%R,,8
	JT A,1F
	LET A,%R,,4
1H	XOR B,B
	LET C,%B,Z,$33
	GRTR C,%R,,16
	LET Z,A
	JT C,SPFIRE
	; Move
	LET E,W
	SMOV E,0
	LET S,0

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

; **** Gate ****
; Parameter: duration (open gate only)

	EV T,_GATE
	SFX A,"@22<<C#>C#>C#"
	LET A,_OPENGATE
	PTMK A,0
	LET A,18
1H	PTMP A,0
	LET S,0

	EV A,_OPENGATE
	GTMP A,0
	LOOP A,1B
	LET A,_GATE
	PTMK A,0
	SFX A,"@22`<<C#"
	LET S,0

; **** One Step ****
	EV T,_ONESTEP
	PACK B,0
	PICK A,1
	UNPC B,B
	DROP A,1
	SFX A,"@30Z.K3K5K7K9K11"
	LET A,_NORMAL
	PTUK A,0
	LET S,0

; **** Duplicator ****
; Parameter:
;   bit2-bit0 = Phase
;   bit5-bit3 = Duplication rate
;   bit7-bit6 = Source direction
	EV A,_DUPLICATOR
	; Timing
	INC A,1
	LSH A,%B,Z,$33
	DEC A,A
	AND A,%I,,$EC
	JNZ A,0
	; Set phase
	LET A,%B,Z,$30
	EQ A,5
	JT A,1F
	; Not time to do duplication, yet
	INC A,Z
	PTMP A,0
	LET S,0
	; Reset phase
1H	LET A,Z
	AND A,$F8
	PTMP A,0
	; Check duplication
	LET D,%B,Z,$26 ; direction
	BACK A,D
	JF A,1F
	XOR D,2
	PACK F,0
	PUSH F,$0013
	PACK F,0
	CWOT F,$11
	JF F,1F
	BACK A,D
	BACK A,D
	JF A,1F
	MTIL A,0
	EMAT H,A
	JT H,1F
	; Duplication OK
	SFX A,"@30SCDEFG"
	SINK G,F
	PTM A,F
	LET S,0
	; Duplication failed
1H	SFX A,"@30<<G#F#"
	LET S,0

; **** Explosives ****
; Parameter (lit bomb): Remaining time

	EV T,_BOMB
	SIM A,0
	XOR B,B
	PSD B,A
	LET A,_LITBOMB
	PTMK A,0
	SFX A,"@30TCF>CF>C"
	LET S,0

	EV T,_LITBOMB,1

	EV B,_LITBOMB
	DEC A,Z
	JNEG A,1F
	PTMP A,0
	JZ A,2F
	AND A,1
	PEER A,3F
	SFX A,A
	LET S,0
3H	DATA "@10T!","@10T$"
	; Clean up explosion
1H	LET Z,3F
	DIE A,W
	GOTO A,5F
	; Make explosion
2H	SFX A,"@30O7ZC<.C<C<C<C<C<C<C"
	LET Z,4F
	; Find all cells to make/clear explosion
5H	LET Y,%,Y,-5
	LET X,%,X,-3
	LET W,6
1H	CALL A,Z
	REWD D,1B
	INC Y,Y
	LET W,8
	INC X,X
1H	CALL A,Z
	REWD C,1B
	INC Y,Y
	LET W,10
	DEC X,X
1H	CALL A,Z
	REWD D,1B
	INC Y,Y
	LET W,12
	INC X,X
1H	CALL A,Z
	REWD C,1B
	INC Y,Y
	LET W,12
1H	CALL A,Z
	REWD D,1B
	INC Y,Y
	LET W,12
1H	CALL A,Z
	REWD C,1B
	INC Y,Y
	LET W,12
1H	CALL A,Z
	REWD D,1B
	INC Y,Y
	LET W,12
1H	CALL A,Z
	REWD C,1B
	INC Y,Y
	LET W,10
	INC X,X
1H	CALL A,Z
	REWD D,1B
	INC Y,Y
	LET W,8
	DEC X,X
1H	CALL A,Z
	REWD C,1B
	INC Y,Y
	LET W,6
	INC X,X
1H	CALL A,Z
	REWD D,1B
	LET S,0
	; Doing explosion per cell
4H	CALM X,0
	LET T,W
	JF A,0
	KILM A,0
	SINK A,0
	LET A,_BREAKABLE
	PTMK A,0
	LET A,%R,,7
	ADD A,9
	PTMC A,0
	LET S,0
	; Cleaning explosion per cell
3H	GTMK A,0
	XOR A,_BREAKABLE
	JNZ A,0
	PTMK A,0
	LET S,0

; **** Lasers ****
	EV B,_LASERGUN
	LET H,Z
	LSH H,8
	GTMC A,0
	ADD H,A
	LSH H,8
	ADD H,_BEAM
	; Check current beam state
	FORW A,Z
	MTIL A,0
	XOR A,H
	AND A,$0002FFFF
	JNZ A,1F
	; Beam is active; destroy beam
2H	FLOA A,0
	FORW A,Z
	JF A,0
	MTIL A,0
	XOR A,H
	AND A,$0002FFFF
	JZ A,2B
	LET S,0
	; Beam is not active
1H	GTMK B,0
	JNZ B,2F
	; Empty; add a laser beam
	PTM H,0
	FORW A,Z
	JT A,1B
	LET S,0
	; Not empty
2H	EQ B,_PLAYER
	JT B,2F
	; Check if breakable
	EATT C,B
	BTST C,8
	JF C,0
	; Destroy it
	KILM A,0
	SFX A,"@24O4DO1DO5DO3D"
	GOTO A,1B
	; Move and damage player (unless player has energy)
2H	ROB E,0
	JT E,0
	CALL D,OUCH1
	INC G,0
	INC D,Z
	SMOV G,$0083
	JT G,1B
	DEC D,Z
	SMOV G,$0083
	JT G,1B
	VSET H,0
	LET S,0

; **** Conveyor ****
; Parameter:
;   bit1-bit0 = Animation frame
;   bit7 = Clockwise
; Lock bit: Initialized
	EV B,_CONVEYOR
	LAY A,W
	BTST A,7
	JT A,1F
	; Initialize
	OR A,$80
	LOCK A,W
	LET A,X
	ADD A,Y
	MOD A,3
	PSD A,W
	LET S,1
	; Do spin
1H	LET A,%L,Z,16-7
	AND A,%L,1,16
	ADD A,$200FF
	SPIN D,A
	; Animation
	INC A,%UR,Z,6
	ADD A,Z
	AND A,$83
	PTMP A,0
	LET S,0

; **** Gun ****
; Parameter:
;   bit1-bit0 = Direction
;   but7 = Spin
; Misc1: Intelligence (0-128)
; Misc2: Firing rate (0-128)
; Misc3: Firiting type (0=bullet, 1=star)
	EV B,_GUN
	; Get direction and spin if necessary
	LET A,Z
	BTST A,7
	TINC A,Z
	AND A,$83
	PTMP A,0
	; Check if it is ready to shoot
	GM2 B,W
	GRTR B,%R,,128
	JF B,0
	; Check intelligence
	GM1 B,W
	GRTR B,%R,,128
	JF B,1F
	; Only shoot toward player
	LET C,%XP,X,0
	LET D,%YP,Y,0
	BTST A,0
	LET E,%OF,C,D
	ABS F,%OT,C,D
	GRTR F,2
	JT F,0
	INC F,A
	BTST F,1
	MUL E,%OF,1,-1
	JNEG E,0
	; Do shoot
1H	GM3 B,W
	CASE B,1F
1H	DATA 1F,2F,0,0
1H	LET Z,A
	GOTO A,SHOOT
2H	FORW A,0
	LET B,127
	CALL A,THSTAR
	LET S,0

; **** Destroyable objects ****
	EV X,_EMPTY,1
	EV X,_BREAKABLE,1
	EV X,_BULLET,1
	EV X,_STAR,1
	EV X,_LANDMINE,1
	EV X,_ROCK,1
	EV X,_RUNNER,1
	EV X,_LION,1
	EV X,_TIGER,1
	EV X,_BEAR,1
	EV X,_SHARK,1
	EV X,_HEAD,1
	EV X,_SEGMENT,1
	EV X,_MOUSE,1
	EV X,_SNAKE,1
	EV X,_SPIDER,1
	EV X,_BIRD,1
	EV X,_LUMBERJACK,1

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
	; THSTAR: B=duration, XY=location; clobbers BCD
THSTAR	GTMK C,0
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

	; #RETURN
	TA $C5
	LET A,Z
	POKE A,NWAITS
	LET S,Z

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
	ED 'H',"Heart",_HEART,$0304
	ED 'K',"Key",_KEY,$0000
	ED 'D',"Door",_DOOR,$040F
	ED 'Z',"Stone",_STONE,$0000
	ED 'E',"Energizer",_ENERGIZER,$0305
	ED 'P',"Potion",_POTION,$0200
	ED 'S',"Scroll",_SCROLL,$0B0F
	ED 'Q',"Checkpoint",_CHECKPOINT,$0309
	ED 'C',"Chest",_CHEST,$0106
	ED 'U',"Pouch",_POUCH,$0000
	ED 1,"Explosives:"
	ED 'B',"Bomb",E_BOMB,_BOMB+$8200
	ED 'L',"Lit Bomb",E_BOMB,_LITBOMB+$8200
	ED 2

	ED1 2
	ED 1,"Creatures:"
	ED 'L',"Lion",E_LION,_LION+$8600
	ED 'T',"Tiger",E_LION,_TIGER+$8600
	ED 'B',"Bear",E_LION,_BEAR+$8600
	ED 'K',"Shark",E_LION,_SHARK+$8600
	ED 'R',"Runner",E_RUNN,$8200
	ED 'V',"Slime",_SLIME,$0000
	ED 'M',"Mouse",E_LION,_MOUSE+$8600
	ED 'S',"Snake",E_SNAK,$8600
	ED 'P',"Spider",E_LION,_SPIDER+$8600
	ED 'I',"Bird",E_LION,_BIRD+$8600
	ED 'J',"Lumberjack",E_LION,_LUMBERJACK+$8600
	ED 'D',"Dragon",E_LION,_DRAGON+$8600
	ED 1,"Centipedes:"
	ED '1',"Head",E_CENT,_HEAD+$8200
	ED '2',"Segment",E_CENT,_SEGMENT+$8200
	ED 2

	ED1 3
	ED 1,"Terrains:"
	ED 'W',"Water",_WATER,$0319
	ED 'F',"Forest",_FOREST,$0320
	ED 'I',"Ice",_ICE,$0331
	ED 'X',"Web",_WEB,$0000
	ED 'Y',"Tree",_TREE,$030A
	ED 'R',"Fire",_FIRE,$034E
	ED 1,"Walls:"
	ED 'S',"Solid",_SOLID,$0000
	ED 'N',"Normal",_NORMAL,$0000
	ED 'L',"Line",_LINE,$0000
	ED 'B',"Breakable",_BREAKABLE,$0000
	ED 'T',"Text",_TEXT,$0000
	ED 'V',"Invisible",_INVISIBLE,$0000
	ED 1,"Floors:"
	ED 'E',"Empty",_EMPTY,$0300
	ED 'O',"Floor",_FLOOR,$0000
	ED 'K',"Fake",_FAKE,$0000
	ED 2

	ED1 4
	ED 1,"Puzzles:"
	ED 'O',"Rock",_ROCK,$0307
	ED '0',"Boulder",_BOULDER,$0000
	ED '1',"Slider \x12",_SLIDERNS,$0000
	ED '2',"Slider \x1D",_SLIDEREW,$0000
	ED '3',"Pusher",E_PUSH,_PUSHER+$8200
	ED '4',"Indirect Push",_INDIRECTPUSH,$0000
	ED 1,"Miscellaneous:"
	ED 'T',"Transporter",_TRANSPORTER,$0000
	ED 'R',"Ricochet",_RICOCHET,$030A
	ED 'K',"Spike",_SPIKE,$0000
	ED 'G',"Gate",_GATE,$0000
	ED 'Q',"One Step",_ONESTEP,$0000
	ED 'X',"Land Mine",_LANDMINE,$0000
	ED 'D',"Duplicator",_DUPLICATOR,$030F
	ED 'P',"Passage",_PASSAGE,$040F
	ED 'C',"Conveyor",E_CONV,_CONVEYOR+$8200
	ED 2

	ED1 5
	ED 1,"Guns:"
	ED 'G',"Gun",_GUN,$0800
	ED 'L',"Laser Gun",_LASERGUN,$0800
	ED 1,"Projectiles/Beams:"
	ED 'B',"Bullet",_BULLET+$0200,$010F
	ED 'S',"Star",_STAR+$0200,$010F
	ED 'F',"Fire",_SPITFIRE+$0200,$010C
	ED 'M',"Beam",_BEAM,$0000
	ED 1,"Special:"
	ED 'E',"Empty",_EMPTY,$0300
	ED 'Z',"Player",_PLAYER+$0100,$031F
	ED 'O',"Object",_OBJECT,$0800
	ED 'X',"Sensor",_SENSOR,$0800
	ED 2

	ED1 6
	ED 0,_TEXT,$1000

	ED1 7
	ED 1,"Reveal:"
	ED 'I',"Invisible walls",RL_INV,$C000
	ED 'S',"Stats",RL_STA,$C000
	ED 'U',"Under layer",RL_UND,$C000
	ED 'L',"Under layer (only)",RL_UNO,$C000
	ED 2

; **** Parameter edit ****

	ED0 _TEXT,$0100
	ED0 _SPIKE,$0100

	ED0 _OBJECT
	ED0 _SENSOR
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
	ED2 'O',"~Avalanche",7
	ED2 'O',"~Destroy Creatures",8
	ED2 'O',"Expl~ode Bombs",9
	ED 0

E_LION	ED3 _LION,$0C,_TIGER,$0B,_BEAR,$06,_SHARK,$07,_MOUSE,$0F,_LUMBERJACK,$0A,_SPIDER,$07,_BIRD,$0E,_DRAGON,$0C
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
	ED0 _MOUSE
	ED0 _LUMBERJACK
	ED0 _SPIDER
	ED0 _BIRD
	ED 1,$0008
	ED 'H',"Creature"
	ED2 'N',"~Intelligence: ",$0030,0,15
	ED 0

	ED0 _TIGER
	ED 1,$0088
	ED 'H',"Creature"
	ED2 'N',"~Intelligence: ",$0030,0,15
	ED2 'N',"~Firing rate: ",$0034,0,15
	;ED2 'B',"~Stars",$2206
	ED 0

	ED0 _BEAR
	ED 1,$0006
	ED 'H',"Bear"
	ED2 'N',"~Range: ",$0030,0,255
	ED 0

	ED0 _TRANSPORTER
	ED0 _PUSHER
	ED0 _RUNNER
	ED0 _SPITFIRE
	ED0 _BEAM
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

E_SNAK	ED '=',"-MP2.T"
	ED '@',"_1",1
	ED 'P',_SNAKE,$5800
	ED 0

	ED0 _SNAKE
	ED 'H',"Direction:"
	ED 'O',$0010
	ED2 'O',"~East",0
	ED2 'O',"~North",1
	ED2 'O',"~West",2
	ED2 'O',"~South",3
	ED 'H',0
	ED2 'N',"~Intelligence: ",$0034,0,15
	ED 0

	ED0 _CHEST
	ED 'H',"Contents of chest:"
	ED 'O',$0034
	ED2 'O',"~Nothing",0
	ED2 'O',"~Ammo (x5)",1
	ED2 'O',"~Money (x5)",2
	ED2 'O',"~Gems (x5)",3
	ED2 'O',"~Stones",4
	ED2 'O',"~Torches",5
	ED2 'O',"T~rap",6
	ED 'H',0
	ED2 'N',"Amo~unt: ",$0030,0,15
	ED 0

E_PUSH	ED '=',"K-MP"
	ED '@',"_4",4
	ED 'P',$FFFF,$4800
	ED 0

	ED0 _POUCH
	ED 'H',"Contents of pouch:"
	ED2 'N',"~Gems: 5x",$0030,0,15
	ED2 'N',"~Money: 5x",$0034,0,15
	ED 0

	ED0 _DRAGON
	ED 'H',"Dragon"
	ED2 'N',"~Hit points: ",$0020,0,7
	ED2 'N',"~Firing rate: ",$0023,0,7
	ED2 'B',"~Random movement",$0007
	ED 0

	ED0 _DUPLICATOR
	ED 'H',"Duplicator"
	ED2 'N',"~Phase: ",$0020,0,5
	ED2 'N',"~Duplication rate: ",$0023,0,7
	ED 'H',"Source direction:"
	ED 'O',$0016
	ED2 'O',"~East",0
	ED2 'O',"~North",1
	ED2 'O',"~West",2
	ED2 'O',"~South",3
	ED 0

E_BOMB	ED '=',"K9.M"
	ED '@',"_6",6
	ED 'P',$FFFF,$4800
	ED 0

	ED0 _BOMB
	ED0 _LITBOMB
	ED 'H',"Bomb"
	ED2 'N',"~Time: ",$0030,0,9
	ED 0

	ED0 _LASERGUN
	ED 'H',"Direction:"
	ED 'O',$0010
	ED2 'O',"~East",0
	ED2 'O',"~North",1
	ED2 'O',"~West",2
	ED2 'O',"~South",3
	ED 'H',0
	ED2 'N',"Spee~d: ",$1070,0,255
	ED2 'N',"~Phase: ",$2070,0,255
	ED 0

	ED0 _GUN
	ED 'H',"Direction:"
	ED 'O',$0010
	ED2 'O',"~East",0
	ED2 'O',"~North",1
	ED2 'O',"~West",2
	ED2 'O',"~South",3
	ED 'H',"Firing type:"
	ED 'O',$1310
	ED2 'O',"~Bullet",0
	ED2 'O',"S~tar",1
	;ED2 'O',"Fir~e",2
	ED 'H',0
	ED2 'N',"Spee~d: ",$1070,0,255
	ED2 'N',"~Intelligence: ",$1170,0,128
	ED2 'N',"~Firing rate: ",$1270,0,128
	ED2 'B',"S~pin",$0007
	ED 0

E_CONV	ED '=',"-MP"
	ED '@',"_3",3
	ED 'P',_CONVEYOR,$5800
	ED 0

	ED0 _CONVEYOR
	ED 'H',"Conveyor:"
	ED 'O',$0007
	ED2 'O',"Clock~wise",1
	ED2 'O',"Countercloc~kwise",0
	ED 0

; **** Revealing lists ****

RL_INV	ED _INVISIBLE+$2100,$00B0
	ED 0

RL_STA	ED $C002,$8800
	ED 0

RL_UND	ED $D100,$0000
	ED $1001,$00B1
	ED 0

RL_UNO	ED $0100,$07F9
	ED $1001,$00B1
	ED 0

; **** Editor board info ****
	ED1 32,"Restart if zapped"
	ED1 36,"Time limit: "
	ED1 37,"Max shots: "

	; Text editor
	ED1 64,60

	; New board
	ED1 65
	ED 2,60,25,2,0
	ED ':',"bi s0 u0 ="
	ED 'S',1
	ED4 A,$FFFF
	ED5 A,1,$1170
	ED4 A,0
	ED5 A,1,$1070
	ED5 A,1,$1270
	ED5 A,1,$1370
	ED 'S',2
	ED5 A,1,$1170
	ED5 A,1,$1270
	ED5 A,1,$1370
	ED4 A,1
	ED5 A,1,$1070
	ED ':',"%/b p <0E>Normal<00>"
	ED 'V',$80,$08,$B1
	ED 0

