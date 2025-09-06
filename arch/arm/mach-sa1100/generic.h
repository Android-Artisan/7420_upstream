/*
 * linux/arch/arm/mach-sa1100/generic.h
 *
 * Author: Nicolas Pitre
 */

struct sys_timer;

extern struct sys_timer sa1100_timer;
extern void __init sa1100_map_io(void);
extern void __init sa1100_init_irq(void);
extern void __init sa1100_init_gpio(void);

#define SET_BANK(__nr,__start,__size) \
	mi->bank[__nr].start = (__start), \
	mi->bank[__nr].size = (__size), \
	mi->bank[__nr].node = (((unsigned)(__start) - PHYS_OFFSET) >> 27)

extern void (*sa1100fb_backlight_power)(int on);
extern void (*sa1100fb_lcd_power)(int on);
#include <linux/cpufreq.h>
#include <linux/reboot.h>

extern void sa1100_timer_init(void);
extern void __init sa1100_map_io(void);
extern void __init sa1100_init_irq(void);
extern void __init sa1100_init_gpio(void);
extern void sa11x0_restart(enum reboot_mode, const char *);
extern void sa11x0_init_late(void);

#define SET_BANK(__nr,__start,__size) \
	mi->bank[__nr].start = (__start), \
	mi->bank[__nr].size = (__size)

extern void sa1110_mb_enable(void);
extern void sa1110_mb_disable(void);

struct cpufreq_policy;

extern unsigned int sa11x0_freq_to_ppcr(unsigned int khz);
extern int sa11x0_verify_speed(struct cpufreq_policy *policy);
extern unsigned int sa11x0_getspeed(unsigned int cpu);
extern unsigned int sa11x0_ppcr_to_freq(unsigned int idx);
extern struct cpufreq_frequency_table sa11x0_freq_table[];
extern unsigned int sa11x0_getspeed(unsigned int cpu);

struct flash_platform_data;
struct resource;

extern void sa11x0_set_flash_data(struct flash_platform_data *flash,
				  struct resource *res, int nr);

struct sa11x0_ssp_plat_ops;
extern void sa11x0_set_ssp_data(struct sa11x0_ssp_plat_ops *ops);

struct irda_platform_data;
void sa11x0_set_irda_data(struct irda_platform_data *irda);

struct mcp_plat_data;
void sa11x0_set_mcp_data(struct mcp_plat_data *data);
void sa11x0_register_mtd(struct flash_platform_data *flash,
			 struct resource *res, int nr);

struct irda_platform_data;
void sa11x0_register_irda(struct irda_platform_data *irda);

struct mcp_plat_data;
void sa11x0_ppc_configure_mcp(void);
void sa11x0_register_mcp(struct mcp_plat_data *data);

struct sa1100fb_mach_info;
void sa11x0_register_lcd(struct sa1100fb_mach_info *inf);

#ifdef CONFIG_PM
int sa11x0_pm_init(void);
#else
static inline int sa11x0_pm_init(void) { return 0; }
#endif

int sa11xx_clk_init(void);
