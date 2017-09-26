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
 *    Ian Craggs - initial implementation and documentation
 *    Ian Craggs - async client updates
 *******************************************************************************/

/**
 * @file
 * \brief Socket related functions
 *
 * Some other related functions are in the SocketBuffer module
 */


#include "Socket.h"
#include "Log.h"
#include "SocketBuffer.h"
#include "Messages.h"
#include "StackTrace.h"
#if defined(OPENSSL)
#include "SSLSocket.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>

#include "Heap.h"

int Socket_close_only(int socket);
int Socket_continueWrites(fd_set* pwset);

#if defined(WIN32) || defined(WIN64)
#define iov_len len
#define iov_base buf
#endif

/**
 * Structure to hold all socket data for the module
 */
Sockets s;
static fd_set wset;

/**
 * Set a socket non-blocking, OS independently
 * @param sock the socket to set non-blocking
 * @return TCP call error code
 */
int Socket_setnonblocking(int sock)	//-�����ļ�������
{
	int rc;
#if defined(WIN32) || defined(WIN64)
	u_long flag = 1L;

	FUNC_ENTRY;
	rc = ioctl(sock, FIONBIO, &flag);
#else
	int flags;

	FUNC_ENTRY;
	if ((flags = fcntl(sock, F_GETFL, 0)))	//-fcntl()���������ļ���������һЩ���ԡ�F_GETFL ȡ���ļ�������״̬��꣬�����Ϊopen�����Ĳ���flags��
		flags = 0;
	rc = fcntl(sock, F_SETFL, flags | O_NONBLOCK);	//-F_SETFL �����ļ�������״̬��꣬����argΪ����꣬��ֻ����O_APPEND��O_NONBLOCK��O_ASYNCλ�ĸı䣬����λ�ĸı佫����Ӱ�졣
#endif
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Gets the specific error corresponding to SOCKET_ERROR
 * @param aString the function that was being used when the error occurred
 * @param sock the socket on which the error occurred
 * @return the specific TCP error code
 */
int Socket_error(char* aString, int sock)	//-�����Ӧ�����׽��ֵĴ�������
{
#if defined(WIN32) || defined(WIN64)
	int errno;
#endif

	FUNC_ENTRY;
#if defined(WIN32) || defined(WIN64)
	errno = WSAGetLastError();
#endif
	if (errno != EINTR && errno != EAGAIN && errno != EINPROGRESS && errno != EWOULDBLOCK)	//-���󱨸�,
	{//-���д����붼��������,ͨ����Щ����֪��ʲô����
		if (strcmp(aString, "shutdown") != 0 || (errno != ENOTCONN && errno != ECONNRESET))
			Log(TRACE_MINIMUM, -1, "Socket error %s in %s for socket %d", strerror(errno), aString, sock);
	}
	FUNC_EXIT_RC(errno);
	return errno;
}


/**
 * Initialize the socket module
 */
void Socket_outInitialize()	//-��ʼ���׽���ģ��
{
#if defined(WIN32) || defined(WIN64)
	WORD    winsockVer = 0x0202;
	WSADATA wsd;

	FUNC_ENTRY;
	WSAStartup(winsockVer, &wsd);
#else
	FUNC_ENTRY;
	signal(SIGPIPE, SIG_IGN);	//-����һ���򵥵����,���Ƕ�������˵�����ܶ�ѧϰ��֪ʶ����
#endif
	//-��linux��дsocket�ĳ����ʱ���������send��һ��disconnected socket�ϣ��ͻ��õײ��׳�һ��SIGPIPE�źš�
	//-����źŵ�ȱʡ���������˳����̣������ʱ���ⶼ�������������ġ����������Ҫ��������źŵĴ�������
	//-��������ʵ���������Ĺ���,
	//-SIG_IGN(�����ź�),���źŵĽ������߳�û��Ӱ��  ��
	SocketBuffer_initialize();	//-�����˺ö��¶���
	s.clientsds = ListInitialize();	//-�����Ǵ洢���ݵ�һ����ʽ,�������ʹ����
	s.connect_pending = ListInitialize();
	s.write_pending = ListInitialize();
	s.cur_clientsds = NULL;
	FD_ZERO(&(s.rset));														/* Initialize the descriptor set */
	FD_ZERO(&(s.pending_wset));	//-��ָ�����ļ�����������գ��ڶ��ļ����������Ͻ�������ǰ�����������г�ʼ�����������գ�������ϵͳ�����ڴ�ռ��ͨ����������մ������Խ���ǲ���֪�ġ�
	s.maxfdp1 = 0;	//-��ָ�����������ļ��������ķ�Χ���������ļ������������ֵ��1
	memcpy((void*)&(s.rset_saved), (void*)&(s.rset), sizeof(s.rset_saved));
	FUNC_EXIT;
}


/**
 * Terminate the socket module
 */
void Socket_outTerminate()
{
	FUNC_ENTRY;
	ListFree(s.connect_pending);
	ListFree(s.write_pending);
	ListFree(s.clientsds);	//-�ͷ�����ͷָ��
	SocketBuffer_terminate();
#if defined(WIN32) || defined(WIN64)
	WSACleanup();
#endif
	FUNC_EXIT;
}


/**
 * Add a socket to the list of socket to check with select
 * @param newSd the new socket to add
 */
int Socket_addSocket(int newSd)	//-�׽�����һ����λ,һ���׽����ֶ�Ӧ��һ���б�
{
	int rc = 0;

	FUNC_ENTRY;
	if (ListFindItem(s.clientsds, &newSd, intcompare) == NULL) /* make sure we don't add the same socket twice */
	{//-���û�����ӹ�����׽���,����ͽ��������б����
		int* pnewSd = (int*)malloc(sizeof(newSd));	//-�ȿ���һ���ڴ�ռ�
		*pnewSd = newSd;
		ListAppend(s.clientsds, pnewSd, sizeof(newSd));	//-���б�������һ����Ŀ
		FD_SET(newSd, &(s.rset_saved));	//-��һ���������ļ����������뼯��֮��
		s.maxfdp1 = max(s.maxfdp1, newSd + 1);	//-���ش����ֵ
		rc = Socket_setnonblocking(newSd);
	}
	else
		Log(LOG_ERROR, -1, "addSocket: socket %d already in the list", newSd);

	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Don't accept work from a client unless it is accepting work back, i.e. its socket is writeable
 * this seems like a reasonable form of flow control, and practically, seems to work.
 * @param socket the socket to check
 * @param read_set the socket read set (see select doc)
 * @param write_set the socket write set (see select doc)
 * @return boolean - is the socket ready to go?
 */
int isReady(int socket, fd_set* read_set, fd_set* write_set)	//-�ж�socket�Ƿ���ָ���Ŀɲ���������,,����1˵��׼����д��
{
	int rc = 1;

	FUNC_ENTRY;
	if  (ListFindItem(s.connect_pending, &socket, intcompare) && FD_ISSET(socket, write_set))	//-�ж�������fd�Ƿ��ڸ�������������fdset�У�ͨ�����select����ʹ�ã�����⵽fd״̬�����仯ʱ�����棬���򣬷��ؼ٣�Ҳ������Ϊ������ָ�����ļ��������Ƿ���Զ�д����
		ListRemoveItem(s.connect_pending, &socket, intcompare);	//-����׽����Ͽ���д��,˵�����ڽ������������ݴ��������,���Դ����Ҷ����������
	else
		rc = FD_ISSET(socket, read_set) && FD_ISSET(socket, write_set) && Socket_noPendingWrites(socket);
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 *  Returns the next socket ready for communications as indicated by select
 *  @param more_work flag to indicate more work is waiting, and thus a timeout value of 0 should
 *  be used for the select
 *  @param tp the timeout to be used for the select, unless overridden
 *  @return the socket next ready, or 0 if none is ready
 */
int Socket_getReadySocket(int more_work, struct timeval *tp)	//-����еĻ����ؿ��Բ������׽���������,ÿ����������˳�����
{
	int rc = 0;
	static struct timeval zero = {0L, 0L}; /* 0 seconds */
	static struct timeval one = {1L, 0L}; /* 1 second */
	struct timeval timeout = one;

	FUNC_ENTRY;
	if (s.clientsds->count == 0)	//-�ж����׽��������е��׽�������������
		goto exit;	//-���û��˵��û���׽���,��ô�Ͳ����еȴ���

	if (more_work)	//-ѡ���ʱ�䲻ͬ
		timeout = zero;
	else if (tp)
		timeout = *tp;

	while (s.cur_clientsds != NULL)	//-ָ��ǰ���׽���������,��¼����һ������Ԫ��,ͨ��������Զ�λ�������е�һ����,Ȼ������ҵ�����
	{
		if (isReady(*((int*)(s.cur_clientsds->content)), &(s.rset), &wset))	//-�����׽�����׼���ÿ��Բ�����,���ڴ��ڿ���״̬
			break;
		ListNextElement(s.clientsds, &s.cur_clientsds);
	}	//-Ѱ�ҵ�׼���õ�Ԫ��������в���

	if (s.cur_clientsds == NULL)
	{//-������˵��û���ҵ����ʵ��׽���,�����ǵ�һ�β���
		int rc1;
		fd_set pwset;

		memcpy((void*)&(s.rset), (void*)&(s.rset_saved), sizeof(s.rset));	//-����Ķ��׽��ּ���
		memcpy((void*)&(pwset), (void*)&(s.pending_wset), sizeof(pwset));	//-���������д�׽��ּ���
		//-select�ܹ�����������Ҫ���ӵ��ļ��������ı仯���������д�����쳣��
		if ((rc = select(s.maxfdp1, &(s.rset), &pwset, NULL, &timeout)) == SOCKET_ERROR)	//-��·�������׽���,ȷ���׽��ֵ�״̬
		{//-���һ�������е��׽��ֵ�״̬,���������FD_SET����,����ʵ�ַ�������ʽ
			Socket_error("read select", 0);
			goto exit;
		}
		//-��ֵ��select����
		//-��ֵ��ĳЩ�ļ��ɶ�д�����
		//-0���ȴ���ʱ��û�пɶ�д�������ļ�
		Log(TRACE_MAX, -1, "Return code %d from read select", rc);	//-����״̬�����仯������������
		//-����û��ʵ�ʲ��������ǶԸ���Ȥ�Ľ��м��,����б�Ҫ�ͽ��е����ٴ���
		if (Socket_continueWrites(&pwset) == SOCKET_ERROR)	//-�����Եļ����������,����еĻ���д��ȥ
		{
			rc = 0;
			goto exit;
		}

		memcpy((void*)&wset, (void*)&(s.rset_saved), sizeof(wset));	//-�������׽��ֵĿ�д��,
		//-����1:��һ������ֵ����ָ�����������ļ��������ķ�Χ���������ļ������������ֵ��1
		//-����2:����ѡ��ָ�룬ָ��һ��ȴ��ɶ��Լ����׽ӿڡ�
		//-����3:����ѡ��ָ�룬ָ��һ��ȴ���д�Լ����׽ӿ�.
		//-����4:����ѡ��ָ�룬ָ��һ��ȴ���������׽ӿڡ�
		//-����5:select()���ȴ�ʱ�䣬������������ΪNULL������ʱ��ֵ��Ϊ0��0���룬�ͱ��һ������ķ���������
		if ((rc1 = select(s.maxfdp1, NULL, &(wset), NULL, &zero)) == SOCKET_ERROR)	//-��һ���׽�������в�����Ҫ���Ӽ�,Ȼ������ǲ���
		{//-���select������һ�����ӵ�����,���ӵ���,�������Ҫʵ�ʲ�����
			Socket_error("write select", 0);
			rc = rc1;
			goto exit;
		}
		Log(TRACE_MAX, -1, "Return code %d from write select", rc1);

		if (rc == 0 && rc1 == 0)
			goto exit; /* no work to do */
		//-������˵���пɶ����׽���,�����ͷ�ҵ���һ�ɶ��ķ���
		s.cur_clientsds = s.clientsds->first;
		while (s.cur_clientsds != NULL)
		{
			int cursock = *((int*)(s.cur_clientsds->content));
			if (isReady(cursock, &(s.rset), &wset))	//-���ط�0��,˵������׽��ֿ��Բ�����,׼������
				break;
			ListNextElement(s.clientsds, &s.cur_clientsds);
		}
	}

	if (s.cur_clientsds == NULL)
		rc = 0;
	else
	{
		rc = *((int*)(s.cur_clientsds->content));
		ListNextElement(s.clientsds, &s.cur_clientsds);	//-ǰ��һ��������һ������,����һ��������һ��Ԫ��,����ֱ��ָ������ŵ���һ��Ԫ��
	}
exit:
	FUNC_EXIT_RC(rc);
	return rc;
} /* end getReadySocket */


/**
 *  Reads one byte from a socket
 *  @param socket the socket to read from
 *  @param c the character read, returned
 *  @return completion code
 */
int Socket_getch(int socket, char* c)	//-�������е����ݶ���Ҫ�Զ��еĲ���,Ȼ���ٸ�Ӧ�ó���ʹ��,������ֱ�������׽�����
{
	int rc = SOCKET_ERROR;

	FUNC_ENTRY;
	if ((rc = SocketBuffer_getQueuedChar(socket, c)) != SOCKETBUFFER_INTERRUPTED)
		goto exit;

	if ((rc = recv(socket, c, (size_t)1, 0)) == SOCKET_ERROR)	//-����׽�������,����Ѿ�����ײ������
	{
		int err = Socket_error("recv - getch", socket);
		if (err == EWOULDBLOCK || err == EAGAIN)
		{
			rc = TCPSOCKET_INTERRUPTED;
			SocketBuffer_interrupted(socket, 0);
		}
	}
	else if (rc == 0)
		rc = SOCKET_ERROR; 	/* The return value from recv is 0 when the peer has performed an orderly shutdown. */
	else if (rc == 1)
	{
		SocketBuffer_queueChar(socket, *c);
		rc = TCPSOCKET_COMPLETE;
	}
exit:
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 *  Attempts to read a number of bytes from a socket, non-blocking. If a previous read did not
 *  finish, then retrieve that data.
 *  @param socket the socket to read from
 *  @param bytes the number of bytes to read
 *  @param actual_len the actual number of bytes read
 *  @return completion code
 */
char *Socket_getdata(int socket, int bytes, int* actual_len)	//-���׽����л��һЩ����
{
	int rc;
	char* buf;

	FUNC_ENTRY;
	if (bytes == 0)
	{
		buf = SocketBuffer_complete(socket);	//?����׽��ֻ�����û���������������
		goto exit;
	}

	buf = SocketBuffer_getQueuedData(socket, bytes, actual_len);
	//-����ĺ������Ǵӽ��ջ�������������,����Ӧ�����ڶ�����Ѱ�ҿռ�
	//-recv����������copy���ݣ������Ľ���������Э������ɵ�
	//-recv����������ʵ��copy���ֽ���
	//-��һ������ָ�����ն��׽�����������
	//-�ڶ�������ָ��һ�����������û������������recv�������յ������ݣ�
	//-����������ָ��buf�ĳ��ȣ�
	//-���ĸ�����һ����0��
	if ((rc = recv(socket, buf + (*actual_len), (size_t)(bytes - (*actual_len)), 0)) == SOCKET_ERROR)	//-*�õ��׽�������
	{//-��������
		rc = Socket_error("recv - getdata", socket);
		if (rc != EAGAIN && rc != EWOULDBLOCK)
		{
			buf = NULL;
			goto exit;
		}
	}
	else if (rc == 0) /* rc 0 means the other end closed the socket, albeit "gracefully" */
	{//-��������ֹ
		buf = NULL;
		goto exit;
	}
	else
		*actual_len += rc;	//-������ֽ���

	if (*actual_len == bytes)
		SocketBuffer_complete(socket);	//-�������ɿ��ܾ��Ƿ��һ��������,˵��һ֡����������,���Կ�ʼ������
	else /* we didn't read the whole packet */
	{
		SocketBuffer_interrupted(socket, *actual_len);	//-Ӧ���ǶԱ�־��һ�ִ���,����ʹ�û������ܱ�ʶ���ڲ�ͬ��״̬��
		Log(TRACE_MAX, -1, "%d bytes expected but %d bytes now received", bytes, *actual_len);
	}
exit:
	FUNC_EXIT;
	return buf;
}


/**
 *  Indicate whether any data is pending outbound for a socket.
 *  @return boolean - true == data pending.
 */
int Socket_noPendingWrites(int socket)	//-��д���������м������׽������Ƿ�������û�з��ͳ�ȥ
{
	int cursock = socket;
	return ListFindItem(s.write_pending, &cursock, intcompare) == NULL;	//-������������ҵ�,��ô����1
}


/**
 *  Attempts to write a series of iovec buffers to a socket in *one* system call so that
 *  they are sent as one packet.
 *  @param socket the socket to write to
 *  @param iovecs an array of buffers to write
 *  @param count number of buffers in iovecs
 *  @param bytes number of bytes actually written returned
 *  @return completion code, especially TCPSOCKET_INTERRUPTED
 */
int Socket_writev(int socket, iobuf* iovecs, int count, unsigned long* bytes)	//-����û��ʲô����Ľ����Կ⺯���ı�ʶλ�������жϴ���
{
	int rc;

	FUNC_ENTRY;
#if defined(WIN32) || defined(WIN64)
	rc = WSASend(socket, iovecs, count, (LPDWORD)bytes, 0, NULL, NULL);
	if (rc == SOCKET_ERROR)
	{
		int err = Socket_error("WSASend - putdatas", socket);
		if (err == EWOULDBLOCK || err == EAGAIN)
			rc = TCPSOCKET_INTERRUPTED;
	}
#else
	*bytes = 0L;
	//-readv��writev����������һ�κ��������ж���д�������������������ʱҲ��������������Ϊɢ������scatter read���;ۼ�д��gather write����
	rc = writev(socket, iovecs, count);	//-�����������������ݵķ���,,���ش����ֽ���������ʱ����-1.
	if (rc == SOCKET_ERROR)
	{
		int err = Socket_error("writev - putdatas", socket);
		if (err == EWOULDBLOCK || err == EAGAIN)	//-���ڷ�����ģʽ������Ҫ���¶�����д,ֻ����ʱû�����
			rc = TCPSOCKET_INTERRUPTED;	//-��ʾ�������ж�,��û�н���,��Ҫ�ȴ�
	}
	else
		*bytes = rc;	//-���ɹ��򷵻��Ѷ���д���ֽ������������򷵻�-1�� 
#endif
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 *  Attempts to write a series of buffers to a socket in *one* system call so that they are
 *  sent as one packet.
 *  @param socket the socket to write to
 *  @param buf0 the first buffer
 *  @param buf0len the length of data in the first buffer
 *  @param count number of buffers
 *  @param buffers an array of buffers to write
 *  @param buflens an array of corresponding buffer lengths
 *  @return completion code, especially TCPSOCKET_INTERRUPTED
 */
int Socket_putdatas(int socket, char* buf0, size_t buf0len, int count, char** buffers, size_t* buflens, int* frees)	//-�������ͳ�ȥ,���ǲ�����ȫ���ܷ��͵�,û�е�д�뻺����
{//-����дһϵ�еĻ�������һ���׽������������Ǳ���Ϊһ��֡���ͳ�ȥ
	unsigned long bytes = 0L;
	iobuf iovecs[5];
	int frees1[5];
	int rc = TCPSOCKET_INTERRUPTED, i, total = buf0len;

	FUNC_ENTRY;
	if (!Socket_noPendingWrites(socket))	//-��д֮ǰ�ж����Ƿ�����׼������û�з���,����1˵��������û��д��
	{//-�������׽����ϻ������ݷ��ͷ��ͳ�ȥ,��ô��д�Ļ��ͱ���
		Log(LOG_SEVERE, -1, "Trying to write to socket %d for which there is already pending output", socket);
		rc = SOCKET_ERROR;
		goto exit;
	}

	for (i = 0; i < count; i++)	//-�Ѽ������������ܳ��������
		total += buflens[i];
	//-����Ϊ���յ�Ӳ��д,������֯������
	iovecs[0].iov_base = buf0;
	iovecs[0].iov_len = buf0len;
	frees1[0] = 1;
	for (i = 0; i < count; i++)	//-�����л����������ݻ����ط��洢,����ṹ�����ʽд������
	{
		iovecs[i+1].iov_base = buffers[i];
		iovecs[i+1].iov_len = buflens[i];
		frees1[i+1] = frees[i];
	}

	if ((rc = Socket_writev(socket, iovecs, count+1, &bytes)) != SOCKET_ERROR)	//-����ʵ�������ݵ����շ��ͳ�ȥ
	{
		if (bytes == total)
			rc = TCPSOCKET_COMPLETE;	//-���������
		else
		{//-û����ɷ��͵ĺ�������
			int* sockmem = (int*)malloc(sizeof(int));
			Log(TRACE_MIN, -1, "Partial write: %ld bytes of %d actually written on socket %d",
					bytes, total, socket);
#if defined(OPENSSL)
			SocketBuffer_pendingWrite(socket, NULL, count+1, iovecs, frees1, total, bytes);
#else
			SocketBuffer_pendingWrite(socket, count+1, iovecs, frees1, total, bytes);	//-�������м�¼���������Ϣ
#endif
			*sockmem = socket;
			ListAppend(s.write_pending, sockmem, sizeof(int));	//-���������ϸ��������;��ͬ
			FD_SET(socket, &(s.pending_wset));	//-��fd����set����,����һ�������׽���
			rc = TCPSOCKET_INTERRUPTED;
		}
	}
exit:
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 *  Add a socket to the pending write list, so that it is checked for writing in select.  This is used
 *  in connect processing when the TCP connect is incomplete, as we need to check the socket for both
 *  ready to read and write states. 
 *  @param socket the socket to add
 */
void Socket_addPendingWrite(int socket)
{
	FD_SET(socket, &(s.pending_wset));	//-��һ���������ļ����������뼯��֮��
}


/**
 *  Clear a socket from the pending write list - if one was added with Socket_addPendingWrite
 *  @param socket the socket to remove
 */
void Socket_clearPendingWrite(int socket)	//-��Щ����������׽��ֵ�,��ʵ�ʶ��Ǳ�ϵͳ���߼�����
{//-��FD����Ϊfile descriptor����д
	if (FD_ISSET(socket, &(s.pending_wset)))	//-�����select�������غ�ĳ���������Ƿ�׼���ã��Ա���н������Ĵ��������
		FD_CLR(socket, &(s.pending_wset));	//-��һ���������ļ��������Ӽ�����ɾ��
}


/**
 *  Close a socket without removing it from the select list.
 *  @param socket the socket to close
 *  @return completion code
 */
int Socket_close_only(int socket)	//-���׽�����ʵ�ֹر�,���ǻ�û��������б��е�����
{
	int rc;

	FUNC_ENTRY;
#if defined(WIN32) || defined(WIN64)
	if (shutdown(socket, SD_BOTH) == SOCKET_ERROR)
		Socket_error("shutdown", socket);
	if ((rc = closesocket(socket)) == SOCKET_ERROR)
		Socket_error("close", socket);
#else
	if (shutdown(socket, SHUT_WR) == SOCKET_ERROR)	//-ʵ�ְ뿪��Socket:����shutdown()ʱֻ���ڻ����е�����ȫ�����ͳɹ���Ż᷵��
		Socket_error("shutdown", socket);
	if ((rc = recv(socket, NULL, (size_t)0, 0)) == SOCKET_ERROR)
		Socket_error("shutdown", socket);
	if ((rc = close(socket)) == SOCKET_ERROR)
		Socket_error("close", socket);
#endif
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 *  Close a socket and remove it from the select list.
 *  @param socket the socket to close
 *  @return completion code
 */
void Socket_close(int socket)	//-�����׽��ֵĹرպܼ�,���������ϵͳ�л����Լ���һ��
{
	FUNC_ENTRY;
	Socket_close_only(socket);
	FD_CLR(socket, &(s.rset_saved));
	if (FD_ISSET(socket, &(s.pending_wset)))
		FD_CLR(socket, &(s.pending_wset));
	if (s.cur_clientsds != NULL && *(int*)(s.cur_clientsds->content) == socket)
		s.cur_clientsds = s.cur_clientsds->next;
	ListRemoveItem(s.connect_pending, &socket, intcompare);
	ListRemoveItem(s.write_pending, &socket, intcompare);
	SocketBuffer_cleanup(socket);

	if (ListRemoveItem(s.clientsds, &socket, intcompare))
		Log(TRACE_MIN, -1, "Removed socket %d", socket);
	else
		Log(LOG_ERROR, -1, "Failed to remove socket %d", socket);
	if (socket + 1 >= s.maxfdp1)
	{
		/* now we have to reset s.maxfdp1 */
		ListElement* cur_clientsds = NULL;

		s.maxfdp1 = 0;
		while (ListNextElement(s.clientsds, &cur_clientsds))
			s.maxfdp1 = max(*((int*)(cur_clientsds->content)), s.maxfdp1);
		++(s.maxfdp1);
		Log(TRACE_MAX, -1, "Reset max fdp1 to %d", s.maxfdp1);
	}
	FUNC_EXIT;
}

//-����Ҫ��͸���е�����,����ҲҪ����ģ�鴦��,���Ҳ����ĵ�ʱ��Ҫ�ܹ���ģ�����ʹ��,��Ҫ˼���Ĳ��
/**
 *  Create a new socket and TCP connect to an address/port
 *  @param addr the address string
 *  @param port the TCP port
 *  @param sock returns the new socket
 *  @return completion code
 */
int Socket_new(char* addr, int port, int* sock)	//-����ʵ�ֵ���Ӳ������(�׽���),ʹ��TCP���ӵ�һ����ַ
{
	int type = SOCK_STREAM;
	struct sockaddr_in address;
#if defined(AF_INET6)
	struct sockaddr_in6 address6;
#endif
	int rc = SOCKET_ERROR;
#if defined(WIN32) || defined(WIN64)
	short family;
#else
	sa_family_t family = AF_INET;	//-���������ǰѧϰ�ܶ���׻��ֱ��,����������ʹ��,��������ǰѧϰ�ĸ���,ֱ�Ӳ����Ĵ���,����϶���ʹ���˿�
#endif
	struct addrinfo *result = NULL;
	struct addrinfo hints = {0, AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP, 0, NULL, NULL, NULL};

	FUNC_ENTRY;
	*sock = -1;

	if (addr[0] == '[')	//-���������ݿ�����IP6�����
	  ++addr;

	if ((rc = getaddrinfo(addr, NULL, &hints, &result)) == 0)	//-�����˿⺯��,0�����ɹ�����0��������
	{//-getaddrinfo�����ܹ��������ֵ���ַ�Լ����񵽶˿�������ת�������ص���һ��addrinfo�Ľṹ���б�ָ�������һ����ַ�嵥��
		struct addrinfo* res = result;	//-ͨ�����溯�����������Ľ������������

		/* prefer ip4 addresses */
		while (res)
		{
			if (res->ai_family == AF_INET)	//-ai_familyָ���˵�ַ��
			{
				result = res;
				break;
			}
			res = res->ai_next;	//-����ָ��addrinfo�ṹ�������ָ��,����һ��Ԫ��һ��Ԫ�صĲ���
		}
		//-���ȶ����ֽ�����Ϣת��,Ȼ�����ж�ʹ��
		if (result == NULL)
			rc = -1;
		else
#if defined(AF_INET6)
		if (result->ai_family == AF_INET6)
		{
			address6.sin6_port = htons(port);
			address6.sin6_family = family = AF_INET6;
			address6.sin6_addr = ((struct sockaddr_in6*)(result->ai_addr))->sin6_addr;
		}
		else
#endif
		if (result->ai_family == AF_INET)
		{//-AF_INET          2            IPv4 
			address.sin_port = htons(port);	//-�����ͱ����������ֽ�˳��ת��������ֽ�˳��
			address.sin_family = family = AF_INET;
			address.sin_addr = ((struct sockaddr_in*)(result->ai_addr))->sin_addr;
		}
		else
			rc = -1;

		freeaddrinfo(result);	//-��getaddrinfo���صĴ洢�ռ䣬����addrinfo�ṹ��ai_addr�ṹ��ai_canonname�ַ�����������malloc��̬��ȡ�ġ���Щ�ռ�ɵ��� freeaddrinfo�ͷš�
	}
	else
	  	Log(LOG_ERROR, -1, "getaddrinfo failed for addr %s with rc %d", addr, rc);

	if (rc != 0)
		Log(LOG_ERROR, -1, "%s is not a valid IP address", addr);
	else
	{
		*sock =	socket(family, type, 0);	//-*�����������"�ײ�"�Ŀ���,����ľͲ���Ҫȥ������,,������һ���׽���
		if (*sock == INVALID_SOCKET)	//-���洴��һ���׽���,����ɹ�������һ���׽���������
			rc = Socket_error("socket", *sock);
		else
		{
#if defined(NOSIGPIPE)
			int opt = 1;

			if (setsockopt(*sock, SOL_SOCKET, SO_NOSIGPIPE, (void*)&opt, sizeof(opt)) != 0)
				Log(LOG_ERROR, -1, "Could not set SO_NOSIGPIPE for socket %d", *sock);
#endif

			Log(TRACE_MIN, -1, "New socket %d for %s, port %d",	*sock, addr, port);
			if (Socket_addSocket(*sock) == SOCKET_ERROR)	//-����һ���׽��ֵ��׽�������,���޸���һЩ����ֵ
				rc = Socket_error("setnonblocking", *sock);
			else
			{
				/* this could complete immmediately, even though we are non-blocking */
				if (family == AF_INET)
					rc = connect(*sock, (struct sockaddr*)&address, sizeof(address));	//-������ָ��socket������
	#if defined(AF_INET6)
				else
					rc = connect(*sock, (struct sockaddr*)&address6, sizeof(address6));
	#endif
				//-����ʹ�ÿ⺯��ʵ�����׽��ֵ�����,���Ͳ�Ķ�������Ҫ֪��,֪���������ξ͹���
				if (rc == SOCKET_ERROR)
					rc = Socket_error("connect", *sock);
				if (rc == EINPROGRESS || rc == EWOULDBLOCK)
				{//-EINPROGRESS����ô�ʹ������ӻ��ڽ�����;����EWOULDBLOCK�������ģ���Ϊ����һ�����ӱ��뻨��һЩʱ�䡣
					int* pnewSd = (int*)malloc(sizeof(int));	//-����һ��ȫ�ֱ�������һ��ʼ�Ͷ���һ��ȫ�ֱ���,���ǵ���Ҫ��ʱ��������,����������
					*pnewSd = *sock;
					ListAppend(s.connect_pending, pnewSd, sizeof(int));	//-�������ӱ�������,���Ǵ洢������������,�����߼�����
					Log(TRACE_MIN, 15, "Connect pending");
				}
			}
		}
	}
	FUNC_EXIT_RC(rc);
	return rc;
}


static Socket_writeComplete* writecomplete = NULL;

void Socket_setWriteCompleteCallback(Socket_writeComplete* mywritecomplete)
{
	writecomplete = mywritecomplete;
}

/**
 *  Continue an outstanding write for a particular socket
 *  @param socket that socket
 *  @return completion code
 */
int Socket_continueWrite(int socket)	//-���һ����д,���ǲ�һ����д����,���п��ܼ���
{
	int rc = 0;
	pending_writes* pw;
	unsigned long curbuflen = 0L, /* cumulative total of buffer lengths */
		bytes;
	int curbuf = -1, i;
	iobuf iovecs1[5];

	FUNC_ENTRY;
	pw = SocketBuffer_getWrite(socket);	//-�õ�����׽���׼��д������,���������ʽ��̬�洢��
	
#if defined(OPENSSL)
	if (pw->ssl)
	{
		rc = SSLSocket_continueWrite(pw);
		goto exit;
	} 	
#endif

	for (i = 0; i < pw->count; ++i)	//-��Щϸ�ں����ٿ�
	{
		if (pw->bytes <= curbuflen)
		{ /* if previously written length is less than the buffer we are currently looking at,
				add the whole buffer */
			iovecs1[++curbuf].iov_len = pw->iovecs[i].iov_len;
			iovecs1[curbuf].iov_base = pw->iovecs[i].iov_base;
		}
		else if (pw->bytes < curbuflen + pw->iovecs[i].iov_len)
		{ /* if previously written length is in the middle of the buffer we are currently looking at,
				add some of the buffer */
			int offset = pw->bytes - curbuflen;
			iovecs1[++curbuf].iov_len = pw->iovecs[i].iov_len - offset;
			iovecs1[curbuf].iov_base = pw->iovecs[i].iov_base + offset;
			break;
		}
		curbuflen += pw->iovecs[i].iov_len;
	}

	if ((rc = Socket_writev(socket, iovecs1, curbuf+1, &bytes)) != SOCKET_ERROR)
	{
		pw->bytes += bytes;	//-�Ѿ����ͳ�ȥ���ֽ���
		if ((rc = (pw->bytes == pw->total)))
		{  /* topic and payload buffers are freed elsewhere, when all references to them have been removed */
			for (i = 0; i < pw->count; i++)
			{
				if (pw->frees[i])
					free(pw->iovecs[i].iov_base);
			}
			Log(TRACE_MIN, -1, "ContinueWrite: partial write now complete for socket %d", socket);		
		}
		else
			Log(TRACE_MIN, -1, "ContinueWrite wrote +%lu bytes on socket %d", bytes, socket);
	}
#if defined(OPENSSL)
exit:
#endif
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 *  Continue any outstanding writes for a socket set
 *  @param pwset the set of sockets
 *  @return completion code
 */
int Socket_continueWrites(fd_set* pwset)	//-����һЩΪ����д�׽���,���������һ��ѭ������,�������������ͳ�ȥ
{
	int rc1 = 0;
	ListElement* curpending = s.write_pending->first;

	FUNC_ENTRY;
	while (curpending)
	{
		int socket = *(int*)(curpending->content);	//-ȡ�������е�һ���ļ�������
		if (FD_ISSET(socket, pwset) && Socket_continueWrite(socket))	//-���ȼ�鼯����ָ�����ļ��������Ƿ���Զ�д
		{//-������˵���ɹ���,�������,���ܻ�û��д��,��Ҫ�������߻���һ��
			if (!SocketBuffer_writeComplete(socket))
				Log(LOG_SEVERE, -1, "Failed to remove pending write from socket buffer list");
			FD_CLR(socket, &(s.pending_wset));	//-�Ѿ�д��ȥ��,���Դ����Ҷ�����ȥ��
			if (!ListRemove(s.write_pending, curpending->content))
			{
				Log(LOG_SEVERE, -1, "Failed to remove pending write from list");
				ListNextElement(s.write_pending, &curpending);
			}
			curpending = s.write_pending->current;
						
			if (writecomplete)	//-�ڴ�����ʱ������������ص�����
				(*writecomplete)(socket);
		}
		else
			ListNextElement(s.write_pending, &curpending);	//-û���Ǿͻ���һ��Ԫ�ش���
	}
	FUNC_EXIT_RC(rc1);
	return rc1;
}


/**
 *  Convert a numeric address to character string
 *  @param sa	socket numerical address
 *  @param sock socket
 *  @return the peer information
 */
char* Socket_getaddrname(struct sockaddr* sa, int sock)	//-�����ֵ�ַת��Ϊ�ַ���
{
/**
 * maximum length of the address string
 */
#define ADDRLEN INET6_ADDRSTRLEN+1
/**
 * maximum length of the port string
 */
#define PORTLEN 10
	static char addr_string[ADDRLEN + PORTLEN];

#if defined(WIN32) || defined(WIN64)
	int buflen = ADDRLEN*2;
	wchar_t buf[ADDRLEN*2];
	if (WSAAddressToString(sa, sizeof(struct sockaddr_in6), NULL, buf, (LPDWORD)&buflen) == SOCKET_ERROR)
		Socket_error("WSAAddressToString", sock);
	else
		wcstombs(addr_string, buf, sizeof(addr_string));
	/* TODO: append the port information - format: [00:00:00::]:port */
	/* strcpy(&addr_string[strlen(addr_string)], "what?"); */
#else
	struct sockaddr_in *sin = (struct sockaddr_in *)sa;
	inet_ntop(sin->sin_family, &sin->sin_addr, addr_string, ADDRLEN);
	sprintf(&addr_string[strlen(addr_string)], ":%d", ntohs(sin->sin_port));
#endif
	return addr_string;
}


/**
 *  Get information about the other end connected to a socket
 *  @param sock the socket to inquire on
 *  @return the peer information
 */
char* Socket_getpeer(int sock)	//-�õ������������ӵ�һ���׽��ֵ���Ϣ
{
	struct sockaddr_in6 sa;
	socklen_t sal = sizeof(sa);
	int rc;

	if ((rc = getpeername(sock, (struct sockaddr*)&sa, &sal)) == SOCKET_ERROR)
	{
		Socket_error("getpeername", sock);
		return "unknown";
	}

	return Socket_getaddrname((struct sockaddr*)&sa, sock);
}


#if defined(Socket_TEST)

int main(int argc, char *argv[])
{
	Socket_connect("127.0.0.1", 1883);
	Socket_connect("localhost", 1883);
	Socket_connect("loadsadsacalhost", 1883);
}

#endif

