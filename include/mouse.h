#ifndef MOUSE_H_INCLUDED
#define MOUSE_H_INCLUDED

#include <mtask.h>

extern MsgQueue_t *mouse_event_mq;

typedef struct {
	char btn1;
	char btn2;
	char btn3;
	int x_movement;
	int y_movement;
} mouse_event_t;

#endif
