
/* This file is part of minemu
 *
 * Copyright 2010-2011 Erik Bosman <erik@minemu.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <string.h>
#include <stddef.h>

#include "jit_code.h"
#include "jmp_cache.h"
#include "runtime.h"
#include "error.h"
#include "taint_code.h"
#include "taint.h"
#include "debug.h"
#include "mm.h"
#include "threads.h"

int call_strategy = PRESEED_ON_CALL;

#define TAINT                  (0x80)
#define TAINT_MASK           (~(TAINT-1))

#define TAINT_MRM              (0x00)
#define TAINT_REG_OFF          (0x20)
#define TAINT_REG              (0x30)
#define TAINT_OFF              (0x40)
#define TAINT_STRING           (0x48)
#define TAINT_IMPL             (0x50)
#define TAINT_ADDR             (0x60)

#define TAINT_MRM_OP( action )       ( (action&~0x1f) == TAINT_MRM     )
#define TAINT_REG_OFF_OP( action )   ( (action&~0x0f) == TAINT_REG_OFF )
#define TAINT_REG_OP( action )       ( (action&~0x0f) == TAINT_REG     )
#define TAINT_OFF_OP( action )       ( (action&~0x0f) == TAINT_OFF     )
#define TAINT_IMPL_OP( action )      ( (action&~0x0f) == TAINT_IMPL    )
#define TAINT_ADDR_OP( action )      ( (action&~0x0f) == TAINT_ADDR    )

#define TAINT_STRING_OP( action )    ( (action&~0x07) == (TAINT|TAINT_STRING)  )

enum
{
	/* arguments: modrm / offset */

	TAINT_OR_MEM_TO_REG = TAINT_MRM,
	TAINT_OR_REG_TO_MEM,
	TAINT_XOR_MEM_TO_REG,
	TAINT_XOR_REG_TO_MEM,
	TAINT_COPY_MEM_TO_REG,
	TAINT_COPY_REG_TO_MEM,

	TAINT_BYTE_OR_MEM_TO_REG,
	TAINT_BYTE_OR_REG_TO_MEM,
	TAINT_BYTE_XOR_MEM_TO_REG,
	TAINT_BYTE_XOR_REG_TO_MEM,
	TAINT_BYTE_COPY_MEM_TO_REG,
	TAINT_BYTE_COPY_REG_TO_MEM,

	TAINT_COPY_ZX_MEM_TO_REG,
	TAINT_BYTE_COPY_ZX_MEM_TO_REG,

	TAINT_SWAP_REG_MEM,
	TAINT_BYTE_SWAP_REG_MEM,

	TAINT_COPY_MEM_TO_PUSH,
	TAINT_COPY_POP_TO_MEM,

	TAINT_ERASE_MEM,
	TAINT_BYTE_ERASE_MEM,

	TAINT_LEA,

	/* arguments: register / offset */

	TAINT_COPY_REG_TO_PUSH = TAINT_REG_OFF,
	TAINT_COPY_POP_TO_REG,

	/* arguments: register */

	TAINT_SWAP_AX_REG = TAINT_REG,
	TAINT_ERASE_REG,
	TAINT_BYTE_ERASE_REG,

	/* arguments: offset */

	TAINT_ERASE_PUSH = TAINT_OFF,
	TAINT_PUSHA,
	TAINT_POPA,
	TAINT_LEAVE,
	TAINT_ENTER,
	TAINT_COPY_STR_TO_STR = TAINT_STRING,
	TAINT_COPY_AX_TO_STR,
	TAINT_COPY_STR_TO_AX,
	TAINT_BYTE_COPY_STR_TO_STR,
	TAINT_BYTE_COPY_AL_TO_STR,
	TAINT_BYTE_COPY_STR_TO_AL,

	/* no arguments */

	TAINT_ERASE_AX = TAINT_IMPL,
	TAINT_ERASE_DX,
	TAINT_ERASE_AX_DX,
	TAINT_ERASE_AXH,
	TAINT_BYTE_ERASE_AL,

	/* arguments: address, offset */

	TAINT_COPY_AX_TO_OFFSET = TAINT_ADDR,
	TAINT_COPY_OFFSET_TO_AX,
	TAINT_BYTE_COPY_AL_TO_OFFSET,
	TAINT_BYTE_COPY_OFFSET_TO_AL,
};

static const unsigned char segment_prefix_mapping[] =
{
	[TAINT_COPY_MEM_TO_REG] = TAINT_ERASE_REG,
	[TAINT_BYTE_COPY_MEM_TO_REG] = TAINT_BYTE_ERASE_REG,
	[TAINT_COPY_ZX_MEM_TO_REG] = TAINT_ERASE_REG,
	[TAINT_BYTE_COPY_ZX_MEM_TO_REG] = TAINT_ERASE_REG,
	[TAINT_SWAP_REG_MEM] = TAINT_ERASE_REG,
	[TAINT_BYTE_SWAP_REG_MEM] = TAINT_BYTE_ERASE_REG,
	[TAINT_SWAP_AX_REG] = TAINT_ERASE_REG,
	[TAINT_ERASE_REG] = TAINT_ERASE_REG,
	[TAINT_BYTE_ERASE_REG] = TAINT_BYTE_ERASE_REG,
	[TAINT_COPY_STR_TO_AX] = TAINT_ERASE_AX,
	[TAINT_BYTE_COPY_STR_TO_AL] = TAINT_BYTE_ERASE_AL,
	[TAINT_ERASE_AX] = TAINT_ERASE_AX,
	[TAINT_ERASE_DX] = TAINT_ERASE_DX,
	[TAINT_ERASE_AX_DX] = TAINT_ERASE_AX_DX,
	[TAINT_ERASE_AXH] = TAINT_ERASE_AXH,
	[TAINT_BYTE_ERASE_AL] = TAINT_BYTE_ERASE_AL,
	[TAINT_COPY_OFFSET_TO_AX] = TAINT_BYTE_ERASE_AL,
	[TAINT_BYTE_COPY_OFFSET_TO_AL] = TAINT_BYTE_ERASE_AL,
};

union
{
	struct { int (*f)(char *, char *, long); int (*f16)(char *, char *, long); } mrm;
	struct { int (*f)(char *, int, long);    int (*f16)(char *, int, long);    } reg_off;
	struct { int (*f)(char *, int);          int (*f16)(char *, int);          } reg;
	struct { int (*f)(char *, long);         int (*f16)(char *, long);         } off;
	struct { int (*f)(char *);               int (*f16)(char *);               } impl;
	struct { int (*f)(char *, long, long);   int (*f16)(char *, long, long);   } addr;
}
	taint_ops[] =
{
	[TAINT_OR_MEM_TO_REG].mrm =        { .f = taint_or_mem32_to_reg32,   .f16 = taint_or_mem16_to_reg16   },
	[TAINT_OR_REG_TO_MEM].mrm =        { .f = taint_or_reg32_to_mem32,   .f16 = taint_or_reg16_to_mem16   },
	[TAINT_XOR_MEM_TO_REG].mrm =       { .f = taint_xor_mem32_to_reg32,  .f16 = taint_xor_mem16_to_reg16  },
	[TAINT_XOR_REG_TO_MEM].mrm =       { .f = taint_xor_reg32_to_mem32,  .f16 = taint_xor_reg16_to_mem16  },
	[TAINT_COPY_MEM_TO_REG].mrm =      { .f = taint_copy_mem32_to_reg32, .f16 = taint_copy_mem16_to_reg16 },
	[TAINT_COPY_REG_TO_MEM].mrm =      { .f = taint_copy_reg32_to_mem32, .f16 = taint_copy_reg16_to_mem16 },

	[TAINT_BYTE_OR_MEM_TO_REG].mrm =   { .f = taint_or_mem8_to_reg8   },
	[TAINT_BYTE_OR_REG_TO_MEM].mrm =   { .f = taint_or_reg8_to_mem8   },
	[TAINT_BYTE_XOR_MEM_TO_REG].mrm =  { .f = taint_xor_mem8_to_reg8  },
	[TAINT_BYTE_XOR_REG_TO_MEM].mrm =  { .f = taint_xor_reg8_to_mem8  },
	[TAINT_BYTE_COPY_MEM_TO_REG].mrm = { .f = taint_copy_mem8_to_reg8 },
	[TAINT_BYTE_COPY_REG_TO_MEM].mrm = { .f = taint_copy_reg8_to_mem8 },

	[TAINT_COPY_ZX_MEM_TO_REG].mrm =      { .f = taint_copy_mem16_to_reg32, .f16 = taint_or_mem16_to_reg16  },
	[TAINT_BYTE_COPY_ZX_MEM_TO_REG].mrm = { .f = taint_copy_mem8_to_reg32,  .f16 = taint_copy_mem8_to_reg16 },

	[TAINT_SWAP_REG_MEM].mrm =      { .f = taint_swap_reg32_mem32, .f16 = taint_swap_reg16_mem16 },
	[TAINT_BYTE_SWAP_REG_MEM].mrm = { .f = taint_swap_reg8_mem8                                  },

	[TAINT_COPY_MEM_TO_PUSH].mrm = { .f = taint_copy_push_mem32, .f16 = taint_copy_push_mem16 },
	[TAINT_COPY_POP_TO_MEM].mrm =  { .f = taint_copy_pop_mem32,  .f16 = taint_copy_pop_mem16 },

	[TAINT_ERASE_MEM].mrm =        { .f = taint_erase_mem32, .f16 = taint_erase_mem16 },
	[TAINT_BYTE_ERASE_MEM].mrm =   { .f = taint_erase_mem8 },

	[TAINT_LEA].mrm =              { .f = taint_lea },

	/* arguments: register / offset */

	[TAINT_COPY_REG_TO_PUSH].reg_off = { .f = taint_copy_push_reg32, .f16 = taint_copy_push_reg16 },
	[TAINT_COPY_POP_TO_REG].reg_off  = { .f = taint_copy_pop_reg32,  .f16 = taint_copy_pop_reg16  },

	/* arguments: register */

	[TAINT_SWAP_AX_REG].reg    = { .f = taint_swap_eax_reg32, .f16 = taint_swap_ax_reg16 },
	[TAINT_ERASE_REG].reg      = { .f = taint_erase_reg32, .f16 = taint_erase_reg16      },
	[TAINT_BYTE_ERASE_REG].reg = { .f = taint_erase_reg8                                 },

	/* arguments: offset */

	[TAINT_COPY_STR_TO_STR].off      = { .f = taint_copy_str32_to_str32, .f16 = taint_copy_str16_to_str16 },
	[TAINT_COPY_AX_TO_STR].off       = { .f = taint_copy_eax_to_str32,   .f16 = taint_copy_ax_to_str16    },
	[TAINT_COPY_STR_TO_AX].off       = { .f = taint_copy_str32_to_eax,   .f16 = taint_copy_str16_to_ax    },
	[TAINT_BYTE_COPY_STR_TO_STR].off = { .f = taint_copy_str8_to_str8                                     },
	[TAINT_BYTE_COPY_AL_TO_STR].off  = { .f = taint_copy_al_to_str8                                       },
	[TAINT_BYTE_COPY_STR_TO_AL].off  = { .f = taint_copy_str8_to_al                                       },
	[TAINT_ERASE_PUSH].off           = { .f = taint_erase_push32,        .f16 = taint_erase_push16        },
	[TAINT_PUSHA].off                = { .f = taint_copy_pusha32,        .f16 = taint_copy_pusha16        },
	[TAINT_POPA].off                 = { .f = taint_copy_popa32,         .f16 = taint_copy_popa16         },
	[TAINT_LEAVE].off                = { .f = taint_leave32,             .f16 = taint_leave16             },
	[TAINT_ENTER].off                = { .f = taint_enter32,             .f16 = taint_enter16             },

	/* no arguments */

	[TAINT_ERASE_AX].impl =      { .f = taint_erase_eax,      .f16 = taint_erase_ax    },
	[TAINT_ERASE_DX].impl =      { .f = taint_erase_edx,      .f16 = taint_erase_dx    },
	[TAINT_ERASE_AX_DX].impl =   { .f = taint_erase_eax_edx,  .f16 = taint_erase_ax_dx },
	[TAINT_ERASE_AXH].impl =     { .f = taint_erase_eax_high, .f16 = taint_erase_ah    },
	[TAINT_BYTE_ERASE_AL].impl = { .f = taint_erase_al                                 },

	/* arguments: address / offset */

	[TAINT_COPY_AX_TO_OFFSET].addr = { .f = taint_copy_eax_to_addr32, .f16 = taint_copy_ax_to_addr16 },
	[TAINT_COPY_OFFSET_TO_AX].addr = { .f = taint_copy_addr32_to_eax, .f16 = taint_copy_addr16_to_ax },
	[TAINT_BYTE_COPY_AL_TO_OFFSET].addr = { .f = taint_copy_al_to_addr8 },
	[TAINT_BYTE_COPY_OFFSET_TO_AL].addr = { .f = taint_copy_addr8_to_al },
};

/* op action */

#define C  COPY_INSTRUCTION
#define U  UNDEFINED_INSTRUCTION
#define BAD UNDEFINED_INSTRUCTION /* using these indicates an emulator bug */

#define JR JUMP_RELATIVE
#define JC JUMP_CONDITIONAL
#define JF JUMP_FAR
#define JI JUMP_INDIRECT

#define L  LOOP

#define CMOV CONDITIONAL_MOVE

#define CR CALL_RELATIVE
#define CF CALL_FAR
#define CI CALL_INDIRECT

#define R  RETURN
#define RC RETURN_CLEANUP
#define RF RETURN_FAR

#define SE  SYSENTER
#define SC  SYSCALL

#define CX8 CMPXCHG8
#define CXG CMPXCHG
#define CX8B CMPXCHG8B
#define CPUI CPUID
#define XXX (C) /* todo */
#define PRIV (C)
#define MM (U)

#define TOMR ( TAINT | TAINT_OR_MEM_TO_REG           )
#define TORM ( TAINT | TAINT_OR_REG_TO_MEM           )
#define TXMR ( TAINT | TAINT_XOR_MEM_TO_REG          )
#define TXRM ( TAINT | TAINT_XOR_REG_TO_MEM          )
#define TCMR ( TAINT | TAINT_COPY_MEM_TO_REG         )
#define TCRM ( TAINT | TAINT_COPY_REG_TO_MEM         )
#define TCRP ( TAINT | TAINT_COPY_REG_TO_PUSH        )
#define TCMP ( TAINT | TAINT_COPY_MEM_TO_PUSH        )
#define TCPR ( TAINT | TAINT_COPY_POP_TO_REG         )
#define TCPM ( TAINT | TAINT_COPY_POP_TO_MEM         )
#define TCAO ( TAINT | TAINT_COPY_AX_TO_OFFSET       )
#define TCOA ( TAINT | TAINT_COPY_OFFSET_TO_AX       )
#define TCSS ( TAINT | TAINT_COPY_STR_TO_STR         )
#define TCAS ( TAINT | TAINT_COPY_AX_TO_STR          )
#define TCSA ( TAINT | TAINT_COPY_STR_TO_AX          )
#define TZMR ( TAINT | TAINT_COPY_ZX_MEM_TO_REG      )
#define TSRM ( TAINT | TAINT_SWAP_REG_MEM            )
#define TSAR ( TAINT | TAINT_SWAP_AX_REG             )
#define TER  ( TAINT | TAINT_ERASE_REG               )
#define TEM  ( TAINT | TAINT_ERASE_MEM               )
#define TEP  ( TAINT | TAINT_ERASE_PUSH              )
#define TEH  ( TAINT | TAINT_ERASE_AXH               )
#define TED  ( TAINT | TAINT_ERASE_DX                )
#define TEA  ( TAINT | TAINT_ERASE_AX                )
#define TEAD ( TAINT | TAINT_ERASE_AX_DX             )
#define TPUA ( TAINT | TAINT_PUSHA                   )
#define TPPA ( TAINT | TAINT_POPA                    )
#define TLEA ( TAINT | TAINT_LEA                     )
#define TLVE ( TAINT | TAINT_LEAVE                   )
#define TENT ( TAINT | TAINT_ENTER                   )

#define BOMR ( TAINT | TAINT_BYTE_OR_MEM_TO_REG      )
#define BORM ( TAINT | TAINT_BYTE_OR_REG_TO_MEM      )
#define BXMR ( TAINT | TAINT_BYTE_XOR_MEM_TO_REG     )
#define BXRM ( TAINT | TAINT_BYTE_XOR_REG_TO_MEM     )
#define BCMR ( TAINT | TAINT_BYTE_COPY_MEM_TO_REG    )
#define BCRM ( TAINT | TAINT_BYTE_COPY_REG_TO_MEM    )
#define BCAO ( TAINT | TAINT_BYTE_COPY_AL_TO_OFFSET  )
#define BCOA ( TAINT | TAINT_BYTE_COPY_OFFSET_TO_AL  )
#define BCSS ( TAINT | TAINT_BYTE_COPY_STR_TO_STR    )
#define BCAS ( TAINT | TAINT_BYTE_COPY_AL_TO_STR     )
#define BCSA ( TAINT | TAINT_BYTE_COPY_STR_TO_AL     )
#define BZMR ( TAINT | TAINT_BYTE_COPY_ZX_MEM_TO_REG )
#define BSRM ( TAINT | TAINT_BYTE_SWAP_REG_MEM       )
#define BER  ( TAINT | TAINT_BYTE_ERASE_REG          )
#define BEM  ( TAINT | TAINT_BYTE_ERASE_MEM          )
#define BEA  ( TAINT | TAINT_BYTE_ERASE_AL           )

const unsigned char jit_action[] =
{
	[MAIN_OPTABLE] =
/*        ?0   ?1   ?2   ?3   ?4   ?5   ?6   ?7   ?8   ?9   ?A   ?B   ?C   ?D   ?E   ?F  */
/* 0? */ BORM,TORM,BOMR,TOMR,  C ,  C ,  C ,  C ,BORM,TORM,BOMR,TOMR,  C ,  C ,  C , BAD,
/* 1? */ BORM,TORM,BOMR,TOMR,  C ,  C ,  C ,  C ,BXRM,TXRM,BXMR,TXMR,  C ,  C ,  C ,  C ,
/* 2? */ BORM,TORM,BOMR,TOMR,  C ,  C , BAD,  C ,BXRM,TXRM,BXMR,TXMR,  C ,  C , BAD,  C ,
/* 3? */ BXRM,TXRM,BXMR,TXMR,  C ,  C , BAD,  C ,  C ,  C ,  C ,  C ,  C ,  C , BAD,  C ,
/* 4? */   C ,  C ,  C ,  C ,  C ,  C ,  C ,  C ,  C ,  C ,  C ,  C ,  C ,  C ,  C ,  C ,
/* 5? */ TCRP,TCRP,TCRP,TCRP,TCRP,TCRP,TCRP,TCRP,TCPR,TCPR,TCPR,TCPR,TCPR,TCPR,TCPR,TCPR,
/* 6? */ TPUA,TPPA,  C ,PRIV, BAD, BAD, BAD, BAD, TEP,TCMR, TEP,TCMR,PRIV,PRIV,PRIV,PRIV,
/* 7? */  JC , JC , JC , JC , JC , JC , JC , JC , JC , JC , JC , JC , JC , JC , JC , JC ,
/* 8? */   C ,  C ,  C ,  C ,  C ,  C ,BSRM,TSRM,BCRM,TCRM,BCMR,TCMR, TEM,TLEA,  C ,TCPM,
/* 9? */   C ,TSAR,TSAR,TSAR,TSAR,TSAR,TSAR,TSAR, TEH, TED,  CF,  C , TEP,  C ,  C , BEA,
/* A? */ BCOA,TCOA,BCAO,TCAO,BCSS,TCSS,  C ,  C ,  C ,  C ,BCAS,TCAS,BCSA,TCSA,  C ,  C ,
/* B? */  BER, BER, BER, BER, BER, BER, BER, BER, TER, TER, TER, TER, TER, TER, TER, TER,
/* C? */  XXX, XXX,  RC,  R , XXX, XXX, BEM, TEM,TENT,TLVE,  RF,  RF,  C , INT,  C ,  C ,
/* D? */   C ,  C , XXX, XXX,  C ,  C , XXX,  C ,  C ,  C ,  C ,  C ,  C ,  C ,  C ,  C ,
/* E? */   L ,  L ,  L ,  L ,PRIV,PRIV,PRIV,PRIV, CR , JR , JF , JR ,PRIV,PRIV,PRIV,PRIV,
/* F? */  BAD,  U , BAD, BAD,PRIV,  C , BAD, BAD,  C ,  C ,  C ,  C ,  C ,  C ,  C , BAD,

	[ESC_OPTABLE] =
/*        ?0   ?1   ?2   ?3   ?4   ?5   ?6   ?7   ?8   ?9   ?A   ?B   ?C   ?D   ?E   ?F */
/* 0? */ PRIV,PRIV,PRIV,PRIV,  C , SC ,  C ,  U ,  C ,  C ,  C ,  U ,  C ,  C ,  C ,  C ,
/* 1? */  MM , MM , MM , MM , MM , MM , MM , MM ,  C ,  C ,  C ,  C ,  C ,  C ,  C ,  C ,
/* 2? */ PRIV,PRIV,PRIV,PRIV,  C ,  C ,  C ,  C , MM , MM , MM , MM , MM , MM , MM , MM ,
/* 3? */ PRIV,TEAD,TEAD,TEAD, SE ,  C ,  C ,PRIV, BAD,  C , BAD,  C ,  C ,  C ,  C ,  C ,
/* 4? */ CMOV,CMOV,CMOV,CMOV,CMOV,CMOV,CMOV,CMOV,CMOV,CMOV,CMOV,CMOV,CMOV,CMOV,CMOV,CMOV,
/* 5? */   MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,
/* 6? */   MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,
/* 7? */   MM,  MM,  MM,  MM,  MM,  MM,  MM,  C ,PRIV,PRIV,  C ,  C ,  MM,  MM,  MM,  MM,
/* 8? */  JC , JC , JC , JC , JC , JC , JC , JC , JC , JC , JC , JC , JC , JC , JC , JC ,
/* 9? */  BEM, BEM, BEM, BEM, BEM, BEM, BEM, BEM, BEM, BEM, BEM, BEM, BEM, BEM, BEM, BEM,
/* A? */  TEP,  C ,CPUI,  C , XXX, XXX,  C ,  C , TEP,  C ,PRIV,  C , XXX, XXX, XXX,TOMR,
/* B? */  CX8, CXG,  C ,  C ,  C ,  C ,BZMR,TZMR, XXX,  C ,  C ,  C ,  C ,  C ,BZMR,TZMR,
/* C? */ BORM,TORM,  MM,TCRM,  MM,  MM,  MM,CX8B,  C ,  C ,  C ,  C ,  C ,  C ,  C ,  C ,
/* D? */   MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,
/* E? */   MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,
/* F? */   MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,  MM,

	[G38_OPTABLE] =
/*        ?0  ?1  ?2  ?3  ?4  ?5  ?6  ?7  ?8  ?9  ?A  ?B  ?MM ?D  ?E  ?F */
/* 0? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* 1? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* 2? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* 3? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* 4? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* 5? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* 6? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* 7? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* 8? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* 9? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* A? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* B? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* C? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* D? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* E? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* F? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,

	[G3A_OPTABLE] =
/*        ?0  ?1  ?2  ?3  ?4  ?5  ?6  ?7  ?8  ?9  ?A  ?B  ?C  ?D  ?E  ?F */
/* 0? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* 1? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* 2? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* 3? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* 4? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* 5? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* 6? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* 7? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* 8? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* 9? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* A? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* B? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* C? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* D? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* E? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,
/* F? */  MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM, MM,

	[GF6_OPTABLE] =
          C , U , C , C ,XXX,XXX,XXX,XXX,

	[GF7_OPTABLE] =
          C , U , C , C ,XXX,XXX,XXX,XXX,

	[GFF_OPTABLE] =
          C , C , CI, U , JI, U ,TCMP, U ,

	[BAD_OP] = U,

	[CUTOFF_OP] = JOIN,
};

long imm_at(char *addr, long size)
{
	long imm=0;
	memcpy(&imm, addr, size);
	if (size == 1)
		return *(signed char*)&imm;
	else
		return imm;
}

void imm_to(char *dest, long imm)
{
	memcpy(dest, &imm, sizeof(long));
}

int gen_code(char *dst, char *fmt, ...)
{
	/* format string parser for generating binary data
	 * usage:
	 *     [0-9A-F]{2}          generate byte from hex data
	 *
	 *     L                    generate little endian dword from
	 *                          arg[n]
	 *
	 *     S                    generate little endian word from
	 *                          arg[n]
	 *
	 *     .                    generate byte from arg[n]
	 *
	 *     ?                    generate byte from arg[n], but only if arg[n]
	 *                          is nonzero.
	 *
	 *     $                    copy (int)arg[n+1] bytes from (char*)arg[n]
	 *
	 *     &                    like %n in printf
	 *
	 *     +                    add (char)arg[n] to previously written byte
	 *
	 */
	va_list ap;
	va_start(ap, fmt);
	int i,j=0, c, outc=1;

	for (i=0; (c=fmt[i]); i++)
	{
		if ( (unsigned int)(c-'0') < 10 )
			outc = outc*16 + (c-'0');
		else if ( (unsigned int)(c-'A') < 6 )
			outc = outc*16 + (c-'A'+10);
		else if ( (unsigned int)(c-'a') < 6 )
			outc = outc*16 + (c-'a'+10);
		else switch (c)
		{
			case 'L': case 'l':
			{
				long l = va_arg(ap, long);
				imm_to(&dst[j], l);
				j += 4;
				break;
			}
			case 'S': case 's':
			{
				short s = va_arg(ap, int);
				dst[j]   = (char)s;
				dst[j+1] = (char)(s>>8);
				j += 2;
				break;
			}
			case '&':
			{
				int *ix = va_arg(ap, int *);
				*ix = j;
				break;
			}
			case '$':
			{
				char *s = va_arg(ap, char *);
				int len = va_arg(ap, int);
				memcpy(&dst[j], s, len);
				j+=len;
				break;
			}
			case '+':
			{
				char x = va_arg(ap, int);
				dst[j-1] += x;
				break;
			}
			case '.':
			{
				char x = va_arg(ap, int);
				dst[j] = x;
				j++;
				break;
			}
			case '?':
			{
				char x = va_arg(ap, int);
				if (x)
				{
					dst[j] = x;
					j++;
				}
				break;
			}
			default:
				break;
		}
		if (outc > 0xff)
		{
			dst[j] = (char)outc;
			outc = 1;
			j++;
		}
	}
	va_end(ap);
	return j;
}

int jump_to(char *dest, char *jmp_addr)
{
	dest[0] = '\xE9';
	imm_to(&dest[1], (long)jmp_addr - (long)&dest[5]);
	return 5;
}

int generate_hook(char *dest, char *addr, hook_func_t func)
{
	int imm_index;
	int len = gen_code(
		dest,

		"64 C7 05 L L"               /* movl func, hook */
		"64 C7 05 L L"               /* movl addr, user_eip */
		"64 C7 05 L & DEADBEEF",     /* movl &dest[len], jit_eip */

		offsetof(thread_ctx_t, hook_func), func,
		offsetof(thread_ctx_t, user_rip), addr,
		offsetof(thread_ctx_t, jit_rip), &imm_index
	);

	/* jump into runtime code */
	len += jump_to(&dest[len], (void *)(long)hook_stub);
	imm_to(&dest[imm_index], (long)dest+len);
	return len;
}

static int generate_linux_sysenter(char *dest, trans_t *trans)
{
	int len = gen_code(
		dest,

		"66 0f ef ed"    /* clear ijmp taint register (pxor %xmm5,%xmm5 */
	);

	/* jump into runtime code */
	len += jump_to(&dest[len], (void *)(long)linux_sysenter_emu);
	*trans = (trans_t){ .len=len };
	return len;
}

static int generate_linux_syscall(char *dest, instr_t *instr, trans_t *trans)
{
	int len = gen_code(
		dest,

		"66 0f ef ed"    /* clear ijmp taint register (pxor %xmm5,%xmm5 */
		"64 48 C7 04 25 L L",     /* movq $post_addr, user_eip */

		offsetof(thread_ctx_t, user_rip), &instr->addr[instr->len]
	);

	/* jump into runtime code */
	len += jump_to(&dest[len], (void *)(long)linux_syscall_emu);
	*trans = (trans_t){ .len=len };
	return len;
}

static int generate_int80(char *dest, instr_t *instr, trans_t *trans)
{
	/* save origin, original address */
	int len = gen_code(
		dest,

		"66 0f ef ed"    /* clear ijmp taint register (pxor %xmm5,%xmm5 */
		"64 C7 05 L L",     /* movl $post_addr, user_eip */

		offsetof(thread_ctx_t, user_rip), &instr->addr[instr->len]
	);

	/* jump into runtime code */
	len += jump_to(&dest[len], (void *)(long)int80_emu);
	*trans = (trans_t){ .len=len };
	return len;
}

static int generate_cpuid(char *dest, instr_t *instr, trans_t *trans)
{
	/* save origin, jit_address */
	int retaddr_index;
	int len = gen_code(
		dest,

		"64 C7 05 L &DEADBEEF",    /* movl $post_addr, jit_eip */

		offsetof(thread_ctx_t, jit_rip), &retaddr_index
	);

	/* jump into runtime code */
	len += jump_to(&dest[len], (void *)(long)cpuid_emu);
	*trans = (trans_t){ .len=len };
	imm_to(&dest[retaddr_index], (long)dest+trans->len);
	return len;
}

static int generate_ijump_tail(char *dest)
{
	return jump_to(dest, (char *)(long)runtime_ijmp);
}

static int generate_ijump(char *dest, instr_t *instr, trans_t *trans)
{
	long mrm_len = instr->len - instr->mrm;
	int len_taint=0, i;

	if ( taint_flag == TAINT_ON )
		len_taint = taint_ijmp(dest, instr->p[2], &instr->addr[instr->mrm], TAINT_OFFSET);
	else
		len_taint = gen_code(dest, "66 0f ef ed");

	int len = len_taint+gen_code(
		&dest[len_taint],

		"66 0F 3A 22 E1 00" /* pinsrd $0, %ecx, %xmm4       */
		"66 0F 3A 22 D8 00" /* pinsrd $0, %eax, %xmm3       */
		"? 8B &$"           /* mov ... ( -> %eax )          */
		"66 0F 3A 16 E9 00",/* pextrd $0, %xmm5, %ecx       */

		instr->p[2], &i, &instr->addr[instr->mrm], mrm_len
	);

	dest[len_taint+i] &= 0xC7; /* -> %eax */
	len += generate_ijump_tail(&dest[len]);
	*trans = (trans_t){ .len = len };

	return len;
}

static int generate_call(char *dest, char *jmp_addr,
                         instr_t *instr, trans_t *trans,
                         char *map, unsigned long map_len)
{
	int hash = HASH_INDEX(&instr->addr[instr->len]);
	int len_taint=0, retaddr_index, len;

	if ( taint_flag == TAINT_ON )
		len_taint = taint_erase_push32(dest, TAINT_OFFSET);

	if ( call_strategy == PRESEED_ON_CALL )
	{
		/* As a speed optimisation, we insert the return address directly
		 * into the cache, this makes relocating code more messy though :-(
		 */
		len = len_taint+gen_code(
			&dest[len_taint],

			"68 L"                   /* push $retaddr                                        */
			"64 C7 05 L L"           /* movl $addr,     jmp_cache[HASH_INDEX(addr)].addr     */
			"64 C7 05 L & DEADBEEF", /* movl $jit_addr, jmp_cache[HASH_INDEX(addr)].jit_addr */

			&instr->addr[instr->len],
			hash*8, CACHE_MANGLE(&instr->addr[instr->len]),
			hash*8+4, &retaddr_index
		);
		retaddr_index += len_taint;
	}
	else if ( call_strategy == PREFETCH_ON_CALL )
	{
		len = len_taint+gen_code(
			&dest[len_taint],

			"68 L"                /* push $retaddr                                        */
			"64 0F 18 0D L",      /* prefetch jmp_cache[HASH_INDEX(addr)]                 */

			&instr->addr[instr->len],
			hash*4
		);
	}
	else
	{
		len = len_taint+gen_code(
			&dest[len_taint],

			"68 L",               /* push $retaddr                                        */

			&instr->addr[instr->len]
		);
	}

	generate_jump(&dest[len], jmp_addr, trans, map, map_len);
	if (trans->imm)
		trans->imm += len;
		trans->len += len;

	if ( call_strategy == PRESEED_ON_CALL )
		imm_to(&dest[retaddr_index], (long)dest+trans->len);

	return trans->len;
}

static int generate_icall(char *dest, instr_t *instr, trans_t *trans)
{
	int hash = HASH_INDEX(&instr->addr[instr->len]);
	long mrm_len = instr->len - instr->mrm;
	int len_taint=0, mrm, retaddr_index, len;

	/* XXX FUGLY as a speed optimisation, we insert the return address
	 * directly into the cache, this makes relocating code more messy.
	 * proposed fix: scan memory for call instructions upon relocation,
	 * change the address in-place
	 */
	if ( taint_flag == TAINT_ON )
		len_taint = taint_icall(dest, instr->p[2], &instr->addr[instr->mrm], TAINT_OFFSET);
	else
		len_taint = gen_code(dest, "66 0f ef ed");

	if ( call_strategy == PRESEED_ON_CALL )
	{
		len = len_taint+gen_code(
			&dest[len_taint],

			"66 0F 3A 22 E1 00"      /* pinsrd $0, %ecx, %xmm4       */
			"66 0F 3A 22 D8 00"      /* pinsrd $0, %eax, %xmm3       */
			"? 8B &$"                /* mov ... ( -> %eax )                                  */
			"66 0F 3A 16 E9 00"      /* pextrd $0, %xmm5, %ecx       */
			"68 L"                   /* push $retaddr                                        */
			"64 C7 05 L L"           /* movl $addr,     jmp_cache[HASH_INDEX(addr)].addr     */
			"64 C7 05 L & DEADBEEF", /* movl $jit_addr, jmp_cache[HASH_INDEX(addr)].jit_addr */

			instr->p[2], &mrm, &instr->addr[instr->mrm], mrm_len,
			&instr->addr[instr->len],
			hash*8, CACHE_MANGLE(&instr->addr[instr->len]),
			hash*8+4, &retaddr_index
		);
	}
	else if ( call_strategy == PREFETCH_ON_CALL )
	{
		len = len_taint+gen_code(
			&dest[len_taint],

			"66 0F 3A 22 E1 00"   /* pinsrd $0, %ecx, %xmm4       */
			"66 0F 3A 22 D8 00"   /* pinsrd $0, %eax, %xmm3       */
			"? 8B &$"             /* mov ... ( -> %eax )                                  */
			"66 0F 3A 16 E9 00"   /* pextrd $0, %xmm5, %ecx       */
			"68 L"                /* push $retaddr                                        */
			"64 0F 18 0D L",      /* prefetch jmp_cache[HASH_INDEX(addr)]                 */

			instr->p[2], &mrm, &instr->addr[instr->mrm], mrm_len,
			&instr->addr[instr->len],
			hash*8
		);
	}
	else
	{
		len = len_taint+gen_code(
			&dest[len_taint],

			"66 0F 3A 22 E1 00"   /* pinsrd $0, %ecx, %xmm4       */
			"66 0F 3A 22 D8 00"   /* pinsrd $0, %eax, %xmm3       */
			"? 8B &$"             /* mov ... ( -> %eax )                                  */
			"66 0F 3A 16 E9 00"   /* pextrd $0, %xmm5, %ecx       */
			"68 L",               /* push $retaddr                                        */

			instr->p[2], &mrm, &instr->addr[instr->mrm], mrm_len,
			&instr->addr[instr->len]
		);
	}

	dest[len_taint+mrm] &= 0xC7; /* -> %eax */
	len += generate_ijump_tail(&dest[len]);

	if ( call_strategy == PRESEED_ON_CALL )
		imm_to(&dest[len_taint+retaddr_index], ((long)dest)+len);

	*trans = (trans_t){ .len = len };
	return len;
}

static int generate_ret(char *dest, char *addr, trans_t *trans)
{
	int len = jump_to(dest, (void *)(long)runtime_ret);
	*trans = (trans_t){ .len=len };
	return len;
}

static int generate_ret_cleanup(char *dest, char *addr, trans_t *trans)
{
	int len = gen_code(
		dest,

		"66 0F 3A 22 E1 00" /* pinsrd $0, %ecx, %xmm4       */
		"66 0F 3A 22 D8 00" /* pinsrd $0, %eax, %xmm3       */
		"8b 8C 24 L"        /* mov TAINT_OFFSET(%esp), %ecx */
		"58"                /* pop %eax                     */
		"8D A4 24 S 00 00", /* lea N(%esp),%esp             */

		TAINT_OFFSET,
		addr[1] + (addr[2]<<8)
	);

	len += generate_ijump_tail(&dest[len]);
	*trans = (trans_t){ .len=len };

	return len;
}

static int generate_cross_map_jump(char *dest, char *jmp_addr, trans_t *trans)
{
	int len = gen_code(
		dest,
		"66 0F 3A 22 E1 00"   /* pinsrd $0, %ecx, %xmm4       */
		"66 0F 3A 22 D8 00"   /* pinsrd $0, %eax, %xmm3       */
		"B8 L"                /* mov jmp_addr, %eax           */
		"B9 00 00 00 00",     /* mov $0x0,%ecx                */

		jmp_addr
	);
	len += generate_ijump_tail(&dest[len]);
	*trans = (trans_t){ .len=len };
	return len;
}

int generate_jump(char *dest, char *jmp_addr, trans_t *trans,
                  char *map, unsigned long map_len)
{
	if (contains(map, map_len, jmp_addr))
	{
		dest[0] = '\xE9'; /* jmp i32 */
		*trans = (trans_t){ .jmp_addr=jmp_addr, .imm=1, .len=5 };
		return trans->len;
	}
	else
		return generate_cross_map_jump(dest, jmp_addr,  trans);
}

static int generate_jcc(char *dest, char *jmp_addr, int cond, trans_t *trans,
                        char *map, unsigned long map_len)
{
	if (contains(map, map_len, jmp_addr))
	{
		dest[0] = '\x0F'; /* jcc i32 */
		dest[1] = '\x80'+cond;
		*trans = (trans_t){ .jmp_addr=jmp_addr, .imm=2, .len=6 };
		return trans->len;
	}
	else
	{
		int stub_len = generate_cross_map_jump(&dest[2], jmp_addr, trans);
		trans->len = stub_len+2;
		dest[0] = '\x70'+ (cond^1); /* j!cc over( jmp *mem ) */
		dest[1] = stub_len;
		return trans->len;
	}
}

int generate_ill(char *dest, trans_t *trans)
{
	dest[0] = '\x0F';
	dest[1] = '\x0B';
	*trans = (trans_t){ .len=2 };
	return 2;
}

static int generate_cmov(char *dest, instr_t *instr, trans_t *trans)
{
	int taint_len = 0, mrm = instr->mrm, len = instr->len;
	char *addr = instr->addr;

	if ( (instr->p[2] == 0) && (taint_flag == TAINT_ON) ) /* we don't do segments (yet?) */
	{
		if (instr->p[3] == 0x66)
			taint_len = taint_copy_mem16_to_reg16(&dest[2], &addr[mrm], TAINT_OFFSET);
		else
			taint_len = taint_copy_mem32_to_reg32(&dest[2], &addr[mrm], TAINT_OFFSET);
	}
	dest[0] = '\x70' + ( (addr[mrm-1]&0xf) ^ 1 );
	dest[1] = taint_len+len-1;
	memcpy(&dest[2+taint_len], addr, mrm-2);
	dest[taint_len+mrm] = '\x8b';
	memcpy(&dest[taint_len+mrm+1], &addr[mrm], len-mrm);
	*trans = (trans_t){ .len = taint_len+len+1 };
	return trans->len;
}

static int copy_instr(char *dest, instr_t *instr, trans_t *trans)
{
	memcpy(dest, instr->addr, instr->len);
	*trans = (trans_t){ .len = instr->len };
	return trans->len;
}

static int taint_cmpxchg8(char *dest, instr_t *instr, trans_t *trans)
{
	int len = 0;

	if (!instr->p[2])
		len += taint_cmpxchg8_pre(&dest[len], &instr->addr[instr->mrm], TAINT_OFFSET);

	copy_instr(&dest[len], instr, trans);
	len += trans->len;

	if (!instr->p[2])
		len += taint_cmpxchg8_post(&dest[len], &instr->addr[instr->mrm], TAINT_OFFSET);
	return trans->len = len;
}

static int taint_cmpxchg(char *dest, instr_t *instr, trans_t *trans)
{
	int op16 = (instr->p[3] == 0x66);
	int len = 0;

	if (!instr->p[2])
		len += (op16?taint_cmpxchg16_pre:taint_cmpxchg32_pre)(&dest[len], &instr->addr[instr->mrm], TAINT_OFFSET);

	copy_instr(&dest[len], instr, trans);
	len += trans->len;

	if (!instr->p[2])
		len += (op16?taint_cmpxchg16_post:taint_cmpxchg32_post)(&dest[len], &instr->addr[instr->mrm], TAINT_OFFSET);

	return trans->len = len;
}

static int taint_cmpxchg8b(char *dest, instr_t *instr, trans_t *trans)
{
	int len = 0;

	if (!instr->p[2])
		len += taint_cmpxchg8b_pre(&dest[len], &instr->addr[instr->mrm], TAINT_OFFSET);

	copy_instr(&dest[len], instr, trans);
	len += trans->len;

	if (!instr->p[2])
		len += taint_cmpxchg8b_post(&dest[len], &instr->addr[instr->mrm], TAINT_OFFSET);

	return trans->len = len;
}

static int taint_rep(char *dest, instr_t *instr, trans_t *trans)
{
	int act = jit_action[instr->op]^TAINT, op16 = (instr->p[3] == 0x66);

	if (instr->p[2]) /* we don't do segments (yet?) */
		return copy_instr(dest, instr, trans);

	int len = 2;

	dest[0] = '\xe3';

	if ( taint_flag == TAINT_ON )
		len +=(op16 && taint_ops[act].off.f16 ? taint_ops[act].off.f16 : taint_ops[act].off.f)
		      (&dest[len], TAINT_OFFSET);

	if ( instr->p[3] )
	{
		dest[len] = instr->p[3];
		len++;
	}
	if ( instr->p[4] )
	{
		dest[len] = instr->p[4];
		len++;
	}
	dest[len] = instr->op;
	dest[len+1] = '\xe2';
	len += 3;

	dest[1] = len-2;
	dest[len-1] = -len;

	*trans = (trans_t){ .len = len };
	return len;
}

static int taint_instr(char *dest, instr_t *instr, trans_t *trans)
{
	int len = 0, act = jit_action[instr->op]^TAINT, op16 = (instr->p[3] == 0x66);

	if (instr->p[2]) /* we don't do segments (yet?) */
	{
		act = segment_prefix_mapping[act];
		if ( !act )
			return copy_instr(dest, instr, trans);
	}

	if ( taint_flag == TAINT_ON )
	{
		if (TAINT_MRM_OP(act))
			len = (op16 && taint_ops[act].mrm.f16 ? taint_ops[act].mrm.f16 : taint_ops[act].mrm.f)
			      (dest, &instr->addr[instr->mrm], TAINT_OFFSET);

		else if (TAINT_REG_OFF_OP( act ))
			len = (op16 && taint_ops[act].reg_off.f16 ? taint_ops[act].reg_off.f16 : taint_ops[act].reg_off.f)
			      (dest, instr->addr[instr->mrm-1]&7, TAINT_OFFSET);

		else if (TAINT_REG_OP( act ))
			len = (op16 && taint_ops[act].reg.f16 ? taint_ops[act].reg.f16 : taint_ops[act].reg.f)
			      (dest, instr->addr[instr->mrm-1]&7);

		else if (TAINT_OFF_OP( act ))
			len = (op16 && taint_ops[act].off.f16 ? taint_ops[act].off.f16 : taint_ops[act].off.f)
			      (dest, TAINT_OFFSET);

		else if (TAINT_IMPL_OP( act ))
			len = (op16 && taint_ops[act].impl.f16 ? taint_ops[act].impl.f16 : taint_ops[act].impl.f)
			      (dest);

		else if (TAINT_ADDR_OP( act ))
			len = (op16 && taint_ops[act].addr.f16 ? taint_ops[act].addr.f16 : taint_ops[act].addr.f)
			      (dest, *(long*)&instr->addr[instr->imm], TAINT_OFFSET);
	}

	len += copy_instr(&dest[len], instr, trans);
	*trans = (trans_t){ .len = len };
	return len;
}

static void translate_control(char *dest, instr_t *instr, trans_t *trans,
                              char *map, unsigned long map_len)
{
	char *pc = instr->addr+instr->len;
	long imm=0, imm_len, off;

	imm_len=instr->len-instr->imm;
	if (jit_action[instr->op] == JUMP_FAR)
		imm_len -= 2;

	imm = imm_at(&instr->addr[instr->imm], imm_len);

	switch (jit_action[instr->op])
	{
		case JUMP_CONDITIONAL:
			generate_jcc(dest, pc+imm, instr->addr[instr->mrm-1]&0x0f,
			             trans, map, map_len);
			break;
		case JUMP_RELATIVE:
			generate_jump(dest, pc+imm, trans, map, map_len);
			break;
		case JUMP_FAR:
			generate_jump(dest, (char*)imm, trans, map, map_len);
			break;
		case JUMP_INDIRECT:
			generate_ijump(dest, instr, trans);
			break;
		case CALL_INDIRECT:
			generate_icall(dest, instr, trans);
			break;
		case RETURN:
			generate_ret(dest, instr->addr, trans);
			break;
		case RETURN_CLEANUP: /* return from call and pop some things */
			generate_ret_cleanup(dest, instr->addr, trans);
			break;
		case LOOP: /* loops */
			off = gen_code(
				dest,

				"$  02"
				"EB 05",

				instr->addr, instr->len-1
			);

			generate_jump(&dest[off], pc+imm, trans, map, map_len);
			if (trans->imm)
				trans->imm += off;
			trans->len += off;
			break;
		case CALL_RELATIVE:
			generate_call(dest, pc+imm, instr, trans, map, map_len);
			break;
		case JOIN:
			generate_ill(dest, trans);
			break;
		default:
			die("unimplemented action: %d", jit_action[instr->op]);
	}

	if ( imm_len == 2 )
		trans->jmp_addr = (char *)((long)trans->jmp_addr & 0xffffL);
}

void translate_op(char *dest, instr_t *instr, trans_t *trans,
                  char *map, unsigned long map_len)
{
	int action = jit_action[instr->op];

	if ( (action & CONTROL_MASK) == CONTROL )
		translate_control(dest, instr, trans, map, map_len);
	else if ( TAINT_STRING_OP(action) &&
	          ( instr->p[1] == 0xf2 || instr->p[1] == 0xf3 ) )
		taint_rep(dest, instr, trans);
	else if ( (action & TAINT_MASK) == TAINT )
		taint_instr(dest, instr, trans);
	else if (action == COPY_INSTRUCTION)
		copy_instr(dest, instr, trans);
	else if (action == CONDITIONAL_MOVE)
		generate_cmov(dest, instr, trans);
	else if ( (action == UNDEFINED_INSTRUCTION) || (action == JOIN) )
		generate_ill(dest, trans);
	else if (action == INT)
	{
		if (instr->addr[1] == '\x80')
			generate_int80(dest, instr, trans);
		else
			copy_instr(dest, instr, trans);
	}
	else if (action == SYSENTER)
		generate_linux_sysenter(dest, trans);
	else if (action == SYSCALL)
		generate_linux_syscall(dest, instr, trans);
	else if (action == CMPXCHG8)
		taint_cmpxchg8(dest, instr, trans);
	else if (action == CMPXCHG)
		taint_cmpxchg(dest, instr, trans);
	else if (action == CMPXCHG8B)
		taint_cmpxchg8b(dest, instr, trans);
	else if (action == CPUID)
		generate_cpuid(dest, instr, trans);
	else
			die("unimplemented action: %d", action);
}

