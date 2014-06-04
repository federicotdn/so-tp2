#ifndef RTC_H_INCLUDED
#define RTC_H_INCLUDED

typedef void (*RtcFunc_t)(void *arg);

struct time;
struct date;

int RtcGetTime(struct time *t);
int RtcGetDate(struct date *d);

int RtcTimedFunction(RtcFunc_t fn, void *arg, unsigned int seconds);

#endif
