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
 *    Ian Craggs, Allan Stockdill-Mander - SSL updates
 *******************************************************************************/

#if !defined(SOCKETBUFFER_H)
#define SOCKETBUFFER_H

#if defined(WIN32) || defined(WIN64)
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

#if defined(OPENSSL)
#include <openssl/ssl.h>
#endif

#if defined(WIN32) || defined(WIN64)
	typedef WSABUF iobuf;
#else
	typedef struct iovec iobuf;
#endif

/*
I/O vector，与readv和wirtev操作相关的结构体。readv和writev函数用于在一次函数调用
中读、写多个非连续缓冲区。有时也将这两个函数称为散布读（scatter read）和聚集写（gather write）。
成员iov_base指向一个缓冲区，这个缓冲区是存放readv所接收的数据或是writev将要发送的数据。
成员iov_len确定了接收的最大长度以及实际写入的长度。
read和write的衍生函数，readv和writev可以在一个原子操作中读取或写入多个缓冲区。
ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
fd是要在其上进行读或是写的文件描述符；
iov是读或写所用的I/O向量；
iovcnt是要使用的向量元素个数。

* 将三个独立的字符串一次写入终端。*
#include <sys/uio.h>
int main(int argc,char **argv)
{
    char part1[] = "This is iov";
    char part2[] = " and ";
    char part3[] = " writev test";
    struct iovec iov[3];
    iov[0].iov_base = part1;
    iov[0].iov_len = strlen(part1);
    iov[1].iov_base = part2;
    iov[1].iov_len = strlen(part2);
    iov[2].iov_base = part3;
    iov[2].iov_len = strlen(part3);
    writev(1,iov,3);
    return 0;
}
*/

typedef struct
{
	int socket;
	int index, headerlen;
	char fixed_header[5];	/**< header plus up to 4 length bytes */
	int buflen, 			/**< total length of the buffer */
		datalen; 			/**< current length of data in buf */
	char* buf;
} socket_queue;	//-建立一个结构体用于存储套接字队列信息

typedef struct
{
	int socket, total, count;
#if defined(OPENSSL)
	SSL* ssl;
#endif
	unsigned long bytes;
	iobuf iovecs[5];
	int frees[5];
} pending_writes;

#define SOCKETBUFFER_COMPLETE 0
#if !defined(SOCKET_ERROR)
	#define SOCKET_ERROR -1
#endif
#define SOCKETBUFFER_INTERRUPTED -22 /* must be the same value as TCPSOCKET_INTERRUPTED */

void SocketBuffer_initialize(void);
void SocketBuffer_terminate(void);
void SocketBuffer_cleanup(int socket);
char* SocketBuffer_getQueuedData(int socket, int bytes, int* actual_len);
int SocketBuffer_getQueuedChar(int socket, char* c);
void SocketBuffer_interrupted(int socket, int actual_len);
char* SocketBuffer_complete(int socket);
void SocketBuffer_queueChar(int socket, char c);

#if defined(OPENSSL)
void SocketBuffer_pendingWrite(int socket, SSL* ssl, int count, iobuf* iovecs, int* frees, int total, int bytes);
#else
void SocketBuffer_pendingWrite(int socket, int count, iobuf* iovecs, int* frees, int total, int bytes);
#endif
pending_writes* SocketBuffer_getWrite(int socket);
int SocketBuffer_writeComplete(int socket);
pending_writes* SocketBuffer_updateWrite(int socket, char* topic, char* payload);

#endif
