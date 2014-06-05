#include <rtc.h>
#include <kernel.h>

void rtc_test(void *arg)
{
	int *num = (int*)arg; 
	printk("\nFuncion completada. arg: %d\n-> ", *num);
}

void rtc_test2(void *arg)
{
	int *num = (int*)arg; 
	struct RtcTime_t t;
	
	printk("\nFuncion completada. arg: %d\n-> ", *num);
	RtcGetTime(&t);
	printk("Hora: %u:%u:%u\n", t.hours, t.minutes, t.seconds);
}

int rtcalarm_main(int argc, char *argv[])
{
	struct RtcTime_t time;
	char buf[30];
	int arg = 1337;
	
	printk("RTC Alarm Test\n");
	printk("Ingresar hora:\n");
	mt_getline(buf, 10);
	time.hours = atoi(buf);
	
	printk("Ingresar minutos:\n");
	mt_getline(buf, 10);
	time.minutes = atoi(buf);
	
	printk("Ingresar segundos:\n");
	mt_getline(buf, 10);
	time.seconds = atoi(buf);
	
	RtcAlarmFunction(rtc_test2, &arg, &time);
	
	printk("Esperando...\n");
	
	mt_getline(buf, 10);
	return 0;
}

int rtctime_main(int argc, char *argv[])
{
	struct RtcTime_t t;
	RtcGetTime(&t);
	printk("sec: %u, min: %u, hour: %d\n", t.seconds, t.minutes, t.hours);
	return 0;
}

int rtctest_main(int argc, char *argv[])
{
	char buf[10];
	int test_arg = 42;
		
	printk("RTC Test\n");
	printk("Ingresar un tiempo para crear una tarea con ese tiempo de demora.\n");
	printk("Presionar ENTER sin ingresar un numero para salir.\n");
	printk("-> ");
	
	while (mt_getline(buf, 10) > 1)
	{
		int sec = atoi(buf);
		
		if (RtcTimedFunction(rtc_test, &test_arg, sec) == 0)
			printk("Tarea creada con tiempo: %d segundos.\n", sec);
		else
			printk("Error al agregar tarea.\n");
		
		printk("-> ");
	}
	
	return 0;
}
