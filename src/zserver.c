#include <kernel.h>
#include <mouse.h>
#include <z.h>

#define Z_POINTER 'o'
#define Z_MOUSE_XINIT 10
#define Z_MOUSE_YINIT 10

static void mouse_listener(void *arg)
{
	mouse_event_t event;
	int x_old = Z_MOUSE_XINIT, y_old = Z_MOUSE_YINIT;
	int x = Z_MOUSE_XINIT, y = Z_MOUSE_YINIT;
	int width = mt_cons_nrows();
	int height = mt_cons_ncols();
	
	while ( GetMsgQueueCond(mouse_event_mq, &event) )
		; /* ignorar eventos viejos */
	
	while (1)
	{	
		if (! GetMsgQueue(mouse_event_mq, &event) )
			continue;
		
		if (event.x_movement || event.y_movement)
		{
			mt_cons_gotoxy(x_old, y_old);
			mt_cons_putc(' ');
			
			x += event.x_movement;
			y += -event.y_movement;
			
			if (x < 0)
				x = 0;
			else if (x > height - 1)
				x = height - 1;
				
			if (y < 0)
				y = 0;
			else if (y > width - 1)
				y = width - 1;
				
			mt_cons_gotoxy(x, y);
			mt_cons_putc(Z_POINTER);
			
			x_old = x;
			y_old = y;
		}
	}
}

int startz_main(int argc, char *argv[])
{
	printk("Z Server\n");
	mt_cons_cursor(false);
	mt_cons_raw(true);
	Task_t *mouse_tsk = CreateTask(mouse_listener, 0, NULL, "Z Mouse Listener", DEFAULT_PRIO);
	Ready(mouse_tsk);
	return 0;
}
