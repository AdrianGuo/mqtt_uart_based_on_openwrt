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
I/O vector����readv��wirtev������صĽṹ�塣readv��writev����������һ�κ�������
�ж���д�������������������ʱҲ��������������Ϊɢ������scatter read���;ۼ�д��gather write����
��Աiov_baseָ��һ��������������������Ǵ��readv�����յ����ݻ���writev��Ҫ���͵����ݡ�
��Աiov_lenȷ���˽��յ���󳤶��Լ�ʵ��д��ĳ��ȡ�
read��write������������readv��writev������һ��ԭ�Ӳ����ж�ȡ��д������������
ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
fd��Ҫ�����Ͻ��ж�����д���ļ���������
iov�Ƕ���д���õ�I/O������
iovcnt��Ҫʹ�õ�����Ԫ�ظ�����

* �������������ַ���һ��д���նˡ�*
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
} socket_queue;	//-����һ���ṹ�����ڴ洢�׽��ֶ�����Ϣ

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
