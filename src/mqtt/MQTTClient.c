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
 *    Ian Craggs - bug 384016 - segv setting will message
 *    Ian Craggs - bug 384053 - v1.0.0.7 - stop MQTTClient_receive on socket error 
 *    Ian Craggs, Allan Stockdill-Mander - add ability to connect with SSL
 *    Ian Craggs - multiple server connection support
 *    Ian Craggs - fix for bug 413429 - connectionLost not called
 *    Ian Craggs - fix for bug 421103 - trying to write to same socket, in publish/retries
 *    Ian Craggs - fix for bug 419233 - mutexes not reporting errors
 *    Ian Craggs - fix for bug 420851
 *    Ian Craggs - fix for bug 432903 - queue persistence
 *    Ian Craggs - MQTT 3.1.1 support
 *    Ian Craggs - fix for bug 438176 - MQTT version selection
 *    Rong Xiang, Ian Craggs - C++ compatibility
 *    Ian Craggs - fix for bug 443724 - stack corruption
 *    Ian Craggs - fix for bug 447672 - simultaneous access to socket structure
 *******************************************************************************/
//-同步API执行
/**
 * @file
 * \brief Synchronous API implementation
 *
 */

#define _GNU_SOURCE /* for pthread_mutexattr_settype */
#include <stdlib.h>
#if !defined(WIN32) && !defined(WIN64)
	#include <sys/time.h>
#endif

#include "MQTTClient.h"
#if !defined(NO_PERSISTENCE)
#include "MQTTPersistence.h"
#endif

#include "utf-8.h"
#include "MQTTProtocol.h"
#include "MQTTProtocolOut.h"
#include "Thread.h"
#include "SocketBuffer.h"
#include "StackTrace.h"
#include "Heap.h"

#if defined(OPENSSL)
#include <openssl/ssl.h>
#endif

#define URI_TCP "tcp://"

#define BUILD_TIMESTAMP "##MQTTCLIENT_BUILD_TAG##"
#define CLIENT_VERSION  "##MQTTCLIENT_VERSION_TAG##"

char* client_timestamp_eye = "MQTTClientV3_Timestamp " BUILD_TIMESTAMP;
char* client_version_eye = "MQTTClientV3_Version " CLIENT_VERSION;

static ClientStates ClientState =
{
	CLIENT_VERSION, /* version */
	NULL /* client list */
};

ClientStates* bstate = &ClientState;

MQTTProtocol state;

#if defined(WIN32) || defined(WIN64)
static mutex_type mqttclient_mutex = NULL;
static mutex_type socket_mutex = NULL;
extern mutex_type stack_mutex;
extern mutex_type heap_mutex;
extern mutex_type log_mutex;
BOOL APIENTRY DllMain(HANDLE hModule,
					  DWORD  ul_reason_for_call,
					  LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
			Log(TRACE_MAX, -1, "DLL process attach");
			if (mqttclient_mutex == NULL)
			{
				mqttclient_mutex = CreateMutex(NULL, 0, NULL);
				stack_mutex = CreateMutex(NULL, 0, NULL);
				heap_mutex = CreateMutex(NULL, 0, NULL);
				log_mutex = CreateMutex(NULL, 0, NULL);
				socket_mutex = CreateMutex(NULL, 0, NULL);
			}
		case DLL_THREAD_ATTACH:
			Log(TRACE_MAX, -1, "DLL thread attach");
		case DLL_THREAD_DETACH:
			Log(TRACE_MAX, -1, "DLL thread detach");
		case DLL_PROCESS_DETACH:
			Log(TRACE_MAX, -1, "DLL process detach");
	}
	return TRUE;
}
#else
//-互斥锁是pthread_mutex_t的结构体，而PTHREAD_MUTEX_INITIALIZER这个宏是一个结构常量，
static pthread_mutex_t mqttclient_mutex_store = PTHREAD_MUTEX_INITIALIZER;	//-完成静态的初始化锁,这里完成初始化之后,下面就可以使用这个变量了
static mutex_type mqttclient_mutex = &mqttclient_mutex_store;
static pthread_mutex_t socket_mutex_store = PTHREAD_MUTEX_INITIALIZER;
static mutex_type socket_mutex = &socket_mutex_store;

void MQTTClient_init()	//-客户端初始化
{
	pthread_mutexattr_t attr;
	int rc;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
	if ((rc = pthread_mutex_init(mqttclient_mutex, &attr)) != 0)
		printf("MQTTClient: error %d initializing client_mutex\n", rc);
	if ((rc = pthread_mutex_init(socket_mutex, &attr)) != 0)
		printf("MQTTClient: error %d initializing socket_mutex\n", rc);
}

#define WINAPI
#endif

static volatile int initialized = 0;
static List* handles = NULL;
static time_t last;
static int running = 0;
static int tostop = 0;
static thread_id_type run_id = 0;

MQTTPacket* MQTTClient_waitfor(MQTTClient handle, int packet_type, int* rc, long timeout);
MQTTPacket* MQTTClient_cycle(int* sock, unsigned long timeout, int* rc);
int MQTTClient_cleanSession(Clients* client);
void MQTTClient_stop();
int MQTTClient_disconnect_internal(MQTTClient handle, int timeout);
int MQTTClient_disconnect1(MQTTClient handle, int timeout, int internal, int stop);
void MQTTClient_writeComplete(int socket);

typedef struct
{
	MQTTClient_message* msg;
	char* topicName;
	int topicLen;
	unsigned int seqno; /* only used on restore */
} qEntry;


typedef struct
{
	char* serverURI;		//-网址就可以连接到服务器了
#if defined(OPENSSL)	//-加密选项,这个现在不考虑,等正常使用可以了,再说
	int ssl;
#endif
	Clients* c;
	MQTTClient_connectionLost* cl;
	MQTTClient_messageArrived* ma;
	MQTTClient_deliveryComplete* dc;
	void* context;

	sem_type connect_sem;		//-各色控制流程的信号量
	int rc; /* getsockopt return code in connect */
	sem_type connack_sem;
	sem_type suback_sem;
	sem_type unsuback_sem;
	MQTTPacket* pack;

} MQTTClients;

void MQTTClient_sleep(long milliseconds)
{
	FUNC_ENTRY;	//-这样的一个进入函数,作为一个跟踪,在配置的情况下,会打印出log以便了解程序流程
#if defined(WIN32) || defined(WIN64)
	Sleep(milliseconds);
#else
	usleep(milliseconds*1000);
#endif
	FUNC_EXIT;
}


#if defined(WIN32) || defined(WIN64)
#define START_TIME_TYPE DWORD
START_TIME_TYPE MQTTClient_start_clock(void)
{
	return GetTickCount();
}
#elif defined(AIX)
#define START_TIME_TYPE struct timespec
START_TIME_TYPE MQTTClient_start_clock(void)
{
	static struct timespec start;
	clock_gettime(CLOCK_REALTIME, &start);
	return start;
}
#else
#define START_TIME_TYPE struct timeval
START_TIME_TYPE MQTTClient_start_clock(void)
{
	static struct timeval start;
	gettimeofday(&start, NULL);	//-系统函数,获得当前系统时间,精确到微妙
	return start;
}
#endif


#if defined(WIN32) || defined(WIN64)
long MQTTClient_elapsed(DWORD milliseconds)
{
	return GetTickCount() - milliseconds;
}
#elif defined(AIX)
#define assert(a)
long MQTTClient_elapsed(struct timespec start)
{
	struct timespec now, res;

	clock_gettime(CLOCK_REALTIME, &now);
	ntimersub(now, start, res);
	return (res.tv_sec)*1000L + (res.tv_nsec)/1000000L;
}
#else
long MQTTClient_elapsed(struct timeval start)	//-得到一个时间段,单位是毫秒
{
	struct timeval now, res;

	gettimeofday(&now, NULL);
	timersub(&now, &start, &res);
	return (res.tv_sec)*1000 + (res.tv_usec)/1000;
}
#endif

//-根据信息创建一个客户实体,并用结构体记录身份
int MQTTClient_create(MQTTClient* handle, const char* serverURI, const char* clientId,
		int persistence_type, void* persistence_context)
{
	int rc = 0;
	MQTTClients *m = NULL;	//-这个只是客户端实体中的一个元素

	FUNC_ENTRY;
	rc = Thread_lock_mutex(mqttclient_mutex);	//-这里整个MQTT协议库是针对很多平台的,所以简单问题复杂了,但是这些方法都是值得学习的.
	//-上面对线程进行了上锁
	if (serverURI == NULL || clientId == NULL)	//-首先判断是否有必要的参数
	{
		rc = MQTTCLIENT_NULL_PARAMETER;
		goto exit;
	}

	if (!UTF8_validateString(clientId))	//-判断是否有非法字符存在
	{
		rc = MQTTCLIENT_BAD_UTF8_STRING;
		goto exit;
	}

	if (!initialized)	//-可能是一个程序可以创建几个客户端,但是有部分东西只可以使用一次
	{
		#if defined(HEAP_H)
			Heap_initialize();	//-stack的空间由操作系统自动分配和释放，heap的空间是手动申请和释放的，heap常用new关键字来分配。
		#endif
		Log_initialize((Log_nameValue*)MQTTClient_getVersionInfo());	//-建立自己的打印语句,并安格式输出信息
		bstate->clients = ListInitialize();	//-创建并初始化了一个链表,这里是起点
		Socket_outInitialize();	//-进行了一系列的初始化,重点还是存储空间的
		Socket_setWriteCompleteCallback(MQTTClient_writeComplete);	//-对回调函数赋值,这个减少了程序的耦合性
		handles = ListInitialize();
#if defined(OPENSSL)
		SSLSocket_initialize();
#endif
		initialized = 1;	//-保证仅仅初始化一次
	}
	m = malloc(sizeof(MQTTClients));
	*handle = m;	//-刚刚创建了一个客户端结构对象,用于保存数据
	memset(m, '\0', sizeof(MQTTClients));
	if (strncmp(URI_TCP, serverURI, strlen(URI_TCP)) == 0)	//-若str1与str2的前n个字符相同，则返回0
		serverURI += strlen(URI_TCP);	//-去除特定的前几个字符
#if defined(OPENSSL)
	else if (strncmp(URI_SSL, serverURI, strlen(URI_SSL)) == 0)
	{
		serverURI += strlen(URI_SSL);
		m->ssl = 1;
	}
#endif
	m->serverURI = MQTTStrdup(serverURI);	//-通过参数创建一个客户端,但是程序里面需要使用一定的方法来操作,比如这里就记录了有效的一个参数
	ListAppend(handles, m, sizeof(MQTTClients));	//-在链表中增加一个元素,链表存储的东西很简单,就是一个指针而已
	//-上面在链表中增加了一个元素,仅仅是一个指针,而实际内容是由指针指向的存储单元决定的,下面就实际赋值了
	m->c = malloc(sizeof(Clients));
	memset(m->c, '\0', sizeof(Clients));
	m->c->context = m;
	m->c->outboundMsgs = ListInitialize();	//-靠 结构体里面套结构体好简单啊
	m->c->inboundMsgs = ListInitialize();	//-
	m->c->messageQueue = ListInitialize();
	m->c->clientID = MQTTStrdup(clientId);
	m->connect_sem = Thread_create_sem();	//-弄了这么多的信号量,控制流程的?
	m->connack_sem = Thread_create_sem();
	m->suback_sem = Thread_create_sem();
	m->unsuback_sem = Thread_create_sem();

#if !defined(NO_PERSISTENCE)
	rc = MQTTPersistence_create(&(m->c->persistence), persistence_type, persistence_context);	//-目前系统中没有使用持久空间,那就先不考虑,先来一层一层拨
	if (rc == 0)
	{//-创建持久空间成功,下面开始初始化
		rc = MQTTPersistence_initialize(m->c, m->serverURI);
		if (rc == 0)
			MQTTPersistence_restoreMessageQueue(m->c);
	}
#endif
	ListAppend(bstate->clients, m->c, sizeof(Clients) + 3*sizeof(List));

exit:
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}


void MQTTClient_terminate(void)	//-客户端终止的开始
{
	FUNC_ENTRY;
	MQTTClient_stop();
	if (initialized)
	{
		ListFree(bstate->clients);
		ListFree(handles);
		handles = NULL;
		Socket_outTerminate();
#if defined(OPENSSL)
		SSLSocket_terminate();
#endif
		#if defined(HEAP_H)
			Heap_terminate();
		#endif
		Log_terminate();
		initialized = 0;
	}
	FUNC_EXIT;
}


void MQTTClient_emptyMessageQueue(Clients* client)	//-清空消息队列
{
	FUNC_ENTRY;
	/* empty message queue */
	if (client->messageQueue->count > 0)
	{
		ListElement* current = NULL;
		while (ListNextElement(client->messageQueue, &current))
		{
			qEntry* qe = (qEntry*)(current->content);
			free(qe->topicName);
			free(qe->msg->payload);
			free(qe->msg);
		}
		ListEmpty(client->messageQueue);
	}
	FUNC_EXIT;
}


void MQTTClient_destroy(MQTTClient* handle)	//-程序结束了,对对象的销毁
{
	MQTTClients* m = *handle;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);	//-调用目的就是上锁,如果已经被其他线程锁定了就会阻塞等待其他线程结束.

	if (m == NULL)
		goto exit;

	if (m->c)
	{
		int saved_socket = m->c->net.socket;
		char* saved_clientid = MQTTStrdup(m->c->clientID);
#if !defined(NO_PERSISTENCE)
		MQTTPersistence_close(m->c);
#endif
		MQTTClient_emptyMessageQueue(m->c);
		MQTTProtocol_freeClient(m->c);
		if (!ListRemove(bstate->clients, m->c))
			Log(LOG_ERROR, 0, NULL);
		else
			Log(TRACE_MIN, 1, NULL, saved_clientid, saved_socket);
		free(saved_clientid);
	}
	if (m->serverURI)
		free(m->serverURI);
	Thread_destroy_sem(m->connect_sem);
	Thread_destroy_sem(m->connack_sem);
	Thread_destroy_sem(m->suback_sem);
	Thread_destroy_sem(m->unsuback_sem);
	if (!ListRemove(handles, m))
		Log(LOG_ERROR, -1, "free error");
	*handle = NULL;
	if (bstate->clients->count == 0)
		MQTTClient_terminate();

exit:
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT;
}


void MQTTClient_freeMessage(MQTTClient_message** message)	//-释放消息
{
	FUNC_ENTRY;
	free((*message)->payload);
	free(*message);
	*message = NULL;
	FUNC_EXIT;
}


void MQTTClient_free(void* memory)	//-释放什么
{
	FUNC_ENTRY;
	free(memory);
	FUNC_EXIT;
}


int MQTTClient_deliverMessage(int rc, MQTTClients* m, char** topicName, int* topicLen, MQTTClient_message** message)	//-接收到了消息然后投递消息到需要的地方
{
	qEntry* qe = (qEntry*)(m->c->messageQueue->first->content);

	FUNC_ENTRY;
	*message = qe->msg;
	*topicName = qe->topicName;
	*topicLen = qe->topicLen;
	if (strlen(*topicName) != *topicLen)
		rc = MQTTCLIENT_TOPICNAME_TRUNCATED;	//-一种错误情况,主题名字不对
#if !defined(NO_PERSISTENCE)
	if (m->c->persistence)
		MQTTPersistence_unpersistQueueEntry(m->c, (MQTTPersistence_qEntry*)qe);
#endif
	ListRemove(m->c->messageQueue, m->c->messageQueue->first->content);	//-到这里说明消息已经处理到下一个标志了,这里可以去除了
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * List callback function for comparing clients by socket
 * @param a first integer value
 * @param b second integer value
 * @return boolean indicating whether a and b are equal
 */
int clientSockCompare(void* a, void* b)	//?比较回调函数是否相同
{
	MQTTClients* m = (MQTTClients*)a;
	return m->c->net.socket == *(int*)b;
}


/**
 * Wrapper function to call connection lost on a separate thread.  A separate thread is needed to allow the
 * connectionLost function to make API calls (e.g. connect)
 * @param context a pointer to the relevant client
 * @return thread_return_type standard thread return value - not used here
 */
thread_return_type WINAPI connectionLost_call(void* context)	//?包装函数 指定线程 连接丢失
{
	MQTTClients* m = (MQTTClients*)context;

	(*(m->cl))(m->context, NULL);
	return 0;
}


/* This is the thread function that handles the calling of callback functions if set */
thread_return_type WINAPI MQTTClient_run(void* n)	//-*使用一个专门的线程维护一个客户端的运行
{
	long timeout = 10L; /* first time in we have a small timeout.  Gets things started more quickly */

	FUNC_ENTRY;
	running = 1;
	run_id = Thread_getid();
	//-此线程用于维护客户端的正常运行,比如周期处理接收到的数据内容
	Thread_lock_mutex(mqttclient_mutex);
	while (!tostop)
	{
		int rc = SOCKET_ERROR;
		int sock = -1;
		MQTTClients* m = NULL;
		MQTTPacket* pack = NULL;

		Thread_unlock_mutex(mqttclient_mutex);
		pack = MQTTClient_cycle(&sock, timeout, &rc);
		Thread_lock_mutex(mqttclient_mutex);
		if (tostop)
			break;
		timeout = 1000L;

		/* find client corresponding to socket */
		if (ListFindItem(handles, &sock, clientSockCompare) == NULL)	//-在链表中寻找客户端对应的套接字
		{
			/* assert: should not happen */
			continue;
		}
		m = (MQTTClient)(handles->current->content);
		if (m == NULL)
		{
			/* assert: should not happen */
			continue;
		}
		if (rc == SOCKET_ERROR)
		{
			if (m->c->connected)
			{
				Thread_unlock_mutex(mqttclient_mutex);
				MQTTClient_disconnect_internal(m, 0);
				Thread_lock_mutex(mqttclient_mutex);
			}
			else 
			{
				if (m->c->connect_state == 2 && !Thread_check_sem(m->connect_sem))
				{
					Log(TRACE_MIN, -1, "Posting connect semaphore for client %s", m->c->clientID);
					Thread_post_sem(m->connect_sem);
				}
				if (m->c->connect_state == 3 && !Thread_check_sem(m->connack_sem))
				{
					Log(TRACE_MIN, -1, "Posting connack semaphore for client %s", m->c->clientID);
					Thread_post_sem(m->connack_sem);	//-给信号量加一的目的是什么
				}
			}
		}
		else
		{
			if (m->c->messageQueue->count > 0)
			{
				qEntry* qe = (qEntry*)(m->c->messageQueue->first->content);
				int topicLen = qe->topicLen;

				if (strlen(qe->topicName) == topicLen)
					topicLen = 0;

				Log(TRACE_MIN, -1, "Calling messageArrived for client %s, queue depth %d",
					m->c->clientID, m->c->messageQueue->count);
				Thread_unlock_mutex(mqttclient_mutex);
				rc = (*(m->ma))(m->context, qe->topicName, topicLen, qe->msg);
				Thread_lock_mutex(mqttclient_mutex);
				/* if 0 (false) is returned by the callback then it failed, so we don't remove the message from
				 * the queue, and it will be retried later.  If 1 is returned then the message data may have been freed,
				 * so we must be careful how we use it.
				 */
				if (rc)
					ListRemove(m->c->messageQueue, qe);
				else
					Log(TRACE_MIN, -1, "False returned from messageArrived for client %s, message remains on queue",
						m->c->clientID);
			}
			if (pack)
			{
				if (pack->header.bits.type == CONNACK && !Thread_check_sem(m->connack_sem))
				{
					Log(TRACE_MIN, -1, "Posting connack semaphore for client %s", m->c->clientID);	//-这里就是实现日志打印,如果没有不影响功能(即使没有配置正确)
					m->pack = pack;
					Thread_post_sem(m->connack_sem);
				}
				else if (pack->header.bits.type == SUBACK)
				{
					Log(TRACE_MIN, -1, "Posting suback semaphore for client %s", m->c->clientID);
					m->pack = pack;
					Thread_post_sem(m->suback_sem);
				}
				else if (pack->header.bits.type == UNSUBACK)
				{
					Log(TRACE_MIN, -1, "Posting unsuback semaphore for client %s", m->c->clientID);
					m->pack = pack;
					Thread_post_sem(m->unsuback_sem);	//-通过控制信号量,决定别的进程的标志
				}
			}
			else if (m->c->connect_state == 1 && !Thread_check_sem(m->connect_sem))
			{
				int error;
				socklen_t len = sizeof(error);

				if ((m->rc = getsockopt(m->c->net.socket, SOL_SOCKET, SO_ERROR, (char*)&error, &len)) == 0)
					m->rc = error;
				Log(TRACE_MIN, -1, "Posting connect semaphore for client %s rc %d", m->c->clientID, m->rc);
				Thread_post_sem(m->connect_sem);
			}
#if defined(OPENSSL)
			else if (m->c->connect_state == 2 && !Thread_check_sem(m->connect_sem))
			{			
				rc = SSLSocket_connect(m->c->net.ssl, m->c->net.socket);
				if (rc == 1 || rc == SSL_FATAL)
				{
					if (rc == 1 && !m->c->cleansession && m->c->session == NULL)
						m->c->session = SSL_get1_session(m->c->net.ssl);
					m->rc = rc;
					Log(TRACE_MIN, -1, "Posting connect semaphore for SSL client %s rc %d", m->c->clientID, m->rc);
					Thread_post_sem(m->connect_sem);
				}
			}
#endif
		}
	}
	run_id = 0;
	running = tostop = 0;
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT;
	return 0;
}


void MQTTClient_stop()	//-停止客户端
{
	int rc = 0;

	FUNC_ENTRY;
	if (running == 1 && tostop == 0)
	{
		int conn_count = 0;
		ListElement* current = NULL;

		if (handles != NULL)
		{
			/* find out how many handles are still connected */
			while (ListNextElement(handles, &current))
			{
				if (((MQTTClients*)(current->content))->c->connect_state > 0 ||
						((MQTTClients*)(current->content))->c->connected)
					++conn_count;
			}
		}
		Log(TRACE_MIN, -1, "Conn_count is %d", conn_count);
		/* stop the background thread, if we are the last one to be using it */
		if (conn_count == 0)
		{
			int count = 0;
			tostop = 1;
			if (Thread_getid() != run_id)
			{
				while (running && ++count < 100)
				{
					Thread_unlock_mutex(mqttclient_mutex);
					Log(TRACE_MIN, -1, "sleeping");
					MQTTClient_sleep(100L);
					Thread_lock_mutex(mqttclient_mutex);
				}
			}
			rc = 1;
		}
	}
	FUNC_EXIT_RC(rc);
}

//-设置回调,
int MQTTClient_setCallbacks(MQTTClient handle, void* context, MQTTClient_connectionLost* cl,
														MQTTClient_messageArrived* ma, MQTTClient_deliveryComplete* dc)
{
	int rc = MQTTCLIENT_SUCCESS;
	MQTTClients* m = handle;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);

	if (m == NULL || ma == NULL || m->c->connect_state != 0)
		rc = MQTTCLIENT_FAILURE;
	else
	{
		m->context = context;
		m->cl = cl;
		m->ma = ma;
		m->dc = dc;
	}

	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}


void MQTTClient_closeSession(Clients* client)	//-关闭会话
{
	FUNC_ENTRY;
	client->good = 0;
	client->ping_outstanding = 0;	//-对会话进行关闭,这里就需要把标识位恢复到初始值
	if (client->net.socket > 0)
	{//-如果客户端有套接字的存在的话,就需要断开
		if (client->connected)
			MQTTPacket_send_disconnect(&client->net, client->clientID);	//-一个客户端会可能建立几个套接字,和服务器连接
		Thread_lock_mutex(socket_mutex);
#if defined(OPENSSL)
		SSLSocket_close(&client->net);
#endif
		Socket_close(client->net.socket);
		Thread_unlock_mutex(socket_mutex);
		client->net.socket = 0;
#if defined(OPENSSL)
		client->net.ssl = NULL;
#endif
	}
	client->connected = 0;
	client->connect_state = 0;

	if (client->cleansession)	//-如果客户端连接时使用了clean session标志，那么这个客户端之前所维护的信息将会被丢弃。
		MQTTClient_cleanSession(client);
	FUNC_EXIT;
}


int MQTTClient_cleanSession(Clients* client)	//-清除会话
{
	int rc = 0;

	FUNC_ENTRY;
#if !defined(NO_PERSISTENCE)
	rc = MQTTPersistence_clear(client);
#endif
	MQTTProtocol_emptyMessageList(client->inboundMsgs);
	MQTTProtocol_emptyMessageList(client->outboundMsgs);
	MQTTClient_emptyMessageQueue(client);
	client->msgID = 0;
	FUNC_EXIT_RC(rc);
	return rc;
}


void Protocol_processPublication(Publish* publish, Clients* client)	//-处理接收到的消息,这个是最多发一次的消息
{
	qEntry* qe = NULL;
	MQTTClient_message* mm = NULL;

	FUNC_ENTRY;
	qe = malloc(sizeof(qEntry));
	mm = malloc(sizeof(MQTTClient_message));

	qe->msg = mm;

	qe->topicName = publish->topic;
	qe->topicLen = publish->topiclen;
	publish->topic = NULL;

	/* If the message is QoS 2, then we have already stored the incoming payload
	 * in an allocated buffer, so we don't need to copy again.
	 */
	if (publish->header.bits.qos == 2)
		mm->payload = publish->payload;
	else
	{
		mm->payload = malloc(publish->payloadlen);
		memcpy(mm->payload, publish->payload, publish->payloadlen);
	}

	mm->payloadlen = publish->payloadlen;
	mm->qos = publish->header.bits.qos;
	mm->retained = publish->header.bits.retain;
	if (publish->header.bits.qos == 2)
		mm->dup = 0;  /* ensure that a QoS2 message is not passed to the application with dup = 1 */
	else
		mm->dup = publish->header.bits.dup;
	mm->msgid = publish->msgId;

	ListAppend(client->messageQueue, qe, sizeof(qe) + sizeof(mm) + mm->payloadlen + strlen(qe->topicName)+1);	//-接收到的消息存储在开辟的新空间,但是仍然通过链表记忆结构
#if !defined(NO_PERSISTENCE)
	if (client->persistence)
		MQTTPersistence_persistQueueEntry(client, (MQTTPersistence_qEntry*)qe);	//-把接收到的信息存储到持久文件中
#endif
	FUNC_EXIT;
}

//-下面针对校验过的参数进行连接
int MQTTClient_connectURIVersion(MQTTClient handle, MQTTClient_connectOptions* options, const char* serverURI, int MQTTVersion,
	START_TIME_TYPE start, long millisecsTimeout)
{
	MQTTClients* m = handle;
	int rc = SOCKET_ERROR;
	int sessionPresent = 0;

	FUNC_ENTRY;
	if (m->ma && !running)	//-一个程序只能运行一次下面的周期函数
	{//-如果没有设回调函数,都没有必要启动这个线程
		Thread_start(MQTTClient_run, handle);	//-这里创建了一个脱离线程,然后就去执行MQTTClient_run了
		if (MQTTClient_elapsed(start) >= millisecsTimeout)	//-得到经过的时长
		{//-超时表示连接出错
			rc = SOCKET_ERROR;
			goto exit;
		}
		MQTTClient_sleep(100L);	//-使用系统函数进行了休眠,但是自己定义了一个转接函数
	}

	Log(TRACE_MIN, -1, "Connecting to serverURI %s with MQTT version %d", serverURI, MQTTVersion);
#if defined(OPENSSL)
	rc = MQTTProtocol_connect(serverURI, m->c, m->ssl, MQTTVersion);
#else
	rc = MQTTProtocol_connect(serverURI, m->c, MQTTVersion);	//-主线程继续下去
#endif
	if (rc == SOCKET_ERROR)
		goto exit;
	//-上面的步骤进行了TCP/IP连接,但是不一定就完成了,也可能完成了并发出了协议层连接,或者下面还要继续等待连接或协议层连接完成
	if (m->c->connect_state == 0)	//-到了这里连接状态必须变了,否则就是错误
	{
		rc = SOCKET_ERROR;
		goto exit;
	}
	//-上面的连接没有使用阻塞等待,下面使用了
	if (m->c->connect_state == 1) /* TCP connect started - wait for completion */
	{
		Thread_unlock_mutex(mqttclient_mutex);
		MQTTClient_waitfor(handle, CONNECT, &rc, millisecsTimeout - MQTTClient_elapsed(start));	//-延时等待CONNECT帧,将返回结果
		Thread_lock_mutex(mqttclient_mutex);
		if (rc != 0)
		{
			rc = SOCKET_ERROR;
			goto exit;
		}
		
#if defined(OPENSSL)
		if (m->ssl)
		{
			if (SSLSocket_setSocketForSSL(&m->c->net, m->c->sslopts) != MQTTCLIENT_SUCCESS)
			{
				if (m->c->session != NULL)
					if ((rc = SSL_set_session(m->c->net.ssl, m->c->session)) != 1)
						Log(TRACE_MIN, -1, "Failed to set SSL session with stored data, non critical");
				rc = SSLSocket_connect(m->c->net.ssl, m->c->net.socket);
				if (rc == TCPSOCKET_INTERRUPTED)
					m->c->connect_state = 2;  /* the connect is still in progress */
				else if (rc == SSL_FATAL)
				{
					rc = SOCKET_ERROR;
					goto exit;
				}
				else if (rc == 1) 
				{
					rc = MQTTCLIENT_SUCCESS;
					m->c->connect_state = 3;
					if (MQTTPacket_send_connect(m->c, MQTTVersion) == SOCKET_ERROR)
					{
						rc = SOCKET_ERROR;
						goto exit;
					}
					if (!m->c->cleansession && m->c->session == NULL)
						m->c->session = SSL_get1_session(m->c->net.ssl);
				}
			}
			else
			{
				rc = SOCKET_ERROR;
				goto exit;
			}
		}
		else
		{
#endif
			m->c->connect_state = 3; /* TCP connect completed, in which case send the MQTT connect packet */
			if (MQTTPacket_send_connect(m->c, MQTTVersion) == SOCKET_ERROR)	//-进行MQTT协议层的连接
			{
				rc = SOCKET_ERROR;
				goto exit;
			}
#if defined(OPENSSL)
		}
#endif
	}
	
#if defined(OPENSSL)
	if (m->c->connect_state == 2) /* SSL connect sent - wait for completion */
	{
		Thread_unlock_mutex(mqttclient_mutex);
		MQTTClient_waitfor(handle, CONNECT, &rc, millisecsTimeout - MQTTClient_elapsed(start));	//-里面是阻塞方式
		Thread_lock_mutex(mqttclient_mutex);
		if (rc != 1)
		{
			rc = SOCKET_ERROR;
			goto exit;
		}
		if(!m->c->cleansession && m->c->session == NULL)
			m->c->session = SSL_get1_session(m->c->net.ssl);
		m->c->connect_state = 3; /* TCP connect completed, in which case send the MQTT connect packet */
		if (MQTTPacket_send_connect(m->c, MQTTVersion) == SOCKET_ERROR)
		{
			rc = SOCKET_ERROR;
			goto exit;
		}
	}
#endif

	if (m->c->connect_state == 3) /* MQTT connect sent - wait for CONNACK */
	{//-说明请求连接名义已经发送出去了,下面需要等待应答
		MQTTPacket* pack = NULL;

		Thread_unlock_mutex(mqttclient_mutex);
		pack = MQTTClient_waitfor(handle, CONNACK, &rc, millisecsTimeout - MQTTClient_elapsed(start));	//-在一定时间内等待应答
		Thread_lock_mutex(mqttclient_mutex);
		if (pack == NULL)
			rc = SOCKET_ERROR;
		else
		{
			Connack* connack = (Connack*)pack;
			Log(TRACE_PROTOCOL, 1, NULL, m->c->net.socket, m->c->clientID, connack->rc);
			if ((rc = connack->rc) == MQTTCLIENT_SUCCESS)
			{//-到这里说明MQTT协议层的连接已经成功了
				m->c->connected = 1;	//-MQTT成功的进行了连接
				m->c->good = 1;
				m->c->connect_state = 0;	//?收到应答之后变为0
				if (MQTTVersion == 4)
					sessionPresent = connack->flags.bits.sessionPresent;
				if (m->c->cleansession)
					rc = MQTTClient_cleanSession(m->c);
				if (m->c->outboundMsgs->count > 0)
				{
					ListElement* outcurrent = NULL;

					while (ListNextElement(m->c->outboundMsgs, &outcurrent))
					{
						Messages* m = (Messages*)(outcurrent->content);
						m->lastTouch = 0;
					}
					MQTTProtocol_retry((time_t)0, 1, 1);
					if (m->c->connected != 1)	//-判断MQTT是否被连接
						rc = MQTTCLIENT_DISCONNECTED;
				}
			}
			free(connack);
			m->pack = NULL;
		}
	}
exit:
	if (rc == MQTTCLIENT_SUCCESS)
	{
		if (options->struct_version == 4) /* means we have to fill out return values */
		{//-如果满足了一切要求了,就再写入下面内容			
			options->returned.serverURI = serverURI;
			options->returned.MQTTVersion = MQTTVersion;    
			options->returned.sessionPresent = sessionPresent;
		}
	}
	else
	{
		Thread_unlock_mutex(mqttclient_mutex);
		MQTTClient_disconnect1(handle, 0, 0, (MQTTVersion == 3)); /* not "internal" because we don't want to call connection lost */
		Thread_lock_mutex(mqttclient_mutex);
	}
	FUNC_EXIT_RC(rc);
  return rc;
}

//-这个是一个轻量级的协议,但是人家写的很模块化,所以刚开始的时候容易绕晕,但是一旦看好之后,如果需要修改的
//-话将显得比较方便,如果是移植性的大动作就更有优势了,所以需要学习,但是要掌握方法,否则很难
int MQTTClient_connectURI(MQTTClient handle, MQTTClient_connectOptions* options, const char* serverURI)	//-一个客户端针对一个网址进行连接,使用给定的参数
{
	MQTTClients* m = handle;	//-这里是值得思考的,这样做的目的
	START_TIME_TYPE start;
	long millisecsTimeout = 30000L;
	int rc = SOCKET_ERROR;
	int MQTTVersion = 0;

	FUNC_ENTRY;
	millisecsTimeout = options->connectTimeout * 1000;
	//-因为链接可能是阻塞的,所以需要超时判断
	start = MQTTClient_start_clock();	//-对系统函数进行了封装,其实不封装也行,但是这样操作后就分层了
	//-对指定的一个客户端进行赋值,对传递进来的参数和客户端绑定
	m->c->keepAliveInterval = options->keepAliveInterval;	//-把选项参数填写到结构体中,其实就是一个格式的转化,也是提供了分层
	m->c->cleansession = options->cleansession;
	m->c->maxInflightMessages = (options->reliable) ? 1 : 10;

	if (m->c->will)
	{//-刚开始如果有内容的话就需要释放这里
		free(m->c->will->msg);
		free(m->c->will->topic);
		free(m->c->will);
		m->c->will = NULL;
	}

	if (options->will && options->will->struct_version == 0)	//?大量的信息不能理解意思和用途
	{//-开始填写数据
		m->c->will = malloc(sizeof(willMessages));
		m->c->will->msg = MQTTStrdup(options->will->message);
		m->c->will->qos = options->will->qos;
		m->c->will->retained = options->will->retained;
		m->c->will->topic = MQTTStrdup(options->will->topicName);
	}
	
#if defined(OPENSSL)
	if (m->c->sslopts)
	{
		if (m->c->sslopts->trustStore)
			free((void*)m->c->sslopts->trustStore);
		if (m->c->sslopts->keyStore)
			free((void*)m->c->sslopts->keyStore);
		if (m->c->sslopts->privateKey)
			free((void*)m->c->sslopts->privateKey);
		if (m->c->sslopts->privateKeyPassword)
			free((void*)m->c->sslopts->privateKeyPassword);
		if (m->c->sslopts->enabledCipherSuites)
			free((void*)m->c->sslopts->enabledCipherSuites);
		free(m->c->sslopts);
		m->c->sslopts = NULL;
	}

	if (options->struct_version != 0 && options->ssl)
	{
		m->c->sslopts = malloc(sizeof(MQTTClient_SSLOptions));
		memset(m->c->sslopts, '\0', sizeof(MQTTClient_SSLOptions));
		if (options->ssl->trustStore)
			m->c->sslopts->trustStore = MQTTStrdup(options->ssl->trustStore);
		if (options->ssl->keyStore)
			m->c->sslopts->keyStore = MQTTStrdup(options->ssl->keyStore);
		if (options->ssl->privateKey)
			m->c->sslopts->privateKey = MQTTStrdup(options->ssl->privateKey);
		if (options->ssl->privateKeyPassword)
			m->c->sslopts->privateKeyPassword = MQTTStrdup(options->ssl->privateKeyPassword);
		if (options->ssl->enabledCipherSuites)
			m->c->sslopts->enabledCipherSuites = MQTTStrdup(options->ssl->enabledCipherSuites);
		m->c->sslopts->enableServerCertAuth = options->ssl->enableServerCertAuth;
	}
#endif

	m->c->username = options->username;	//-把各色各样的参数存储到客户端信息表中
	m->c->password = options->password;
	m->c->retryInterval = options->retryInterval;

	if (options->struct_version >= 3)
		MQTTVersion = options->MQTTVersion;
	else
		MQTTVersion = MQTTVERSION_DEFAULT;

	if (MQTTVersion == MQTTVERSION_DEFAULT)	//-不同的协议栈版本连接是有差异的
	{
		if ((rc = MQTTClient_connectURIVersion(handle, options, serverURI, 4, start, millisecsTimeout)) != MQTTCLIENT_SUCCESS)
			rc = MQTTClient_connectURIVersion(handle, options, serverURI, 3, start, millisecsTimeout);
	}
	else
		rc = MQTTClient_connectURIVersion(handle, options, serverURI, MQTTVersion, start, millisecsTimeout);	//-完成了连接,使用了阻塞等待

	FUNC_EXIT_RC(rc);
	return rc;
}

//-这里的参数传递可以学习下,没有指定类型,而是由内部临时变量指定的
int MQTTClient_connect(MQTTClient handle, MQTTClient_connectOptions* options)	//-连接请求：客户端请求连接到服务器；
{
	MQTTClients* m = handle;
	int rc = SOCKET_ERROR;	//-这里赋给的初值也是有讲究的,是一个编程思路

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);	//-目前这一套可以先不管,其实没有逻辑思路在里面,就是一种安全保证,系统特有的

	if (options == NULL)
	{
		rc = MQTTCLIENT_NULL_PARAMETER;
		goto exit;
	}

	if (strncmp(options->struct_id, "MQTC", 4) != 0 || 
		(options->struct_version != 0 && options->struct_version != 1 && options->struct_version != 2
			&& options->struct_version != 3 && options->struct_version != 4))
	{
		rc = MQTTCLIENT_BAD_STRUCTURE;
		goto exit;
	}

	if (options->will) /* check validity of will options structure */
	{//-当服务器与客户端通信时遇到I/O错误或客户端没有在Keep Alive期内进行通讯时，Server会代表客户端发布一个Will消息。
		if (strncmp(options->will->struct_id, "MQTW", 4) != 0 || options->will->struct_version != 0)
		{
			rc = MQTTCLIENT_BAD_STRUCTURE;
			goto exit;
		}
	}//-用于监控客户端与服务器之间的连接状况,这个需要在连接的时候说明,并不是所有的都需要的
	
#if defined(OPENSSL)
	if (options->struct_version != 0 && options->ssl) /* check validity of SSL options structure */
	{
		if (strncmp(options->ssl->struct_id, "MQTS", 4) != 0 || options->ssl->struct_version != 0)
		{
			rc = MQTTCLIENT_BAD_STRUCTURE;
			goto exit;
		}
	}
#endif

	if ((options->username && !UTF8_validateString(options->username)) ||
		(options->password && !UTF8_validateString(options->password)))
	{
		rc = MQTTCLIENT_BAD_UTF8_STRING;
		goto exit;
	}
	//-下面根据协议栈版本号判断使用程序
	if (options->struct_version < 2 || options->serverURIcount == 0)
		rc = MQTTClient_connectURI(handle, options, m->serverURI);	//-前面就一系列的参数进行了校验,如果校验通过,这里进入链接
	else
	{
		int i;
		
		for (i = 0; i < options->serverURIcount; ++i)	//-判断有几个服务器地址等待连接,然后一个一个连
		{//-同一个客户端实现从几个服务器上接收相同的主题
			char* serverURI = options->serverURIs[i];
			
			if (strncmp(URI_TCP, serverURI, strlen(URI_TCP)) == 0)	//-去掉不需要的头
				serverURI += strlen(URI_TCP);
#if defined(OPENSSL)
			else if (strncmp(URI_SSL, serverURI, strlen(URI_SSL)) == 0)
			{
				serverURI += strlen(URI_SSL);
				m->ssl = 1;
			}
#endif
			if ((rc = MQTTClient_connectURI(handle, options, serverURI)) == MQTTCLIENT_SUCCESS)	//-连接服务器(通过网址)
				break;
		}	
	}

exit:
	if (m->c->will)
	{//-感觉这个是告诉服务器的,告诉好了本地就没有必要保留了,所以释放空间
		free(m->c->will);
		m->c->will = NULL;
	}
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTClient_disconnect1(MQTTClient handle, int timeout, int internal, int stop)	//-断开连接可能除了硬件上的还有协议上的还有程序变量上的
{
	MQTTClients* m = handle;
	START_TIME_TYPE start;
	int rc = MQTTCLIENT_SUCCESS;
	int was_connected = 0;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);

	if (m == NULL || m->c == NULL)
	{
		rc = MQTTCLIENT_FAILURE;
		goto exit;
	}
	if (m->c->connected == 0 && m->c->connect_state == 0)	//-一个个根据客户端的标志进行判断
	{
		rc = MQTTCLIENT_DISCONNECTED;
		goto exit;
	}
	was_connected = m->c->connected; /* should be 1 */
	if (m->c->connected != 0)
	{
		start = MQTTClient_start_clock();
		m->c->connect_state = -2; /* indicate disconnecting */
		while (m->c->inboundMsgs->count > 0 || m->c->outboundMsgs->count > 0)
		{ /* wait for all inflight message flows to finish, up to timeout */
			if (MQTTClient_elapsed(start) >= timeout)	//-在一段时间内保证东西发送出去
				break;
			Thread_unlock_mutex(mqttclient_mutex);
			MQTTClient_yield();
			Thread_lock_mutex(mqttclient_mutex);
		}
	}

	MQTTClient_closeSession(m->c);

	while (Thread_check_sem(m->connect_sem))	//-延迟足够的时间,让别的程序处理
		Thread_wait_sem(m->connect_sem, 100);
	while (Thread_check_sem(m->connack_sem))
		Thread_wait_sem(m->connack_sem, 100);
	while (Thread_check_sem(m->suback_sem))
		Thread_wait_sem(m->suback_sem, 100);
	while (Thread_check_sem(m->unsuback_sem))
		Thread_wait_sem(m->unsuback_sem, 100);
exit:
	if (stop)
		MQTTClient_stop();
	if (internal && m->cl && was_connected)
	{
		Log(TRACE_MIN, -1, "Calling connectionLost for client %s", m->c->clientID);
		Thread_start(connectionLost_call, m);	//-断开连接了还需要一个线程维护?
	}
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTClient_disconnect_internal(MQTTClient handle, int timeout)	//-断开连接 内部
{
	return MQTTClient_disconnect1(handle, timeout, 1, 1);
}


void MQTTProtocol_closeSession(Clients* c, int sendwill)	//-关闭会话
{
	MQTTClient_disconnect_internal((MQTTClient)c->context, 0);
}


int MQTTClient_disconnect(MQTTClient handle, int timeout)	//-断开和服务器的连接
{
	return MQTTClient_disconnect1(handle, timeout, 0, 1);
}


int MQTTClient_isConnected(MQTTClient handle)	//-连接请求：客户端请求连接到服务器；
{
	MQTTClients* m = handle;
	int rc = 0;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);
	if (m && m->c)
		rc = m->c->connected;
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTClient_subscribeMany(MQTTClient handle, int count, char* const* topic, int* qos)	//-一次性订阅几个主题
{
	MQTTClients* m = handle;
	List* topics = ListInitialize();	//-建立了一个链表,可以向里面增加成员了
	List* qoss = ListInitialize();
	int i = 0;
	int rc = MQTTCLIENT_FAILURE;
	int msgid = 0;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);

	if (m == NULL || m->c == NULL)
	{
		rc = MQTTCLIENT_FAILURE;
		goto exit;
	}
	if (m->c->connected == 0)	//-表示客户端没有连接,所以后面根本就不需要发送订阅
	{
		rc = MQTTCLIENT_DISCONNECTED;
		goto exit;
	}
	for (i = 0; i < count; i++)	//-针对待订阅的主题进行检查,看看有没有出错的
	{
		if (!UTF8_validateString(topic[i]))
		{
			rc = MQTTCLIENT_BAD_UTF8_STRING;
			goto exit;
		}
		
		if(qos[i] < 0 || qos[i] > 2)
		{
			rc = MQTTCLIENT_BAD_QOS;
			goto exit;
		}
	}
	if ((msgid = MQTTProtocol_assignMsgId(m->c)) == 0)
	{
		rc = MQTTCLIENT_MAX_MESSAGES_INFLIGHT;
		goto exit;
	}

	for (i = 0; i < count; i++)	//-把需要的内容增加到链表中进行存储
	{
		ListAppend(topics, topic[i], strlen(topic[i]));
		ListAppend(qoss, &qos[i], sizeof(int));
	}

	rc = MQTTProtocol_subscribe(m->c, topics, qoss, msgid);	//-进入发送出订阅报文
	ListFreeNoContent(topics);	//-对建立的链表使用完了,整个都释放了
	ListFreeNoContent(qoss);
	//-上面不一定都发送出去了,所以下面也不是唯一处理的地方
	if (rc == TCPSOCKET_COMPLETE)
	{
		MQTTPacket* pack = NULL;

		Thread_unlock_mutex(mqttclient_mutex);
		pack = MQTTClient_waitfor(handle, SUBACK, &rc, 10000L);	//-前面发送了订阅请求,这里就阻塞等待应答报文
		Thread_lock_mutex(mqttclient_mutex);
		if (pack != NULL)
		{//-不等说明我们需要的东西已经等待到了,可以进行下一步处理了
			Suback* sub = (Suback*)pack;	
			ListElement* current = NULL;
			i = 0;
			while (ListNextElement(sub->qoss, &current))	//-在链表中查找元素
			{//-由发布者负责决定消息可被传递的最大QoS级别，但订阅者能够降低QoS的级别到一个更适合它使用的QoS级别。
				int* reqqos = (int*)(current->content);
				qos[i++] = *reqqos;
			}	
			rc = MQTTProtocol_handleSubacks(pack, m->c->net.socket);	//-上面等到了需要的消息,这里就是需要的处理
			m->pack = NULL;
		}
		else
			rc = SOCKET_ERROR;
	}

	if (rc == SOCKET_ERROR)
	{
		Thread_unlock_mutex(mqttclient_mutex);
		MQTTClient_disconnect_internal(handle, 0);	//-一个东西上出现了错误,那么就需要主动断开连接
		Thread_lock_mutex(mqttclient_mutex);
	}
	else if (rc == TCPSOCKET_COMPLETE)
		rc = MQTTCLIENT_SUCCESS;

exit:
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTClient_subscribe(MQTTClient handle, const char* topic, int qos)	//-订阅
{
	int rc = 0;
	char *const topics[] = {(char*)topic};

	FUNC_ENTRY;
	rc = MQTTClient_subscribeMany(handle, 1, topics, &qos);
	if (qos == MQTT_BAD_SUBSCRIBE) /* addition for MQTT 3.1.1 - error code from subscribe */
		rc = MQTT_BAD_SUBSCRIBE;
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTClient_unsubscribeMany(MQTTClient handle, int count, char* const* topic)	//-一次性取消几个订阅
{
	MQTTClients* m = handle;
	List* topics = ListInitialize();
	int i = 0;
	int rc = SOCKET_ERROR;
	int msgid = 0;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);

	if (m == NULL || m->c == NULL)
	{
		rc = MQTTCLIENT_FAILURE;
		goto exit;
	}
	if (m->c->connected == 0)
	{
		rc = MQTTCLIENT_DISCONNECTED;
		goto exit;
	}
	for (i = 0; i < count; i++)
	{
		if (!UTF8_validateString(topic[i]))
		{
			rc = MQTTCLIENT_BAD_UTF8_STRING;
			goto exit;
		}
	}
	if ((msgid = MQTTProtocol_assignMsgId(m->c)) == 0)
	{
		rc = MQTTCLIENT_MAX_MESSAGES_INFLIGHT;
		goto exit;
	}
	//-上面的代码都是重复的,没有什么新意
	for (i = 0; i < count; i++)
		ListAppend(topics, topic[i], strlen(topic[i]));
	rc = MQTTProtocol_unsubscribe(m->c, topics, msgid);	//-没有什么难的,如果会了其实都是这些重复性的东西,而且如果程序写的好的话,更是
	ListFreeNoContent(topics);

	if (rc == TCPSOCKET_COMPLETE)
	{
		MQTTPacket* pack = NULL;

		Thread_unlock_mutex(mqttclient_mutex);
		pack = MQTTClient_waitfor(handle, UNSUBACK, &rc, 10000L);	//-这个层次先放一放,后面深究
		Thread_lock_mutex(mqttclient_mutex);
		if (pack != NULL)
		{
			rc = MQTTProtocol_handleUnsubacks(pack, m->c->net.socket);
			m->pack = NULL;
		}
		else
			rc = SOCKET_ERROR;
	}

	if (rc == SOCKET_ERROR)
	{
		Thread_unlock_mutex(mqttclient_mutex);
		MQTTClient_disconnect_internal(handle, 0);
		Thread_lock_mutex(mqttclient_mutex);
	}

exit:
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTClient_unsubscribe(MQTTClient handle, const char* topic)	//-主动取消订阅
{
	int rc = 0;
	char *const topics[] = {(char*)topic};
	FUNC_ENTRY;
	rc = MQTTClient_unsubscribeMany(handle, 1, topics);
	FUNC_EXIT_RC(rc);
	return rc;
}

//-发布,到这里的处理已经经过很多校验了
int MQTTClient_publish(MQTTClient handle, const char* topicName, int payloadlen, void* payload,
							 int qos, int retained, MQTTClient_deliveryToken* deliveryToken)
{
	int rc = MQTTCLIENT_SUCCESS;
	MQTTClients* m = handle;
	Messages* msg = NULL;
	Publish* p = NULL;
	int blocked = 0;
	int msgid = 0;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);

	if (m == NULL || m->c == NULL)
		rc = MQTTCLIENT_FAILURE;
	else if (m->c->connected == 0)	//-根据标识位判断是否可以发送,并不是你要发就去发的,还要看有没有这个能力
		rc = MQTTCLIENT_DISCONNECTED;
	else if (!UTF8_validateString(topicName))
		rc = MQTTCLIENT_BAD_UTF8_STRING;
	if (rc != MQTTCLIENT_SUCCESS)
		goto exit;

	/* If outbound queue is full, block until it is not */
	while (m->c->outboundMsgs->count >= m->c->maxInflightMessages || 
         Socket_noPendingWrites(m->c->net.socket) == 0) /* wait until the socket is free of large packets being written */
	{//-进入说明需要阻塞,现在不适合输出
		if (blocked == 0)
		{
			blocked = 1;
			Log(TRACE_MIN, -1, "Blocking publish on queue full for client %s", m->c->clientID);
		}
		Thread_unlock_mutex(mqttclient_mutex);
		MQTTClient_yield();	//-阻塞还需要不停的处理收发,万一是单线程的,否则永远不会空闲
		Thread_lock_mutex(mqttclient_mutex);
		if (m->c->connected == 0)	//-如果都没有连接,就没有必要在这阻塞了
		{
			rc = MQTTCLIENT_FAILURE;
			goto exit;
		}
	}
	if (blocked == 1)
		Log(TRACE_MIN, -1, "Resuming publish now queue not full for client %s", m->c->clientID);
	if (qos > 0 && (msgid = MQTTProtocol_assignMsgId(m->c)) == 0)	//-如果qos > 0就需要分配一个ID号,在里面找一个空的就行
	{	/* this should never happen as we've waited for spaces in the queue */
		rc = MQTTCLIENT_MAX_MESSAGES_INFLIGHT;
		goto exit;
	}
	//-上面等待到了空位,下面开始填写
	p = malloc(sizeof(Publish));

	p->payload = payload;
	p->payloadlen = payloadlen;
	p->topic = (char*)topicName;
	p->msgId = msgid;

	rc = MQTTProtocol_startPublish(m->c, p, qos, retained, &msg);

	/* If the packet was partially written to the socket, wait for it to complete.
	 * However, if the client is disconnected during this time and qos is not 0, still return success, as
	 * the packet has already been written to persistence and assigned a message id so will
	 * be sent when the client next connects.
	 */
	if (rc == TCPSOCKET_INTERRUPTED)
	{//-想法很简单,如果没有发送出去就优先循环查找,而不等周期函数,提高速度
		while (m->c->connected == 1 && SocketBuffer_getWrite(m->c->net.socket))
		{
			Thread_unlock_mutex(mqttclient_mutex);
			MQTTClient_yield();
			Thread_lock_mutex(mqttclient_mutex);
		}
		rc = (qos > 0 || m->c->connected == 1) ? MQTTCLIENT_SUCCESS : MQTTCLIENT_FAILURE;
	}

	if (deliveryToken && qos > 0)
		*deliveryToken = msg->msgid;

	free(p);	//-如果是申请的空间就需要释放,但是如果是临时缓冲区就会随着程序的退出自动释放

	if (rc == SOCKET_ERROR)
	{
		Thread_unlock_mutex(mqttclient_mutex);
		MQTTClient_disconnect_internal(handle, 0);	//-如果出错了就需要主动断开连接
		Thread_lock_mutex(mqttclient_mutex);
		/* Return success for qos > 0 as the send will be retried automatically */
		rc = (qos > 0) ? MQTTCLIENT_SUCCESS : MQTTCLIENT_FAILURE;
	}

exit:
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}


//-发布信息
int MQTTClient_publishMessage(MQTTClient handle, const char* topicName, MQTTClient_message* message,
															 MQTTClient_deliveryToken* deliveryToken)
{
	int rc = MQTTCLIENT_SUCCESS;

	FUNC_ENTRY;
	if (message == NULL)
	{
		rc = MQTTCLIENT_NULL_PARAMETER;
		goto exit;
	}

	if (strncmp(message->struct_id, "MQTM", 4) != 0 || message->struct_version != 0)
	{//-一道一道的校验
		rc = MQTTCLIENT_BAD_STRUCTURE;
		goto exit;
	}

	rc = MQTTClient_publish(handle, topicName, message->payloadlen, message->payload,
								message->qos, message->retained, deliveryToken);
exit:
	FUNC_EXIT_RC(rc);
	return rc;
}

/*
Keep Alive timer位于MQTT CONNECT消息的可变报文头（variable header）中。
Keep Alive timer为秒级, 定义了客户端接收消息时消息之间的最大时间间隔。 这样，服务器无需等
待很长的TCP/IP超时时间即可发现与客户端的连接已经断了。
客户端有义务在每一个Keep Alive time内发生一个消息. 在这个时间周期内，如果没有业务数据相关
（data-related）的消息，客户端会发一个PINGREQ，相应的，服务器会返回一个PINGRESP消息进行确
认。（类似心跳机制）
如果服务器在一个半（1.5）的Keep Alive时间周期内没有收到来自客户端的消息，就会断开与客户
端的连接，就像客户端发送了一个DISCONNECT消息一样。 断开连接不会影响这个客户端的任何订阅。
如果客户端在发送了PINGREQ消息后的一个Keep Alive周期内没有收到PINGRESP消息应答，应该关
闭TCP/IP socket连接。
*/
void MQTTClient_retry(void)	//-再次尝试
{
	time_t now;

	FUNC_ENTRY;
	time(&(now));
	if (difftime(now, last) > 5)	//-最小每5S钟进入一次判断,如果大了就需要发送一个ping请求帧
	{
		time(&(last));
		MQTTProtocol_keepalive(now);	//-针对活跃进行判断,如果需要的话发送一个PINGREQ
		MQTTProtocol_retry(now, 1, 0);
	}
	else
		MQTTProtocol_retry(now, 0, 0);
	FUNC_EXIT;
}

//-参数1:目前所有情况都是外部不用,内部查询后决定判断的
//-参数2:有超时就有值,没有超时设0
//-参数3:是一个标志位外部传入一个,内部修改供外部识别
MQTTPacket* MQTTClient_cycle(int* sock, unsigned long timeout, int* rc)	//-周期处理,里面有实现对套接字的查询
{
	struct timeval tp = {0L, 0L};
	static Ack ack;
	MQTTPacket* pack = NULL;	//-这个数据结构指向了MQTT帧的头部

	FUNC_ENTRY;
	if (timeout > 0L)
	{
		tp.tv_sec = timeout / 1000;
		tp.tv_usec = (timeout % 1000) * 1000; /* this field is microseconds! */
	}

#if defined(OPENSSL)
	if ((*sock = SSLSocket_getPendingRead()) == -1)	//-目前没有使用OPENSSL加密选项,所以后面的*sock肯定不满足条件
	{
		/* 0 from getReadySocket indicates no work to do, -1 == error, but can happen normally */
#endif
		Thread_lock_mutex(socket_mutex);
		*sock = Socket_getReadySocket(0, &tp);	//-返回准备读的套接字,里面首先处理下写出的套接字,这就是他的思路
		Thread_unlock_mutex(socket_mutex);
#if defined(OPENSSL)
	}
#endif
	Thread_lock_mutex(mqttclient_mutex);
	if (*sock > 0)
	{//-如果有套接字准好读了,下面就进行读操作
		MQTTClients* m = NULL;
		if (ListFindItem(handles, sock, clientSockCompare) != NULL)
			m = (MQTTClient)(handles->current->content);
		if (m != NULL)
		{
			if (m->c->connect_state == 1 || m->c->connect_state == 2)
				*rc = 0;  /* waiting for connect state to clear */
			else
			{
				pack = MQTTPacket_Factory(&m->c->net, rc);	//-周期性的检查网络有没有内容接收到
				if (*rc == TCPSOCKET_INTERRUPTED)
					*rc = 0;
			}
		}
		if (pack)	//-如果寻找到有效头部就开始分析报文
		{
			int freed = 1;

			/* Note that these handle... functions free the packet structure that they are dealing with */
			if (pack->header.bits.type == PUBLISH)	//-这个区段代表消息类型的意思,现在总共定义了14种,,发布消息
				*rc = MQTTProtocol_handlePublishes(pack, *sock);	//-确保发布被收到
			else if (pack->header.bits.type == PUBACK || pack->header.bits.type == PUBCOMP)	//-发布确认	发布完成
			{
				int msgid;

				ack = (pack->header.bits.type == PUBCOMP) ? *(Pubcomp*)pack : *(Puback*)pack;
				msgid = ack.msgId;
				*rc = (pack->header.bits.type == PUBCOMP) ?
						MQTTProtocol_handlePubcomps(pack, *sock) : MQTTProtocol_handlePubacks(pack, *sock);	//-确保发布完成
				if (m && m->dc)
				{
					Log(TRACE_MIN, -1, "Calling deliveryComplete for client %s, msgid %d", m->c->clientID, msgid);
					(*(m->dc))(m->context, msgid);
				}
			}
			else if (pack->header.bits.type == PUBREC)	//-发布消息收到
				*rc = MQTTProtocol_handlePubrecs(pack, *sock);
			else if (pack->header.bits.type == PUBREL)	//-发布消息分发
				*rc = MQTTProtocol_handlePubrels(pack, *sock);
			else if (pack->header.bits.type == PINGRESP)	//-ping响应
				*rc = MQTTProtocol_handlePingresps(pack, *sock);	//-这里就完成了一个最简单的交互和服务器之间
			else
				freed = 0;
			if (freed)
				pack = NULL;
		}
	}
	MQTTClient_retry();
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(*rc);
	return pack;
}

//-返回的是不是我们等待的东西将有反馈
MQTTPacket* MQTTClient_waitfor(MQTTClient handle, int packet_type, int* rc, long timeout)	//-客户端等待直到时间到了,正常情况下没有任何实际处理
{
	MQTTPacket* pack = NULL;
	MQTTClients* m = handle;
	START_TIME_TYPE start = MQTTClient_start_clock();		//-为了超时准备的,说明这里面有阻塞

	FUNC_ENTRY;
	if (((MQTTClients*)handle) == NULL)
	{
		*rc = MQTTCLIENT_FAILURE;
		goto exit;
	}

	if (running)	//-这里说明这个和周期函数MQTTClient_run是有关系的,周期函数可以不运行,如果不运行的话,这里可以实现它的功能
	{//-说明周期处理线程正在处理中,下面阻塞等待就可以,有超时机制在
		if (packet_type == CONNECT)	//-等待的类型
		{
			if ((*rc = Thread_wait_sem(m->connect_sem, timeout)) == 0)
				*rc = m->rc;
		}
		else if (packet_type == CONNACK)
			*rc = Thread_wait_sem(m->connack_sem, timeout);	//-等待一个信号量的发送,或者超时
		else if (packet_type == SUBACK)	//-等待接收到的报文类型
			*rc = Thread_wait_sem(m->suback_sem, timeout);	//-等待信号量,如果等不到超时退出
		else if (packet_type == UNSUBACK)
			*rc = Thread_wait_sem(m->unsuback_sem, timeout);	//-里面没有做任何实质性的东西就是延时等待
		if (*rc == 0 && packet_type != CONNECT && m->pack == NULL)
			Log(LOG_ERROR, -1, "waitfor unexpectedly is NULL for client %s, packet_type %d, timeout %ld", m->c->clientID, packet_type, timeout);
		pack = m->pack;
	}
	else
	{//-到这里说明周期线程没有启动,那么下面实现周期函数里面的功能
		*rc = TCPSOCKET_COMPLETE;
		while (1)
		{
			int sock = -1;
			pack = MQTTClient_cycle(&sock, 100L, rc);
			if (sock == m->c->net.socket)
			{
				if (*rc == SOCKET_ERROR)
					break;
				if (pack && (pack->header.bits.type == packet_type))
					break;	//-到这里说明寻找到了,满足条件了就结束等待
				if (m->c->connect_state == 1)
				{
					int error;
					socklen_t len = sizeof(error);

					if ((*rc = getsockopt(m->c->net.socket, SOL_SOCKET, SO_ERROR, (char*)&error, &len)) == 0)	//-获取任意类型、任意状态套接口的选项当前值，并把结果存入optval
						*rc = error;
					break;
				}
#if defined(OPENSSL)
				else if (m->c->connect_state == 2)
				{
					*rc = SSLSocket_connect(m->c->net.ssl, sock);
					if (*rc == SSL_FATAL)
						break;
					else if (*rc == 1) /* rc == 1 means SSL connect has finished and succeeded */
					{
						if (!m->c->cleansession && m->c->session == NULL)
							m->c->session = SSL_get1_session(m->c->net.ssl);
						break;
					}
				}
#endif
				else if (m->c->connect_state == 3)
				{
					int error;
					socklen_t len = sizeof(error);

					if (getsockopt(m->c->net.socket, SOL_SOCKET, SO_ERROR, (char*)&error, &len) == 0)	//?发生错误就退出不发生继续等待,直到超时
					{
						if (error)
						{
							*rc = error;
							break;
						}
					}
				}
			}
			if (MQTTClient_elapsed(start) > timeout)	//-超时结束等待
			{
				pack = NULL;
				break;
			}
		}
	}

exit:
	FUNC_EXIT_RC(*rc);
	return pack;
}

//-客户端接收
int MQTTClient_receive(MQTTClient handle, char** topicName, int* topicLen, MQTTClient_message** message,
											 unsigned long timeout)
{
	int rc = TCPSOCKET_COMPLETE;
	START_TIME_TYPE start = MQTTClient_start_clock();	//-这些东西编程都没有什么意思了
	unsigned long elapsed = 0L;
	MQTTClients* m = handle;

	FUNC_ENTRY;
	if (m == NULL || m->c == NULL)
	{
		rc = MQTTCLIENT_FAILURE;	//-没有这样的客户端存在,接收必然失败
		goto exit;
	}
	if (m->c->connected == 0)
	{
		rc = MQTTCLIENT_DISCONNECTED;
		goto exit;
	}

	*topicName = NULL;
	*message = NULL;

	/* if there is already a message waiting, don't hang around but still do some packet handling */
	if (m->c->messageQueue->count > 0)	//-说明已经有消息被接收到了,等待处理
		timeout = 0L;

	elapsed = MQTTClient_elapsed(start);
	do
	{
		int sock = 0;
		MQTTClient_cycle(&sock, (timeout > elapsed) ? timeout - elapsed : 0L, &rc);	//-接收没有什么特殊的就是周期性的读写套接字,然后根据结果判断
		
		if (rc == SOCKET_ERROR)
		{
			if (ListFindItem(handles, &sock, clientSockCompare) && 	/* find client corresponding to socket */
			  (MQTTClient)(handles->current->content) == handle)
				break; /* there was an error on the socket we are interested in */
		}
		elapsed = MQTTClient_elapsed(start);
	}
	while (elapsed < timeout && m->c->messageQueue->count == 0);

	if (m->c->messageQueue->count > 0)
		rc = MQTTClient_deliverMessage(rc, m, topicName, topicLen, message);

	if (rc == SOCKET_ERROR)
		MQTTClient_disconnect_internal(handle, 0);

exit:
	FUNC_EXIT_RC(rc);
	return rc;
}

//-这个程序是为了单线程准备的,里面有周期性的消息接收和发送,如果启动了一个周期处理线程
//-这个程序就不是必要的.
void MQTTClient_yield(void)	//-客户端放弃,里面有周期处理函数,可以实现发送和接收还有处理
{
	START_TIME_TYPE start = MQTTClient_start_clock();
	unsigned long elapsed = 0L;
	unsigned long timeout = 100L;
	int rc = 0;

	FUNC_ENTRY;
	if (running)
	{
		MQTTClient_sleep(timeout);	//?正在运行的话,休眠等待结束
		goto exit;
	}

	elapsed = MQTTClient_elapsed(start);
	do
	{
		int sock = -1;
		MQTTClient_cycle(&sock, (timeout > elapsed) ? timeout - elapsed : 0L, &rc);	//-这里的周期处理是保证通讯的流结束,而没有没发送出去的内容
		if (rc == SOCKET_ERROR && ListFindItem(handles, &sock, clientSockCompare))
		{
			MQTTClients* m = (MQTTClient)(handles->current->content);
			if (m->c->connect_state != -2)
				MQTTClient_disconnect_internal(m, 0);
		}
		elapsed = MQTTClient_elapsed(start);	//-获取了经过的时间
	}
	while (elapsed < timeout);	//-超时退出
exit:
	FUNC_EXIT;
}


int pubCompare(void* a, void* b)
{
	Messages* msg = (Messages*)a;
	return msg->publish == (Publications*)b;
}

//-这个里面没有任何特殊的就是周期性的查询输出队列中某个帧是否还存在,在规定的时间内没有了就说明成功
//-发送了
int MQTTClient_waitForCompletion(MQTTClient handle, MQTTClient_deliveryToken mdt, unsigned long timeout)	//-等待完成
{
	int rc = MQTTCLIENT_FAILURE;
	START_TIME_TYPE start = MQTTClient_start_clock();
	unsigned long elapsed = 0L;
	MQTTClients* m = handle;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);

	if (m == NULL || m->c == NULL)
	{
		rc = MQTTCLIENT_FAILURE;
		goto exit;
	}
	if (m->c->connected == 0)	//-只有处于连接状态才可以发送的
	{
		rc = MQTTCLIENT_DISCONNECTED;
		goto exit;
	}

	if (ListFindItem(m->c->outboundMsgs, &mdt, messageIDCompare) == NULL)	//-在输出缓冲区中需要,如果没有了说明成功了
	{
		rc = MQTTCLIENT_SUCCESS; /* well we couldn't find it */
		goto exit;
	}

	elapsed = MQTTClient_elapsed(start);
	while (elapsed < timeout)
	{
		Thread_unlock_mutex(mqttclient_mutex);
		MQTTClient_yield();	//-里面有周期函数,循环等待直到结束
		Thread_lock_mutex(mqttclient_mutex);
		if (ListFindItem(m->c->outboundMsgs, &mdt, messageIDCompare) == NULL)
		{
			rc = MQTTCLIENT_SUCCESS; /* well we couldn't find it */
			goto exit;
		}
		elapsed = MQTTClient_elapsed(start);
	}

exit:
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTClient_getPendingDeliveryTokens(MQTTClient handle, MQTTClient_deliveryToken **tokens)	//-得到挂起的传递符号
{
	int rc = MQTTCLIENT_SUCCESS;
	MQTTClients* m = handle;
	*tokens = NULL;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);

	if (m == NULL)
	{
		rc = MQTTCLIENT_FAILURE;
		goto exit;
	}

	if (m->c && m->c->outboundMsgs->count > 0)
	{
		ListElement* current = NULL;
		int count = 0;

		*tokens = malloc(sizeof(MQTTClient_deliveryToken) * (m->c->outboundMsgs->count + 1));
		/*Heap_unlink(__FILE__, __LINE__, *tokens);*/
		while (ListNextElement(m->c->outboundMsgs, &current))
		{
			Messages* m = (Messages*)(current->content);
			(*tokens)[count++] = m->msgid;
		}
		(*tokens)[count] = -1;
	}

exit:
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}

MQTTClient_nameValue* MQTTClient_getVersionInfo()	//-获得版本号
{
	#define MAX_INFO_STRINGS 8
	static MQTTClient_nameValue libinfo[MAX_INFO_STRINGS + 1];	//-这里作为静态变量,以后是可以使用的?
	int i = 0;

	libinfo[i].name = "Product name";
	libinfo[i++].value = "Paho Synchronous MQTT C Client Library";

	libinfo[i].name = "Version";
	libinfo[i++].value = CLIENT_VERSION;

	libinfo[i].name = "Build level";
	libinfo[i++].value = BUILD_TIMESTAMP;
#if defined(OPENSSL)
	libinfo[i].name = "OpenSSL version";
	libinfo[i++].value = SSLeay_version(SSLEAY_VERSION);

	libinfo[i].name = "OpenSSL flags";
	libinfo[i++].value = SSLeay_version(SSLEAY_CFLAGS);

	libinfo[i].name = "OpenSSL build timestamp";
	libinfo[i++].value = SSLeay_version(SSLEAY_BUILT_ON);

	libinfo[i].name = "OpenSSL platform";
	libinfo[i++].value = SSLeay_version(SSLEAY_PLATFORM);

	libinfo[i].name = "OpenSSL directory";
	libinfo[i++].value = SSLeay_version(SSLEAY_DIR);
#endif
	libinfo[i].name = NULL;	//-结束标志
	libinfo[i].value = NULL;
	return libinfo;
}


/**
 * See if any pending writes have been completed, and cleanup if so.
 * Cleaning up means removing any publication data that was stored because the write did
 * not originally complete.
 */
void MQTTProtocol_checkPendingWrites()	//-检查挂起的写是否被完成,如果完成了就清除,检查是通过查看悬挂链表里面是否还存在的情况
{
	FUNC_ENTRY;
	if (state.pending_writes.count > 0)
	{//-说明有内容正在写,下面就判断结束了没有
		ListElement* le = state.pending_writes.first;
		while (le)
		{
			if (Socket_noPendingWrites(((pending_write*)(le->content))->socket))
			{
				MQTTProtocol_removePublication(((pending_write*)(le->content))->p);
				state.pending_writes.current = le;
				ListRemove(&(state.pending_writes), le->content); /* does NextElement itself */
				le = state.pending_writes.current;
			}
			else
				ListNextElement(&(state.pending_writes), &le);
		}
	}
	FUNC_EXIT;
}


void MQTTClient_writeComplete(int socket)	//-整个协议信息的存储可能需要一套机制来管理				
{
	ListElement* found = NULL;
	
	FUNC_ENTRY;
	/* a partial write is now complete for a socket - this will be on a publish*/
	
	MQTTProtocol_checkPendingWrites();
	
	/* find the client using this socket */
	if ((found = ListFindItem(handles, &socket, clientSockCompare)) != NULL)
	{
		MQTTClients* m = (MQTTClients*)(found->content);
		
		time(&(m->c->net.lastSent));
	}
	FUNC_EXIT;
}
