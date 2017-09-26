/*
此文件作为守护进程的独立文件,所有实际内容都在这里处理,说明也在这里
*/
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
#include <sys/types.h>
#include <sys/stat.h>

//-#include <linux/autoconf.h>
//-#include "ralink_gpio.h"

//-下面定义了一个设备节点,在使用之前需要创建这样一个节点使用"mknod /dev/gpio c 252 0"的语句
#define GPIO_DEV	"/dev/gpio"

#define MAXFILE 65535
void sigterm_handler(int arg);
volatile sig_atomic_t _running = 1;




void sigterm_handler(int arg)		//-kill发出的signal信号处理，达到进程的正常退出。
{
		_running = 0;
}


/*
现在需要实现运行灯的闪耀,就不用负责的方法了,就是定时改变电平,以最简单的方法来实现功能就行.

1.控制FL_EM7688EVB开发板SEC灯的亮灭
控制引脚38 GPIO_0		输出0灯亮
首先实现了灯的亮灭,下面需要实现脱离终端自动亮灭,实现守护进程.
*/
int daemon_init(void)	//?参数如何传递过来的,在终端输入命令的时候就带入了参数
{
	pid_t pc,pid;
	int i;
		
		
	//-开始实现守护进程
	pc = fork(); //第一步
	if(pc<0){
	printf("error fork\n");
	exit(1);
	}
	else if(pc>0)
		exit(0);	//-这里父进程的退出,就是实现首护进程的第一步
		
	pid = setsid(); //第二步,,setsid函数用于创建一个新的会话，并担任该会话组的组长。其实就是使进程完全独立出来，从而摆脱其他进程的控制。
	if (pid < 0)
		perror("setsid error");	
		
	chdir("/"); //第三步,,改变当前目录为根目录
	
	umask(0); //第四步,,重设文件权限掩码
	
	for(i=0;i<MAXFILE;i++) //第五步,,关闭文件描述符
		close(i);	
		
	signal(SIGTERM, sigterm_handler);		//-守护进程退出处理,建立一个信号量,kill时可以对应处理
		
	
	return 0;
}