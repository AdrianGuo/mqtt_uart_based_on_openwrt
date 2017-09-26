/*
此文件作为线程的独立文件,所有实际内容都在这里处理,说明也在这里
*/
//-2016/10/25 09:48:56
/*
 线程是进程的一个实体，是CPU调度和分派的基本单位，它是比进程更小的能独立运行的
 基本单位。线程自己基本上不拥有系统资源，只拥有一点在运行中必不可少的资源(如程
 序计数器，一组寄存器和栈)，但是它可与同属一个进程的其他的线程共享进程所拥有的
 全部资源。
 什么时候使用多线程？     当多个任务可以并行执行时，可以为每个任务启动一个线程。

 tips:
pthread库不是linux默认的库，所以在编译时候需要指明libpthread.a库。
解决方法：在编译时，加上-lpthread参数。

*/

#include "debugfl.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
 


static int num=0;	//-前面的static表示仅仅这个文件中使用这个变量
pthread_mutex_t mylock=PTHREAD_MUTEX_INITIALIZER;


void *add(void *arg) 
{//线程执行函数，执行500次加法
  int i = 0,tmp;
  for (; i <500; i++)
  {
	  pthread_mutex_lock(&mylock);
    tmp=num+1;
    num=tmp;
    printf("add+1,result is:%d\n",num);
	pthread_mutex_unlock(&mylock);
  }
  return ((void *)0);
}

void *sub(void *arg)//线程执行函数，执行500次减法
{
  int i=0,tmp;
  for(;i<500;i++)
  {
	  pthread_mutex_lock(&mylock);	//-线程调用该函数让互斥锁上锁，如果该互斥锁已被另一个线程锁定和拥有，则调用该线程将阻塞，直到该互斥锁变为可用为止。
    tmp=num-1;
    num=tmp;
    printf("sub-1,result is:%d\n",num);
	pthread_mutex_unlock(&mylock);
  }
  return ((void *)0);
}

/*
输入运行命令:dreamflower_app -D -X
*/
int thread_sub(int argc, char** argv) //-这个是主线程,再开的就是子线程
{
   
  pthread_t tid1,tid2;
  int err;
  void *tret;
  err=pthread_create(&tid1,NULL,add,NULL);//创建线程
  if(err!=0)
  {
    printf("pthread_create error:%s\n",strerror(err));
    exit(-1);
  }
  err=pthread_create(&tid2,NULL,sub,NULL);
  if(err!=0)
  {
    printf("pthread_create error:%s\n",strerror(err));
     exit(-1);
  }
  err=pthread_join(tid1,&tret);//阻塞等待线程id为tid1的线程，直到该线程退出
  if(err!=0)
  {
    printf("can not join with thread1:%s\n",strerror(err));
    exit(-1);
  }
  printf("thread 1 exit code %d\n",(int)tret);
  err=pthread_join(tid2,&tret);
  if(err!=0)
  {
    printf("can not join with thread1:%s\n",strerror(err));
    exit(-1);
  }
  printf("thread 2 exit code %d\n",(int)tret);
  return 0;
}
