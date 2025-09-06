#ifndef ___ASM_SPARC_SIGINFO_H
#define ___ASM_SPARC_SIGINFO_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/siginfo_64.h>
#else
#include <asm/siginfo_32.h>
#endif
#endif
#ifndef __SPARC_SIGINFO_H
#define __SPARC_SIGINFO_H

#include <uapi/asm/siginfo.h>


#ifdef CONFIG_COMPAT

struct compat_siginfo;

#endif /* CONFIG_COMPAT */

#endif /* !(__SPARC_SIGINFO_H) */
