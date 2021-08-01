#include "ciscy_predef.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
/*
	implement whatever you want here! implement a device driver.
*/

static inline void ciscy_interrupt(uint8_t bt, ciscy_computer* c){
	switch(c->reg[bt>>4].u) {
		default: return;
		case 0:
			if(c->flags & 1) printf("\r\nIllegal instruction executed?\r\n");
			if(c->flags & 2) printf("\r\nMath Error.\r\n");
		break;
		case 1: putchar(c->reg[bt&0xf].u);			break;
		case 2: c->reg[bt&0xf].u = getchar();		break;
	}
}

#include "ciscy_def.h"

#include "stringutil.h"

/*
~~~~~~~~~~~~~~~~~~~~~~~~~~
~~Emulator and Assembler~~
~~~~~~~~~~~~~~~~~~~~~~~~~~
*/


#define MAX_MACROS 0xFF00000
#define MAX_LABELS 0xFF00000

char** label_names = NULL; /*Labels are created on the first pass and replaced on the second.*/
uint64_t* label_decls = NULL;
uint64_t nmacros = 0;
uint64_t nlabels = 0;
uint64_t output_counter = 0;
uint8_t current_pass = 0;
uint8_t current_insn = 0;
uint8_t current_parsed_register = 0;
static uint64_t current_ident = 0;

static char* infilename = NULL;
static FILE* ifile = NULL;
static char* entire_input_file;
static unsigned long entire_input_file_len = 0;
strll tokenized = {0};
static ciscy_computer c;

enum token_ident {
	TOKEN_UNKNOWN,
	TOKEN_SPACE_CHARS,
	TOKEN_COMMENT,
	TOKEN_DIRECTIVE,
	TOKEN_NORMAL,
	TOKEN_UNUSUAL_SINGLE_CHAR,
	TOKEN_MACRODECL_BEGIN,
	TOKEN_MACRODECL_END,
	TOKEN_STRING_CONSTANT,
	TOKEN_CHAR_LITERAL,
	TOKEN_NEWLINE,
	TOKEN_LABEL_DECLARATION
};

static long strll_len(strll* head){
	long len = 1;
	if(!head) return 0;
	while(head->right) {head = head->right; len++;}
	return len;
}

static void strll_remove_right(strll* f){
	strll* right_old = f->right;
	if(right_old == NULL) return;
	f->right = f->right->right;
	if(right_old->text) free(right_old->text);
	free(right_old);
	return;
}

static char isUnusual(char x){
	if(!isalnum(x) && x != '_' && x != '.') /*period must also be included to allow for floating point number declarations.*/
		return 1;
	return 0;
}

static inline void write_short(uint16_t s){
	c.rom[output_counter++ & CISCY_ROM_MASK] = s;
}

static inline void write_u64(uint64_t s){
	write_short(s>>48);
	write_short(s>>32);
	write_short(s>>16);
	write_short(s);
}

static inline long strll_count_until_identification(strll* c, unsigned long identification, char* match_text, char* prefx_text){
	/*
		if match_text is not null, then we attempt to match the text. Otherwise, just match identification.
	*/
	long count = 0;
	for(;c != NULL; c = c->right, count++)
	{
		if(c->identification == identification){
			if(match_text == NULL && prefx_text == NULL) return count;
			if(c->text && streq    (match_text, c->text)) return count;
			if(c->text && strprefix(prefx_text, c->text)) return count;
		}
	}
	return -1; /*We cannot find the requested token. This could be an error*/
}

static inline strll* macro_get_arg_x(strll* invocation_arg_list, unsigned long argx){
	strll* retval =  invocation_arg_list;
	if(argx == 0 
		&& 	invocation_arg_list->identification != TOKEN_NEWLINE
		&& !(retval->identification == TOKEN_UNUSUAL_SINGLE_CHAR && retval->text && retval->text[0] == ',')
		) return invocation_arg_list; /*Arguments to satisfy!*/
	if(
		argx == 0
		&& (
				invocation_arg_list->identification == TOKEN_NEWLINE
				||
				(retval->identification == TOKEN_UNUSUAL_SINGLE_CHAR && retval->text && retval->text[0] == ',')
			)
		) return NULL;
	
	for(unsigned long i = 0; i < argx; i++){
		for(;; retval = retval->right)
		{
			if(retval == NULL) return NULL;
			if(retval->identification == TOKEN_NEWLINE) return NULL;
			if(retval->identification == TOKEN_UNUSUAL_SINGLE_CHAR && retval->text && retval->text[0] == ',') {
				retval = retval->right;
				if(retval == NULL 
					|| retval->identification == TOKEN_NEWLINE 
					|| (retval->identification == TOKEN_UNUSUAL_SINGLE_CHAR && retval->text && retval->text[0] == ',')
				) return NULL;
				break;
			}
		}
	}
	return retval;
}

static inline void strll_show(strll* current, long lvl){
	{long i; 
		for(;current != NULL; current = current->right){
			if(current->text){
				for(i = 0; i < lvl; i++) printf("\t");
				if(current->identification == TOKEN_UNKNOWN	){
					printf("<UNK>%s</UNK>\n",current->text);
				} else if(current->identification == TOKEN_SPACE_CHARS	){
					printf("<SPACE/>\n");
				} else if(current->identification == TOKEN_COMMENT	){
					printf("<COMMENT/>\n");
				} else if(current->identification == TOKEN_NEWLINE	){
					printf("<NEWLINE/>\n");
				} else if(current->identification == TOKEN_DIRECTIVE){
					printf("<DIR>%s</DIR>\n",current->text);
				} else if(current->identification == TOKEN_LABEL_DECLARATION){
					printf("<LBL>%s</LBL>\n",current->text);
				}else if(current->identification == TOKEN_NORMAL){
					printf("<NORMAL>%s</NORMAL>\n",current->text);
				}else if(current->identification == TOKEN_STRING_CONSTANT){
					printf("<STR>%s</STR>\n",current->text);
				}else if(current->identification == TOKEN_CHAR_LITERAL){
					printf("<CHAR>%s</CHAR>\n",current->text);
				}else if(current->identification == TOKEN_UNUSUAL_SINGLE_CHAR){
					printf("<UNU>%s</UNU>\n",current->text);
				}else if(current->identification == TOKEN_MACRODECL_BEGIN){
					printf("<BEGIN_MACRO/>\n");
				}else if(current->identification == TOKEN_MACRODECL_END){
					printf("<END_MACRO/>\n");
				} else {
					printf("<ERROR>%s</ERROR>\n", current->text);
					exit(1);
				}
			}
			/*
			if(current->left)
			{	for(i = 0; i < lvl; i++) printf("\t");
				printf("<LCHILDREN>\n");
				strll_show(current->left, lvl + 1);
				for(i = 0; i < lvl; i++) printf("\t");
				printf("</LCHILDREN>\n");
			}
			if(current->child)
			{	for(i = 0; i < lvl; i++) printf("\t");
				printf("<CHILDREN>\n");
				strll_show(current->child, lvl + 1);
				for(i = 0; i < lvl; i++) printf("\t");
				printf("</CHILDREN>\n");
			}
			*/
		}
	}
}

static void tokenizer(strll* work){
	const char* STRING_BEGIN = 		"\"";
	const char* STRING_END = 		"\"";
	const char* CHAR_BEGIN = 		"\'";
	const char* CHAR_END = 			"\'";
	/*C-style comments.*/
	const char* COMMENT_BEGIN = 	"/*";
	const char* COMMENT_END = 		"*/";
	const char* COMMENT_BEGIN_2 = "//";
	const char* COMMENT_END_2 = "\n";
	const char* DIRECTIVE_BEGIN =	"#"; /*exclamation mark directives are ignored.*/
	const char* DIRECTIVE_END =	"\n";
	const char* MACRODECL_BEGIN = ".macro";
	const char* MACRODECL_END = ".endm";
	long mode = 0; 
	long i = 0;
	for(;; i++){
		done_selecting_mode:;
		if(i >= (long)strlen(work->text)){
			if(i == 0){
				work->identification = TOKEN_NEWLINE;
			} else {
				printf("\r\n<TOKENIZER ERROR> Unfinished token at EOF. Put some newlines in there, for pete's sake!\r\n");
				exit(1);
			}
			break;
		}
		if(mode == -1){/*Determine what our next mode should be.*/
			if(i != 0){
				puts("Something quite unusual has happened... mode is negative one, but i is not zero!");
				exit(1);
			}
			if(strprefix(MACRODECL_BEGIN, work->text)){
				work->identification = TOKEN_MACRODECL_BEGIN;
				work = consume_bytes(work, strlen(MACRODECL_BEGIN));
				i = -1;
				continue;	
			}
			if(strprefix(MACRODECL_END, work->text)){
				work->identification = TOKEN_MACRODECL_END;
				work = consume_bytes(work, strlen(MACRODECL_END));
				i = -1;
				continue;	
			}
			if(strfind(work->text, STRING_BEGIN) == 0){
				mode = 2;
				i+= strlen(STRING_BEGIN);
				goto done_selecting_mode;
			}
			if(strfind(work->text, COMMENT_BEGIN) == 0){
				mode = 3;
				i+= strlen(COMMENT_BEGIN);
				goto done_selecting_mode;
			}
			if(strfind(work->text, CHAR_BEGIN) == 0){
				mode = 4;
				i+= strlen(CHAR_BEGIN);
				goto done_selecting_mode;
			}
			if(strfind(work->text, DIRECTIVE_BEGIN) == 0){
				mode = 5;
				i+= strlen(DIRECTIVE_BEGIN);
				goto done_selecting_mode;
			}
			if(strfind(work->text, COMMENT_BEGIN_2) == 0){
				mode = 6;
				i+= strlen(COMMENT_BEGIN);
				goto done_selecting_mode;
			}
			if(isspace(work->text[i]) && work->text[i] != '\n'){
				mode = 0;
				i++;
				goto done_selecting_mode;
			}
			if(work->text[i] == '\n' || work->text[i] == ';'){
				work->identification = TOKEN_NEWLINE;
				work = consume_bytes(work, 1);
				i = -1;
				continue;
			}
			if(work->text[i] == '\\' && work->text[i+1] == '\n'){
				work->identification = TOKEN_SPACE_CHARS;
				work = consume_bytes(work, 2);
				i = -1;
				continue;
			}
			if(work->text[i] == '\\' && work->text[i+1] == '\r' && work->text[i+2] == '\n'){ /*Winblows.*/
				work->identification = TOKEN_SPACE_CHARS;
				work = consume_bytes(work, 3);
				i = -1;
				continue;
			}
			if(isUnusual(work->text[i])){ /*Merits its own thing.*/
				if(work->text[i] == '-' && isdigit(work->text[i+1])){
					mode = 1;
					continue;
				} else {
					work->identification = TOKEN_UNUSUAL_SINGLE_CHAR;
					work = consume_bytes(work, 1);
					i = -1;
					continue;
				}
			}
			mode = 1; 
		}
		if(mode == 0){ /*reading whitespace.*/
			if(isspace(work->text[i]) && work->text[i]!='\n') continue; 
			work->identification = TOKEN_SPACE_CHARS;
			work = consume_bytes(work, i); i=-1; mode = -1; continue;
		}
		else if(mode == 1){ /*contiguous non-space characters.*/
			if(isdigit(work->text[i]) &&
				(work->text[i+1] == 'e' || work->text[i+1] == 'E') &&
				work->text[i+2] == '-' &&
				isdigit(work->text[i+3])
			){
				i+= 4;continue;
			}
			if(
				isspace(work->text[i]) ||
				strfind(work->text+i, STRING_BEGIN) == 0 ||
				strfind(work->text+i, COMMENT_BEGIN) == 0 ||
				strfind(work->text+i, CHAR_BEGIN) == 0 ||
				isUnusual(work->text[i])
			)
			{
				if(i>0){ /*Likely*/
					if(work->text[i] != ':'){
						work->identification = TOKEN_NORMAL;
						work = consume_bytes(work, i);
					}else{
						work->identification = TOKEN_LABEL_DECLARATION;
						i++;
						strll* work_old = work;
						work = consume_bytes(work, i);
						work_old->text[strlen(work_old->text)-1] = '\0'; /*Get rid of that ugly colon!*/
					}
					i = -1; mode = -1; continue;
				} 
					else {puts("<TOKENIZER ERROR> I have no idea how this happened.");exit(1);} /*error*/
			}
		}
		else if(mode == 2){ /*string.*/
			if(work->text[i] == '\\' && work->text[i+1] != '\0') {i++; continue;}
			if(strfind(work->text + i, STRING_END) == 0){
				i+=strlen(STRING_END);
				work->identification = TOKEN_STRING_CONSTANT;
				work = consume_bytes(work, i); i = -1; mode = -1; continue;
			}
		}
		else if(mode == 3){ /*comment.*/
			if(work->text[i] == '\\' && work->text[i+1] != '\0') {i++; continue;}
			if(strfind(work->text + i, COMMENT_END) == 0){
				i+=strlen(COMMENT_END);
				work->identification = TOKEN_COMMENT;
				work = consume_bytes(work, i); i = -1; mode = -1; continue;
			}
		}
		else if(mode == 4){ /*char literal.*/
			if(work->text[i] == '\\' && work->text[i+1] != '\0') {i++; continue;}
			if(strfind(work->text + i, CHAR_END) == 0){
				i+=strlen(CHAR_END);
				work->identification = TOKEN_CHAR_LITERAL;
				work = consume_bytes(work, i); i = -1; mode = -1; continue;
			}
		}
		else if(mode == 5){
			if(strprefix(DIRECTIVE_END, work->text + i)){
				i+=strlen(DIRECTIVE_END);
				work->identification = TOKEN_DIRECTIVE;
				work = consume_bytes(work, i); i = -1; mode = -1; continue;
			}
		}
		else if(mode == 6){ /*comment type 2*/
			if(work->text[i] == '\\' && work->text[i+1] != '\0') {i++; continue;}
			if(strfind(work->text + i, COMMENT_END_2) == 0){
				i+=strlen(COMMENT_END_2);
				work->identification = TOKEN_COMMENT;
				work = consume_bytes(work, i); i = -1; mode = -1; continue;
			}
		} else {
			printf("\r\n<TOKENIZER ERROR>\r\n Bad tokenizer mode at '%s'", work->text + i);
			exit(1);
		}
	}
}

void strll_free(strll* freeme){
	for(strll* e = freeme; e != NULL;){
		strll* e_old = e;
		if(e->text) free(e->text);
		e->text = NULL;
		e = e->right;
		if(e_old != freeme) free(e_old);
	}
}

void require_match(char* a, char* b){
	if(!a || !b || !streq(a,b)){
		printf("\r\n<SYNTAX ERROR> required match of %s to %s failed.\r\n", a, b);
		exit(1);
	}
}

void strll_insert(strll* before, strll* insertme){
	strll* e;
	strll* before_old_right = before->right;
	before->right = insertme;
	/*walk the linked list until we reach the last node.*/
	for(e = insertme; e->right != NULL; e = e->right);
	e->right = before_old_right;
}


strll* strll_pull_out_macro(strll* before_macro){ /*Returns the pulled-out macro.*/
	strll* f = before_macro->right;
	strll* begin = f;
	if(f->identification != TOKEN_MACRODECL_BEGIN){
		printf("\r\n<INTERNAL ERROR> asked to pull out non-existent macro!\r\n");
		exit(1);
	}
	while(f && f->identification != TOKEN_MACRODECL_END) f = f->right;
	if(!f){
		printf("\r\n<PARSER ERROR> Unterminated macro declaration %s\r\n", f->right->text);
		exit(1);		
	}
	before_macro->right = f->right;
	f->right = NULL;
	return begin;
}

/*DOES NOT DUPLICATE LEFT AND RIGHT!!!*/
strll* strlldupe(strll* dupeme){ /*Duplicate a complete set of tokens.*/
	if(dupeme == NULL) return NULL;
	strll* begin = calloc(1, sizeof(strll));
	if(!begin){printf("\r\nFailed Malloc!\r\n"); exit(1);}
	for(strll* o = begin;;){
		o->identification = dupeme->identification;
		if(dupeme->text) {
			o->text	= strdup(dupeme->text);
			if(!o->text) {printf("\r\nFailed Malloc!\r\n"); exit(1);}
		}
		dupeme = dupeme->right;
		if(dupeme == NULL) return begin;
		o->right = calloc(1, sizeof(strll));
		o = o->right;
		if(!o){printf("\r\nFailed Malloc!\r\n"); exit(1);}
	}
}


void strll_replace_single_with_sequence(char* replaceme_text, strll* sequence){
	for(strll* s = &tokenized; s != NULL; s = s->right){
		if(s->right && s->right->text && streq(s->right->text, replaceme_text)){
			strll* deleteme = s->right;
			s->right = s->right->right;
			deleteme->right = NULL;
			strll_free(deleteme);
			free(deleteme);
			/*Replace it with a sequence.*/
			strll_insert(s, strlldupe(sequence));
		}
	}
}

strll* strll_pull_out_until(strll* before_something, unsigned long endid, const char* matchme, const char* error_message){
	/*Returns the pulled-out thing-a-ma-bob.*/
	strll* f = before_something->right;
	strll* begin = f;
	while(f && f->identification != endid && !(matchme && f->text && streq(matchme, f->text))) f = f->right;
	if(!f){
		printf("\r\n<PARSER ERROR> %s\r\n", error_message);
		exit(1);
	}
	before_something->right = f->right;
	f->right = NULL;
	return begin;
}

strll* skip_newline(strll* start){
	while(start && (start->identification == TOKEN_NEWLINE)) start = start->right;
	return start;
}

typedef struct {
	uint64_t val;
	unsigned char is_register;
	unsigned char is_indirect;
} ident;

/*Identifier: Not allowed to be a label.*/
static strll* parse_ident(strll* input, ident* retval, int allow_indirection){
	retval->is_register = 0;
	retval->is_indirect = 0;
	retval->val = 0;
	if(input == NULL) return NULL;
	if(input->identification == TOKEN_UNUSUAL_SINGLE_CHAR && input->text && input->text[0] == '[') 
	{
		if(!allow_indirection){
			printf("\r\n<SYNTAX ERROR> Indirection is not allowed in this instruction.\r\n");
			exit(1);
		}
		retval->is_indirect = 1;
		input = input->right;
	} else {
		retval->is_indirect = 0;
	}
	if(input == NULL) return NULL;	
	if(
		input->identification == TOKEN_NORMAL 
		&& input->text 
		&& isdigit(input->text[0]) 
		&& input->text[strlen(input->text)-1] != 'f'
	){
		retval->val = strtoull(input->text,NULL,0);
		input = input->right;
	} else if(
		input->identification == TOKEN_NORMAL 
		&& input->text 
		&& input->text[0] == '-' 
		&& input->text[strlen(input->text)-1] != 'f'
	){
		if(isdigit(input->text[1])){
			int64_t f = strtoll(input->text,NULL,0);
			memcpy(&retval->val, &f, sizeof(int64_t));
			input = input->right;
		} else {
			printf("\r\n<SYNTAX ERROR> Negative sign with no number? at token '%s'\r\n", input->text);
			exit(1);
		}
	} else if(
		input->identification == TOKEN_NORMAL 
		&& input->text 
		&& (isdigit(input->text[0]) || input->text[0] == '-')
		&& input->text[strlen(input->text)-1] == 'f'
	) { /*Floating point number!*/
		double f = strtod(input->text, NULL);
		memcpy(&retval->val, &f, sizeof(int64_t));
		input = input->right;
	} else if(
		input->identification == TOKEN_NORMAL 
		&& input->text 
		&& (input->text[0] == 'r' || input->text[0] == 'R') 
		&& strlen(input->text) == 2
	){ /*Register name identified.*/
		switch(input->text[1]){
			default: 
				printf("\r\n<SYNTAX ERROR> Invalid register name %s, you cannot use a two-letter name beginning with r or 'R' as variable names!!!\r\n", input->text);
				exit(1);
			case '0': retval->val = 0; break;
			case '1': retval->val = 1; break;
			case '2': retval->val = 2; break;
			case '3': retval->val = 3; break;
			case '4': retval->val = 4; break;
			case '5': retval->val = 5; break;
			case '6': retval->val = 6; break;
			case '7': retval->val = 7; break;
			case '8': retval->val = 8; break;
			case '9': retval->val = 9; break;
			case 'A':case 'a': retval->val = 10; break;
			case 'B':case 'b': retval->val = 11; break;
			case 'C':case 'c': retval->val = 12; break;
			case 'D':case 'd': retval->val = 13; break;
			case 'E':case 'e': retval->val = 14; break;
			case 'F':case 'f': retval->val = 15; break;
		}
		input = input->right;
	} else {
		if(input->text)
			printf("<SYNTAX ERROR> Cannot parse ident at %s\r\n", input->text);
		else
			printf("<SYNTAX ERROR> Cannot parse ident <null text>\r\n");
		exit(1);
	}
	if(retval->is_indirect){
		if(input->identification == TOKEN_UNUSUAL_SINGLE_CHAR && input->text && input->text[0] == ']')
			return input->right;
		else{
			printf("\r\n<SYNTAX ERROR> Indirection missing closing end bracket.\r\n");
			exit(1);
		}
	}
	return input->right;
}








void preprocess(){
	for(strll* c = &tokenized; c != NULL; c = c->right){
		if(c->identification != TOKEN_DIRECTIVE || c->text == NULL) continue;
		/*
			This is a directive with a valid text pointer.
		*/
		if(strprefix("#include", c->text)){
			long loc_end;
			unsigned long unused;
			long loc_begin = strfind(c->text, "\"");
			if(loc_begin == -1){
				printf("<PREPROCESSOR ERROR> include missing opening quotation marks.");
				exit(1);
			}
			loc_begin++;
			loc_end = strfind(c->text + loc_begin, "\"");
			if(loc_end == -1){
				printf("<PREPROCESSOR ERROR> include missing closing quotation marks.");
				exit(1);
			}
			loc_end += loc_begin;
			c->text[loc_end] = '\0';
			strll* included_stuff = calloc(1, sizeof(strll));
			if(!included_stuff){printf("\r\nFailed Malloc.\r\n");exit(1);}
			FILE* f = fopen(c->text + loc_begin, "r");
			if(!f){
				printf("\r\nERROR!!! Cannot open file '%s'", c->text + loc_begin);
			}
			char* temp = read_file_into_alloced_buffer(f, &unused);
			if(!temp){printf("\r\nFailed Malloc.\r\n");exit(1);}
			included_stuff->text = temp;
			tokenizer(included_stuff);
			strll_insert(c, included_stuff);
			c->identification = TOKEN_SPACE_CHARS;
			free(c->text); c->text = NULL;
		}else if(strprefix("#!", c->text)) {
			c->identification = TOKEN_SPACE_CHARS;
			free(c->text); c->text = NULL;
		} else { printf("\r\n<ERROR> unknown directive %s\r\n", c->text); exit(1);}
	}
}

void remove_spaces(){
	for(strll* c = &tokenized; c != NULL; c = c->right){
		if(c->identification == TOKEN_COMMENT){
			if(c->text){
				if(strfind(c->text, "\n") != -1){
					c->identification = TOKEN_NEWLINE;
				} else {
					c->identification = TOKEN_SPACE_CHARS;
				}
				free(c->text); c->text = NULL;
			}
			c->identification = TOKEN_SPACE_CHARS;
		}
	}
	for(strll* c = &tokenized; c != NULL; c = c->right){
		while(c->right && c->right->identification == TOKEN_SPACE_CHARS)
			strll_remove_right(c);
	}
	for(strll* c = &tokenized; c != NULL; c = c->right){
		while(c->identification == TOKEN_NEWLINE && c->right && c->right->identification == TOKEN_NEWLINE)
			strll_remove_right(c);
	}
	
}

void handle_var_decls(){
	uint8_t have_replaced = 0;
	do{
		have_replaced = 0;
		for(strll* c = &tokenized; c != NULL; c = c->right){
			if(c->right && c->right->text && streq(c->right->text, ".var")){
				strll* decl = strll_pull_out_until(c, TOKEN_NEWLINE, NULL, "No Newline to be found for .var declaration.");
				/*we must remove the macrodecl_end thing.*/
				{	strll* f;
					for(f = decl; f && f->right && f->right->identification != TOKEN_NEWLINE; f = f->right);
					if(f)	strll_remove_right(f);
				}
				/*Perform a replacement.*/
				if(decl->right && decl->right->right && decl->right->text)
					strll_replace_single_with_sequence(decl->right->text, decl->right->right);
				else if(decl->right && decl->right->text){
					printf("<SYNTAX ERROR> .var declaration of %s without definition!",decl->right->text);
					exit(1);
				} else {
					printf("<SYNTAX ERROR> Bad .var declaration without a name!");
					exit(1);
				}
				/**/
				if(decl) strll_free(decl);
				free(decl);
				have_replaced = 1; goto end;
			}
		}
		end:;
	} while(have_replaced > 0);
}

static void check_macro_decl(strll* macrodecl){
	if(macrodecl->right == NULL || macrodecl->right->text == NULL || macrodecl->right->identification != TOKEN_NORMAL){
		printf("\r\n<SYNTAX ERROR> Macro declaration without a name. you can't have .macro without a name!\r\n");
		exit(1);
	}
	if(macrodecl->right->right == NULL || macrodecl->right->right->identification != TOKEN_NEWLINE){
		printf("\r\n<SYNTAX ERROR> Macro declaration %s lacks a newline immediately following the name.\r\n", macrodecl->right->text);
		exit(1);
	}
	return;
}


static void perform_macro_replacement(strll* macrodecl){ /**/
	check_macro_decl(macrodecl);
	for(strll* c = &tokenized; c != NULL; c = c->right){
		if(
			c->right 
			&& c->right->identification == TOKEN_UNUSUAL_SINGLE_CHAR 
			&& c->right->text 
			&& c->right->text[0] == '@'
			&& c->right->right 
			&& c->right->right->text 
			&& c->right->right->identification == TOKEN_NORMAL 
			&& streq(macrodecl->right->text, c->right->right->text)
		)	
		{
			/*Match found!*/
			/*The macro declaration was previously validated, so we know that c is the begin tag, right is the name, and rightright is the newline.*/
			strll* insertme = strlldupe(macrodecl->right->right->right); /*rrr is the first token of the macro.*/
			strll* saved_to_delete = c->right; /*The @ sign.*/
										/*     @     mymac   next token*/
			strll* saved_to_place_at_end = c->right->right->right; 
			c->right = insertme;
			strll* f;
			for(f = insertme; f->right != NULL; f = f->right);
			f->right = saved_to_place_at_end;
			c = f;
			saved_to_delete->right->right = NULL; 
			strll_free(saved_to_delete); free(saved_to_delete);
		} else if(
					c->identification == TOKEN_MACRODECL_BEGIN
					&& c->right
					&& c->right->text 
					&& c->right->identification == TOKEN_NORMAL
					&& streq(macrodecl->right->text, c->right->text)
		){
			/*the programmer has been naughty*/
			printf("\r\n<SYNTAX ERROR> Duplicate macro declaration for %s.\r\n", macrodecl->right->text);
			exit(1);
		}
	}
}


strll* parse_label(strll* lbl_decl){
	
	if(lbl_decl->identification != TOKEN_LABEL_DECLARATION || lbl_decl->text == NULL){
		return lbl_decl;
	}
	if(current_pass > 0) return lbl_decl->right;
	/*
		Attempt to find duplicate.
	*/
	for(size_t i = 0; i < nlabels; i++)
		if(streq(label_names[i], lbl_decl->text)){
			printf("\r\n<SYNTAX ERROR> Label %s is duplicated!\r\n", label_names[i]);
			exit(1);
		}
	label_names[nlabels] = lbl_decl->text;
	label_decls[nlabels++] = output_counter;
	return lbl_decl->right;
}


strll* parse_simple_twoop(strll* insn, char* name, uint16_t opcode){
	ident l = {0}, r = {0};
	strll* f = parse_ident(insn->right, &l, 0);
	if(!(f && f->identification == TOKEN_UNUSUAL_SINGLE_CHAR && f->text && f->text[0] ==',')){
		printf("\r\n<SYNTAX ERROR> %s lacking comma!!!", name);
		exit(1);
	}
	f = parse_ident(f->right, &r, 0);
	if(!f){
		printf("\r\n<SYNTAX ERROR> %s FAILED TO PARSE SECOND IDENTIFIER\r\n", name);
		exit(1);
	}
	if(l.is_register == 0 || r.is_register == 0){
		printf("\r\n<ERROR> %s CANNOT OPERATE ON SOMETHING THAT IS NOT A REGISTER.\r\n", name);
		exit(1);
	}
	write_short(
		(opcode<<8) +
		((l.val&0xf)<<4) +
		((r.val&0xf))
	);
	return f;
}

strll* parse_simple_oneop(strll* insn, char* name, uint16_t opcode){
	ident l = {0};
	strll* f = parse_ident(insn->right, &l, 0);
	if(!f){
		printf("\r\n<SYNTAX ERROR> cannot parse %s instruction. Failed to parse register name.\r\n", name);
		exit(1);
	}
	if(l.is_register == 0 || l.is_indirect){
		printf("\r\n<SYNTAX ERROR> invalid target for %s.\r\n", name);
		exit(1);
	}
	write_short(
		(opcode<<8) +
		(l.val & 0xf)
	);
	return f;
}

strll* parse_branch(strll* insn, char* name, uint16_t opcode){
	write_short(
				(32<<8) +
				opcode
			);
	strll* f = insn->right;
	if(!f || !f->text || (f->identification != TOKEN_NORMAL)){
		printf("\r\n<SYNTAX ERROR> invalid branch target for insn %s.\r\n", name);
		exit(1);
	}
	for(uint64_t i = 0; i < nlabels; i++)
		if(streq(label_names[i], f->text)){
			write_u64(label_decls[i]);
			return f->right;
		}
	printf("\r\n<SYNTAX ERROR> Unknown label %s, for branch %s.\r\n", f->text, name);
	exit(1);
	return f->right;
}

strll* parse_call(strll* insn){
	write_short(
				(33<<8)
			);
	strll* f = insn->right;
	if(!f || !f->text || (f->identification != TOKEN_NORMAL)){
		printf("\r\n<SYNTAX ERROR> invalid branch target for insn call\r\n");
		exit(1);
	}
	for(uint64_t i = 0; i < nlabels; i++)
		if(streq(label_names[i], f->text)){
			write_u64(label_decls[i]);
			return f->right;
		}
	printf("\r\n<SYNTAX ERROR> Unknown label %s, for call insn.\r\n", f->text);
	exit(1);
	return f->right;
}

strll*  parse_insn(strll* insn){
	if(insn->identification != TOKEN_NORMAL || insn->text == NULL)
		return insn;
	/*Attempt to match the name of the instruction.*/
	if(streq(insn->text, "nop")){
		write_short(0);
		return insn->right;
	} else if(streq(insn->text, "mov")){
		ident l = {0}, r = {0};
		strll* f = parse_ident(insn->right, &l, 1);
		if(!(f && f->identification == TOKEN_UNUSUAL_SINGLE_CHAR && f->text && f->text[0] ==',')){
			printf("\r\n<SYNTAX ERROR> Mov lacking comma!!!");
			exit(1);
		}
		f = parse_ident(f->right, &r, 0);
		if(!f){
			printf("\r\n<SYNTAX ERROR> MOV FAILED TO PARSE SECOND IDENTIFIER\r\n");
			exit(1);
		}
		
		if(l.is_register == 1 && l.is_indirect == 0 &&
			r.is_register == 1 && r.is_indirect == 0){
			/*register to register mov.*/
			write_short(
				(1<<8) + 
				((l.val&0xf)<<4) +
				((r.val&0xf))
			);
		} else if(l.is_register == 1 && l.is_indirect == 1 &&
					r.is_register == 1 && r.is_indirect == 0){
			/*ist*/
			write_short(
				(6<<8) + 
				((l.val&0xf)<<4) +
				((r.val&0xf))
			);
		} else if(l.is_register == 1 && l.is_indirect == 0 &&
					r.is_register == 1 && r.is_indirect == 1){
			/*ild*/
			write_short(
				(5<<8) + 
				((l.val&0xf)<<4) +
				((r.val&0xf))
			);
		} else if(l.is_register == 1 && l.is_indirect == 0 &&
					r.is_register == 0 && r.is_indirect == 0) {
			/*Load constant value into register.*/
			write_short(
							(39<<8) + 
							((l.val&0xf))
						);
			write_u64(r.val);
		} else if(l.is_register == 1 && l.is_indirect == 0 &&
					r.is_register == 0 && r.is_indirect == 1) {
			/*direct load into register.*/
			write_short(
							(3<<8) + 
							((l.val&0xf))
						);
			write_u64(r.val);
		}else if(l.is_register == 0 && l.is_indirect == 1 &&
				r.is_register == 1 && r.is_indirect == 0) {
			/*direct store from register.*/
			write_short(
							(4<<8) + 
							((r.val&0xf))
						);
			write_u64(l.val);
		} else {
			printf("\r\n<ERROR> cannot codegen for move with l.is_register = %d, l.is_indirect = %d, r.is_register = %d, r.is_indirect = %d", l.is_register, l.is_indirect, r.is_register, r.is_indirect);
			exit(1);
		}

		/*Return.*/
		return f;
	} else if(streq(insn->text, "zreg")){
		ident l = {0};
		strll* f = parse_ident(insn->right, &l, 0);
		if(!f){
			printf("\r\n<SYNTAX ERROR> cannot parse zreg instruction. Failed to parse register name.\r\n");
			exit(1);
		}
		if(l.is_register == 0 || l.is_indirect){
			printf("\r\n<SYNTAX ERROR> invalid target for zreg.\r\n");
			exit(1);
		}
		write_short(
			(2<<8) +
			(l.val & 0xf)
		);
		return f;
	} else if(streq(insn->text, "iadd") || streq(insn->text, "uadd")){
		return parse_simple_twoop(insn, "iadd or uadd", 7);
	} else if(streq(insn->text, "isub") || streq(insn->text, "usub")){
		return parse_simple_twoop(insn, "isub or usub", 8);
	} else if(streq(insn->text, "imul") || streq(insn->text, "umul")){
		return parse_simple_twoop(insn, "imul or umul", 9);
	} else if(streq(insn->text, "udiv")){
		return parse_simple_twoop(insn, "udiv", 10);
	} else if(streq(insn->text, "umod")){
		return parse_simple_twoop(insn, "umod", 11);
	} else if(streq(insn->text, "idiv")){
		return parse_simple_twoop(insn, "idiv", 12);
	} else if(streq(insn->text, "imod")){
		return parse_simple_twoop(insn, "imod", 13);
	} else if(streq(insn->text, "shl")){
		return parse_simple_twoop(insn, "shl", 14);
	} else if(streq(insn->text, "shr")){
		return parse_simple_twoop(insn, "shr", 15);
	} else if(streq(insn->text, "or")){
		return parse_simple_twoop(insn, "or", 16);
	} else if(streq(insn->text, "and")){
		return parse_simple_twoop(insn, "and", 17);
	} else if(streq(insn->text, "xor")){
		return parse_simple_twoop(insn, "xor", 18);
	} else if(streq(insn->text, "compl")){
		return parse_simple_twoop(insn, "compl", 19);
	} else if(streq(insn->text, "neg")){
		return parse_simple_twoop(insn, "neg", 20);
	} else if(streq(insn->text, "fadd")){
		return parse_simple_twoop(insn, "fadd", 21);
	} else if(streq(insn->text, "fsub")){
		return parse_simple_twoop(insn, "fsub", 22);
	} else if(streq(insn->text, "fmul")){
		return parse_simple_twoop(insn, "fmul", 23);
	} else if(streq(insn->text, "fdiv")){
		return parse_simple_twoop(insn, "fdiv", 24);
	} else if(streq(insn->text, "itof")){
		return parse_simple_twoop(insn, "itof", 25);
	} else if(streq(insn->text, "ftoi")){
		return parse_simple_twoop(insn, "ftoi", 26);
	} else if(streq(insn->text, "ucmp")){
		return parse_simple_twoop(insn, "ucmp", 27);
	} else if(streq(insn->text, "icmp")){
		return parse_simple_twoop(insn, "icmp", 28);
	} else if(streq(insn->text, "fcmp")){
		return parse_simple_twoop(insn, "fcmp", 29);
	} else if(streq(insn->text, "getfl")){
		ident l = {0};
		strll* f = parse_ident(insn->right, &l, 0);
		if(!f){
			printf("\r\n<SYNTAX ERROR> cannot parse getfl instruction. Failed to parse register name.\r\n");
			exit(1);
		}
		if(l.is_register == 0 || l.is_indirect){
			printf("\r\n<SYNTAX ERROR> invalid target for getfl.\r\n");
			exit(1);
		}
		write_short(
			(30<<8) +
			(l.val & 0xf)
		);
		return f;
	} else if(streq(insn->text, "cfl")){
		write_short((31<<8));
		return insn->right;
	} else if(streq(insn->text, "jmp")){
		/*Requires a label's name in the next token.*/
		return parse_branch(insn, "jmp", 8);
	} else if(streq(insn->text, "beq")){
		return parse_branch(insn, "beq", 2);
	} else if(streq(insn->text, "bne")){
		return parse_branch(insn, "bne", 32);
	} else if(streq(insn->text, "blt")){
		return parse_branch(insn, "blt", 1);
	} else if(streq(insn->text, "bnlt")){
		return parse_branch(insn, "bnlt", 16);
	} else if(streq(insn->text, "bgt")){
		return parse_branch(insn, "bgt", 4);
	} else if(streq(insn->text, "bngt")){
		return parse_branch(insn, "bngt", 64);
	} else if(streq(insn->text, "call")){
		return parse_call(insn);
	} else if(streq(insn->text, "ret")){
		write_short((34<<8));
		return insn->right;
	} else if(streq(insn->text, "push")){
		return parse_simple_oneop(insn, "push", 35);
	} else if(streq(insn->text, "pop")){
		return parse_simple_oneop(insn, "pop", 36);
	} else if(streq(insn->text, "getstp")){
		return parse_simple_oneop(insn, "getstp", 37);
	} else if(streq(insn->text, "int")){
		return parse_simple_oneop(insn, "int", 38);
	} else if(streq(insn->text, "halt")){
		write_short(40<<8);
		return insn->right;
	}else{
		printf("\r\n<ERROR> Unknown instruction %s\r\n", insn->text);
		exit(1);
	}
}

void handle_macro_decls(){
	uint8_t have_replaced = 0;
	do{
		have_replaced = 0;
		for(strll* c = &tokenized; c != NULL; c = c->right){
			if(c->right && c->right->identification == TOKEN_MACRODECL_BEGIN	){
				strll* decl = strll_pull_out_macro(c);
				/*we must remove the macrodecl_end thing.*/
				{	strll* f;
					for(f = decl; f && f->right && f->right->identification != TOKEN_MACRODECL_END; f = f->right);
					if(f)	strll_remove_right(f);
				}
				/*DEBUG printout!!!*/
				strll_show(c, 0); printf("\r\n\r\n");
				strll_show(decl, 0); printf("\r\n\r\n");
				/*Do that shiet*/
				perform_macro_replacement(decl);
				if(decl) strll_free(decl);
				free(decl);
				have_replaced = 1; goto end;
			}
		}
		end:;
	} while(have_replaced > 0);
}


int main(int argc, char** argv){
	label_names = calloc(1, sizeof(char*) * MAX_LABELS);
	label_decls = calloc(1, sizeof(uint64_t) * MAX_LABELS);
	if(!label_names || !label_decls){
		printf("\r\nFailed initial mallocs for labels.\r\n");
		exit(1);
	}
	if(argc > 1) infilename = argv[1];
	if(!infilename){
		puts("<TOKENIZER ERROR> No input file");
		exit(1);
	}
	ifile = fopen(infilename, "rb");
	if(!ifile){
		puts("<TOKENIZER ERROR> Cannot open the input file");
		exit(1);
	}
	entire_input_file = read_file_into_alloced_buffer(ifile, &entire_input_file_len);
	fclose(ifile); ifile = NULL;
	
	tokenized.text = entire_input_file;
	tokenizer(&tokenized);
	preprocess();
	remove_spaces();
	handle_var_decls();
	handle_macro_decls();
	for(current_pass=0; current_pass < 2; current_pass++){
		/*TODO*/
	}
	strll_show(&tokenized, 0);
	/*A comment!*/


	free(label_names);
	free(label_decls);
}
#define what whomst

