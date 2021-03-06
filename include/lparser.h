/*
** $Id: lparser.h,v 1.74 2014/10/25 11:50:46 roberto Exp $
** Lua Parser
** See Copyright Notice in lua.h
*/

#ifndef lparser_h
#define lparser_h

#include "llimits.h"
#include "lobject.h"
#include "lzio.h"

#include <stdio.h>

/*
** Expression descriptor
*/

typedef enum {
  VVOID,	/* no value */
  VNIL,
  VTRUE,
  VFALSE,
  VK,		/* info = index of constant in 'k' */
  VKFLT,	/* nval = numerical float value */
  VKINT,	/* nval = numerical integer value */
  VNONRELOC,	/* info = result register */
  VLOCAL,	/* info = local register */
  VUPVAL,       /* info = index of upvalue in 'upvalues' */
  VINDEXED,	/* t = table register/upvalue; idx = index R/K */
  VJMP,		/* info = instruction pc */
  VRELOCABLE,	/* info = instruction pc; 
              op code may be OP_CLOSURE,OP_NEWTABLE,OP_CONCAT,OP_GETUPVAL,
              OP_GETTABUP,OP_GETTABLE,OP_NOT,Code for binary and unary expressions 
              that produce values (arithmetic operations, bitwise operations, 
              concat, length)
              */
  VCALL,	/* info = instruction pc */
  VVARARG	/* info = instruction pc */
} expkind;


#define vkisvar(k)	(VLOCAL <= (k) && (k) <= VINDEXED)
#define vkisinreg(k)	((k) == VNONRELOC || (k) == VLOCAL)

typedef struct expdesc {
  expkind k;
  union {
    struct {  /* for indexed variables (VINDEXED) */
      short idx;  /* index (R/K) */
      lu_byte t;  /* table (register or upvalue) */
      lu_byte vt;  /* whether 't' is register (VLOCAL) or upvalue (VUPVAL) */
    } ind;
    int info;  /* for generic use */
    lua_Number nval;  /* for VKFLT */
    lua_Integer ival;    /* for VKINT */
  } u;
  int t;  /* patch list of 'exit when true' */
  int f;  /* patch list of 'exit when false' */
  int ravi_type; /* RAVI change: type of the expression if known, else LUA_TNONE */
} expdesc;


/* description of active local variable */
typedef struct Vardesc {
  short idx;  /* variable index in stack */
} Vardesc;


/* description of pending goto statements and label statements */
typedef struct Labeldesc {
  TString *name;  /* label identifier */
  int pc;  /* position in code */
  int line;  /* line where it appeared */
  lu_byte nactvar;  /* local level where it appears in current block */
} Labeldesc;


/* list of labels or gotos */
typedef struct Labellist {
  Labeldesc *arr;  /* array */
  int n;  /* number of entries in use */
  int size;  /* array size */
} Labellist;


/* dynamic structures used by the parser */
typedef struct Dyndata {
  /* The actvar structure appears to represent the stack. It is used 
   * in a single parsing run - as part of an implicit function that 
   * wraps the parsed code. As the code is parsed the stack grows and
   * shrinks.
   */
  struct {  /* list of active local variables */
    Vardesc *arr;
    int n; /* tracks the top of the stack where the next item will go */
    int size; /* number of arr objects allocated (memory) */
  } actvar;
  Labellist gt;  /* list of pending gotos */
  Labellist label;   /* list of active labels */
} Dyndata;


/* control of blocks */
struct BlockCnt;  /* defined in lparser.c */


/* state needed to generate code for a given function 
   functions have a register window on the stack 
   the stack is represented in LexState->dyd.actvar (Dyndata) 
   structure (see llex.h). The register window of the function 
   starts from LexState->dyd.actvar.arr[firstlocal]. The 'active' local variables 
   of the function extend upto LexState->dyd.actvar.arr[nactvar-1]. Note that 
   when parsing a 'local' declaration statement the 'nactvar' is adjusted at the end of 
   the statement so that during parsing of the statement the 'nactvar'
   covers locals upto the start of the statement. This means that 
   local variables come into scope (become 'active') after the local statement ends.
   However, if the local statement defines a function then the variable becomes 'active'
   before the function body is parsed.
   A tricky thing to note is that while 'nactvar' is adjusted at the end of the 
   statement - the 'stack' as represented by LexState->dyd.actvar.arr is extended to the required 
   size as the local variables are created by new_localvar(). 
   When a function is the topmost function being parsed, the 
   registers between LexState->dyd.actvar.arr[nactvar] and LexState->dyd.actvar.arr[freereg-1] 
   are used by the parser for evaluating expressions - i.e. these are part of the 
   local registers available to the function 
   Note that function parameters are handled as locals 
  
   example of what all this means: 
  
   Let's say we are parsing following chunk of code
   function testfunc()
     -- at this stage 'nactvar' is 0 (no active variables)
     -- 'firstlocal' is set to current top of the variables stack 
     -- LexState->dyd.actvar.n (i.e. excluding registers used for expression evaluation)
     -- LexState->dyd.actvar.n = 0 at this stage
     local function tryme()
       -- Since we are inside the local statement and 'tryme' is a local variable,
       -- the LexState->dyd.actvar.n goes to 1. As this is a function definition
       -- the local variable declaration is deemed to end here, so 'nactvar' for testfunc()
       -- is gets set to 1 (making 'tryme' an active variable). 
       -- A new FuncState is created for 'tryme' function.
       -- The new tryme() FunState has 'firstlocal' set to value of LexState->dyd.actvar.n, i.e., 1
       local i,j = 5,6
       -- After 'i' is parsed, LexState->dyd.actvar.n = 2, but 'nactvar' = 0 for tryme() 
       -- After 'j' is parsed, LexState->dyd.actvar.n = 3, but 'nactvar' = 0 for tryme()
       -- Only after the full statement above is parsed, 'nactvar' for tryme() is set to '2'
       -- This is done by adjustlocalvar().
       return i,j
     end
     -- Here two things happen
     -- Firstly the FuncState for tryme() is popped so that 
     -- FuncState for testfunc() is now at top
     -- As part of this popping, leaveblock() calls removevars() 
     -- to adjust the LexState->dyd.actvar.n down to 1 where it was 
     -- at before parsing the tryme() function body.
     local i, j = tryme() 
     -- After 'i' is parsed, LexState->dyd.actvar.n = 2, but 'nactvar' = 1 still
     -- After 'j' is parsed, LexState->dyd.actvar.n = 3, but 'nactvar' = 1 still
     -- At the end of the statement 'nactvar' is set to 3.
     return i+j
   end
   -- As before the leaveblock() calls removevars() which resets
   -- LexState->dyd.actvar.n to 0 (the value before testfunc() was parsed)

   A rough debug trace of the above gives:
   function testfunc()
     -- open_func -> fs->firstlocal set to 0 (ls->dyd->actvar.n), and fs->nactvar reset to 0
     local function tryme()
       -- new_localvar -> registering var tryme fs->f->locvars[0] at ls->dyd->actvar.arr[0]
       -- new_localvar -> ls->dyd->actvar.n set to 1
       -- adjustlocalvars -> set fs->nactvar to 1
       -- open_func -> fs->firstlocal set to 1 (ls->dyd->actvar.n), and fs->nactvar reset to 0
       -- adjustlocalvars -> set fs->nactvar to 0 (no parameters)
       local i,j = 5,6
       -- new_localvar -> registering var i fs->f->locvars[0] at ls->dyd->actvar.arr[1]
       -- new_localvar -> ls->dyd->actvar.n set to 2
       -- new_localvar -> registering var j fs->f->locvars[1] at ls->dyd->actvar.arr[2]
       -- new_localvar -> ls->dyd->actvar.n set to 3
       -- adjustlocalvars -> set fs->nactvar to 2
       return i,j
       -- removevars -> reset fs->nactvar to 0
     end
     local i, j = tryme()
     -- new_localvar -> registering var i fs->f->locvars[1] at ls->dyd->actvar.arr[1]
     -- new_localvar -> ls->dyd->actvar.n set to 2
     -- new_localvar -> registering var j fs->f->locvars[2] at ls->dyd->actvar.arr[2]
     -- new_localvar -> ls->dyd->actvar.n set to 3
     -- adjustlocalvars -> set fs->nactvar to 3
     return i+j
     -- removevars -> reset fs->nactvar to 0
   end
 */
typedef struct FuncState {
  Proto *f;  /* current function header */
  struct FuncState *prev;  /* enclosing function */
  struct LexState *ls;  /* lexical state */
  struct BlockCnt *bl;  /* chain of current blocks */
  int pc;  /* next position to code (equivalent to 'ncode') */
  int lasttarget;   /* 'label' of last 'jump label' */
  int jpc;  /* list of pending jumps to 'pc' */
  int nk;  /* number of elements in 'k' */
  int np;  /* number of elements in 'p' */
  int firstlocal;  /* index of first local var (in Dyndata array) - this marks the start of the register window of the function */
  short nlocvars;  /* number of elements in 'f->locvars' - i.e. number of local variables */
  lu_byte nactvar;  /* number of active local variables in the function - see note above on how this works */
  lu_byte nups;  /* number of upvalues */
  lu_byte freereg;  /* first free register - this tracks the top of the stack as parsing progresses */
} FuncState;


LUAI_FUNC LClosure *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff,
                                 Dyndata *dyd, const char *name, int firstchar);


/* Special printf that recognises following conversions:
 * %e - expdesc *
 * %v - LocVar *
 * %t - Lua types 
 * %f - floating decimal
 * %d - int
 * %o - Lua Instruction 
 */
LUAI_FUNC void raviY_printf(FuncState *fs, const char *format, ...);

/* Given a local register obtain available type information - if a 
 * local variable is associated with the register (i.e. register <= fs->nactvar)
 * in the function type info is available then the type will be returned.
 * Else LUA_TNONE is returned. Note that this function only looks
 * at active local variables - see note on FuncState on what this means.
 */
LUAI_FUNC int raviY_get_register_typeinfo(FuncState *fs, int reg);

#define DEBUG_EXPR(p) if ((ravi_parser_debug & 1) != 0) {p;} else {}
#define DEBUG_CODEGEN(p) if ((ravi_parser_debug & 2) != 0) {p;} else {}
#define DEBUG_VARS(p) if ((ravi_parser_debug & 4) != 0) {p;} else {}

#endif
