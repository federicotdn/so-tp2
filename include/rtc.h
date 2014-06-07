#ifndef RTC_H_INCLUDED
#define RTC_H_INCLUDED

#include <mt_id.h>

typedef void (*RtcFunc_t)(void *arg);
typedef long RtcId_t;

struct RtcTime_t {
	unsigned hours;
	unsigned minutes;
	unsigned seconds;
};

struct RtcDate_t {
	unsigned day;
	unsigned month;
	unsigned year;
};

void RtcGetTime(struct RtcTime_t *t);
void RtcGetDate(struct RtcDate_t *d);
int RtcSetTime(struct RtcTime_t *t);
int RtcSetDate(struct RtcDate_t *d);
RtcId_t RtcTimedFunction(RtcFunc_t fn, void *arg, unsigned int seconds);
RtcId_t RtcRepeatFunction(RtcFunc_t fn, void *arg, unsigned int seconds);
RtcId_t RtcAlarmFunction(RtcFunc_t fn, void *arg, struct RtcTime_t *t);
int RtcCancelFunction(RtcId_t id);
int RtcCancelTaskFunctions(mt_id_t task_id);

#endif
