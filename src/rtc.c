#include <rtc.h>
#include <mtask.h>
#include <kernel.h>

#define RTC_INT 8
#define RTC_IN_PORT 0x70
#define RTC_OUT_PORT 0x71

#define CMOS_REG_A 0x0A
#define CMOS_REG_B 0x0B
#define CMOS_REG_C 0x0C

#define RTC_PRIO 10000

#define BIT(val, pos) ((val) & (1 << (pos)))

/*
 * Funciones para leer/escribir registros del CMOS mediante inb/outb
 */
static void write_cmos(unsigned char reg, unsigned data)
{
	DisableInts();
	outb(RTC_IN_PORT, reg);
	outb(RTC_OUT_PORT, data);
	RestoreInts();
}

static unsigned read_cmos(unsigned char reg)
{
	unsigned d = 0;
	
	DisableInts();
	outb(RTC_IN_PORT, reg);
	d = inb(RTC_OUT_PORT);
	RestoreInts();
	
	return d;
}

/* Maneja la interrupcion generada por el RTC (IRQ 8) */
static void rtc_int(unsigned irq)
{
	/* 
	 * El RTC requiere que se lea el registro C para
	 * que la interrupcion se genere nuevamente. 
	 */
	unsigned reg_c = read_cmos(CMOS_REG_C);
	
	/* Asegurarse de ser Update-Ended Interrupt. */
	if (!BIT(reg_c, 4))
		Panic("RTC: Tipo de interrupcion incompatible.");
		
	printk("hola");
}

static void rtc_task_fn(void *arg)
{
	
}

/* Inicializar el RTC */
void mt_rtc_init(void)
{
	unsigned reg_b = 0;
	Task_t *rtc_task;
	
	mt_set_int_handler(RTC_INT, rtc_int);
	mt_enable_irq(RTC_INT);
	
	/* 
	 * Habilitar Update-Ended Interrupt (interrupcion cada 1 segundo),
	 * para lograr esto se habilita el bit 4 del registro C del CMOS. 
	 */
	reg_b = read_cmos(CMOS_REG_B);
	reg_b |= (0x01 << 4);
	write_cmos(CMOS_REG_B, reg_b);
	
	rtc_task = CreateTask(rtc_task_fn, 0, NULL, "RTC Task", RTC_PRIO);
	Ready(rtc_task);
}
