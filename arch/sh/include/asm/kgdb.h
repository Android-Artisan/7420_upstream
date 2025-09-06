/*
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Based on original code by Glenn Engel, Jim Kingdon,
 * David Grothe <dave@gcom.com>, Tigran Aivazian, <tigran@sco.com> and
 * Amit S. Kale <akale@veritas.com>
 * 
 * Super-H port based on sh-stub.c (Ben Lee and Steve Chamberlain) by
 * Henry Bell <henry.bell@st.com>
 * 
 * Header file for low-level support for remote debug using GDB. 
 *
 */

#ifndef __KGDB_H
#define __KGDB_H

#include <asm/ptrace.h>

/* Same as pt_regs but has vbr in place of syscall_nr */
struct kgdb_regs {
        unsigned long regs[16];
        unsigned long pc;
        unsigned long pr;
        unsigned long sr;
        unsigned long gbr;
        unsigned long mach;
        unsigned long macl;
        unsigned long vbr;
};

/* State info */
extern char kgdb_in_gdb_mode;
extern int kgdb_nofault;	/* Ignore bus errors (in gdb mem access) */
extern char in_nmi;		/* Debounce flag to prevent NMI reentry*/

/* SCI */
extern int kgdb_portnum;
extern int kgdb_baud;
extern char kgdb_parity;
extern char kgdb_bits;

/* Init and interface stuff */
extern int kgdb_init(void);
extern int (*kgdb_getchar)(void);
extern void (*kgdb_putchar)(int);

/* Trap functions */
typedef void (kgdb_debug_hook_t)(struct pt_regs *regs);
typedef void (kgdb_bus_error_hook_t)(void);
extern kgdb_debug_hook_t  *kgdb_debug_hook;
extern kgdb_bus_error_hook_t *kgdb_bus_err_hook;

/* Console */
struct console;
void kgdb_console_write(struct console *co, const char *s, unsigned count);
extern int kgdb_console_setup(struct console *, char *);

/* Prototypes for jmp fns */
#define _JBLEN 9
typedef        int jmp_buf[_JBLEN];
extern void    longjmp(jmp_buf __jmpb, int __retval);
extern int     setjmp(jmp_buf __jmpb);

/* Forced breakpoint */
#define breakpoint()	__asm__ __volatile__("trapa   #0x3c")

#endif
#ifndef __ASM_SH_KGDB_H
#define __ASM_SH_KGDB_H

#include <asm/cacheflush.h>
#include <asm/ptrace.h>

enum regnames {
	GDB_R0, GDB_R1, GDB_R2, GDB_R3, GDB_R4, GDB_R5, GDB_R6, GDB_R7,
	GDB_R8, GDB_R9, GDB_R10, GDB_R11, GDB_R12, GDB_R13, GDB_R14, GDB_R15,

	GDB_PC, GDB_PR, GDB_SR, GDB_GBR, GDB_MACH, GDB_MACL, GDB_VBR,
};

#define _GP_REGS	16
#define _EXTRA_REGS	7
#define GDB_SIZEOF_REG	sizeof(u32)

#define DBG_MAX_REG_NUM	(_GP_REGS + _EXTRA_REGS)
#define NUMREGBYTES	(DBG_MAX_REG_NUM * sizeof(GDB_SIZEOF_REG))

static inline void arch_kgdb_breakpoint(void)
{
	__asm__ __volatile__ ("trapa #0x3c\n");
}

#define BREAK_INSTR_SIZE	2
#define BUFMAX			2048

#ifdef CONFIG_SMP
# define CACHE_FLUSH_IS_SAFE	0
#else
# define CACHE_FLUSH_IS_SAFE	1
#endif

#define GDB_ADJUSTS_BREAK_OFFSET

#endif /* __ASM_SH_KGDB_H */
