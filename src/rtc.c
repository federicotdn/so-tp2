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
#define RTC_QUEUE_SIZE 30
#define RTC_INTS_SEC 1024
#define RTC_MAX_TICKS -1U
#define RTC_MAX_SECS (RTC_MAX_TICKS / (unsigned int)RTC_INTS_SEC)

#define BIT(val, pos) ((val) & (1 << (pos)))

enum RTC_ERRORS { RTC_ERR_ADD = 1, RTC_ERR_MEM };

struct rtc_fn {
	RtcFunc_t fn;
	void *arg;
	unsigned int ticks_left;
	struct rtc_fn *next;
};

/*
 * Lista (linked list) de funciones esperando a ser ejecutadas luego de
 * cierto tiempo.  Esta lista solo es manipulada por la rutina de interrupcion RTC.
 * La rutina no utiliza funciones bloqueantes, y nunca reserva o libera memoria 
 * del heap.
 */
static struct rtc_fn *rtc_fn_head;

/* 
 * Cola de funciones nuevas a ser agregadas a la lista de la rutina de
 * interrupcion RTC.  Idealmente, las funciones son removidas de esta cola 
 * inmediatamente. 
 */
static MsgQueue_t *new_rtc_fns;

/*
 * Cola de funciones cuyo tiempo asociado ya transcurrio, y estan listas para ser
 * ejecutadas por el Task rtc_task.  Idealmente, las funciones son removidas de 
 * esta cola inmediatamente.
 */
static MsgQueue_t *ready_rtc_fns;

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
	unsigned reg_c;
	struct rtc_fn *new_fn, *aux;
	
	/* 
	 * El RTC requiere que se lea el registro C para
	 * que la interrupcion se genere nuevamente. 
	 */
	reg_c = read_cmos(CMOS_REG_C);
	
	/* Asegurarse de ser Update-Ended Interrupt. */
	if (!BIT(reg_c, 6))
		Panic("RTC: Tipo de interrupcion incompatible.");
			
	/* Se le resta un tick a cada funcion que esta esperando. */
	aux = rtc_fn_head;
	while (aux != NULL)
	{
		if (aux->ticks_left)
			aux->ticks_left -= 1;
		aux = aux->next;
	}
	
	/* Fijarse si hay funciones nuevas para agregar. */
	while( GetMsgQueueCond(new_rtc_fns, &new_fn) )
	{
		new_fn->next = rtc_fn_head;
		rtc_fn_head = new_fn;
	}
	
	if (rtc_fn_head == NULL)
		return;
	
	/*
	 * Se recorre la lista (linked list) de funciones, buscando una con
	 * tiempo agotado.  Si se encuentra, se la remueve de la lista y se 
	 * la agrega a un MessageQueue de funciones listas a ser ejecutadas
	 * por el 'bottom half' (rtc_task_fn).
	 */
	aux = rtc_fn_head;
	while (aux != NULL && aux->next != NULL)
	{
		struct rtc_fn *next = aux->next;
		if (next->ticks_left == 0)
		{
			aux->next = next->next;
			next->next = NULL;
			PutMsgQueueCond(ready_rtc_fns, &next);
		}
		aux = aux->next;
	}
	
	aux = rtc_fn_head;
	if (aux->ticks_left == 0)	
	{
		rtc_fn_head = aux->next;
		aux->next = NULL;
		PutMsgQueueCond(ready_rtc_fns, &aux);
	}
}

/* Comprueba si hay funciones a ser ejecutadas (listas) y las ejecuta */
static void rtc_task_fn(void *arg)
{
	while (1)
	{
		struct rtc_fn *rdy_fn = NULL;
		if ( !GetMsgQueue(ready_rtc_fns, &rdy_fn) )
			continue;
		
		rdy_fn->fn(rdy_fn->arg);
		Free(rdy_fn);
	}
}

/*
--------------------------------------------------------------------------------
Funciones API publica
--------------------------------------------------------------------------------
*/

/* 
 * Ejecuta una funcion luego de un tiempo en segundos especificado.
 * En el caso promedio, la diferencia entre el tiempo especificado y el tiempo
 * en la que se ejecuta la funcion sera alrededor de un milisegundo (aprox. un ciclo de 
 * interrupcion).
 */
int RtcTimedFunction(RtcFunc_t fn, void *arg, unsigned int seconds)
{
	struct rtc_fn *new_fn;
	unsigned int ticks = 0;
	
	if (seconds == 0)
	{
		fn(arg);
		return 0;
	}
	
	if (seconds > RTC_MAX_SECS)
		return RTC_ERR_ADD;
	
	/* ticks = segundos * interrupciones/segundo */
	ticks = seconds * RTC_INTS_SEC;
	
	new_fn = Malloc(sizeof(struct rtc_fn));
	if (new_fn == NULL)
		return RTC_ERR_MEM;
	
	new_fn->fn = fn;
	new_fn->arg = arg;
	new_fn->ticks_left = ticks;
	new_fn->next = NULL;
	
	/* Agregar la funcion a la cola de funciones nuevas */
	if (PutMsgQueue(new_rtc_fns, &new_fn))
	{
		return 0;
	}
	else
	{
		Free(new_fn);
		return RTC_ERR_ADD;
	}
}

/* Inicializar las utilidades RTC */
void mt_rtc_init(void)
{
	unsigned reg_a = 0, reg_b = 0;
	Task_t *rtc_task;
	rtc_fn_head = NULL;
	
	new_rtc_fns = CreateMsgQueue("rtc_new_fns", RTC_QUEUE_SIZE, sizeof(struct rtc_fn*), false, true);
	ready_rtc_fns = CreateMsgQueue("rtc_rdy_fns", RTC_QUEUE_SIZE, sizeof(struct rtc_fn*), false, false);
	
	mt_set_int_handler(RTC_INT, rtc_int);
	mt_enable_irq(RTC_INT);
	
	rtc_task = CreateTask(rtc_task_fn, 0, NULL, "RTC Task", RTC_PRIO);
	Ready(rtc_task);
	
	/* Configurar registro A para recibir 1024 interrupciones por segundo. */
	reg_a = read_cmos(CMOS_REG_A);
	reg_a &= 0xF0;
	reg_a |= 0x06;
	write_cmos(CMOS_REG_A, reg_a);
	
	/* Habilitar Periodic interrupt, para lograr esto se habilita el bit 6 del registro B del CMOS. */
	reg_b = read_cmos(CMOS_REG_B);
	reg_b |= (0x01 << 6);
	write_cmos(CMOS_REG_B, reg_b);
}
