#ifndef RTC_H_INCLUDED
#define RTC_H_INCLUDED

typedef void (*RtcFunc_t)(void *arg);
typedef unsigned int rtc_secs_t;

struct time;
struct date;

int RtcGetTime(struct time *t);
int RtcGetDate(struct date *d);

int RtcTimedFunction(RtcFunc_t fn, void *arg, rtc_secs_t seconds);

#endif
