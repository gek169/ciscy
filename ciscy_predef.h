#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <stdlib.h>



#ifndef CISCY_MEGS
#define CISCY_MEGS 256
#endif

#ifndef CISCY_ROM_MEGS
#define CISCY_ROM_MEGS 128
#endif

/*The call stack.*/
#ifndef CISCY_STACK_MEGS
#define CISCY_STACK_MEGS 128
#endif

static const uint64_t CISCY_ROM_MASK = ((CISCY_ROM_MEGS  * 1024 * 512)-1);
static const uint64_t CISCY_MASK = ((CISCY_MEGS  * 1024 * 256)-1);
static const uint64_t CISCY_STACK_MASK = ((CISCY_STACK_MEGS  * 1024 * 256)-1);
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint64_t u64;
typedef int64_t i64;

typedef union{
	uint64_t u;
	int64_t i;
	double f;
} uif64;



typedef struct ciscy_computer{
	uint64_t mem[CISCY_MEGS * 1024 * 128];
	uint64_t cstack[CISCY_STACK_MEGS * 1024 * 128];
	uint16_t rom[CISCY_ROM_MEGS  * 1024 * 512]; /*executable code segment.*/
	uif64 reg[16]; /*general purpose registers.*/
	uint64_t pc; 
	uint64_t stp; 
	uint64_t cstp; 
	uint64_t flags;
	/*
		FLAGS:
		1: illegal opcode
		2: math error
		4: less than
		8: equal
		16: greater than
	*/
} ciscy_computer;
