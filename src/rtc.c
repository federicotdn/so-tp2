#include <rtc.h>
#include <mtask.h>
#include <kernel.h>

#define RTC_INT 8
#define CMOS_IN_PORT 0x70
#define CMOS_OUT_PORT 0x71

#define CMOS_REG_A 0x0A
#define CMOS_REG_B 0x0B
#define CMOS_REG_C 0x0C

#define RTC_REG_SEC 0x00
#define RTC_REG_MIN 0x02
#define RTC_REG_HOUR 0x04

#define RTC_PRIO 10000
#define RTC_QUEUE_SIZE 30
#define RTC_INTS_SEC 1024
#define RTC_MAX_TICKS -1U
#define RTC_MAX_SECS (RTC_MAX_TICKS / (unsigned int)RTC_INTS_SEC)

/* Codigos para tipos de funciones a programar con el RTC */
#define RTC_ONCE 1
#define RTC_REPEAT 2
#define RTC_ALARM 3
#define RTC_DISABLED 4

#define BIT(val, pos) ((val) & (1 << (pos)))
#define BCD_TO_BIN(t) ((t & 0x0F) + ((t >> 4) * 10))

/* Errores */
#define RTC_ERR_ADD -1
#define RTC_ERR_MEM -2
#define RTC_ERR_FMT -3
#define RTC_ERR_ID  -4

struct rtc_fn {
	RtcFunc_t fn;
	void *arg;
	
	char mode;
	unsigned int ticks_left;
	unsigned int ticks_init;
	struct RtcTime_t exec_time;
	
	RtcId_t id;
	struct rtc_fn *next;
};

struct rtc_id {
	RtcId_t id;
	struct rtc_id *next;
};

/*
 * Lista de IDs disponibles para ser usados nuevamente, y contador para generar
 * IDs nuevos.  Los IDs son utilizados para identificar funciones agregadas al 
 * sistema RTC.
 */
static struct rtc_id *rtc_id_head;
static RtcId_t rtc_id_counter;

/*
 * Mutex para sincronizar el acceso a rtc_id_head y rtc_id_counter.
 */
static Mutex_t *rtc_mutex;

/*
 * Lista (linked list) de funciones esperando a ser ejecutadas luego de
 * cierto tiempo.  Esta lista solo es manipulada por la rutina de interrupcion RTC.
 * La rutina no utiliza funciones bloqueantes, y nunca reserva o libera memoria 
 * del heap.
 * 
 * rtc_fn_head es un nodo sentinela para facilitar el codigo de eliminado de funciones.
 */
static struct rtc_fn rtc_fn_head;

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
 * Cola de IDs de funciones a remover del sistema.  Se utiliza para cancelar funciones que
 * fueron programadas previamente.
 */
static MsgQueue_t *remove_rtc_fns;

/*
--------------------------------------------------------------------------------
Funciones internas
--------------------------------------------------------------------------------
*/

/*
 * Funciones para leer/escribir registros del CMOS mediante inb/outb
 */
static void write_cmos(unsigned char reg, unsigned data)
{
	DisableInts();
	outb(CMOS_IN_PORT, reg);
	outb(CMOS_OUT_PORT, data);
	RestoreInts();
}

static unsigned read_cmos(unsigned char reg)
{
	unsigned d = 0;
	
	DisableInts();
	outb(CMOS_IN_PORT, reg);
	d = inb(CMOS_OUT_PORT);
	RestoreInts();
	
	return d;
}

int rtc_update_in_progress(void)
{
	int reg_a = read_cmos(CMOS_REG_A);
	return BIT(reg_a, 7);
}

/* Maneja la interrupcion generada por el RTC (IRQ 8) */
static void rtc_int(unsigned irq)
{
	static unsigned alarm_count = 0;
	struct RtcTime_t curr_time;
	unsigned reg_c;
	struct rtc_fn *new_fn, *aux;
	RtcId_t id;
	
	/* 
	 * El RTC requiere que se lea el registro C para
	 * que la interrupcion se genere nuevamente. 
	 */
	reg_c = read_cmos(CMOS_REG_C);
	
	/* Asegurarse de ser Periodic Interrupt. */
	if (!BIT(reg_c, 6))
		Panic("RTC: Tipo de interrupcion incompatible.");
			
	/* 
	 * Se le resta un tick a cada funcion que esta esperando,
	 * pero solo si no es de tipo RTC_ALARM. 
	 */
	aux = rtc_fn_head.next;
	while (aux != NULL)
	{
		if (aux->ticks_left && aux->mode != RTC_ALARM)
			aux->ticks_left -= 1;
		aux = aux->next;
	}
	
	/* Fijarse si hay funciones nuevas para agregar. */
	while( GetMsgQueueCond(new_rtc_fns, &new_fn) )
	{
		new_fn->next = rtc_fn_head.next;
		rtc_fn_head.next = new_fn;
		
		if (new_fn->mode == RTC_ALARM)
			alarm_count++;
	}
	
	/* Fijarse si hay funciones para cancelar/remover */
	while ( GetMsgQueueCond(remove_rtc_fns, &id) )
	{
		aux = &rtc_fn_head;
		while (aux != NULL && aux->next != NULL)
		{
			struct rtc_fn *next = aux->next;
			if (next->id == id)
			{
				if (next->mode == RTC_ALARM)
					alarm_count--;
				
				next->mode = RTC_DISABLED;
				aux->next = next->next;
				next->next = NULL;
				PutMsgQueueCond(ready_rtc_fns, &next);
			}
			
			aux = aux->next;
		}
	}
	
	/* Dos optimizaciones */
	if (rtc_fn_head.next == NULL)
		return;
	
	if (alarm_count > 0)
		RtcGetTime(&curr_time);

	/*
	 * Se recorre la lista (linked list) de funciones, buscando una con
	 * tiempo agotado.  Si se encuentra, se la remueve de la lista y se 
	 * la agrega a un MessageQueue de funciones listas a ser ejecutadas
	 * por el 'bottom half' (rtc_task_fn).  En el caso de las funciones
	 * a repetir, el proceso es el mismo pero no se la quita de la 
	 * lista actual.  
	 * 
	 * Para las funciones de tipo alarma, el proceso es similar
	 * pero se compara el valor de tiempo de la alarma con la hora actual,
	 * en vez de fijarse si tiene tiempo agotado.
	 */
	aux = &rtc_fn_head;
	while (aux != NULL && aux->next != NULL)
	{
		struct rtc_fn *next = aux->next;
		
		if (next->mode != RTC_ALARM && next->ticks_left == 0)
		{
			if (next->mode == RTC_ONCE)
			{
				aux->next = next->next;
				next->next = NULL;
			}
			else
			{
				next->ticks_left = next->ticks_init;
			}
			
			PutMsgQueueCond(ready_rtc_fns, &next);
		}
		else if (next->mode == RTC_ALARM)
		{
			struct RtcTime_t *exec_time = &next->exec_time;
			
			if (curr_time.seconds == exec_time->seconds &&
				curr_time.minutes == exec_time->minutes &&
				curr_time.hours   == exec_time->hours )
			{
				aux->next = next->next;
				next->next = NULL;
				alarm_count--;
				PutMsgQueueCond(ready_rtc_fns, &next);
			}
		}

		aux = aux->next;
	}
}

/*
 * Generar un nuevo ID para programar una funcion.
 */
static RtcId_t rtc_gen_id()
{
	RtcId_t id;
	EnterMutex(rtc_mutex);
	
	if (rtc_id_head != NULL)
	{
		struct rtc_id *aux;
		id = rtc_id_head->id;
		aux = rtc_id_head;
		rtc_id_head = rtc_id_head->next;
		Free(aux);
	}
	else
	{
		id = rtc_id_counter++;
	}
	
	LeaveMutex(rtc_mutex);
	return id;
}

/*
 * "Devolver" el ID de una funcion eliminada para poder re-utilizarlo para
 * otra funcion.
 */
static void rtc_return_id(RtcId_t id)
{
	struct rtc_id *aux;
	
	EnterMutex(rtc_mutex);
	
	aux = Malloc(sizeof(struct rtc_id));
	if (aux == NULL)
	{
		LeaveMutex(rtc_mutex);
		return;
	}
		
	aux->id = id;
	aux->next = rtc_id_head;
	rtc_id_head = aux;
	
	LeaveMutex(rtc_mutex);
}

/* 
 * Comprueba si hay funciones a ser ejecutadas (listas) y las ejecuta.
 * Tambien, libera la memoria de las funciones que no seran ejecutadas
 * nuevamente.
 */
static void rtc_task_fn(void *arg)
{
	while (1)
	{
		struct rtc_fn *rdy_fn = NULL;
		if ( !GetMsgQueue(ready_rtc_fns, &rdy_fn) )
			continue;
		
		if (rdy_fn->mode != RTC_DISABLED)
			rdy_fn->fn(rdy_fn->arg);
		
		if (rdy_fn->mode != RTC_REPEAT)
		{
			rtc_return_id(rdy_fn->id);
			Free(rdy_fn);
		}
	}
}

static int rtc_is_valid_time(struct RtcTime_t *t)
{
	return (t->hours < 24 && t->minutes < 60 && t->seconds < 60);
}

static RtcId_t rtc_add_function(RtcFunc_t fn, void *arg, unsigned int seconds, char mode)
{
	struct rtc_fn *new_fn;
	unsigned int ticks = 0;
	
	if (seconds == 0)
		return RTC_ERR_FMT;
	
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
	new_fn->ticks_init = ticks;
	new_fn->mode = mode;
	new_fn->next = NULL;
	new_fn->id = rtc_gen_id();
	
	/* Agregar la funcion a la cola de funciones nuevas */
	if (PutMsgQueue(new_rtc_fns, &new_fn))
	{
		return new_fn->id;
	}
	else
	{
		rtc_return_id(new_fn->id);
		Free(new_fn);
		return RTC_ERR_ADD;
	}
}

static RtcId_t rtc_add_function_alarm(RtcFunc_t fn, void *arg, struct RtcTime_t *t)
{
	struct rtc_fn *new_fn;
	
	new_fn = Malloc(sizeof(struct rtc_fn));
	if (new_fn == NULL)
		return RTC_ERR_MEM;
		
	new_fn->fn = fn;
	new_fn->arg = arg;
	new_fn->ticks_init = new_fn->ticks_left = 0;
	new_fn->mode = RTC_ALARM;
	new_fn->next = NULL;
	new_fn->id = rtc_gen_id();
	
	new_fn->exec_time.hours = t->hours;
	new_fn->exec_time.minutes = t->minutes;
	new_fn->exec_time.seconds = t->seconds;
	
	if (PutMsgQueue(new_rtc_fns, &new_fn))
	{
		return new_fn->id;
	}
	else
	{
		rtc_return_id(new_fn->id);
		Free(new_fn);
		return RTC_ERR_ADD;
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
 * 
 * Devuelve un ID asociado a la funcion agregada (positivo), o un error (negativo) si 
 * hubo un error.
 */
RtcId_t RtcTimedFunction(RtcFunc_t fn, void *arg, unsigned int seconds)
{
	return rtc_add_function(fn, arg, seconds, RTC_ONCE);
}

/*
 * Igual a RtcTimedFunction, con la diferencia de que la funcion se ejecutara cada
 * N segundos continuamente, a menos que el usuario cancele la funcion utilizando
 * RtcCancelFunction.
 */
RtcId_t RtcRepeatFunction(RtcFunc_t fn, void *arg, unsigned int seconds)
{
	return rtc_add_function(fn, arg, seconds, RTC_REPEAT);
}

/*
 * Igual a RtcTimedFuncion, con la diferencia de que la funcion no se ejecutra dentro
 * de cierto tiempo, si no que se ejecuta a cierta hora especificada por el usuario,
 * una sola vez.
 */
RtcId_t RtcAlarmFunction(RtcFunc_t fn, void *arg, struct RtcTime_t *t)
{
	if (rtc_is_valid_time(t))
		return rtc_add_function_alarm(fn, arg, t);
	else
		return RTC_ERR_FMT;
}

/*
 * Cancela una funcion programada creada con RtcTimedFunction, RtcRepeatFunction o
 * RtcAlarmFunction.  Identifica cada funcion con un ID de tipo RtcId_t.
 */
int RtcCancelFunction(RtcId_t id)
{
	if (id < 1)
		return RTC_ERR_ID;
		
	if ( PutMsgQueue(remove_rtc_fns, &id) )
		return 0;
	else
		return RTC_ERR_ADD;
}

/*
 * Leer la hora actual.  Formato: 24hs.
 */
void RtcGetTime(struct RtcTime_t *t)
{
	unsigned seconds, last_seconds;
	unsigned minutes, last_minutes;
	unsigned hours, last_hours;
	unsigned reg_b;
	bool valid = false, hour_pm = false;
	
	while (!valid)
	{
		while (rtc_update_in_progress())
			;
		last_seconds = read_cmos(RTC_REG_SEC);
		last_minutes = read_cmos(RTC_REG_MIN);
		last_hours = read_cmos(RTC_REG_HOUR);
		
		while (rtc_update_in_progress())
			;
		seconds = read_cmos(RTC_REG_SEC);
		minutes = read_cmos(RTC_REG_MIN);
		hours = read_cmos(RTC_REG_HOUR);		
		
		if (seconds == last_seconds && 
			minutes == last_minutes && 
			hours == last_hours)
		{
			valid = true;
		}
	}
	
	reg_b = read_cmos(CMOS_REG_B);
	if (BIT(hours, 7))
	{
		hour_pm = true;
	}
	hours &= 0x7F;
	
	/* Convertir BCD -> binario */
	if (!BIT(reg_b, 2))
	{
		seconds = BCD_TO_BIN(seconds);
		minutes = BCD_TO_BIN(minutes);
		hours = BCD_TO_BIN(hours);
	}
	
	/* Convertir am/pm -> 24hs */
	if (!BIT(reg_b, 1) && hour_pm)
	{
		hours = (hours + 12) % 24;
	}
	
	t->seconds = seconds;
	t->minutes = minutes;
	t->hours = hours;
}

void RtcGetDate(struct RtcDate_t *d)
{
	
}

int RtcSetTime(struct RtcTime_t *t)
{
	
}

int RtcSetDate(struct RtcDate_t *d)
{
	
}

/* Inicializar las utilidades RTC */
void mt_rtc_init(void)
{
	unsigned reg_a = 0, reg_b = 0;
	Task_t *rtc_task;
	
	rtc_fn_head.next = NULL;
	rtc_id_head = NULL;
	rtc_id_counter = 1;
	
	rtc_mutex = CreateMutex("RTCMutex");
	
	new_rtc_fns = CreateMsgQueue("rtc_new_fns", RTC_QUEUE_SIZE, sizeof(struct rtc_fn*), false, true);
	ready_rtc_fns = CreateMsgQueue("rtc_rdy_fns", RTC_QUEUE_SIZE, sizeof(struct rtc_fn*), false, false);
	remove_rtc_fns = CreateMsgQueue("rtc_remove_fns", RTC_QUEUE_SIZE, sizeof(RtcId_t), false, true);
	
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
