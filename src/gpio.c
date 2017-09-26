//-2016/3/9 23:03:56
/*
现在我需要实现gpio功能,但是不能像以前那样简单的移植了,需要知道怎么实现的
首先是框架实现
*/

#include "debugfl.h"

#include <stdio.h>             
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
//-#include <linux/autoconf.h>
//-#include "ralink_gpio.h"

//-下面定义了一个设备节点,在使用之前需要创建这样一个节点使用"mknod /dev/gpio c 252 0"的语句
#define GPIO_DEV	"/dev/gpio"


enum {
	gpio_in,
	gpio_out,
};
enum {
	gpio3100,			//-0
	gpio6332,			//-1
	gpio9564,			//-2
};

/*
int gpio_set_dir(int r, int dir)	//-前面一个是引脚号		后面是方向
{
	int fd, req;
	
	//-增加打印语句方便知道程序进程
	printf("boot000\n");
	
	//-打开设备GPIO
	fd = open(GPIO_DEV, O_RDONLY);	//-这里打开了一个文件,也许由于IO的简单就是一个空文件而已没有任何描述
	if(fd < 0)
	{
		system("mknod /dev/gpio c 252 0");
		fd = open(GPIO_DEV, O_RDONLY);
	}
	
	if (fd < 0) {
		//-perror( ) 用来将上一个函数发生错误的原因输出到标准设备(stderr)。参数 s 所指的字符
		//-串会先打印出,后面再加上错误原因字符串。此错误原因依照全局变量errno 的值来决定要输出的字符串。
		perror(GPIO_DEV);
		return -1;
	}
	//-增加打印语句方便知道程序进程
	printf("boot001\n");
	
	if (dir == gpio_in) {

		if (r == gpio9564)
			req = RALINK_GPIO9564_SET_DIR_IN;
		else if (r == gpio6332)
			req = RALINK_GPIO6332_SET_DIR_IN;
		else
			req = RALINK_GPIO_SET_DIR_IN;
	}
	else {
		if (r == gpio9564)
			req = RALINK_GPIO9564_SET_DIR_OUT;		//-这里的取出是为了,模块之间减少耦合性
		else if (r == gpio6332)
			req = RALINK_GPIO6332_SET_DIR_OUT;
		else
			req = RALINK_GPIO_SET_DIR_OUT;
	}
	//-增加打印语句方便知道程序进程
	printf("boot002\n");
	if (ioctl(fd, req, 0xffffffff) < 0) 	//-这里设置了输入或者输出特定的引脚,具体实现是如何的需要思考
	{
		perror("ioctl");
		close(fd);
		return -1;
	}
	//-增加打印语句方便知道程序进程
	printf("boot003\n");
	close(fd);
	return 0;
}

int gpio_write_int(int r, int value)	//-实现了向IO口输出指定电平
{
	int fd, req;

	fd = open(GPIO_DEV, O_RDONLY);
	if (fd < 0) {
		perror(GPIO_DEV);
		return -1;
	}


	if (r == gpio9564)
		req = RALINK_GPIO9564_WRITE;
	else if (r == gpio6332)
		req = RALINK_GPIO6332_WRITE;
	else
		req = RALINK_GPIO_WRITE;
//-上面根据标志得到了一个变量值		
	if (ioctl(fd, req, value) < 0) 	//-不同的标志对应不同的数值输出
	{
		perror("ioctl");
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}


void gpio_test_write(int data)	//-调用的一个子函数,其中一个流程
{
	int i = 0;

	//set gpio direction to output
	gpio_set_dir(gpio3100, gpio_out);

  if(data != 0)
  {
  	printf("turn off SEC LED = 0x%x\n", data);
		//turn off LEDs
		gpio_write_int(gpio3100, data);	//-GPIO_0 熄灭使用的值是0x00000800,,输入指令时显示的是十进制数据
		sleep(3);
	}
	else
	{
		printf("turn on SEC LED = 0x%x\n", data);
		//turn on all LEDs
		gpio_write_int(gpio3100, 0);
	}
	
	//-测试用
	//-while(1)
	//-{
	//-		gpio_write_int(gpio3100, 0x00000800);
	//-		sleep(1);		//-单位是秒
	//-		gpio_write_int(gpio3100, 0x00000000);
	//-		sleep(1);
	//-}

}

void gpio_led_blink(void)
{
		gpio_write_int(gpio3100, 0x00000800);
		sleep(1);		//-单位是秒
		gpio_write_int(gpio3100, 0x00000000);
		//-usleep(1000*500);
		sleep(1);
}
*/




void usage(char *cmd)
{//-作为一个系统中的应用,下面的信息打印就不需要去深究了,知道使用的前提和效果就好
	printf("Usage: %s w - writing test (output)\n", cmd);
	printf("       %s r - reading test (input)\n", cmd);
	printf("       %s i (<gpio>) - interrupt test for gpio number\n", cmd);
	printf("       %s l <gpio> <on> <off> <blinks> <rests> <times>\n", cmd);
	printf("            - set led on <gpio>(0~24) on/off interval, no. of blinking/resting cycles, times of blinking\n");
	exit(0);
}

/*
现在需要实现运行灯的闪耀,就不用负责的方法了,就是定时改变电平,以最简单的方法来实现功能就行.

1.控制FL_EM7688EVB开发板SEC灯的亮灭
控制引脚38 GPIO_0		输出0灯亮
首先实现了灯的亮灭,下面需要实现脱离终端自动亮灭,实现守护进程.
*/
int ate_sub(void)	//?参数如何传递过来的,在终端输入命令的时候就带入了参数
{
	pid_t pc,pid;
	int i;
		
		
	
	
	return 0;
}