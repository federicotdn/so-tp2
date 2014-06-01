#ifndef MOUSE_H_INCLUDED
#define MOUSE_H_INCLUDED

#include <mtask.h>

extern MsgQueue_t *mouse_event_mq;

typedef struct {
	char btn_left;
	char btn_right;
	char btn_middle;
	int x_movement;
	int y_movement;
} mouse_event_t;

#endif
