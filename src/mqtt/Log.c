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
 *    Ian Craggs - updates for the async client
 *    Ian Craggs - fix for bug #427028
 *******************************************************************************/
//-首先配置好就可以输出信息,然后如果设置好,还可以格式化打印出信息,看起来像日志
/**
 * @file
 * \brief Logging and tracing module
 *
 * 
 */

#include "Log.h"
#include "MQTTPacket.h"
#include "MQTTProtocol.h"
#include "MQTTProtocolClient.h"
#include "Messages.h"
#include "LinkedList.h"
#include "StackTrace.h"
#include "Thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

#if !defined(WIN32) && !defined(WIN64)
#include <syslog.h>
#include <sys/stat.h>
#define GETTIMEOFDAY 1
#else
#define snprintf _snprintf
#endif

#if defined(GETTIMEOFDAY)
	#include <sys/time.h>
#else
	#include <sys/timeb.h>
#endif

#if !defined(WIN32) && !defined(WIN64)
/**
 * _unlink mapping for linux
 */
#define _unlink unlink
#endif


#if !defined(min)
#define min(A,B) ( (A) < (B) ? (A):(B))
#endif
//-这个变量是一个定值,用于定义日志元素的
trace_settings_type trace_settings =
{
	TRACE_MINIMUM,
	400,
	-1
};

#define MAX_FUNCTION_NAME_LENGTH 256

typedef struct
{
#if defined(GETTIMEOFDAY)
	struct timeval ts;
#else
	struct timeb ts;
#endif
	int sametime_count;
	int number;
	int thread_id;
	int depth;
	char name[MAX_FUNCTION_NAME_LENGTH + 1];
	int line;
	int has_rc;
	int rc;
	int level;
} traceEntry;

static int start_index = -1,
			next_index = 0;
static traceEntry* trace_queue = NULL;	//-这里的跟踪队列是一些特定的信息,而并不是常规打印内容
static int trace_queue_size = 0;

static FILE* trace_destination = NULL;	/**< flag to indicate if trace is to be sent to a stream */	//-这里指定了内容输出的地点
static char* trace_destination_name = NULL; /**< the name of the trace file */
static char* trace_destination_backup_name = NULL; /**< the name of the backup trace file */
static int lines_written = 0; /**< number of lines written to the current output file */
static int max_lines_per_file = 1000; /**< maximum number of lines to write to one trace file */
static int trace_output_level = -1;
static Log_traceCallback* trace_callback = NULL;
static void Log_output(int log_level, char* msg);

static int sametime_count = 0;
#if defined(GETTIMEOFDAY)
struct timeval ts, last_ts;
#else
struct timeb ts, last_ts;
#endif
static char msg_buf[512];

#if defined(WIN32) || defined(WIN64)
mutex_type log_mutex;
#else
static pthread_mutex_t log_mutex_store = PTHREAD_MUTEX_INITIALIZER;
static mutex_type log_mutex = &log_mutex_store;
#endif


int Log_initialize(Log_nameValue* info)	//-这里输入的参数也是写入到输出文件中的,用于说明现在执行系统的基本信息的,并不是指输出地方的
{
	int rc = -1;
	char* envval = NULL;

	if ((trace_queue = malloc(sizeof(traceEntry) * trace_settings.max_trace_entries)) == NULL)	//-这里开辟了一个存储空间
		return rc;
	trace_queue_size = trace_settings.max_trace_entries;	//-从定值表中取出需要的值,这里是日志元素个数

	if ((envval = getenv("MQTT_C_CLIENT_TRACE")) != NULL && strlen(envval) > 0)	//-从环境中取字符串,获取环境变量的值
	{//-如果定义了的话将返回变量指针
		if (strcmp(envval, "ON") == 0 || (trace_destination = fopen(envval, "w")) == NULL)	//-文件顺利打开后，指向该流的文件指针就会被返回
			trace_destination = stdout;	//-stdout是一个文件指针,C己经在头文件中定义好的了，可以直接使用，把它赋给另一个文件指针。stdout（Standardoutput）标准输出
		else
		{
			trace_destination_name = malloc(strlen(envval) + 1);
			strcpy(trace_destination_name, envval);
			trace_destination_backup_name = malloc(strlen(envval) + 3);
			sprintf(trace_destination_backup_name, "%s.0", trace_destination_name);	//-把格式化的数据写入某个字符串缓冲区
		}
	}//-上面实现了,几种情况选择性输入,这里可以通过配置实现
	if ((envval = getenv("MQTT_C_CLIENT_TRACE_MAX_LINES")) != NULL && strlen(envval) > 0)
	{//-下面的参数可以配置,如果没有正确配置就使用默认值
		max_lines_per_file = atoi(envval);
		if (max_lines_per_file <= 0)
			max_lines_per_file = 1000;
	}
	if ((envval = getenv("MQTT_C_CLIENT_TRACE_LEVEL")) != NULL && strlen(envval) > 0)	//-这个所谓的环境变量也许就是一个宏定义
	{
		if (strcmp(envval, "MAXIMUM") == 0 || strcmp(envval, "TRACE_MAXIMUM") == 0)
			trace_settings.trace_level = TRACE_MAXIMUM;
		else if (strcmp(envval, "MEDIUM") == 0 || strcmp(envval, "TRACE_MEDIUM") == 0)
			trace_settings.trace_level = TRACE_MEDIUM;
		else if (strcmp(envval, "MINIMUM") == 0 || strcmp(envval, "TRACE_MEDIUM") == 0)
			trace_settings.trace_level = TRACE_MINIMUM;
		else if (strcmp(envval, "PROTOCOL") == 0  || strcmp(envval, "TRACE_PROTOCOL") == 0)
			trace_output_level = TRACE_PROTOCOL;
		else if (strcmp(envval, "ERROR") == 0  || strcmp(envval, "TRACE_ERROR") == 0)
			trace_output_level = LOG_ERROR;
	}
	Log_output(TRACE_MINIMUM, "=========================================================");	//-从这里开始已经在输出跟踪信息了,上面主要是在校验各种参数
	Log_output(TRACE_MINIMUM, "                   Trace Output");
	if (info)	//-*看看人家这个程序写的,太好了,太漂亮了
	{
		while (info->name)
		{
			snprintf(msg_buf, sizeof(msg_buf), "%s: %s", info->name, info->value);	//-将可变个参数(...)按照format格式化成字符串，然后将其复制到str中
			Log_output(TRACE_MINIMUM, msg_buf);	//-格式整理好了之后,进行输出处理
			info++;
		}
	}
#if !defined(WIN32) && !defined(WIN64)
	struct stat buf;
	if (stat("/proc/version", &buf) != -1)	//-通过文件名filename获取文件信息，并保存在buf所指的结构体stat中
	{//-先判断上面的文件在不在,上面的文件记录了一些信息,下面将读取需要的信息
		FILE* vfile;
		
		if ((vfile = fopen("/proc/version", "r")) != NULL)
		{
			int len;
			
			strcpy(msg_buf, "/proc/version: ");
			len = strlen(msg_buf);
			if (fgets(&msg_buf[len], sizeof(msg_buf) - len, vfile))	//-从文件结构体指针stream中读取数据，每次读取一行。
				Log_output(TRACE_MINIMUM, msg_buf);
			fclose(vfile);
		}
	}
#endif
	Log_output(TRACE_MINIMUM, "=========================================================");	//-输出分割符,以上是系统的基本信息打印
		
	return rc;
}


void Log_setTraceCallback(Log_traceCallback* callback)	//-提供了跟踪回调,但是目前没有使用
{
	trace_callback = callback;
}


void Log_setTraceLevel(enum LOG_LEVELS level)	//-设置跟踪等级
{
	if (level < TRACE_MINIMUM) /* the lowest we can go is TRACE_MINIMUM*/
		trace_settings.trace_level = level;
	trace_output_level = level;
}


void Log_terminate()	//-跟踪结束处理
{
	free(trace_queue);
	trace_queue = NULL;
	trace_queue_size = 0;
	if (trace_destination)
	{
		if (trace_destination != stdout)
			fclose(trace_destination);
		trace_destination = NULL;
	}
	if (trace_destination_name)
		free(trace_destination_name);	//-动态分配的内存用完之后可以用free释放掉，传给free的参数正是先前malloc返回的内存块首地址。
	if (trace_destination_backup_name)
		free(trace_destination_backup_name);	//-释放之后malloc又可以用了,否则不好用
	start_index = -1;
	next_index = 0;
	trace_output_level = -1;
	sametime_count = 0;
}


static traceEntry* Log_pretrace()	//-返回一个指向准备填写跟踪数据空间的指针
{
	traceEntry *cur_entry = NULL;

	/* calling ftime/gettimeofday seems to be comparatively expensive, so we need to limit its use */
	if (++sametime_count % 20 == 0)	//-由于使用很贵,所以限制使用次数
	{
#if defined(GETTIMEOFDAY)
		gettimeofday(&ts, NULL);
		if (ts.tv_sec != last_ts.tv_sec || ts.tv_usec != last_ts.tv_usec)
#else
		ftime(&ts);	//-将目前日期由tp所指的结构返回的函数
		if (ts.time != last_ts.time || ts.millitm != last_ts.millitm)	//-即获得了当前日期
#endif
		{
			sametime_count = 0;	//-结合上面控制了每20次更新一次显示时间
			last_ts = ts;
		}
	}

	if (trace_queue_size != trace_settings.max_trace_entries)
	{//-这里仅仅是防错处理,或者防止log没有初始化,而使用了跟踪,所以这进行了修正或初始化,正常是不进入的
		traceEntry* new_trace_queue = malloc(sizeof(traceEntry) * trace_settings.max_trace_entries);	//-新开辟的空间

		memcpy(new_trace_queue, trace_queue, min(trace_queue_size, trace_settings.max_trace_entries) * sizeof(traceEntry));
		free(trace_queue);
		trace_queue = new_trace_queue;
		trace_queue_size = trace_settings.max_trace_entries;

		if (start_index > trace_settings.max_trace_entries + 1 ||
				next_index > trace_settings.max_trace_entries + 1)
		{
			start_index = -1;
			next_index = 0;
		}
	}

	/* add to trace buffer */
	cur_entry = &trace_queue[next_index];	//-这个指向了一个跟踪存储空间
	if (next_index == start_index) /* means the buffer is full */
	{
		if (++start_index == trace_settings.max_trace_entries)
			start_index = 0;
	} else if (start_index == -1)
		start_index = 0;
	if (++next_index == trace_settings.max_trace_entries)	//-这里移动了索引号,指向了下一个准备填写数据的地方
		next_index = 0;	//-实现了循环记录

	return cur_entry;
}


static char* Log_formatTraceEntry(traceEntry* cur_entry)	//-根据传入的参数,进行格式化数据到一个缓冲区,其实就是格式转化
{
	struct tm *timeinfo;
	int buf_pos = 31;

#if defined(GETTIMEOFDAY)
	timeinfo = localtime(&cur_entry->ts.tv_sec);	//-把前面获取的系统时间(以秒为单位的大整数),转化为当地时间(用年月日时分秒表示)
#else
	timeinfo = localtime(&cur_entry->ts.time);
#endif
	strftime(&msg_buf[7], 80, "%Y%m%d %H%M%S ", timeinfo);
#if defined(GETTIMEOFDAY)
	sprintf(&msg_buf[22], ".%.3lu ", cur_entry->ts.tv_usec / 1000L);
#else
	sprintf(&msg_buf[22], ".%.3hu ", cur_entry->ts.millitm);
#endif
	buf_pos = 27;

	sprintf(msg_buf, "(%.4d)", cur_entry->sametime_count);
	msg_buf[6] = ' ';

	if (cur_entry->has_rc == 2)
		strncpy(&msg_buf[buf_pos], cur_entry->name, sizeof(msg_buf)-buf_pos);
	else
	{
		char* format = Messages_get(cur_entry->number, cur_entry->level);
		if (cur_entry->has_rc == 1)
			snprintf(&msg_buf[buf_pos], sizeof(msg_buf)-buf_pos, format, cur_entry->thread_id,
					cur_entry->depth, "", cur_entry->depth, cur_entry->name, cur_entry->line, cur_entry->rc);
		else
			snprintf(&msg_buf[buf_pos], sizeof(msg_buf)-buf_pos, format, cur_entry->thread_id,
					cur_entry->depth, "", cur_entry->depth, cur_entry->name, cur_entry->line);
	}
	return msg_buf;
}


static void Log_output(int log_level, char* msg)	//-把信息打印到预设的文件中
{
	if (trace_destination)	//-判断文件路径是否有效,有才有必要写
	{
		fprintf(trace_destination, "%s\n", msg);	//-传送格式化输出到一个文件中,,输出到文件其实这里的情况就是显示了在终端,在Linux文件中所有东西都是文件

		if (trace_destination != stdout && ++lines_written >= max_lines_per_file)	//-如果文件写满了,就重新写,把原来的删除
		{//-如果不是标准输出设备,而是写文件的话,这里就进行适合文件的处理	
			//?这里的目的是什么还不清楚
			fclose(trace_destination);	//-把缓冲区内最后剩余的数据输出到内核缓冲区，并释放文件指针和有关的缓冲区。		
			_unlink(trace_destination_backup_name); /* remove any old backup trace file */	//-会删除参数pathname指定的文件
			rename(trace_destination_name, trace_destination_backup_name); /* rename recently closed to backup */
			trace_destination = fopen(trace_destination_name, "w"); /* open new trace file */
			if (trace_destination == NULL)
				trace_destination = stdout;
			lines_written = 0;
		}
		else
			fflush(trace_destination);	//-fflush(stdout)刷新标准输出缓冲区，把输出缓冲区里的东西打印到标准输出设备上,,这里相当于立即输出
	}
		
	if (trace_callback)	//-这里应该是预留的一个打印回调函数,但是目前并没有使用,人家这么做是的程序扩展性很好
		(*trace_callback)(log_level, msg);
}


static void Log_posttrace(int log_level, traceEntry* cur_entry)	//-投递跟踪,,也是向跟踪文件中输入信息,只是这个信息是特定内容
{
	if (((trace_output_level == -1) ? log_level >= trace_settings.trace_level : log_level >= trace_output_level))
	{//-上面还是水平控制语句,所有的代码都是一样的,但是由于参数不同,效果是不同的
		char* msg = NULL;
		
		if (trace_destination || trace_callback)
			msg = &Log_formatTraceEntry(cur_entry)[7];	//-前面所有的信息都记录在一个结构体中,这里根据信息,形成了一个内容(仅仅转换成了直观表达)
		
		Log_output(log_level, msg);	//-就是把内容输出到需要的地方,比如文件,终端窗口等
	}
}


static void Log_trace(int log_level, char* buf)	//-跟踪是一个独立功能和打印信息不一样
{
	traceEntry *cur_entry = NULL;

	if (trace_queue == NULL)	//-不等于0才有跟踪功能,才需要继续处理
		return;

	cur_entry = Log_pretrace();	//-进入跟踪之前的前期处理

	memcpy(&(cur_entry->ts), &ts, sizeof(ts));
	cur_entry->sametime_count = sametime_count;

	cur_entry->has_rc = 2;
	strncpy(cur_entry->name, buf, sizeof(cur_entry->name));
	cur_entry->name[MAX_FUNCTION_NAME_LENGTH] = '\0';

	Log_posttrace(log_level, cur_entry);	//-感觉有点像跟踪打印的固定结束语一样
}


/**
 * Log a message.  If possible, all messages should be indexed by message number, and
 * the use of the format string should be minimized or negated altogether.  If format is
 * provided, the message number is only used as a message label.
 * @param log_level the log level of the message
 * @param msgno the id of the message to use if the format string is NULL
 * @param aFormat the printf format string to be used if the message id does not exist
 * @param ... the printf inserts
 */
void Log(int log_level, int msgno, char* format, ...)	//-参数是可变的,下面需要通过一定的方法获取具体参数
{
	if (log_level >= trace_settings.trace_level)	//-这里控制了等级打印,不同等级打印不同信息
	{
		char* temp = NULL;
		static char msg_buf[512];
		va_list args;	//-定义一个va_list型的变量,这个变量是指向参数的指针.

		/* we're using a static character buffer, so we need to make sure only one thread uses it at a time */
		Thread_lock_mutex(log_mutex);	//-这里调用了一个互斥锁,其实就是对库函数的又一封装 
		if (format == NULL && (temp = Messages_get(msgno, log_level)) != NULL)	//-尝试获得协议内容,在一个数组内查找何时的元素,根据偏移量确定
			format = temp;
		//-在C中，当我们无法列出传递函数的所有实参的类型和数目时,可以用省略号指定参数表
		va_start(args, format);	//-用va_start宏初始化变量,这个宏的第二个参数是第一个可变参数的前一个参数,是一个固定的参数
		vsnprintf(msg_buf, sizeof(msg_buf), format, args);	//-将可变参数格式化输出到一个字符数组。

		Log_trace(log_level, msg_buf);	//-上面获取了可变参数的值,这里进行了使用
		va_end(args);	//-用va_end宏结束可变参数的获取
		Thread_unlock_mutex(log_mutex); 
	}

	/*if (log_level >= LOG_ERROR)
	{
		char* filename = NULL;
		Log_recordFFDC(&msg_buf[7]);
	}
	*/
}


/**
 * The reason for this function is to make trace logging as fast as possible so that the
 * function exit/entry history can be captured by default without unduly impacting
 * performance.  Therefore it must do as little as possible.
 * @param log_level the log level of the message
 * @param msgno the id of the message to use if the format string is NULL
 * @param aFormat the printf format string to be used if the message id does not exist
 * @param ... the printf inserts
 */
void Log_stackTrace(int log_level, int msgno, int thread_id, int current_depth, const char* name, int line, int* rc)	//?猜测用于堆栈的跟踪
{
	traceEntry *cur_entry = NULL;

	if (trace_queue == NULL)	//-光有调试需求也不行,也需要开辟了这样的功能
		return;

	if (log_level < trace_settings.trace_level)	//-跟踪设置了一个水平选项,可以有选择性的跟踪,而不一定需要修改代码,仅仅修改一个参数即可
		return;
	//-上面的思想很好,这样可以在选择的基础上最大限度的减少程序修改
	Thread_lock_mutex(log_mutex);
	cur_entry = Log_pretrace();

	memcpy(&(cur_entry->ts), &ts, sizeof(ts));	//-向跟踪信息体中填写数据
	cur_entry->sametime_count = sametime_count;
	cur_entry->number = msgno;
	cur_entry->thread_id = thread_id;
	cur_entry->depth = current_depth;
	strcpy(cur_entry->name, name);
	cur_entry->level = log_level;
	cur_entry->line = line;
	if (rc == NULL)
		cur_entry->has_rc = 0;
	else
	{
		cur_entry->has_rc = 1;
		cur_entry->rc = *rc;
	}
	//-上面是向结构体组织内容,但是这个内容并不形象,下面就转化成了形象的内容形式,输出人直接阅读
	Log_posttrace(log_level, cur_entry);
	Thread_unlock_mutex(log_mutex);
}


FILE* Log_destToFile(char* dest)
{
	FILE* file = NULL;

	if (strcmp(dest, "stdout") == 0)
		file = stdout;
	else if (strcmp(dest, "stderr") == 0)
		file = stderr;
	else
	{
		if (strstr(dest, "FFDC"))
			file = fopen(dest, "ab");
		else
			file = fopen(dest, "wb");
	}
	return file;
}


int Log_compareEntries(char* entry1, char* entry2)
{
	int comp = strncmp(&entry1[7], &entry2[7], 19);

	/* if timestamps are equal, use the sequence numbers */
	if (comp == 0)
		comp = strncmp(&entry1[1], &entry2[1], 4);

	return comp;
}


#if 0
/**
 * Write the contents of the stored trace to a stream
 * @param dest string which contains a file name or the special strings stdout or stderr
 */
int Log_dumpTrace(char* dest)
{
	FILE* file = NULL;
	ListElement* cur_trace_entry = NULL;
	const int msgstart = 7;
	int rc = -1;
	int trace_queue_index = 0;

	if ((file = Log_destToFile(dest)) == NULL)
	{
		Log(LOG_ERROR, 9, NULL, "trace", dest, "trace entries");
		goto exit;
	}

	fprintf(file, "=========== Start of trace dump ==========\n");
	/* Interleave the log and trace entries together appropriately */
	ListNextElement(trace_buffer, &cur_trace_entry);
	trace_queue_index = start_index;
	if (trace_queue_index == -1)
		trace_queue_index = next_index;
	else
	{
		Log_formatTraceEntry(&trace_queue[trace_queue_index++]);
		if (trace_queue_index == trace_settings.max_trace_entries)
			trace_queue_index = 0;
	}
	while (cur_trace_entry || trace_queue_index != next_index)
	{
		if (cur_trace_entry && trace_queue_index != -1)
		{	/* compare these timestamps */
			if (Log_compareEntries((char*)cur_trace_entry->content, msg_buf) > 0)
				cur_trace_entry = NULL;
		}

		if (cur_trace_entry)
		{
			fprintf(file, "%s\n", &((char*)(cur_trace_entry->content))[msgstart]);
			ListNextElement(trace_buffer, &cur_trace_entry);
		}
		else
		{
			fprintf(file, "%s\n", &msg_buf[7]);
			if (trace_queue_index != next_index)
			{
				Log_formatTraceEntry(&trace_queue[trace_queue_index++]);
				if (trace_queue_index == trace_settings.max_trace_entries)
					trace_queue_index = 0;
			}
		}
	}
	fprintf(file, "========== End of trace dump ==========\n\n");
	if (file != stdout && file != stderr && file != NULL)
		fclose(file);
	rc = 0;
exit:
	return rc;
}
#endif


