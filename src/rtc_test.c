#include <rtc.h>
#include <kernel.h>

void rtc_test(void *arg)
{
	printk("\nFuncion completada.\n-> ");
}

int rtctest_main(int argc, char *argv[])
{
	char buf[10];
		
	printk("RTC Test\n");
	printk("Ingresar un tiempo para crear una tarea con ese tiempo de demora.\n");
	printk("Presionar ENTER sin ingresar un numero para salir.\n");
	printk("-> ");
	
	while (mt_getline(buf, 10) > 1)
	{
		int sec = atoi(buf);
		RtcTimedFunction(rtc_test, NULL, sec);
		printk("Tarea creada con tiempo: %d segundos.\n", sec);
		
		printk("-> ");
	}
	
	RtcTimedFunction(rtc_test, NULL, atoi(argv[1]));
	return 0;
}
