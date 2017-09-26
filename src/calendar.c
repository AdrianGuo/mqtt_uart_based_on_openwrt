//-2016/10/11 18:30:03
/*
（一） Linux 的 时间概念
       Linux 系统下统计程序运行实践最简单直接的方法就是使用 time 命令，此命令的用途在于测量特定指令执行时所需消耗的时间及系统资源等资讯，在统计的时间结 果中包含以下数据：
       (1) 实际时间（ real time ）：从命令行执行到运行终止的消逝时间；
       (2) 用户 CPU 时间（ user CPU time ）：命 令执行完成花费的系统 CPU 时间，即命令在用户态中执行时间的总和；
       (3) 系统 CPU 时间（ system CPU time ）： 命令执行完成花费的系统 CPU 时间，即命令在核心态中执行时间的总和。
       其中，用户 CPU 时 间和系统 CPU 时间之和为 CPU 时 间，即命令占用 CPU 执行的时间总和。实际时间要大于 CPU时 间，因为 Linux 是多任务操作系统，往往在执行一条命令时，系统还要处理其他任务。另一个需要注意的问题是即使每次 执行相同的命令，所花费的时间也不一定相同，因为其花费的时间与系统运行相关。


（二）下面直接给出5种变量类型的代码断来进行说明，
clock_t、struct tms相关为计时测领，
time_t、struct tm相关为日历时间测量。
timeval 相当于更精确的time_t，格式为秒数.微秒数。

现在就在这样的基础上形成一个历程,主要功能就是读取系统时间,可以作为一个测量依据更好
功能:
1.获得程序的实际运行时间（秒)
2.time(time_t * timer)获取当前的系统时间，返回的结果是一个time_t类型，其实就是一个大整数，其值表示从CUT（Coordinated Universal Time）时间1970年1月1日00:00:00（称为UNIX系统的Epoch时间）到当前时刻的秒数。然后调用localtime将time_t所表示的CUT时间转换为本地时间（我们是+8区，比CUT多8个小时）并转成struct tm类型，该类型的各数据成员分别表示年月日时分秒。
3.使用C语言编写程序需要获得当前精确时间（1970年1月1日到现在的时间），或者为执行计时，可以使用gettimeofday()函数。
*/

#include "debugfl.h"

//相关的头文件  
#include <time.h>
#include <sys/times.h> //需要sys目录
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int calendar_sub(int argc,char* argv[])
{
  long    i = 1000000000L;
  clock_t start_c, finish_c;
  time_t start_t,finish_t;
  struct timeval start_stv,finish_stv; //对time_t类型的封装
  struct tms start_stms,finish_stms;//对clock_t类型的封装
  struct tm *start_stm,*finish_stm; //对time_t进行变体，类型为int
  double  duration;
  long luration;
  
   /* 测量一个事件持续的时间*/
  printf( "Time to do %ld empty loops is \n", i );

  start_c = times(&start_stms); //从后面可以看出times和clock取得的clock_t的数量级不一样，一个为毫秒＊10，一个为微妙
  start_c = clock();//进程时间测量，进程开始时调用时返回为0

  start_t = time(NULL); //只能精确到秒,,timer = time(NULL);//这一句也可以改成time(&timer);
  gettimeofday(&start_stv,NULL);	//-它获得的时间精确到微秒（1e-6 s)量级。在一段代码前后分别使用gettimeofday可以计算代码执行时间


  start_stm = localtime(&start_t); //时钟向日历时间的转换，mktime为其逆向过程函数，日历的函数有很多，我一般用这个记那么多格式也累，用的时候再man

  //也可以通过ctime直接打印出时间来。
  printf("====\nstart time:%04d-%02d-%02d %02d:%02d:%02d\n",
         start_stm->tm_year+1900,
         start_stm->tm_mon+1,
         start_stm->tm_mday,
         start_stm->tm_hour,
         start_stm->tm_min,
         start_stm->tm_sec);
  printf("start time:%s",ctime(&start_t));
    
  while( i-- )      ;

  finish_c = times(&finish_stms);
  finish_c = clock();

  finish_t = time(NULL);
  gettimeofday(&finish_stv,NULL);
  

  duration = (double)(finish_c - start_c)/CLOCKS_PER_SEC; //-这个是获得程序的实际运行时间（秒)
  printf( "1. [clock_t] get  %f secondes,start = %u,finish =%u\n",duration,start_c,finish_c );

  luration = (finish_t - start_t);
  printf("2. [time_t] get %u seconds,start = %u, finish = %u\n",luration,start_t,finish_t);

  luration = (finish_stv.tv_sec - start_stv.tv_sec)*1000 + ( finish_stv.tv_usec - start_stv.tv_usec)/1000;
  printf("3. [struct timeval] get %u ms,start=%u,finish=%u\n",luration,start_stv.tv_sec,finish_stv.tv_sec);

  long clktck=sysconf(_SC_CLK_TCK);	//-该函数是获取一些系统的参数,,这里获得The  number  of  clock  ticks  per  second
  
  printf("================_SC_CLK_TCK=%d,CLOCKS_PER_SEC=%d\n",clktck,CLOCKS_PER_SEC);

  
  printf("4. [struct tms] get user= %7.2f,sys=%7.2f,child_user=%7.2f,child_sys=%7.2f\n",
         (finish_stms.tms_utime - start_stms.tms_utime)/(double)clktck,
         (finish_stms.tms_stime - start_stms.tms_stime)/(double)clktck,
         (finish_stms.tms_cutime - start_stms.tms_cutime)/(double)clktck,
         (finish_stms.tms_cstime - start_stms.tms_cstime)/(double)clktck ); 

  //-exit(0);

}





//-　　int tm_sec; /* 秒–取值区间为[0,59] */
//-　　int tm_min; /* 分 - 取值区间为[0,59] */
//-　　int tm_hour; /* 时 - 取值区间为[0,23] */
//-　　int tm_mday; /* 一个月中的日期 - 取值区间为[1,31] */
//-　　int tm_mon; /* 月份（从一月开始，0代表一月） - 取值区间为[0,11] */
//-　　int tm_year; /* 年份，其值从1900开始 */
//-　　int tm_wday; /* 星期–取值区间为[0,6]，其中0代表星期天，1代表星期一，以此类推 */
//-　　int tm_yday; /* 从每年的1月1日开始的天数–取值区间为[0,365]，其中0代表1月1日，1代表1月2日，以此类推 */
//-　　int tm_isdst; /* 夏令时标识符，实行夏令时的时候，tm_isdst为正。不实行夏令时的进候，tm_isdst为0；不了解情况时，tm_isdst()为负。*/

//-　　time_t tv_sec; /* seconds */
//-　　suseconds_t tv_usec; /* microseconds */
