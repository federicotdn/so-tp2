/*
 * Driver para mouse PS/2 provisto por la catedra.
 */

#include <kernel.h>
#include <mouse.h>

// Definiciones del controlador de mouse PS/2
#define MOUSE				0x60
#define MOUSE_CTL			0x64
#define MOUSE_IBF			0x01
#define MOUSE_OBF			0x02
#define MOUSE_INT			12
#define MOUSE_ENABLE		0xA8
#define MOUSE_GETSTATUS		0x20
#define MOUSE_SETSTATUS		0x60
#define MOUSE_ENAIRQ		0x02
#define MOUSE_CMD			0xD4
#define MOUSE_DEFAULT		0xF6
#define MOUSE_ENASTREAM		0xF4
#define MOUSE_ACK			0xFA
#define MOUSE_NACK			0xFF

// Parámetros de recepción por polling
#define MOUSE_NTRIES		1000
#define MOUSE_DELAY			10

// Tarea de entrada del mouse
#define MOUSE_PRIO			10000		// Alta prioridad, para que funcione como "bottom half" de la interrupción
#define MOUSE_BUFSIZE		32

#define BIT(val, pos) ((val) & (1 << (pos))) 

static MsgQueue_t *mouse_mq;
MsgQueue_t *mouse_event_mq;

// Interrupción de mouse.
static void 
mouse_int(unsigned irq)
{
	unsigned c = inb(MOUSE);
	PutMsgQueueCond(mouse_mq, &c);
}

// Escribe en el registro de datos del controlador
static void 
mouse_send(unsigned data)
{
	printk("mouse_send 0x%02.2x\n", data);
    while ( inb(MOUSE_CTL) & MOUSE_OBF )
		Yield();
    outb(MOUSE, data);
}

// Escribe en el registro de control del controlador
static void 
mouse_send_ctl(unsigned data)
{
	printk("mouse_send_ctl 0x%02.2x\n", data);
    while ( inb(MOUSE_CTL) & MOUSE_OBF )
		Yield();
    outb(MOUSE_CTL, data);
}

// Lee del registro de datos del controlador
static void
mouse_receive(unsigned *p, bool wait)
{
	unsigned tries_left;

	for ( tries_left = MOUSE_NTRIES ; tries_left && !(inb(MOUSE_CTL) & MOUSE_IBF) ; tries_left-- )
		Yield();
	if ( wait && !tries_left )
	{
		printk("mouse_receive delay\n");
		Delay(MOUSE_DELAY);
		if ( !(inb(MOUSE_CTL) & MOUSE_IBF) )
			printk("mouse_receive timeout\n");
	}
	unsigned char c = inb(MOUSE);
	if ( p )
		*p = c;
	//printk("mouse_receive 0x%02.2x\n", c);
}

// Esta inicialización se hace con interrupción de mouse inactiva.
// Las respuestas se leen por polling.
static void
init_mouse(void)
{
	// Habilitar PS2 auxiliar
	mouse_send_ctl(MOUSE_ENABLE);
	mouse_receive(NULL, false);		// ignoramos la respuesta

	// Habilitar generación de IRQ12 leyendo y modificando
	// el "compaq status byte"
	unsigned status;
	mouse_send_ctl(MOUSE_GETSTATUS);
	mouse_receive(&status, false);	// suponemos que no va a fracasar
	status |= MOUSE_ENAIRQ;			// habilitar la generación de IRQ12
	mouse_send_ctl(MOUSE_SETSTATUS);
	mouse_send(status);
	mouse_receive(NULL, false);		// ignoramos la respuesta

	// Setear parámetros default
	mouse_send_ctl(MOUSE_CMD);
	mouse_send(MOUSE_DEFAULT);
	mouse_receive(NULL, true);		// ignoramos la respuesta

	// Habilitar el mouse para que mande eventos
	mouse_send_ctl(MOUSE_CMD);
	mouse_send(MOUSE_ENASTREAM);
	mouse_receive(NULL, true);		// ignoramos la respuesta
}

static void
mouse_task(void *arg)
{
	while ( true )
	{
		unsigned char c;
		mouse_event_t event;
		int x_sgn = 1;
		int y_sgn = 1;
		
		if ( !GetMsgQueue(mouse_mq, &c) )
			continue;
		
		if (BIT(c, 4))
			x_sgn = -1;
		
		if (BIT(c, 5))
			y_sgn = -1;
			
		event.btn_left = BIT(c, 0);
		event.btn_right = BIT(c, 1);
		event.btn_middle = BIT(c, 2);
		
		if ( !GetMsgQueue(mouse_mq, &c) )
			continue;
			
		event.x_movement = c * x_sgn;
		
		if ( !GetMsgQueue(mouse_mq, &c) )
			continue;
			
		event.y_movement = c * y_sgn;
		
		printk("mouse_task hola2\n");
	}
}

// Interfaz pública
void
mt_mouse_init(void)
{
	mouse_mq = CreateMsgQueue("Mouse bytes", MOUSE_BUFSIZE, 1, false, false);
	mouse_event_mq = CreateMsgQueue("Mouse events", MOUSE_BUFSIZE, sizeof(mouse_event_t), false, false);
	Ready(CreateTask(mouse_task, 0, NULL, "Mouse task", MOUSE_PRIO));
	init_mouse();
	mt_set_int_handler(MOUSE_INT, mouse_int);
	mt_enable_irq(MOUSE_INT);
}

