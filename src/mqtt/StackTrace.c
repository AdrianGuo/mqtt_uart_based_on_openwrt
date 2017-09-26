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
 *    Ian Craggs - initial API and implementation and/or initial documentation
 *******************************************************************************/
//-这个文件目前并没有适用到,作为一个功能块在这里供调用
#include "StackTrace.h"
#include "Log.h"
#include "LinkedList.h"

#include "Clients.h"
#include "Thread.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(WIN32) || defined(WIN64)
#define snprintf _snprintf
#endif

/*BE
def STACKENTRY
{
	n32 ptr STRING open "name"
	n32 dec "line"
}

defList(STACKENTRY)
BE*/

#define MAX_STACK_DEPTH 50
#define MAX_FUNCTION_NAME_LENGTH 30
#define MAX_THREADS 255

typedef struct
{
	thread_id_type threadid;
	char name[MAX_FUNCTION_NAME_LENGTH];
	int line;
} stackEntry;

typedef struct
{
	thread_id_type id;
	int maxdepth;
	int current_depth;
	stackEntry callstack[MAX_STACK_DEPTH];
} threadEntry;

#include "StackTrace.h"

static int thread_count = 0;
static threadEntry threads[MAX_THREADS];	//-跟踪就是把信息输送处理显示,没有记录,这里开辟了全局变量记录可能的信息
static threadEntry *cur_thread = NULL;

#if defined(WIN32) || defined(WIN64)
mutex_type stack_mutex;
#else
static pthread_mutex_t stack_mutex_store = PTHREAD_MUTEX_INITIALIZER;
static mutex_type stack_mutex = &stack_mutex_store;
#endif


int setStack(int create)	//-首先在里面找,如果有就返回偏移量,没有的话建一个
{
	int i = -1;
	thread_id_type curid = Thread_getid();	//-获取线程自身ID

	cur_thread = NULL;
	for (i = 0; i < MAX_THREADS && i < thread_count; ++i)
	{
		if (threads[i].id == curid)
		{
			cur_thread = &threads[i];	//-寻找到当前的线程ID
			break;
		}
	}

	if (cur_thread == NULL && create && thread_count < MAX_THREADS)
	{//-在里面没有找到一样的,是一个新的线程那么就创建一个记录
		cur_thread = &threads[thread_count];
		cur_thread->id = curid;
		cur_thread->maxdepth = 0;
		cur_thread->current_depth = 0;
		++thread_count;
	}
	return cur_thread != NULL; /* good == 1 */
}

//-C标准中指定了一些预定义的宏，对于编程经常会用到。
//-__func__		当前所在函数名
//-__LINE__		代表当前源代码中的行号的整数常量
//-TRACE_MINIMUM		跟踪是分水平的,这个就是指跟踪的水平
void StackTrace_entry(const char* name, int line, int trace_level)
{
	Thread_lock_mutex(stack_mutex);
	if (!setStack(1))
		goto exit;
	if (trace_level != -1)	//-如果运行到这里就有了记录的变量
		Log_stackTrace(trace_level, 9, (int)cur_thread->id, cur_thread->current_depth, name, line, NULL);	//-里面进行了信息的输出打印
	strncpy(cur_thread->callstack[cur_thread->current_depth].name, name, sizeof(cur_thread->callstack[0].name)-1);	//-复制字符串的前n个字符
	cur_thread->callstack[(cur_thread->current_depth)++].line = line;	//-增加了进入的层次号,
	if (cur_thread->current_depth > cur_thread->maxdepth)
		cur_thread->maxdepth = cur_thread->current_depth;	//-记录了曾经进入的最深层次号
	if (cur_thread->current_depth >= MAX_STACK_DEPTH)	//-与堆栈最大的深度进行比较
		Log(LOG_FATAL, -1, "Max stack depth exceeded");
exit:
	Thread_unlock_mutex(stack_mutex);
}


void StackTrace_exit(const char* name, int line, void* rc, int trace_level)
{
	Thread_lock_mutex(stack_mutex);
	if (!setStack(0))	//-参数0表示寻找相同的,如果不存在不创建新的
		goto exit;
	if (--(cur_thread->current_depth) < 0)	//-这里的目的就是退出一层,通过打印信息了解程序执行的流程
		Log(LOG_FATAL, -1, "Minimum stack depth exceeded for thread %lu", cur_thread->id);
	if (strncmp(cur_thread->callstack[cur_thread->current_depth].name, name, sizeof(cur_thread->callstack[0].name)-1) != 0)
		Log(LOG_FATAL, -1, "Stack mismatch. Entry:%s Exit:%s\n", cur_thread->callstack[cur_thread->current_depth].name, name);
	if (trace_level != -1)
	{
		if (rc == NULL)
			Log_stackTrace(trace_level, 10, (int)cur_thread->id, cur_thread->current_depth, name, line, NULL);
		else
			Log_stackTrace(trace_level, 11, (int)cur_thread->id, cur_thread->current_depth, name, line, (int*)rc);
	}
exit:
	Thread_unlock_mutex(stack_mutex);
}


void StackTrace_printStack(FILE* dest)
{
	FILE* file = stdout;
	int t = 0;

	if (dest)
		file = dest;
	for (t = 0; t < thread_count; ++t)
	{
		threadEntry *cur_thread = &threads[t];

		if (cur_thread->id > 0)
		{
			int i = cur_thread->current_depth - 1;

			fprintf(file, "=========== Start of stack trace for thread %lu ==========\n", (unsigned long)cur_thread->id);
			if (i >= 0)
			{
				fprintf(file, "%s (%d)\n", cur_thread->callstack[i].name, cur_thread->callstack[i].line);
				while (--i >= 0)
					fprintf(file, "   at %s (%d)\n", cur_thread->callstack[i].name, cur_thread->callstack[i].line);
			}
			fprintf(file, "=========== End of stack trace for thread %lu ==========\n\n", (unsigned long)cur_thread->id);
		}
	}
	if (file != stdout && file != stderr && file != NULL)
		fclose(file);
}


char* StackTrace_get(thread_id_type threadid)
{
	int bufsize = 256;
	char* buf = NULL;
	int t = 0;

	if ((buf = malloc(bufsize)) == NULL)
		goto exit;
	buf[0] = '\0';
	for (t = 0; t < thread_count; ++t)
	{
		threadEntry *cur_thread = &threads[t];

		if (cur_thread->id == threadid)
		{
			int i = cur_thread->current_depth - 1;
			int curpos = 0;

			if (i >= 0)
			{
				curpos += snprintf(&buf[curpos], bufsize - curpos -1,
						"%s (%d)\n", cur_thread->callstack[i].name, cur_thread->callstack[i].line);
				while (--i >= 0)
					curpos += snprintf(&buf[curpos], bufsize - curpos -1,
							"   at %s (%d)\n", cur_thread->callstack[i].name, cur_thread->callstack[i].line);
				if (buf[--curpos] == '\n')
					buf[curpos] = '\0';
			}
			break;
		}
	}
exit:
	return buf;
}

