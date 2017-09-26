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
int Socket_setnonblocking(int sock)	//-设置文件的属性
{
	int rc;
#if defined(WIN32) || defined(WIN64)
	u_long flag = 1L;

	FUNC_ENTRY;
	rc = ioctl(sock, FIONBIO, &flag);
#else
	int flags;

	FUNC_ENTRY;
	if ((flags = fcntl(sock, F_GETFL, 0)))	//-fcntl()用来操作文件描述符的一些特性。F_GETFL 取得文件描述符状态旗标，此旗标为open（）的参数flags。
		flags = 0;
	rc = fcntl(sock, F_SETFL, flags | O_NONBLOCK);	//-F_SETFL 设置文件描述符状态旗标，参数arg为新旗标，但只允许O_APPEND、O_NONBLOCK和O_ASYNC位的改变，其他位的改变将不受影响。
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
int Socket_error(char* aString, int sock)	//-获得相应错误套接字的错误类型
{
#if defined(WIN32) || defined(WIN64)
	int errno;
#endif

	FUNC_ENTRY;
#if defined(WIN32) || defined(WIN64)
	errno = WSAGetLastError();
#endif
	if (errno != EINTR && errno != EAGAIN && errno != EINPROGRESS && errno != EWOULDBLOCK)	//-错误报告,
	{//-所有错误码都是正整数,通过这些可以知道什么错误
		if (strcmp(aString, "shutdown") != 0 || (errno != ENOTCONN && errno != ECONNRESET))
			Log(TRACE_MINIMUM, -1, "Socket error %s in %s for socket %d", strerror(errno), aString, sock);
	}
	FUNC_EXIT_RC(errno);
	return errno;
}


/**
 * Initialize the socket module
 */
void Socket_outInitialize()	//-初始化套接字模块
{
#if defined(WIN32) || defined(WIN64)
	WORD    winsockVer = 0x0202;
	WSADATA wsd;

	FUNC_ENTRY;
	WSAStartup(winsockVer, &wsd);
#else
	FUNC_ENTRY;
	signal(SIGPIPE, SIG_IGN);	//-这是一个简单的语句,但是对于我来说包含很多学习的知识点在
#endif
	//-在linux下写socket的程序的时候，如果尝试send到一个disconnected socket上，就会让底层抛出一个SIGPIPE信号。
	//-这个信号的缺省处理方法是退出进程，大多数时候这都不是我们期望的。因此我们需要重载这个信号的处理方法。
	//-上面语句就实现了这样的功能,
	//-SIG_IGN(忽略信号),该信号的交付对线程没有影响  　
	SocketBuffer_initialize();	//-创建了好多新东西
	s.clientsds = ListInitialize();	//-链表是存储数据的一种形式,这里大量使用了
	s.connect_pending = ListInitialize();
	s.write_pending = ListInitialize();
	s.cur_clientsds = NULL;
	FD_ZERO(&(s.rset));														/* Initialize the descriptor set */
	FD_ZERO(&(s.pending_wset));	//-将指定的文件描述符集清空，在对文件描述符集合进行设置前，必须对其进行初始化，如果不清空，由于在系统分配内存空间后，通常并不作清空处理，所以结果是不可知的。
	s.maxfdp1 = 0;	//-是指集合中所有文件描述符的范围，即所有文件描述符的最大值加1
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
	ListFree(s.clientsds);	//-释放链表头指针
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
int Socket_addSocket(int newSd)	//-套接字是一个单位,一个套接字又对应了一个列表
{
	int rc = 0;

	FUNC_ENTRY;
	if (ListFindItem(s.clientsds, &newSd, intcompare) == NULL) /* make sure we don't add the same socket twice */
	{//-如果没有增加过这个套接字,下面就进行增加列表操作
		int* pnewSd = (int*)malloc(sizeof(newSd));	//-先开辟一个内存空间
		*pnewSd = newSd;
		ListAppend(s.clientsds, pnewSd, sizeof(newSd));	//-在列表中增加一个项目
		FD_SET(newSd, &(s.rset_saved));	//-将一个给定的文件描述符加入集合之中
		s.maxfdp1 = max(s.maxfdp1, newSd + 1);	//-返回大的数值
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
int isReady(int socket, fd_set* read_set, fd_set* write_set)	//-判断socket是否在指定的可操作集合中,,返回1说明准备好写了
{
	int rc = 1;

	FUNC_ENTRY;
	if  (ListFindItem(s.connect_pending, &socket, intcompare) && FD_ISSET(socket, write_set))	//-判断描述符fd是否在给定的描述符集fdset中，通常配合select函数使用，当检测到fd状态发生变化时返回真，否则，返回假（也可以认为集合中指定的文件描述符是否可以读写）。
		ListRemoveItem(s.connect_pending, &socket, intcompare);	//-这个套接字上可以写了,说明正在进行了悬挂内容处理结束了,可以从悬挂队列中溢出了
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
int Socket_getReadySocket(int more_work, struct timeval *tp)	//-如果有的话返回可以操作的套接字描述符,每次在链表中顺序查找
{
	int rc = 0;
	static struct timeval zero = {0L, 0L}; /* 0 seconds */
	static struct timeval one = {1L, 0L}; /* 1 second */
	struct timeval timeout = one;

	FUNC_ENTRY;
	if (s.clientsds->count == 0)	//-判断在套接字链表中的套接字描述符个数
		goto exit;	//-如果没有说明没有套接字,那么就不会有等待的

	if (more_work)	//-选择的时间不同
		timeout = zero;
	else if (tp)
		timeout = *tp;

	while (s.cur_clientsds != NULL)	//-指向当前的套接字描述符,记录的是一个链表元素,通过这个可以定位到链表中的一个点,然后可以找到所有
	{
		if (isReady(*((int*)(s.cur_clientsds->content)), &(s.rset), &wset))	//-检查的套接字是准备好可以操作了,现在处于空闲状态
			break;
		ListNextElement(s.clientsds, &s.cur_clientsds);
	}	//-寻找到准备好的元素下面进行操作

	if (s.cur_clientsds == NULL)
	{//-到这里说明没有找到合适的套接字,或者是第一次查找
		int rc1;
		fd_set pwset;

		memcpy((void*)&(s.rset), (void*)&(s.rset_saved), sizeof(s.rset));	//-保存的读套接字集合
		memcpy((void*)&(pwset), (void*)&(s.pending_wset), sizeof(pwset));	//-保存的正在写套接字集合
		//-select能够监视我们需要监视的文件描述符的变化情况——读写或是异常。
		if ((rc = select(s.maxfdp1, &(s.rset), &pwset, NULL, &timeout)) == SOCKET_ERROR)	//-多路检测可用套接字,确定套接字的状态
		{//-检查一个集合中的套接字的状态,这个集合由FD_SET设置,可以实现非阻塞方式
			Socket_error("read select", 0);
			goto exit;
		}
		//-负值：select错误
		//-正值：某些文件可读写或出错
		//-0：等待超时，没有可读写或错误的文件
		Log(TRACE_MAX, -1, "Return code %d from read select", rc);	//-返回状态发生变化的描述符总数
		//-上面没有实际操作仅仅是对感兴趣的进行检查,如果有必要就进行到这再处理
		if (Socket_continueWrites(&pwset) == SOCKET_ERROR)	//-周期性的检查悬挂内容,如果有的话就写出去
		{
			rc = 0;
			goto exit;
		}

		memcpy((void*)&wset, (void*)&(s.rset_saved), sizeof(wset));	//-检查读的套接字的可写性,
		//-参数1:是一个整数值，是指集合中所有文件描述符的范围，即所有文件描述符的最大值加1
		//-参数2:（可选）指针，指向一组等待可读性检查的套接口。
		//-参数3:（可选）指针，指向一组等待可写性检查的套接口.
		//-参数4:（可选）指针，指向一组等待错误检查的套接口。
		//-参数5:select()最多等待时间，对阻塞操作则为NULL。若将时间值设为0秒0毫秒，就变成一个纯粹的非阻塞函数
		if ((rc1 = select(s.maxfdp1, NULL, &(wset), NULL, &zero)) == SOCKET_ERROR)	//-在一个套接字组合中查找需要的子集,然后对他们操作
		{//-这个select仅仅是一个监视的作用,监视到了,下面就需要实际操作了
			Socket_error("write select", 0);
			rc = rc1;
			goto exit;
		}
		Log(TRACE_MAX, -1, "Return code %d from write select", rc1);

		if (rc == 0 && rc1 == 0)
			goto exit; /* no work to do */
		//-到这里说明有可读的套接字,下面从头找到第一可读的返回
		s.cur_clientsds = s.clientsds->first;
		while (s.cur_clientsds != NULL)
		{
			int cursock = *((int*)(s.cur_clientsds->content));
			if (isReady(cursock, &(s.rset), &wset))	//-返回非0数,说明这个套接字可以操作了,准备好了
				break;
			ListNextElement(s.clientsds, &s.cur_clientsds);
		}
	}

	if (s.cur_clientsds == NULL)
		rc = 0;
	else
	{
		rc = *((int*)(s.cur_clientsds->content));
		ListNextElement(s.clientsds, &s.cur_clientsds);	//-前面一个参数是一个链表,后面一个参数是一个元素,这里直接指向紧接着的下一个元素
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
int Socket_getch(int socket, char* c)	//-这里所有的数据都需要对队列的操作,然后再给应用程序使用,而不是直接来自套接字流
{
	int rc = SOCKET_ERROR;

	FUNC_ENTRY;
	if ((rc = SocketBuffer_getQueuedChar(socket, c)) != SOCKETBUFFER_INTERRUPTED)
		goto exit;

	if ((rc = recv(socket, c, (size_t)1, 0)) == SOCKET_ERROR)	//-获得套接字数据,这个已经是最底层接收了
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
char *Socket_getdata(int socket, int bytes, int* actual_len)	//-从套接字中获得一些数据
{
	int rc;
	char* buf;

	FUNC_ENTRY;
	if (bytes == 0)
	{
		buf = SocketBuffer_complete(socket);	//?这个套接字缓冲区没有数据了所以完成
		goto exit;
	}

	buf = SocketBuffer_getQueuedData(socket, bytes, actual_len);
	//-下面的函数才是从接收缓冲区复制数据,上面应该是在队列中寻找空间
	//-recv函数仅仅是copy数据，真正的接收数据是协议来完成的
	//-recv函数返回其实际copy的字节数
	//-第一个参数指定接收端套接字描述符；
	//-第二个参数指明一个缓冲区，该缓冲区用来存放recv函数接收到的数据；
	//-第三个参数指明buf的长度；
	//-第四个参数一般置0。
	if ((rc = recv(socket, buf + (*actual_len), (size_t)(bytes - (*actual_len)), 0)) == SOCKET_ERROR)	//-*得到套接字数据
	{//-发生错误
		rc = Socket_error("recv - getdata", socket);
		if (rc != EAGAIN && rc != EWOULDBLOCK)
		{
			buf = NULL;
			goto exit;
		}
	}
	else if (rc == 0) /* rc 0 means the other end closed the socket, albeit "gracefully" */
	{//-连接已中止
		buf = NULL;
		goto exit;
	}
	else
		*actual_len += rc;	//-读入的字节数

	if (*actual_len == bytes)
		SocketBuffer_complete(socket);	//-这里的完成可能就是封闭一个缓冲区,说明一帧内容完整了,可以开始处理了
	else /* we didn't read the whole packet */
	{
		SocketBuffer_interrupted(socket, *actual_len);	//-应该是对标志的一种处理,这样使得缓冲区能被识别在不同的状态下
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
int Socket_noPendingWrites(int socket)	//-在写悬挂链表中检查这个套接字上是否有内容没有发送出去
{
	int cursock = socket;
	return ListFindItem(s.write_pending, &cursock, intcompare) == NULL;	//-如果不存在悬挂的,那么返回1
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
int Socket_writev(int socket, iobuf* iovecs, int count, unsigned long* bytes)	//-里面没有什么特殊的仅仅对库函数的标识位进行了判断处理
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
	//-readv和writev函数用于在一次函数调用中读、写多个非连续缓冲区。有时也将这两个函数称为散布读（scatter read）和聚集写（gather write）。
	rc = writev(socket, iovecs, count);	//-这里启动了最终数据的发送,,返回传输字节数，出错时返回-1.
	if (rc == SOCKET_ERROR)
	{
		int err = Socket_error("writev - putdatas", socket);
		if (err == EWOULDBLOCK || err == EAGAIN)	//-用于非阻塞模式，不需要重新读或者写,只是暂时没有完成
			rc = TCPSOCKET_INTERRUPTED;	//-表示发生了中断,还没有结束,需要等待
	}
	else
		*bytes = rc;	//-若成功则返回已读，写的字节数，若出错则返回-1。 
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
int Socket_putdatas(int socket, char* buf0, size_t buf0len, int count, char** buffers, size_t* buflens, int* frees)	//-尽量发送出去,但是并不是全部能发送的,没有的写入缓冲区
{//-尝试写一系列的缓冲区到一个套接字以至于他们被作为一个帧发送出去
	unsigned long bytes = 0L;
	iobuf iovecs[5];
	int frees1[5];
	int rc = TCPSOCKET_INTERRUPTED, i, total = buf0len;

	FUNC_ENTRY;
	if (!Socket_noPendingWrites(socket))	//-在写之前判断下是否有正准备发还没有发的,返回1说明不存在没有写的
	{//-如果这个套接字上还有内容发送发送出去,那么就写的话就报错
		Log(LOG_SEVERE, -1, "Trying to write to socket %d for which there is already pending output", socket);
		rc = SOCKET_ERROR;
		goto exit;
	}

	for (i = 0; i < count; i++)	//-把几个缓冲区的总长计算出来
		total += buflens[i];
	//-下面为最终的硬件写,最终组织内容了
	iovecs[0].iov_base = buf0;
	iovecs[0].iov_len = buf0len;
	frees1[0] = 1;
	for (i = 0; i < count; i++)	//-把所有缓冲区的内容换个地方存储,这个结构体的形式写函数认
	{
		iovecs[i+1].iov_base = buffers[i];
		iovecs[i+1].iov_len = buflens[i];
		frees1[i+1] = frees[i];
	}

	if ((rc = Socket_writev(socket, iovecs, count+1, &bytes)) != SOCKET_ERROR)	//-这里实现了数据的最终发送出去
	{
		if (bytes == total)
			rc = TCPSOCKET_COMPLETE;	//-发送完成了
		else
		{//-没有完成发送的后续处理
			int* sockmem = (int*)malloc(sizeof(int));
			Log(TRACE_MIN, -1, "Partial write: %ld bytes of %d actually written on socket %d",
					bytes, total, socket);
#if defined(OPENSSL)
			SocketBuffer_pendingWrite(socket, NULL, count+1, iovecs, frees1, total, bytes);
#else
			SocketBuffer_pendingWrite(socket, count+1, iovecs, frees1, total, bytes);	//-在链表中记录下了这个信息
#endif
			*sockmem = socket;
			ListAppend(s.write_pending, sockmem, sizeof(int));	//-这个链表和上个链表的用途不同
			FD_SET(socket, &(s.pending_wset));	//-将fd加入set集合,多了一个监视套接字
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
	FD_SET(socket, &(s.pending_wset));	//-将一个给定的文件描述符加入集合之中
}


/**
 *  Clear a socket from the pending write list - if one was added with Socket_addPendingWrite
 *  @param socket the socket to remove
 */
void Socket_clearPendingWrite(int socket)	//-这些看似是针对套接字的,但实际都是本系统内逻辑处理
{//-“FD”即为file descriptor的缩写
	if (FD_ISSET(socket, &(s.pending_wset)))	//-检查在select函数返回后，某个描述符是否准备好，以便进行接下来的处理操作。
		FD_CLR(socket, &(s.pending_wset));	//-将一个给定的文件描述符从集合中删除
}


/**
 *  Close a socket without removing it from the select list.
 *  @param socket the socket to close
 *  @return completion code
 */
int Socket_close_only(int socket)	//-在套接字上实现关闭,但是还没有清空在列表中的属性
{
	int rc;

	FUNC_ENTRY;
#if defined(WIN32) || defined(WIN64)
	if (shutdown(socket, SD_BOTH) == SOCKET_ERROR)
		Socket_error("shutdown", socket);
	if ((rc = closesocket(socket)) == SOCKET_ERROR)
		Socket_error("close", socket);
#else
	if (shutdown(socket, SHUT_WR) == SOCKET_ERROR)	//-实现半开放Socket:调用shutdown()时只有在缓存中的数据全部发送成功后才会返回
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
void Socket_close(int socket)	//-本身套接字的关闭很简单,但是在这个系统中还有自己的一套
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

//-我需要吃透所有的内容,但是也要进行模块处理,当我不关心的时候要能够以模块进行使用,主要思考的层次
/**
 *  Create a new socket and TCP connect to an address/port
 *  @param addr the address string
 *  @param port the TCP port
 *  @param sock returns the new socket
 *  @return completion code
 */
int Socket_new(char* addr, int port, int* sock)	//-这里实现的是硬件连接(套接字),使用TCP连接到一个地址
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
	sa_family_t family = AF_INET;	//-这个就是以前学习很多的套机字编程,在这里完美使用,而且我以前学习的更深,直接操作寄存器,这里肯定是使用了库
#endif
	struct addrinfo *result = NULL;
	struct addrinfo hints = {0, AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP, 0, NULL, NULL, NULL};

	FUNC_ENTRY;
	*sock = -1;

	if (addr[0] == '[')	//-这个程序兼容考虑了IP6的情况
	  ++addr;

	if ((rc = getaddrinfo(addr, NULL, &hints, &result)) == 0)	//-调用了库函数,0——成功，非0——出错
	{//-getaddrinfo函数能够处理名字到地址以及服务到端口这两种转换，返回的是一个addrinfo的结构（列表）指针而不是一个地址清单。
		struct addrinfo* res = result;	//-通过上面函数解析出来的结果保存在这里

		/* prefer ip4 addresses */
		while (res)
		{
			if (res->ai_family == AF_INET)	//-ai_family指定了地址族
			{
				result = res;
				break;
			}
			res = res->ai_next;	//-利用指向addrinfo结构体链表的指针,进行一个元素一个元素的查找
		}
		//-首先对名字进行信息转化,然后再判断使用
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
			address.sin_port = htons(port);	//-将整型变量从主机字节顺序转变成网络字节顺序
			address.sin_family = family = AF_INET;
			address.sin_addr = ((struct sockaddr_in*)(result->ai_addr))->sin_addr;
		}
		else
			rc = -1;

		freeaddrinfo(result);	//-由getaddrinfo返回的存储空间，包括addrinfo结构、ai_addr结构和ai_canonname字符串，都是用malloc动态获取的。这些空间可调用 freeaddrinfo释放。
	}
	else
	  	Log(LOG_ERROR, -1, "getaddrinfo failed for addr %s with rc %d", addr, rc);

	if (rc != 0)
		Log(LOG_ERROR, -1, "%s is not a valid IP address", addr);
	else
	{
		*sock =	socket(family, type, 0);	//-*到这里就是最"底层"的库了,下面的就不需要去考虑了,,创建了一个套接字
		if (*sock == INVALID_SOCKET)	//-上面创建一个套接字,如果成功将返回一个套接字描述符
			rc = Socket_error("socket", *sock);
		else
		{
#if defined(NOSIGPIPE)
			int opt = 1;

			if (setsockopt(*sock, SOL_SOCKET, SO_NOSIGPIPE, (void*)&opt, sizeof(opt)) != 0)
				Log(LOG_ERROR, -1, "Could not set SO_NOSIGPIPE for socket %d", *sock);
#endif

			Log(TRACE_MIN, -1, "New socket %d for %s, port %d",	*sock, addr, port);
			if (Socket_addSocket(*sock) == SOCKET_ERROR)	//-增加一个套接字到套接字链表,并修改了一些属性值
				rc = Socket_error("setnonblocking", *sock);
			else
			{
				/* this could complete immmediately, even though we are non-blocking */
				if (family == AF_INET)
					rc = connect(*sock, (struct sockaddr*)&address, sizeof(address));	//-建立与指定socket的连接
	#if defined(AF_INET6)
				else
					rc = connect(*sock, (struct sockaddr*)&address6, sizeof(address6));
	#endif
				//-上面使用库函数实现了套接字的连接,更低层的东西不需要知道,知道到这个层次就够了
				if (rc == SOCKET_ERROR)
					rc = Socket_error("connect", *sock);
				if (rc == EINPROGRESS || rc == EWOULDBLOCK)
				{//-EINPROGRESS，那么就代表连接还在进行中;报告EWOULDBLOCK是正常的，因为建立一个连接必须花费一些时间。
					int* pnewSd = (int*)malloc(sizeof(int));	//-对于一个全局变量不是一开始就定义一个全局变量,而是当需要的时候再申请,不用再销毁
					*pnewSd = *sock;
					ListAppend(s.connect_pending, pnewSd, sizeof(int));	//-这里连接被挂起了,我们存储到挂起链表中,后面逻辑处理
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
int Socket_continueWrite(int socket)	//-针对一个的写,但是不一定就写完了,还有可能继续
{
	int rc = 0;
	pending_writes* pw;
	unsigned long curbuflen = 0L, /* cumulative total of buffer lengths */
		bytes;
	int curbuf = -1, i;
	iobuf iovecs1[5];

	FUNC_ENTRY;
	pw = SocketBuffer_getWrite(socket);	//-得到这个套接字准备写的内容,以链表的形式动态存储的
	
#if defined(OPENSSL)
	if (pw->ssl)
	{
		rc = SSLSocket_continueWrite(pw);
		goto exit;
	} 	
#endif

	for (i = 0; i < pw->count; ++i)	//-这些细节后面再扣
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
		pw->bytes += bytes;	//-已经发送出去的字节数
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
int Socket_continueWrites(fd_set* pwset)	//-继续一些为决的写套接字,这个里面有一种循环机制,把数据连续发送出去
{
	int rc1 = 0;
	ListElement* curpending = s.write_pending->first;

	FUNC_ENTRY;
	while (curpending)
	{
		int socket = *(int*)(curpending->content);	//-取出集合中的一个文件描述符
		if (FD_ISSET(socket, pwset) && Socket_continueWrite(socket))	//-首先检查集合中指定的文件描述符是否可以读写
		{//-到这里说明成功了,下面继续,可能还没有写完,需要继续或者换下一个
			if (!SocketBuffer_writeComplete(socket))
				Log(LOG_SEVERE, -1, "Failed to remove pending write from socket buffer list");
			FD_CLR(socket, &(s.pending_wset));	//-已经写出去了,所以从悬挂队列中去除
			if (!ListRemove(s.write_pending, curpending->content))
			{
				Log(LOG_SEVERE, -1, "Failed to remove pending write from list");
				ListNextElement(s.write_pending, &curpending);
			}
			curpending = s.write_pending->current;
						
			if (writecomplete)	//-在创建的时候设置了这个回调函数
				(*writecomplete)(socket);
		}
		else
			ListNextElement(s.write_pending, &curpending);	//-没有那就换下一个元素处理
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
char* Socket_getaddrname(struct sockaddr* sa, int sock)	//-将数字地址转换为字符串
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
char* Socket_getpeer(int sock)	//-得到其它结束连接到一个套接字的信息
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

