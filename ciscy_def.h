
/*
INSTRUCTION STRUCTURE
	
	VV -- Opcode Type (mov, iadd,fadd, )
	00 (00)

	the (00) is used like this:
	~~~~~~~~~~~~~~~~~~~~~~~~~~~
	REGISTER-TO-REGISTER MOV INSTRUCTIONS, MATH INSNS
	(00)
	 ^^ -- affected registers. destination, source.
	(this is the same for all instructions which use 2 register operands.)
	~~~~~~~~~~~~~~~~~~~~~~~~~~~
	IMMEDIATE LOAD 64 BIT VALUE, DIRECT LOAD/STORE TO ADDRESS
	       V -- value
	(00)   00 00 00 00 00 00 00 00
	  ^ -- affected register.
  ~~~~~~~~~~~~~~~~~~~~~~~~~~~
  	IMMEDIATE LOAD 16 BIT VALUE
  	       V -- value
  	(00)   00 00
  	  ^ -- affected register.
	~~~~~~~~~~~~~~~~~~~~~~~~~~~
	INDIRECT LOAD 
	(00)
	 ^^
	 ||
	 |specifies addressing register.
	 |
	 Specifies destination register.
	~~~~~~~~~~~~~~~~~~~~~~~~~~~
	JUMP IMMEDIATE
	(00)
	
*/

static inline u16 ciscy_rom_fetch(ciscy_computer* c){
	return c->rom[(c->pc++) & CISCY_ROM_MASK];
}

static inline u64 ciscy_rom_fetch_u64(ciscy_computer* c){
	uint64_t f;
	f = ciscy_rom_fetch(c);
	f <<= 16;f |= ciscy_rom_fetch(c);
	f <<= 16;f |= ciscy_rom_fetch(c);
	f <<= 16;f |= ciscy_rom_fetch(c);
	return f;
}


/*register to register move.*/
static inline void ciscy_mov(uint8_t bt, ciscy_computer* c){
	c->reg[bt>>4] = c->reg[bt&0xf];
}


/*Get the stack pointer.*/
static inline void ciscy_getstp(uint8_t bt, ciscy_computer* c){
	c->reg[bt&0xf].u = c->stp;
}

static inline void ciscy_zreg(uint8_t bt, ciscy_computer* c){
	c->reg[bt&0xf].u = 0;
}

static inline void ciscy_immediate_load(uint8_t bt, uint64_t value, ciscy_computer* c){
	c->reg[bt&0xf].u = value;
}

static inline void ciscy_direct_load(uint8_t bt, uint64_t value, ciscy_computer* c){
	c->reg[bt&0xf].u = c->mem[value & CISCY_MASK];
}

static inline void ciscy_direct_store(uint8_t bt, uint64_t value, ciscy_computer* c){
	c->mem[value & CISCY_MASK] = c->reg[bt&0xf].u;
}

static inline void ciscy_ist(uint8_t bt, ciscy_computer* c){
	c->mem[c->reg[bt>>4].u & CISCY_MASK] = c->reg[bt&0xf].u;
}

static inline void ciscy_ild(uint8_t bt, ciscy_computer* c){
	c->reg[bt>>4].u = c->mem[c->reg[bt&0xf].u & CISCY_MASK];
}


/*
type will be an integer constant.
*/
static inline void ciscy_int_math(uint8_t bt, const uint8_t type, ciscy_computer* c){
	switch(type & 7){
		default: c->flags |= 1; break;
		case 0: break;
		case 1: c->reg[bt>>4].u += c->reg[bt&0xf].u; break;
		case 2: c->reg[bt>>4].u -= c->reg[bt&0xf].u; break;
		case 3: c->reg[bt>>4].u *= c->reg[bt&0xf].u; break;
		case 4: if(c->reg[bt&0xf].u) {c->reg[bt>>4].u /= c->reg[bt&0xf].u;} else {c->flags |= 2;} break;
		case 5: if(c->reg[bt&0xf].u) {c->reg[bt>>4].u %= c->reg[bt&0xf].u;} else {c->flags |= 2;} break;
		case 6: if(c->reg[bt&0xf].i) {c->reg[bt>>4].i /= c->reg[bt&0xf].i;} else {c->flags |= 2;} break;
		case 7: if(c->reg[bt&0xf].i) {c->reg[bt>>4].i %= c->reg[bt&0xf].i;} else {c->flags |= 2;} break;
		case 8: c->reg[bt>>4].u <<= c->reg[bt&0xf].u; break;
		case 9: c->reg[bt>>4].u >>= c->reg[bt&0xf].u; break;
		case 10: c->reg[bt>>4].u |= c->reg[bt&0xf].u; break;
		case 11: c->reg[bt>>4].u &= c->reg[bt&0xf].u; break;
		case 12: c->reg[bt>>4].u ^= c->reg[bt&0xf].u; break;
		case 13: c->reg[bt>>4].u = ~c->reg[bt&0xf].u; break;
		case 14: c->reg[bt>>4].i = -c->reg[bt&0xf].i; break;
	}
}

/*
type will be an integer constant. Constant propagation makes life easy for the optimizer.
*/
static inline void ciscy_float_math(uint8_t bt, const uint8_t type, ciscy_computer* c){
	switch(type){
		default: c->flags |= 1; break;
		case 0: break;
		case 1: c->reg[bt>>4].f += c->reg[bt&0xf].f; break;
		case 2: c->reg[bt>>4].f -= c->reg[bt&0xf].f; break;
		case 3: c->reg[bt>>4].f *= c->reg[bt&0xf].f; break;
		case 4: 
			if(c->reg[bt&0xf].f != 0.0 && c->reg[bt&0xf].f != -0.0)
				{c->reg[bt>>4].f /= c->reg[bt&0xf].f;} else {c->flags |= 2;}
		case 5: c->reg[bt>>4].f = c->reg[bt&0xf].i; break;
		case 6: c->reg[bt>>4].i = c->reg[bt&0xf].f; break;
		break;
	}
}

static inline void ciscy_getflags(uint8_t bt, ciscy_computer* c){
	c->reg[bt&0xf].u = c->flags;
}

static inline void ciscy_pushreg(uint8_t bt, ciscy_computer* c){
	c->mem[c->stp++ & CISCY_MASK] = c->reg[bt&0xf].u;
}

static inline void ciscy_popreg(uint8_t bt, ciscy_computer* c){
	c->reg[bt&0xf].u = c->mem[(c->stp-1) & CISCY_MASK];c->stp--;
}

static inline void ciscy_call(u64 new_pc, ciscy_computer* c){
	c->cstack[c->cstp++ & CISCY_STACK_MASK] = c->pc;
	c->pc = new_pc;
}

static inline void ciscy_ret(ciscy_computer* c){
	c->pc = c->cstack[(c->cstp-1) & CISCY_STACK_MASK];c->cstp--;
}


static inline void ciscy_clearflags(ciscy_computer* c){
	c->flags = 0;
}

static inline void ciscy_branch(uint8_t bt, uint64_t dest, ciscy_computer* c){
	/*lt*/
	if((bt & 1) && (c->flags & 4)) c->pc = dest;
	/*eq*/
	if((bt & 2) && (c->flags & 8)) c->pc = dest;
	/*gt*/
	if((bt & 4) && (c->flags & 16)) c->pc = dest;
	/*unconditional*/
	if(bt & 8) c->pc = dest;

	/*notlt*/
	if((bt & 16) && ((c->flags & 4) == 0)) c->pc = dest;
	/*noteq*/
	if((bt & 64) && ((c->flags & 8) == 0)) c->pc = dest;
	/*notgt*/
	if((bt & 64) && ((c->flags & 16) == 0)) c->pc = dest;
}

static inline void ciscy_cmp(uint8_t bt, uint8_t type, ciscy_computer* c){
	switch(type){
		default: c->flags |= 1; break;
		case 0: break;
		case 1: 
			if(
				c->reg[bt>>4].u < c->reg[bt&0xf].u
			) c->flags |= 4; else c->flags &= ~((uint64_t)4);
			if(
				c->reg[bt>>4].u == c->reg[bt&0xf].u
			) c->flags |= 8; else c->flags &= ~((uint64_t)8);
			if(
				c->reg[bt>>4].u > c->reg[bt&0xf].u
			) c->flags |= 16;  else c->flags &= ~((uint64_t)16);
		break;

		case 2: 
			if(
				c->reg[bt>>4].i < c->reg[bt&0xf].i
			) c->flags |= 4; else c->flags &= ~((uint64_t)4);
			if(
				c->reg[bt>>4].i == c->reg[bt&0xf].i
			) c->flags |= 8; else c->flags &= ~((uint64_t)8);
			if(
				c->reg[bt>>4].i > c->reg[bt&0xf].i
			) c->flags |= 16; else c->flags &= ~((uint64_t)16);
		break;

		case 3: 
			if(
				c->reg[bt>>4].f < c->reg[bt&0xf].f
			) c->flags |= 4; else c->flags &= ~((uint64_t)4);
			if(
				c->reg[bt>>4].f == c->reg[bt&0xf].f
			) c->flags |= 8; else c->flags &= ~((uint64_t)8);
			if(
				c->reg[bt>>4].f > c->reg[bt&0xf].f
			) c->flags |= 16; else c->flags &= ~((uint64_t)16);
		break;
	}
}



#define really_dispatch() fetched_opcode = ciscy_rom_fetch(c); switch(fetched_opcode>>8){\
	default: goto DO_HALT       ;\
	case 0:  goto NO_OP         ;\
	case 1:  goto REG_TO_REG_MOV;\
	case 2:  goto ZREG          ;\
	case 3:  goto DIRECT_LOAD   ;\
	case 4:  goto DIRECT_STORE  ;\
	case 5:  goto ILD_64        ;\
	case 6:  goto IST_64        ;\
	case 7:  goto IADD          ;\
	case 8:  goto ISUB          ;\
	case 9:  goto IMUL          ;\
	case 10: goto UDIV          ;\
	case 11: goto UMOD          ;\
	case 12: goto IDIV          ;\
	case 13: goto IMOD          ;\
	case 14: goto SHL           ;\
	case 15: goto SHR           ;\
	case 16: goto DO_OR         ;\
	case 17: goto DO_AND        ;\
	case 18: goto DO_XOR        ;\
	case 19: goto DO_COMPL      ;\
	case 20: goto DO_NEG        ;\
	case 21: goto FADD          ;\
	case 22: goto FSUB          ;\
	case 23: goto FMUL          ;\
	case 24: goto FDIV          ;\
	case 25: goto ITOF          ;\
	case 26: goto FTOI          ;\
	case 27: goto UCMP          ;\
	case 28: goto ICMP          ;\
	case 29: goto FCMP          ;\
	case 30: goto GETFLAGS      ;\
	case 31: goto CLEARFLAGS    ;\
	case 64: goto BRANCH        ;\
	case 33: goto CALL          ;\
	case 34: goto RET           ;\
	case 35: goto PUSHREG       ;\
	case 36: goto POPREG        ;\
	case 37: goto GETSTP        ;\
	case 38: goto INTERRUPT     ;\
	case 39: goto IMMEDIATE     ;\
	case 40: goto DO_HALT       ;\
};

#ifndef USE_COMPUTED_GOTO
#ifdef USE_WHILE_LOOP
#define dispatch() goto end;
#else
#define dispatch() really_dispatch()
#endif


#else
/*Computed goto.*/
#define dispatch()  fetched_opcode = ciscy_rom_fetch(c); goto *table[fetched_opcode>>8];
#endif
static inline void ciscy_emulate(ciscy_computer* c){
#ifdef USE_COMPUTED_GOTO
	static const void* table[256] = {
	&&NO_OP         ,
	&&REG_TO_REG_MOV,
	&&ZREG          ,
	&&DIRECT_LOAD   ,
	&&DIRECT_STORE  ,
	&&ILD_64        ,
	&&IST_64        ,
	&&IADD          ,
	&&ISUB          ,
	&&IMUL          ,
	&&UDIV          ,
	&&UMOD          ,
	&&IDIV          ,
	&&IMOD          ,
	&&SHL           ,
	&&SHR           ,
	&&DO_OR         ,
	&&DO_AND        ,
	&&DO_XOR        ,
	&&DO_COMPL      ,
	&&DO_NEG        ,
	&&FADD          ,
	&&FSUB          ,
	&&FMUL          ,
	&&FDIV          ,
	&&ITOF          ,
	&&FTOI          ,
	&&UCMP          ,
	&&ICMP          ,
	&&FCMP          ,
	&&GETFLAGS      ,
	&&CLEARFLAGS    ,
	&&BRANCH        ,
	&&CALL          ,
	&&RET           ,
	&&PUSHREG       ,
	&&POPREG        ,
	&&GETSTP        ,
	&&INTERRUPT     ,
	&&IMMEDIATE     ,
	&&DO_HALT       ,
	/*TODO- privileges, interrupt vectors.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT, /*some halts.*/
	&&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT,       &&DO_HALT,&&DO_HALT,&&DO_HALT,&&DO_HALT
};
#endif
	uint16_t fetched_opcode;
	if(sizeof(float) != 4 ||
		sizeof(uint64_t) != 4 ||
		sizeof(int64_t) != 4 ||
		sizeof(uif64) != 4
	) {
			abort();
	}
#ifdef USE_WHILE_LOOP
	while(1){
#endif

#ifdef USE_WHILE_LOOP
	really_dispatch();
#endif
		NO_OP:/*This is the part where we do absolutely, positively, nothing!*/			dispatch();
		REG_TO_REG_MOV: ciscy_mov(fetched_opcode, c); 									dispatch();
		ZREG: ciscy_mov(fetched_opcode, c); 											dispatch();
		DIRECT_LOAD: ciscy_direct_load(fetched_opcode, ciscy_rom_fetch_u64(c), c); 		dispatch();
		DIRECT_STORE: ciscy_direct_store(fetched_opcode, ciscy_rom_fetch_u64(c), c); 	dispatch();
		ILD_64: ciscy_ild(fetched_opcode, c); 											dispatch();
		IST_64: ciscy_ist(fetched_opcode, c); 											dispatch();
		IADD: ciscy_int_math(fetched_opcode,1,c); 										dispatch();
		ISUB: ciscy_int_math(fetched_opcode,2,c); 										dispatch();
		IMUL: ciscy_int_math(fetched_opcode,3,c); 										dispatch();
		UDIV: ciscy_int_math(fetched_opcode,4,c); 										dispatch();
		UMOD: ciscy_int_math(fetched_opcode,5,c); 										dispatch();
		IDIV: ciscy_int_math(fetched_opcode,6,c); 										dispatch();
		IMOD: ciscy_int_math(fetched_opcode,7,c); 										dispatch();
		SHL:  ciscy_int_math(fetched_opcode,8,c); 										dispatch();
		SHR:  ciscy_int_math(fetched_opcode,9,c); 										dispatch();
		DO_OR:  ciscy_int_math(fetched_opcode,10,c);									dispatch();
		DO_AND:  ciscy_int_math(fetched_opcode,11,c);									dispatch();
		DO_XOR:  ciscy_int_math(fetched_opcode,12,c);									dispatch();
		DO_COMPL:  ciscy_int_math(fetched_opcode,13,c);									dispatch();
		DO_NEG:  ciscy_int_math(fetched_opcode,14,c);									dispatch();
		FADD: ciscy_float_math(fetched_opcode,1,c); 									dispatch();
		FSUB: ciscy_float_math(fetched_opcode,2,c); 									dispatch();
		FMUL: ciscy_float_math(fetched_opcode,3,c); 									dispatch();
		FDIV: ciscy_float_math(fetched_opcode,4,c); 									dispatch();
		ITOF: ciscy_float_math(fetched_opcode,5,c); 									dispatch();
		FTOI: ciscy_float_math(fetched_opcode,6,c); 									dispatch();
		UCMP: ciscy_cmp(fetched_opcode, 1, c);											dispatch();
		ICMP: ciscy_cmp(fetched_opcode, 2, c);											dispatch();
		FCMP: ciscy_cmp(fetched_opcode, 3, c);											dispatch();
		GETFLAGS: ciscy_getflags(fetched_opcode, c);									dispatch();
		CLEARFLAGS: ciscy_clearflags(c);												dispatch();
		BRANCH: ciscy_branch(fetched_opcode,ciscy_rom_fetch_u64(c),c);					dispatch();
		CALL: ciscy_call(ciscy_rom_fetch_u64(c), c);									dispatch();
		RET: ciscy_ret(c);																dispatch();
		PUSHREG: ciscy_pushreg(fetched_opcode, c);										dispatch();
		POPREG: ciscy_popreg(fetched_opcode, c);										dispatch();
		GETSTP: ciscy_getstp(fetched_opcode, c);										dispatch();
		INTERRUPT: ciscy_interrupt(fetched_opcode, c);									dispatch();
		IMMEDIATE: ciscy_immediate_load(fetched_opcode,ciscy_rom_fetch_u64(c),c);		dispatch();
		DO_HALT:																		return; /*TODO: make this drop privilege.*/
		/*
			TODO: add more insns.
		*/
		
#ifdef USE_WHILE_LOOP
	end:;}
#endif
}




