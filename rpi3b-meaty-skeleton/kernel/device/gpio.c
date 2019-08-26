#include<device/gpio.h>
#include<kernel/rpi-base.h>
#include<kernel/rpi-mailbox-interface.h>

#define  GPIO_BASE ((volatile __attribute__((aligned(4))) uint32_t*)(uintptr_t)(PERIPHERAL_BASE + 0x200000))

//Refer: http://www.science.smith.edu/dftwiki/index.php/Tutorial:_Programming_the_GPIO
// GPIO setup macros. Always use INP_GPIO(x) before using OUT_GPIO(x)
#define INP_GPIO(g)   *(GPIO_BASE + ((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g)   *(GPIO_BASE + ((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(GPIO_BASE + (((g)/10))) |= (((a)<=3?(a) + 4:(a)==4?3:2)<<(((g)%10)*3))

// Unsed for now
// TODO: use them
#define GPIO_SET0  *(GPIO_BASE + 7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_SET1  *(GPIO_BASE + 8)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR0  *(GPIO_BASE + 10) // clears bits which are 1 ignores bits which are 0
#define GPIO_CLR1  *(GPIO_BASE + 11) // clears bits which are 1 ignores bits which are 0
#define GPIO_READ0(g)  *(GPIO_BASE + 13) &= (1<<(g))
#define GPIO_READ1(g)  *(GPIO_BASE + 14) &= (1<<(g))


#define	OkLed		16


/* GPIO regs */
enum {
    Set0	= 0x1c>>2,
    Clr0	= 0x28>>2,
    Lev0	= 0x34>>2,
    PUD	= 0x94>>2,
    Off	= 0x0,
    Pulldown= 0x1,
    Pullup	= 0x2,
    PUDclk0	= 0x98>>2,
    PUDclk1	= 0x9c>>2,
};

static inline void delay(int32_t count)
{
	__asm__ volatile("__delay_%=: subs %[count], %[count], #1; bne __delay_%=\n"
					 : "=r"(count)
					 : [count] "0"(count)
					 : "cc");
}

uint32_t read_digital_pin(uint32_t bcm_pin) {
    return (GPIO_BASE[Lev0 + bcm_pin/32] & (1 << (bcm_pin % 32))) != 0;
}

void select_alt_func(uint32_t bcm_pin, alt_func alt_fun) {
    SET_GPIO_ALT(bcm_pin, alt_fun);
}

void set_mode_output(uint32_t bcm_pin) {
    INP_GPIO(bcm_pin);
    OUT_GPIO(bcm_pin);
}

void set_mode_input(uint32_t bcm_pin) {
    INP_GPIO(bcm_pin);
}

void set_pin(uint32_t bcm_pin) {
    GPIO_BASE[Set0 + bcm_pin/32] = 1 << (bcm_pin % 32);
}

void clear_pin(uint32_t bcm_pin) {
    GPIO_BASE[Clr0 + bcm_pin/32] = 1 << (bcm_pin % 32);
}

void pullup_pin(uint32_t bcm_pin) {
    volatile uint32_t *gp, *reg;
    uint32_t mask;

    gp = GPIO_BASE;
    reg = &gp[PUDclk0 + bcm_pin/32];
    mask = 1 << (bcm_pin % 32);
    gp[PUD] = Pullup;
    delay(150);
    *reg = mask;
    delay(150);
    *reg = 0;
}

void pulldown_pin(uint32_t bcm_pin) 
{
    volatile uint32_t *gp, *reg;
    uint32_t mask;

    gp = GPIO_BASE;
    reg = &gp[PUDclk0 + bcm_pin/32];
    mask = 1 << (bcm_pin % 32);
    gp[PUD] = Pulldown;
    delay(150);
    *reg = mask;
    delay(150);
    *reg = 0;
}

void disable_pulling(uint32_t bcm_pin) 
{
    volatile uint32_t *gp, *reg;
    uint32_t mask;

    gp = GPIO_BASE;
    reg = &gp[PUDclk0 + bcm_pin/32];
    mask = 1 << (bcm_pin % 32);
    gp[PUD] = Off;
    delay(150);
    *reg = mask;
    delay(150);
    *reg = 0;
}

void set_activity_led(uint32_t on_off) {
    // function changes according to PI version
    // This is hardcoded implementation for RPI 3B (not B+)
    RPI_PropertyInit();
    RPI_PropertyAddTag(TAG_SET_GPIO_STATE, 130, on_off);
    RPI_PropertyProcess();
}

