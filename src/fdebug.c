/*
此文件用于文件的创建和读写,使用这种方法可以进行信息的输出,用于辅助调试
*/

#include <fcntl.h>
#include <stdlib.h>
#include<unistd.h>




//输出目录   
#define DIR_OUT_FILE "/tmp/out" 







void f_debug(char *data)
{
	int fd;

//查看程序是否运行   
        //新建输出文件   
        system("touch "DIR_OUT_FILE);
	//打开输出文件   
        fd = open(DIR_OUT_FILE,O_CREAT|O_RDWR,0777); 
	//-char buf[100] = {'1','2',3}; 
	//-strcpy(buf,argv[2]);
	//-sprintf(buf, "%d", fd_uart1);
	//全部   
        //-write(fd,buf,100);
		write(fd,data,100);

	//删除输出文件   
        //-system("rm "DIR_OUT_FILE); 
}