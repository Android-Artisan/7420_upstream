/*
 *  include/asm-s390/setup.h
 *
 *  S390 version
 *    Copyright IBM Corp. 1999,2006
 */

#ifndef _ASM_S390_SETUP_H
#define _ASM_S390_SETUP_H

#define COMMAND_LINE_SIZE	1024

#define ARCH_COMMAND_LINE_SIZE	896

#ifdef __KERNEL__

#include <asm/types.h>

#define PARMAREA		0x10400
#define MEMORY_CHUNKS		256

#ifndef __ASSEMBLY__

#ifndef __s390x__
#define IPL_DEVICE        (*(unsigned long *)  (0x10404))
#define INITRD_START      (*(unsigned long *)  (0x1040C))
#define INITRD_SIZE       (*(unsigned long *)  (0x10414))
#else /* __s390x__ */
#define IPL_DEVICE        (*(unsigned long *)  (0x10400))
#define INITRD_START      (*(unsigned long *)  (0x10408))
#define INITRD_SIZE       (*(unsigned long *)  (0x10410))
#endif /* __s390x__ */
#define COMMAND_LINE      ((char *)            (0x10480))

#define CHUNK_READ_WRITE 0
#define CHUNK_READ_ONLY  1

struct mem_chunk {
	unsigned long addr;
	unsigned long size;
	int type;
};

extern struct mem_chunk memory_chunk[];
extern unsigned long real_memory_size;

void detect_memory_layout(struct mem_chunk chunk[]);

#ifdef CONFIG_S390_SWITCH_AMODE
extern unsigned int switch_amode;
#else
#define switch_amode	(0)
#endif

#ifdef CONFIG_S390_EXEC_PROTECT
extern unsigned int s390_noexec;
#else
#define s390_noexec	(0)
#endif
 *  S390 version
 *    Copyright IBM Corp. 1999, 2010
 */
#ifndef _ASM_S390_SETUP_H
#define _ASM_S390_SETUP_H

#include <linux/const.h>
#include <uapi/asm/setup.h>


#define PARMAREA		0x10400

/*
 * Machine features detected in head.S
 */
extern unsigned long machine_flags;

#define MACHINE_FLAG_VM		(1UL << 0)
#define MACHINE_FLAG_IEEE	(1UL << 1)
#define MACHINE_FLAG_CSP	(1UL << 3)
#define MACHINE_FLAG_MVPG	(1UL << 4)
#define MACHINE_FLAG_DIAG44	(1UL << 5)
#define MACHINE_FLAG_IDTE	(1UL << 6)
#define MACHINE_FLAG_DIAG9C	(1UL << 7)
#define MACHINE_FLAG_MVCOS	(1UL << 8)
#define MACHINE_FLAG_KVM	(1UL << 9)
#define MACHINE_FLAG_HPAGE	(1UL << 10)
#define MACHINE_FLAG_PFMF	(1UL << 11)

#define MACHINE_IS_VM		(machine_flags & MACHINE_FLAG_VM)
#define MACHINE_IS_KVM		(machine_flags & MACHINE_FLAG_KVM)
#define MACHINE_HAS_DIAG9C	(machine_flags & MACHINE_FLAG_DIAG9C)

#ifndef __s390x__
#define MACHINE_HAS_IEEE	(machine_flags & MACHINE_FLAG_IEEE)
#define MACHINE_HAS_CSP		(machine_flags & MACHINE_FLAG_CSP)
#define MACHINE_HAS_IDTE	(0)
#define MACHINE_HAS_DIAG44	(1)
#define MACHINE_HAS_MVPG	(machine_flags & MACHINE_FLAG_MVPG)
#define MACHINE_HAS_MVCOS	(0)
#define MACHINE_HAS_HPAGE	(0)
#define MACHINE_HAS_PFMF	(0)
#else /* __s390x__ */
#define MACHINE_HAS_IEEE	(1)
#define MACHINE_HAS_CSP		(1)
#define MACHINE_HAS_IDTE	(machine_flags & MACHINE_FLAG_IDTE)
#define MACHINE_HAS_DIAG44	(machine_flags & MACHINE_FLAG_DIAG44)
#define MACHINE_HAS_MVPG	(1)
#define MACHINE_HAS_MVCOS	(machine_flags & MACHINE_FLAG_MVCOS)
#define MACHINE_HAS_HPAGE	(machine_flags & MACHINE_FLAG_HPAGE)
#define MACHINE_HAS_PFMF	(machine_flags & MACHINE_FLAG_PFMF)
#endif /* __s390x__ */

#define ZFCPDUMP_HSA_SIZE	(32UL<<20)

#define MACHINE_FLAG_VM		_BITUL(0)
#define MACHINE_FLAG_IEEE	_BITUL(1)
#define MACHINE_FLAG_CSP	_BITUL(2)
#define MACHINE_FLAG_MVPG	_BITUL(3)
#define MACHINE_FLAG_DIAG44	_BITUL(4)
#define MACHINE_FLAG_IDTE	_BITUL(5)
#define MACHINE_FLAG_DIAG9C	_BITUL(6)
#define MACHINE_FLAG_KVM	_BITUL(8)
#define MACHINE_FLAG_ESOP	_BITUL(9)
#define MACHINE_FLAG_EDAT1	_BITUL(10)
#define MACHINE_FLAG_EDAT2	_BITUL(11)
#define MACHINE_FLAG_LPAR	_BITUL(12)
#define MACHINE_FLAG_LPP	_BITUL(13)
#define MACHINE_FLAG_TOPOLOGY	_BITUL(14)
#define MACHINE_FLAG_TE		_BITUL(15)
#define MACHINE_FLAG_TLB_LC	_BITUL(17)
#define MACHINE_FLAG_VX		_BITUL(18)
#define MACHINE_FLAG_CAD	_BITUL(19)

#define LPP_MAGIC		_BITUL(31)
#define LPP_PFAULT_PID_MASK	_AC(0xffffffff, UL)

#ifndef __ASSEMBLY__

#include <asm/lowcore.h>
#include <asm/types.h>

#define IPL_DEVICE        (*(unsigned long *)  (0x10400))
#define INITRD_START      (*(unsigned long *)  (0x10408))
#define INITRD_SIZE       (*(unsigned long *)  (0x10410))
#define OLDMEM_BASE	  (*(unsigned long *)  (0x10418))
#define OLDMEM_SIZE	  (*(unsigned long *)  (0x10420))
#define COMMAND_LINE      ((char *)            (0x10480))

extern int memory_end_set;
extern unsigned long memory_end;
extern unsigned long max_physmem_end;

extern void detect_memory_memblock(void);

#define MACHINE_IS_VM		(S390_lowcore.machine_flags & MACHINE_FLAG_VM)
#define MACHINE_IS_KVM		(S390_lowcore.machine_flags & MACHINE_FLAG_KVM)
#define MACHINE_IS_LPAR		(S390_lowcore.machine_flags & MACHINE_FLAG_LPAR)

#define MACHINE_HAS_DIAG9C	(S390_lowcore.machine_flags & MACHINE_FLAG_DIAG9C)
#define MACHINE_HAS_ESOP	(S390_lowcore.machine_flags & MACHINE_FLAG_ESOP)
#define MACHINE_HAS_PFMF	MACHINE_HAS_EDAT1
#define MACHINE_HAS_HPAGE	MACHINE_HAS_EDAT1

#define MACHINE_HAS_IDTE	(S390_lowcore.machine_flags & MACHINE_FLAG_IDTE)
#define MACHINE_HAS_DIAG44	(S390_lowcore.machine_flags & MACHINE_FLAG_DIAG44)
#define MACHINE_HAS_EDAT1	(S390_lowcore.machine_flags & MACHINE_FLAG_EDAT1)
#define MACHINE_HAS_EDAT2	(S390_lowcore.machine_flags & MACHINE_FLAG_EDAT2)
#define MACHINE_HAS_LPP		(S390_lowcore.machine_flags & MACHINE_FLAG_LPP)
#define MACHINE_HAS_TOPOLOGY	(S390_lowcore.machine_flags & MACHINE_FLAG_TOPOLOGY)
#define MACHINE_HAS_TE		(S390_lowcore.machine_flags & MACHINE_FLAG_TE)
#define MACHINE_HAS_TLB_LC	(S390_lowcore.machine_flags & MACHINE_FLAG_TLB_LC)
#define MACHINE_HAS_VX		(S390_lowcore.machine_flags & MACHINE_FLAG_VX)
#define MACHINE_HAS_CAD		(S390_lowcore.machine_flags & MACHINE_FLAG_CAD)

/*
 * Console mode. Override with conmode=
 */
extern unsigned int console_mode;
extern unsigned int console_devno;
extern unsigned int console_irq;

extern char vmhalt_cmd[];
extern char vmpoff_cmd[];

#define CONSOLE_IS_UNDEFINED	(console_mode == 0)
#define CONSOLE_IS_SCLP		(console_mode == 1)
#define CONSOLE_IS_3215		(console_mode == 2)
#define CONSOLE_IS_3270		(console_mode == 3)
#define SET_CONSOLE_SCLP	do { console_mode = 1; } while (0)
#define SET_CONSOLE_3215	do { console_mode = 2; } while (0)
#define SET_CONSOLE_3270	do { console_mode = 3; } while (0)

#define NSS_NAME_SIZE	8
extern char kernel_nss_name[];

#else /* __ASSEMBLY__ */

#ifndef __s390x__
#define IPL_DEVICE        0x10404
#define INITRD_START      0x1040C
#define INITRD_SIZE       0x10414
#else /* __s390x__ */
#define IPL_DEVICE        0x10400
#define INITRD_START      0x10408
#define INITRD_SIZE       0x10410
#endif /* __s390x__ */
#define COMMAND_LINE      0x10480

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#ifdef CONFIG_PFAULT
extern int pfault_init(void);
extern void pfault_fini(void);
#else /* CONFIG_PFAULT */
#define pfault_init()		({-1;})
#define pfault_fini()		do { } while (0)
#endif /* CONFIG_PFAULT */

extern void cmma_init(void);

extern void (*_machine_restart)(char *command);
extern void (*_machine_halt)(void);
extern void (*_machine_power_off)(void);

#else /* __ASSEMBLY__ */

#define IPL_DEVICE        0x10400
#define INITRD_START      0x10408
#define INITRD_SIZE       0x10410
#define OLDMEM_BASE	  0x10418
#define OLDMEM_SIZE	  0x10420
#define COMMAND_LINE      0x10480

#endif /* __ASSEMBLY__ */
#endif /* _ASM_S390_SETUP_H */
