/*
 *  include/asm-s390/sigp.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *               Heiko Carstens (heiko.carstens@de.ibm.com)
 *
 *  sigp.h by D.J. Barrow (c) IBM 1999
 *  contains routines / structures for signalling other S/390 processors in an
 *  SMP configuration.
 */

#ifndef __SIGP__
#define __SIGP__

#include <asm/ptrace.h>
#include <asm/atomic.h>

/* get real cpu address from logical cpu number */
extern volatile int __cpu_logical_map[];

typedef enum
{
	sigp_unassigned=0x0,
	sigp_sense,
	sigp_external_call,
	sigp_emergency_signal,
	sigp_start,
	sigp_stop,
	sigp_restart,
	sigp_unassigned1,
	sigp_unassigned2,
	sigp_stop_and_store_status,
	sigp_unassigned3,
	sigp_initial_cpu_reset,
	sigp_cpu_reset,
	sigp_set_prefix,
	sigp_store_status_at_address,
	sigp_store_extended_status_at_address
} sigp_order_code;

typedef __u32 sigp_status_word;

typedef enum
{
        sigp_order_code_accepted=0,
	sigp_status_stored,
	sigp_busy,
	sigp_not_operational
} sigp_ccode;


/*
 * Definitions for the external call
 */

/* 'Bit' signals, asynchronous */
typedef enum
{
	ec_schedule=0,
	ec_call_function,
	ec_bit_last
} ec_bit_sig;

/*
 * Signal processor
 */
static inline sigp_ccode
signal_processor(__u16 cpu_addr, sigp_order_code order_code)
{
	register unsigned long reg1 asm ("1") = 0;
	sigp_ccode ccode;
#ifndef __S390_ASM_SIGP_H
#define __S390_ASM_SIGP_H

/* SIGP order codes */
#define SIGP_SENSE		      1
#define SIGP_EXTERNAL_CALL	      2
#define SIGP_EMERGENCY_SIGNAL	      3
#define SIGP_START		      4
#define SIGP_STOP		      5
#define SIGP_RESTART		      6
#define SIGP_STOP_AND_STORE_STATUS    9
#define SIGP_INITIAL_CPU_RESET	     11
#define SIGP_CPU_RESET		     12
#define SIGP_SET_PREFIX		     13
#define SIGP_STORE_STATUS_AT_ADDRESS 14
#define SIGP_SET_ARCHITECTURE	     18
#define SIGP_COND_EMERGENCY_SIGNAL   19
#define SIGP_SENSE_RUNNING	     21
#define SIGP_SET_MULTI_THREADING     22
#define SIGP_STORE_ADDITIONAL_STATUS 23

/* SIGP condition codes */
#define SIGP_CC_ORDER_CODE_ACCEPTED 0
#define SIGP_CC_STATUS_STORED	    1
#define SIGP_CC_BUSY		    2
#define SIGP_CC_NOT_OPERATIONAL	    3

/* SIGP cpu status bits */

#define SIGP_STATUS_CHECK_STOP		0x00000010UL
#define SIGP_STATUS_STOPPED		0x00000040UL
#define SIGP_STATUS_EXT_CALL_PENDING	0x00000080UL
#define SIGP_STATUS_INVALID_PARAMETER	0x00000100UL
#define SIGP_STATUS_INCORRECT_STATE	0x00000200UL
#define SIGP_STATUS_NOT_RUNNING		0x00000400UL

#ifndef __ASSEMBLY__

static inline int __pcpu_sigp(u16 addr, u8 order, unsigned long parm,
			      u32 *status)
{
	register unsigned long reg1 asm ("1") = parm;
	int cc;

	asm volatile(
		"	sigp	%1,%2,0(%3)\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		:	"=d"	(ccode)
		: "d" (reg1), "d" (__cpu_logical_map[cpu_addr]),
		  "a" (order_code) : "cc" , "memory");
	return ccode;
}

/*
 * Signal processor with parameter
 */
static inline sigp_ccode
signal_processor_p(__u32 parameter, __u16 cpu_addr, sigp_order_code order_code)
{
	register unsigned int reg1 asm ("1") = parameter;
	sigp_ccode ccode;

	asm volatile(
		"	sigp	%1,%2,0(%3)\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (ccode)
		: "d" (reg1), "d" (__cpu_logical_map[cpu_addr]),
		  "a" (order_code) : "cc" , "memory");
	return ccode;
}

/*
 * Signal processor with parameter and return status
 */
static inline sigp_ccode
signal_processor_ps(__u32 *statusptr, __u32 parameter, __u16 cpu_addr,
		    sigp_order_code order_code)
{
	register unsigned int reg1 asm ("1") = parameter;
	sigp_ccode ccode;

	asm volatile(
		"	sigp	%1,%2,0(%3)\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (ccode), "+d" (reg1)
		: "d" (__cpu_logical_map[cpu_addr]), "a" (order_code)
		: "cc" , "memory");
	*statusptr = reg1;
	return ccode;
}

#endif /* __SIGP__ */
		: "=d" (cc), "+d" (reg1) : "d" (addr), "a" (order) : "cc");
	if (status && cc == 1)
		*status = reg1;
	return cc;
}

#endif /* __ASSEMBLY__ */

#endif /* __S390_ASM_SIGP_H */
