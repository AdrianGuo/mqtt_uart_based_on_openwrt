/*
Linux串口操作中，特别以非阻塞的方式读取和发送数据，做好进程之间的同步很重要。有
时我们会发现这样一个问题，在进行read操作时，一次read不能获得一个完整的数据帧，
这就好比你买了一个电脑，送货的先把显示器送到你家，再把机箱送到，你会发现还少键盘
鼠标什么的，又要过几天才送，这会让你急死。很不幸，在串口操作的时候，接受数据很可
能就是这样分批收货的，但是幸运的是，接受数据的动作很快，别忘了计算机就是靠速度这
一点，抛开这个，啥都不是。
很自然的，我们就会进行数据的拼接，将一堆零散的数据拼接成一个个有用的数据帧，哈哈，
变废为宝。说多了让人很烦，举个例子吧。

假如我们定义的数据帧是以'$'开头，以‘#’结尾的。

1.下面的程序有一个问题:当同时接收到两个命令的时候就会丢失其中一个,这个后续需要处理
*/

#include "debugfl.h"

//串口相关的头文件  
#include<stdio.h>      /*标准输入输出定义*/  
#include<stdlib.h>     /*标准函数库定义*/  
#include<unistd.h>     /*Unix 标准函数定义*/  
#include<sys/types.h>   
#include<sys/stat.h>     
#include<fcntl.h>      /*文件控制定义*/  
#include<termios.h>    /*PPSIX 终端控制定义*/  
#include<errno.h>      /*错误号定义*/  
#include<string.h>  
   
#include "uart1.h"


//首先定义了两个字符数组：
//-一个缓冲数组，用来存放每一次读到的数据
char read_data[256]={0};
//-int read_pt=0;	//-指向待处理位置的
//-存放一个完整的数据帧，以便处理
char read_report[256]={0};



//得到了一个完整的数据帧
void get_complete_frame(int fd)
{
    char read_tmp[256]={0};
    int return_flag=0;
    int i;
    //存放读取到的字节数
    while(1)
    {
      memset(read_tmp,0,sizeof(read_tmp));
      if(read(fd, read_tmp, sizeof(read_tmp))>0)
      {
        //数据帧的拼接
        printf("read_tmp: %s\n",read_tmp);
        for( i=0;i<strlen(read_tmp);i++)
        {
              if(read_tmp[i]=='$')
              {
                memset(read_data,0,sizeof(read_data));
                char tmp[5]={0};
                tmp[0]=read_tmp[i];
                strcat(read_data,tmp);
              }
              else if(read_tmp[i]=='#')
              {
                char tmp[5]={0};
                tmp[0]=read_tmp[i];
                strcat(read_data,tmp);
                return_flag=1;
                memset(read_report,0,sizeof(read_report));
                //遇到帧尾，将read_data帧拷贝到read_buf中，以便处理
                memcpy(read_report,read_data,sizeof(read_data));
                printf("read_report: %s\n",read_report);	//-把准备处理的报文打印出来
              }
              else
              {
                  char tmp[5]={0};
                tmp[0]=read_tmp[i];
                strcat(read_data,tmp);
              }
        }
        //有了一个完整的数据帧就返回处理
        if(return_flag==1)
            return;
      }
      else//读不到数据就返回，以便检查对方是否断线
        return;
    	usleep(100000);
    }
}

/*
从上面的代码中，我们可以看到，每一次从串口读取数据，将读到的数据放在read_tmp中。
对这个数组进行逐个地字符分析，遇到帧头标志就清空缓冲数组read_data中，保证了缓冲
数组中的数组都是以‘$’开头的；如果遇到了帧尾，哈，我们现在遇到有了一个完整的帧
啦，可以去处理帧咯,将数据帧拷贝到read_report中，程序直接对read_report进行处理，处理之
前别忘了帧尾后面的字符是新的一桢的开头部分，要把它们也保存下来。在程序中我们看到
读不到数据就返回，如果不返回，这个函数就会一直运行，那么这样做的效果不就等价于阻
塞操作了么，非阻塞就失去了其意义。
*/


/*
命令实例:
1.recv	$0001#
  send	0001
*/

void uart_1_Main(int fd)
{
	char send_buf[20]="tiger john\n";
	int len;
	
	get_complete_frame(fd);
	if(read_report[0] == '$')
	{//-说明有有效命令接收到,下面开始处理
		if(strcmp(read_report,"$0001#") == 0)
			len = UART0_Send(fd,send_buf,strlen(send_buf));
		else if(strcmp(read_report,"$0002#") == 0)
		{
			send_buf[0] = '2';
			len = UART0_Send(fd,send_buf,strlen(send_buf));
		}
	}
}
