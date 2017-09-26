/*******************************************************************************
 * Copyright (c) 2009, 2014 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution. 
 *
 * The Eclipse Public License is available at 
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at 
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial implementation
 *    Ian Craggs, Allan Stockdill-Mander - async client updates
 *    Ian Craggs - bug #415042 - start Linux thread as disconnected
 *    Ian Craggs - fix for bug #420851
 *******************************************************************************/
//-通过这个文件我知道了我必须彻底吃透这个例程,如果想学习Linux的应用程序的话.这个文件就是很好的其他程序使用的模块!!!
/**
 * @file
 * \brief Threading related functions
 *
 * Used to create platform independent threading functions
 */


#include "Thread.h"
#if defined(THREAD_UNIT_TESTS)	//-如果在测试线程单元模块的话会定义,实际使用中没有定义
#define NOSTACKTRACE
#endif
#include "StackTrace.h"

#undef malloc
#undef realloc
#undef free

#if !defined(WIN32) && !defined(WIN64)
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <limits.h>
#endif
#include <memory.h>
#include <stdlib.h>

//-下面创建了一个脱离线程
/**
 * Start a new thread
 * @param fn the function to run, must be of the correct signature
 * @param parameter pointer to the function parameter, can be NULL
 * @return the new thread
 */
thread_type Thread_start(thread_fn fn, void* parameter)	//-输入的是创建的线程运行的函数和进入的参数
{
#if defined(WIN32) || defined(WIN64)
	thread_type thread = NULL;
#else
	thread_type thread = 0;
	pthread_attr_t attr;
#endif

	FUNC_ENTRY;
#if defined(WIN32) || defined(WIN64)
	thread = CreateThread(NULL, 0, fn, parameter, 0, NULL);
#else
	pthread_attr_init(&attr);	//-初始化一个线程对象的属性,需要用pthread_attr_destroy函数对其去除初始化。
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);	//-线程分离属性设置
	if (pthread_create(&thread, &attr, fn, parameter) != 0)	//-创建线程 thread记录了线程的ID号 attr设置线程属性 fn线程执行的主函数 运行函数的参数
		thread = 0;	//-上面的函数创建成功了返回0,不成功就需要把这个参数清0
	pthread_attr_destroy(&attr);
#endif
	FUNC_EXIT;
	return thread;
}


/**
 * Create a new mutex
 * @return the new mutex
 */
mutex_type Thread_create_mutex()
{
	mutex_type mutex = NULL;
	int rc = 0;

	FUNC_ENTRY;
	#if defined(WIN32) || defined(WIN64)
		mutex = CreateMutex(NULL, 0, NULL);
	#else
		mutex = malloc(sizeof(pthread_mutex_t));
		rc = pthread_mutex_init(mutex, NULL);
	#endif
	FUNC_EXIT_RC(rc);
	return mutex;
}


/**
 * Lock a mutex which has already been created, block until ready
 * @param mutex the mutex
 * @return completion code, 0 is success
 */
int Thread_lock_mutex(mutex_type mutex)	//-互斥锁,会阻塞直到可用
{
	int rc = -1;

	/* don't add entry/exit trace points as the stack log uses mutexes - recursion beckons */
	#if defined(WIN32) || defined(WIN64)
		/* WaitForSingleObject returns WAIT_OBJECT_0 (0), on success */
		rc = WaitForSingleObject(mutex, INFINITE);
	#else
		rc = pthread_mutex_lock(mutex);	//-线程调用该函数让互斥锁上锁，如果该互斥锁已被另一个线程锁定和拥有，则调用该线程将阻塞，直到该互斥锁变为可用为止。
	#endif														//-当pthread_mutex_lock()返回时，该互斥锁已被锁定。

	return rc;
}


/**
 * Unlock a mutex which has already been locked
 * @param mutex the mutex
 * @return completion code, 0 is success
 */
int Thread_unlock_mutex(mutex_type mutex)
{
	int rc = -1;

	/* don't add entry/exit trace points as the stack log uses mutexes - recursion beckons */
	#if defined(WIN32) || defined(WIN64)
		/* if ReleaseMutex fails, the return value is 0 */
		if (ReleaseMutex(mutex) == 0)
			rc = GetLastError();
		else
			rc = 0;
	#else
		rc = pthread_mutex_unlock(mutex);
	#endif

	return rc;
}


/**
 * Destroy a mutex which has already been created
 * @param mutex the mutex
 */
void Thread_destroy_mutex(mutex_type mutex)
{
	int rc = 0;

	FUNC_ENTRY;
	#if defined(WIN32) || defined(WIN64)
		rc = CloseHandle(mutex);
	#else
		rc = pthread_mutex_destroy(mutex);	//-互斥锁销毁函数
		free(mutex);
	#endif
	FUNC_EXIT_RC(rc);
}


/**
 * Get the thread id of the thread from which this function is called
 * @return thread id, type varying according to OS
 */
thread_id_type Thread_getid()
{
	#if defined(WIN32) || defined(WIN64)
		return GetCurrentThreadId();
	#else
		return pthread_self();	//-获得线程自身的ID
	#endif
}


#if defined(USE_NAMED_SEMAPHORES)
#define MAX_NAMED_SEMAPHORES 10

static int named_semaphore_count = 0;

static struct 
{
	sem_type sem;
	char name[NAME_MAX-4];
} named_semaphores[MAX_NAMED_SEMAPHORES];
 
#endif

/*
信号量(Semaphore)，有时被称为信号灯，是在多线程环境下使用的一种设施，是可以用来
保证两个或多个关键代码段不被并发调用。在进入一个关键代码段之前，线程必须获取一个
信号量；一旦该关键代码段完成了，那么该线程必须释放信号量。其它想进入该关键代码段
的线程必须等待直到第一个线程释放信号量。为了完成这个过程，需要创建一个信号量VI，
然后将Acquire Semaphore VI以及Release Semaphore VI分别放置在每个关键代码段的首末
端。确认这些信号量VI引用的是初始创建的信号量。
抽象的来讲，信号量的特性如下：信号量是一个非负整数（车位数），所有通过它的线程/进
程（车辆）都会将该整数减一（通过它当然是为了使用资源），当该整数值为零时，所有试图
通过它的线程都将处于等待状态。在信号量上我们定义两种操作： Wait（等待） 和 Release（释放）。
当一个线程调用Wait操作时，它要么得到资源然后将信号量减一，要么一直等下去（指放入阻塞队列），
直到信号量大于等于一时。Release（释放）实际上是在信号量上执行加操作，对应于车辆离开停车场，
该操作之所以叫做“释放”是因为释放了由信号量守护的资源。
*/
/**
 * Create a new semaphore
 * @return the new condition variable
 */
sem_type Thread_create_sem()
{
	sem_type sem = NULL;
	int rc = 0;

	FUNC_ENTRY;
	#if defined(WIN32) || defined(WIN64)
		sem = CreateEvent(
		        NULL,               // default security attributes
		        FALSE,              // manual-reset event?
		        FALSE,              // initial state is nonsignaled
		        NULL                // object name
		        );
    #elif defined(USE_NAMED_SEMAPHORES)
    	if (named_semaphore_count == 0)
    		memset(named_semaphores, '\0', sizeof(named_semaphores));
    	char* name = &(strrchr(tempnam("/", "MQTT"), '/'))[1]; /* skip first slash of name */
    	if ((sem = sem_open(name, O_CREAT, S_IRWXU, 0)) == SEM_FAILED)
    		rc = -1;
    	else
    	{
    		int i;
    		
    		named_semaphore_count++;
    		for (i = 0; i < MAX_NAMED_SEMAPHORES; ++i)
    		{
    			if (named_semaphores[i].name[0] == '\0')
    			{ 
    				named_semaphores[i].sem = sem;
    				strcpy(named_semaphores[i].name, name);	
    				break;
    			}
    		}
    	}
	#else
		sem = malloc(sizeof(sem_t));	//-信号量其实就是一个变量,只是这个变量被程序赋予了特殊含义和用途
		rc = sem_init(sem, 0, 0);	//-初始化一个定位在 sem 的匿名信号量。
	#endif
	FUNC_EXIT_RC(rc);
	return sem;
}


/*
对于信号量这里好像是用于延时用的,进入了一个阶段设置一个延时后才可以处理其他的东西,不能立即运行
*/
/**
 * Wait for a semaphore to be posted, or timeout.
 * @param sem the semaphore
 * @param timeout the maximum time to wait, in milliseconds
 * @return completion code
 */
int Thread_wait_sem(sem_type sem, int timeout)	//-等待一个被增加的信号量,或者超时
{
/* sem_timedwait is the obvious call to use, but seemed not to work on the Viper,
 * so I've used trywait in a loop instead. Ian Craggs 23/7/2010
 */
	int rc = -1;
#if !defined(WIN32) && !defined(WIN64)
#define USE_TRYWAIT
#if defined(USE_TRYWAIT)
	int i = 0;
	int interval = 10000; /* 10000 microseconds: 10 milliseconds */
	int count = (1000 * timeout) / interval; /* how many intervals in timeout period */
#else
	struct timespec ts;
#endif
#endif

	FUNC_ENTRY;
	#if defined(WIN32) || defined(WIN64)
		rc = WaitForSingleObject(sem, timeout);
	#elif defined(USE_TRYWAIT)
		while (++i < count && (rc = sem_trywait(sem)) != 0)	//-递减由sem_t类型的指针变量sem指向的信号量。
		{//-值大于0,则将信号量的值减一，然后函数立即返回
			if (rc == -1 && ((rc = errno) != EAGAIN))	//-如果信号量的当前值为0，则返回错误而不是阻塞调用。错误值errno设置为EAGAIN。
			{
				rc = 0;
				break;
			}
			usleep(interval); /* microseconds - .1 of a second */
		}
	#else
		if (clock_gettime(CLOCK_REALTIME, &ts) != -1)
		{
			ts.tv_sec += timeout;
			rc = sem_timedwait(sem, &ts);
		}
	#endif

 	FUNC_EXIT_RC(rc);
 	return rc;
}


/**
 * Check to see if a semaphore has been posted, without waiting.
 * @param sem the semaphore
 * @return 0 (false) or 1 (true)
 */
int Thread_check_sem(sem_type sem)	//-获取信号量的值,但是也有可能同时被改变了
{
#if defined(WIN32) || defined(WIN64)
	return WaitForSingleObject(sem, 0) == WAIT_OBJECT_0;
#else
	int semval = -1;
	sem_getvalue(sem, &semval);	//-把 sem 指向的信号量当前值放置在 sval 指向的整数上。
	return semval > 0;
#endif
}

//-同时对同一个信号量做加“1”操作的两个线程是不会冲突的；而同 时对同一个文件进行读、加和写操作的两个程序就有可能会引起冲突。
//- 当有线程阻塞在这个信号量上时，调用这个函数会使其中一个线程不在阻塞，选择机制是有线程的调度策略决定的。
/**
 * Post a semaphore
 * @param sem the semaphore
 * @return completion code
 */
int Thread_post_sem(sem_type sem)	//-原子操作的原理我应该是清楚的,CPU同一刻只能执行一个指令
{
	int rc = 0;

	FUNC_ENTRY;
	#if defined(WIN32) || defined(WIN64)	//-程序适合多个平台运行,是通过这些预编译实现的
		if (SetEvent(sem) == 0)
			rc = GetLastError();
	#else
		if (sem_post(sem) == -1)	//-给信号量的值加上一个“1”，它是一个“原子操作”
			rc = errno;
	#endif

 	FUNC_EXIT_RC(rc);
  return rc;
}


/**
 * Destroy a semaphore which has already been created
 * @param sem the semaphore
 */
int Thread_destroy_sem(sem_type sem)	//-信号量的数据类型为结构sem_t，它本质上是一个长整型的数。
{
	int rc = 0;

	FUNC_ENTRY;
	#if defined(WIN32) || defined(WIN64)
		rc = CloseHandle(sem);
    #elif defined(USE_NAMED_SEMAPHORES)
    	int i;
    	rc = sem_close(sem);
    	for (i = 0; i < MAX_NAMED_SEMAPHORES; ++i)
    	{
    		if (named_semaphores[i].sem == sem)
    		{ 
    			rc = sem_unlink(named_semaphores[i].name);
    			named_semaphores[i].name[0] = '\0';	
    			break;
    		}
    	}
    	named_semaphore_count--;
	#else
		rc = sem_destroy(sem);	//-在我们用完信号量对它进行清理
		free(sem);
	#endif
	FUNC_EXIT_RC(rc);
	return rc;
}


#if !defined(WIN32) && !defined(WIN64)
/**
 * Create a new condition variable
 * @return the condition variable struct
 */
cond_type Thread_create_cond()
{
	cond_type condvar = NULL;
	int rc = 0;

	FUNC_ENTRY;
	condvar = malloc(sizeof(cond_type_struct));
	rc = pthread_cond_init(&condvar->cond, NULL);	//-用来初始化一个条件变量
	rc = pthread_mutex_init(&condvar->mutex, NULL);	//-以动态方式创建互斥锁的，参数attr指定了新建互斥锁的属性。

	FUNC_EXIT_RC(rc);
	return condvar;
}

/**
 * Signal a condition variable
 * @return completion code
 */
int Thread_signal_cond(cond_type condvar)	//-结束一个阻塞的线程,使其继续执行
{
	int rc = 0;

	pthread_mutex_lock(&condvar->mutex);
	rc = pthread_cond_signal(&condvar->cond);	//-发送一个信号给另外一个正在处于阻塞等待状态的线程,使其脱离阻塞状态,继续执行.如果没有线程处在阻塞等待状态,pthread_cond_signal也会成功返回。
	pthread_mutex_unlock(&condvar->mutex);	//-解锁

	return rc;
}

//-条件变量是利用线程间共享的全局变量进行同步的一种机制，主要包括两个动作：一个线程等待"条件变量的条件成立"而挂起；另一个线程使"条件成立"（给出条件成立信号）。
/**
 * Wait with a timeout (seconds) for condition variable
 * @return completion code
 */
int Thread_wait_cond(cond_type condvar, int timeout)	//-调用的线程会被挂起
{
	FUNC_ENTRY;
	int rc = 0;
	struct timespec cond_timeout;
	struct timeval cur_time;

	gettimeofday(&cur_time, NULL);

	cond_timeout.tv_sec = cur_time.tv_sec + timeout;
	cond_timeout.tv_nsec = cur_time.tv_usec * 1000;

	pthread_mutex_lock(&condvar->mutex);
	rc = pthread_cond_timedwait(&condvar->cond, &condvar->mutex, &cond_timeout);
	pthread_mutex_unlock(&condvar->mutex);

	FUNC_EXIT_RC(rc);
	return rc;
}

/**
 * Destroy a condition variable
 * @return completion code
 */
int Thread_destroy_cond(cond_type condvar)	//-销毁一个环境变量,这里使用到的是库函数,不需要更深层次的了解
{
	int rc = 0;

	rc = pthread_mutex_destroy(&condvar->mutex);	//-互斥锁销毁函数,mutex 指向要销毁的互斥锁的指针
	rc = pthread_cond_destroy(&condvar->cond);	//-销毁一个条件变量,cond是指向pthread_cond_t结构的指针
	free(condvar);

	return rc;
}
#endif


#if defined(THREAD_UNIT_TESTS)

#include <stdio.h>

thread_return_type secondary(void* n)
{
	int rc = 0;

	/*
	cond_type cond = n;

	printf("Secondary thread about to wait\n");
	rc = Thread_wait_cond(cond);
	printf("Secondary thread returned from wait %d\n", rc);*/

	sem_type sem = n;

	printf("Secondary thread about to wait\n");
	rc = Thread_wait_sem(sem);
	printf("Secondary thread returned from wait %d\n", rc);

	printf("Secondary thread about to wait\n");
	rc = Thread_wait_sem(sem);
	printf("Secondary thread returned from wait %d\n", rc);
	printf("Secondary check sem %d\n", Thread_check_sem(sem));

	return 0;
}


int main(int argc, char *argv[])
{
	int rc = 0;

	sem_type sem = Thread_create_sem();

	printf("check sem %d\n", Thread_check_sem(sem));

	printf("post secondary\n");
	rc = Thread_post_sem(sem);
	printf("posted secondary %d\n", rc);

	printf("check sem %d\n", Thread_check_sem(sem));

	printf("Starting secondary thread\n");
	Thread_start(secondary, (void*)sem);

	sleep(3);
	printf("check sem %d\n", Thread_check_sem(sem));

	printf("post secondary\n");
	rc = Thread_post_sem(sem);
	printf("posted secondary %d\n", rc);

	sleep(3);

	printf("Main thread ending\n");
}

#endif
