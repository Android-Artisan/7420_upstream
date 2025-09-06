#ifndef _ASM_GENERIC_GPIO_H
#define _ASM_GENERIC_GPIO_H

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/of.h>

#ifdef CONFIG_GPIOLIB

#include <linux/compiler.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>

/* Platforms may implement their GPIO interface with library code,
 * at a small performance cost for non-inlined operations and some
 * extra memory (for code and for per-GPIO table entries).
 *
 * While the GPIO programming interface defines valid GPIO numbers
 * to be in the range 0..MAX_INT, this library restricts them to the
 * smaller range 0..ARCH_NR_GPIOS-1.
 */

#ifndef ARCH_NR_GPIOS
#define ARCH_NR_GPIOS		256
#endif

static inline int gpio_is_valid(int number)
{
	/* only some non-negative numbers are valid */
	return ((unsigned)number) < ARCH_NR_GPIOS;
}

struct seq_file;
struct module;

/**
 * struct gpio_chip - abstract a GPIO controller
 * @label: for diagnostics
 * @dev: optional device providing the GPIOs
 * @owner: helps prevent removal of modules exporting active GPIOs
 * @direction_input: configures signal "offset" as input, or returns error
 * @get: returns value for signal "offset"; for output signals this
 *	returns either the value actually sensed, or zero
 * @direction_output: configures signal "offset" as output, or returns error
 * @set: assigns output value for signal "offset"
 * @dbg_show: optional routine to show contents in debugfs; default code
 *	will be used when this is omitted, but custom code can show extra
 *	state (such as pullup/pulldown configuration).
 * @base: identifies the first GPIO number handled by this chip; or, if
 *	negative during registration, requests dynamic ID allocation.
 * @ngpio: the number of GPIOs handled by this controller; the last GPIO
 *	handled is (base + ngpio - 1).
 * @can_sleep: flag must be set iff get()/set() methods sleep, as they
 *	must while accessing GPIO expander chips over I2C or SPI
 *
 * A gpio_chip can help platforms abstract various sources of GPIOs so
 * they can all be accessed through a common programing interface.
 * Example sources would be SOC controllers, FPGAs, multifunction
 * chips, dedicated GPIO expanders, and so on.
 *
 * Each chip controls a number of signals, identified in method calls
 * by "offset" values in the range 0..(@ngpio - 1).  When those signals
 * are referenced through calls like gpio_get_value(gpio), the offset
 * is calculated by subtracting @base from the gpio number.
 */
struct gpio_chip {
	char			*label;
	struct device		*dev;
	struct module		*owner;

	int			(*direction_input)(struct gpio_chip *chip,
						unsigned offset);
	int			(*get)(struct gpio_chip *chip,
						unsigned offset);
	int			(*direction_output)(struct gpio_chip *chip,
						unsigned offset, int value);
	void			(*set)(struct gpio_chip *chip,
						unsigned offset, int value);
	void			(*dbg_show)(struct seq_file *s,
						struct gpio_chip *chip);
	int			base;
	u16			ngpio;
	unsigned		can_sleep:1;
	unsigned		exported:1;
};

extern const char *gpiochip_is_requested(struct gpio_chip *chip,
			unsigned offset);
extern int __must_check gpiochip_reserve(int start, int ngpio);

/* add/remove chips */
extern int gpiochip_add(struct gpio_chip *chip);
extern int __must_check gpiochip_remove(struct gpio_chip *chip);

 *
 * ARCH_NR_GPIOS is somewhat arbitrary; it usually reflects the sum of
 * builtin/SoC GPIOs plus a number of GPIOs on expanders; the latter is
 * actually an estimate of a board-specific value.
 */

#ifndef ARCH_NR_GPIOS
#define ARCH_NR_GPIOS		512
#endif

/*
 * "valid" GPIO numbers are nonnegative and may be passed to
 * setup routines like gpio_request().  only some valid numbers
 * can successfully be requested and used.
 *
 * Invalid GPIO numbers are useful for indicating no-such-GPIO in
 * platform data and other tables.
 */

static inline bool gpio_is_valid(int number)
{
	return number >= 0 && number < ARCH_NR_GPIOS;
}

struct device;
struct gpio;
struct seq_file;
struct module;
struct device_node;
struct gpio_desc;

/* caller holds gpio_lock *OR* gpio is marked as requested */
static inline struct gpio_chip *gpio_to_chip(unsigned gpio)
{
	return gpiod_to_chip(gpio_to_desc(gpio));
}

/* Always use the library code for GPIO management calls,
 * or when sleeping may be involved.
 */
extern int gpio_request(unsigned gpio, const char *label);
extern void gpio_free(unsigned gpio);

extern int gpio_direction_input(unsigned gpio);
extern int gpio_direction_output(unsigned gpio, int value);

extern int gpio_get_value_cansleep(unsigned gpio);
extern void gpio_set_value_cansleep(unsigned gpio, int value);
static inline int gpio_direction_input(unsigned gpio)
{
	return gpiod_direction_input(gpio_to_desc(gpio));
}
static inline int gpio_direction_output(unsigned gpio, int value)
{
	return gpiod_direction_output_raw(gpio_to_desc(gpio), value);
}

static inline int gpio_set_debounce(unsigned gpio, unsigned debounce)
{
	return gpiod_set_debounce(gpio_to_desc(gpio), debounce);
}

static inline int gpio_get_value_cansleep(unsigned gpio)
{
	return gpiod_get_raw_value_cansleep(gpio_to_desc(gpio));
}
static inline void gpio_set_value_cansleep(unsigned gpio, int value)
{
	return gpiod_set_raw_value_cansleep(gpio_to_desc(gpio), value);
}


/* A platform's <asm/gpio.h> code may want to inline the I/O calls when
 * the GPIO is constant and refers to some always-present controller,
 * giving direct access to chip registers and tight bitbanging loops.
 */
extern int __gpio_get_value(unsigned gpio);
extern void __gpio_set_value(unsigned gpio, int value);

extern int __gpio_cansleep(unsigned gpio);


#ifdef CONFIG_GPIO_SYSFS
static inline int __gpio_get_value(unsigned gpio)
{
	return gpiod_get_raw_value(gpio_to_desc(gpio));
}
static inline void __gpio_set_value(unsigned gpio, int value)
{
	return gpiod_set_raw_value(gpio_to_desc(gpio), value);
}

static inline int __gpio_cansleep(unsigned gpio)
{
	return gpiod_cansleep(gpio_to_desc(gpio));
}

static inline int __gpio_to_irq(unsigned gpio)
{
	return gpiod_to_irq(gpio_to_desc(gpio));
}

extern int gpio_request_one(unsigned gpio, unsigned long flags, const char *label);
extern int gpio_request_array(const struct gpio *array, size_t num);
extern void gpio_free_array(const struct gpio *array, size_t num);

/*
 * A sysfs interface can be exported by individual drivers if they want,
 * but more typically is configured entirely from userspace.
 */
extern int gpio_export(unsigned gpio, bool direction_may_change);
extern void gpio_unexport(unsigned gpio);

#endif	/* CONFIG_GPIO_SYSFS */

#else	/* !CONFIG_HAVE_GPIO_LIB */

static inline int gpio_is_valid(int number)
static inline int gpio_export(unsigned gpio, bool direction_may_change)
{
	return gpiod_export(gpio_to_desc(gpio), direction_may_change);
}

static inline int gpio_export_link(struct device *dev, const char *name,
				   unsigned gpio)
{
	return gpiod_export_link(dev, name, gpio_to_desc(gpio));
}

static inline void gpio_unexport(unsigned gpio)
{
	gpiod_unexport(gpio_to_desc(gpio));
}

#else	/* !CONFIG_GPIOLIB */

static inline bool gpio_is_valid(int number)
{
	/* only non-negative numbers are valid */
	return number >= 0;
}

/* platforms that don't directly support access to GPIOs through I2C, SPI,
 * or other blocking infrastructure can use these wrappers.
 */

static inline int gpio_cansleep(unsigned gpio)
{
	return 0;
}

static inline int gpio_get_value_cansleep(unsigned gpio)
{
	might_sleep();
	return gpio_get_value(gpio);
	return __gpio_get_value(gpio);
}

static inline void gpio_set_value_cansleep(unsigned gpio, int value)
{
	might_sleep();
	gpio_set_value(gpio, value);
}

#endif /* !CONFIG_HAVE_GPIO_LIB */

#ifndef CONFIG_GPIO_SYSFS

/* sysfs support is only available with gpiolib, where it's optional */

static inline int gpio_export(unsigned gpio, bool direction_may_change)
{
	return -ENOSYS;
}

static inline void gpio_unexport(unsigned gpio)
{
}
#endif	/* CONFIG_GPIO_SYSFS */
	__gpio_set_value(gpio, value);
}

#endif /* !CONFIG_GPIOLIB */

#endif /* _ASM_GENERIC_GPIO_H */
