#ifndef RTC_H_INCLUDED
#define RTC_H_INCLUDED

typedef void (*RtcFunc_t)(void *arg);

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
int RtcTimedFunction(RtcFunc_t fn, void *arg, unsigned int seconds);

#endif
