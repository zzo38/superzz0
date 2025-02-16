
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
	WOKE F,$E0
	SFX A,"@8Z.<EC#C<EC#C"
	LET S,0
	; Movement OK; check if standing on food
1H	SIXY H,1
	GTUK C,H
	JZ C,1F
	; Found food
	SFX A,"@8Z.K4K5K6K8K10"
	GIVE S,1
	PTUK F,H
	; Add tail segment
	MTIL D,H
	ADD D,$01000001
	PTM D,G
	; Modify head colour
	GTUC D,H
	PTMC D,H
	; Place another food
2H	LET X,%R,,78
	LET Y,%R,,23
	GTMK D,0
	JNZ D,2B
	PTMK C,0
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
