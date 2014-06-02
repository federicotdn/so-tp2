#ifndef RTC_H_INCLUDED
#define RTC_H_INCLUDED

struct time;
struct date;

int rtc_get_time(struct time *t);
int rtc_get_date(struct date *d);

#endif
