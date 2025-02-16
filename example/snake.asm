
; Keyboard event; change direction when key is pushed.
; The game will start when a key is pushed.
	TA $E0
	LET A,1F
	LET B,2F
	POKE A,$E0
	POKE B,$E1
1H	LET A,Z
	JNEG A,0
	LET B,A
	LET S,0

; Frame event
2H	LET A,1
	SIXY G,1
	SMOV A,1
	JT A,1F
	; Game over
	LET A,0
	POKE A,$E0
	POKE A,$E1
	SFX A,"@8Z.<EC#C<EC#C"
	LET S,0
	; Movement OK; check if standing on food
1H	LET X,%XP,,0
	LET Y,%YP,,0
	GTUK C,0
	JZ C,1F
	; Found food
	SFX A,"@8Z.K4K5K6K8K10"
	GIVE S,1
	PTUK F,0
	; Add tail segment
	MTIL D,0
	BACK B,B
	ADD D,$01000001
	PTM D,0
	; Modify head colour
	FORW B,B
	GTUC D,0
	PTMC D,0
	; Place another food
2H	LET X,%R,,78
	LET Y,%R,,23
	GTMK D,0
	JNZ D,2B
	LET D,_FOOD
	PTMK D,0
	INC D,%R,,15
	PTMC D,0
	LET S,0
	; No food; move tail
1H	LAST A,2
1H	JF A,0
	SIXY H,A
	TELE A,G
	LET G,H
	SIP A,A
	GOTO A,1B
