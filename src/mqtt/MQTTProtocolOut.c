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
 *    Ian Craggs - fix for buffer overflow in addressPort bug #433290
 *    Ian Craggs - MQTT 3.1.1 support
 *    Rong Xiang, Ian Craggs - C++ compatibility
 *******************************************************************************/

/**
 * @file
 * \brief Functions dealing with the MQTT protocol exchanges
 *
 * Some other related functions are in the MQTTProtocolClient module
 */

#include <stdlib.h>

#include "MQTTProtocolOut.h"
#include "StackTrace.h"
#include "Heap.h"

extern MQTTProtocol state;
extern ClientStates* bstate;


/**
 * Separates an address:port into two separate values
 * @param uri the input string - hostname:port
 * @param port the returned port integer
 * @return the address string
 */
char* MQTTProtocol_addressPort(const char* uri, int* port)	//-从一个字符串中分析出自己可以直接使用的数据格式
{
	char* colon_pos = strrchr(uri, ':'); /* reverse find to allow for ':' in IPv6 addresses */ //-从右开始查找,成功则返回该字符以及其后面的字符
	char* buf = (char*)uri;
	int len;

	FUNC_ENTRY;		//-这个是定义的一个宏,可能就是指向一个跟踪函数的
	if (uri[0] == '[')
	{  /* ip v6 */
		if (colon_pos < strrchr(uri, ']'))
			colon_pos = NULL;  /* means it was an IPv6 separator, not for host:port */
	}

	if (colon_pos)
	{
		int addr_len = colon_pos - uri;
		buf = malloc(addr_len + 1);
		*port = atoi(colon_pos + 1);	//-获得了端口号
		MQTTStrncpy(buf, uri, addr_len+1);	//-现在这个层次不去深究这个细节了,知道功能即可,以后需要的话可以学习人家的这方法
	}
	else
		*port = DEFAULT_PORT;

	len = strlen(buf);
	if (buf[len - 1] == ']')
		buf[len - 1] = '\0';

	FUNC_EXIT;
	return buf;
}


/**
 * MQTT outgoing connect processing for a client
 * @param ip_address the TCP address:port to connect to
 * @param aClient a structure with all MQTT data needed
 * @param int ssl
 * @param int MQTTVersion the MQTT version to connect with (3 or 4)
 * @return return code
 */
#if defined(OPENSSL)
int MQTTProtocol_connect(const char* ip_address, Clients* aClient, int ssl, int MQTTVersion)
#else
int MQTTProtocol_connect(const char* ip_address, Clients* aClient, int MQTTVersion)	//-又一个根据参数进行的进一步连接
#endif
{
	int rc, port;
	char* addr;

	FUNC_ENTRY;
	aClient->good = 1;

	addr = MQTTProtocol_addressPort(ip_address, &port);	//-通过字符提取出了自己需要的信息:IP地址+端口号
	rc = Socket_new(addr, port, &(aClient->net.socket));	//-这个里面实现了底层的创建和连接(硬件链路层)
	if (rc == EINPROGRESS || rc == EWOULDBLOCK)	//-上面的连接可能还没有结束,在等待链表内
		aClient->connect_state = 1; /* TCP connect called - wait for connect completion */	//-通过这个标识位可以知道连接状态
	else if (rc == 0)
	{	/* TCP connect completed. If SSL, send SSL connect */
#if defined(OPENSSL)
		if (ssl)
		{
			if (SSLSocket_setSocketForSSL(&aClient->net, aClient->sslopts) != 1)
			{
				rc = SSLSocket_connect(aClient->net.ssl, aClient->net.socket);
				if (rc == -1)
					aClient->connect_state = 2; /* SSL connect called - wait for completion */
			}
			else
				rc = SOCKET_ERROR;
		}
#endif
		//-前面仅仅是实现了"硬件"的连接,下面需要进行MQTT协议的连接.
		if (rc == 0)	//-如果TCP/IP连接完成,下面就立即开始协议层连接了
		{
			/* Now send the MQTT connect packet */
			if ((rc = MQTTPacket_send_connect(aClient, MQTTVersion)) == 0)
				aClient->connect_state = 3; /* MQTT Connect sent - wait for CONNACK */ //-这个变量记录了目前MQTT协议所处的状态,或者说哪个流程中
			else
				aClient->connect_state = 0;	//-MQTT连接命令还没有完全发送出去
		}
	}
	if (addr != ip_address)	//-这里仅仅是防错,阻止可能的内存泄露的
		free(addr);

	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Process an incoming pingresp packet for a socket
 * @param pack pointer to the publish packet
 * @param sock the socket on which the packet was received
 * @return completion code
 */
int MQTTProtocol_handlePingresps(void* pack, int sock)	//-这里都是处理各种帧的,而这些中都已经是识别出协议层了
{
	Clients* client = NULL;
	int rc = TCPSOCKET_COMPLETE;

	FUNC_ENTRY;
	client = (Clients*)(ListFindItem(bstate->clients, &sock, clientSocketCompare)->content);	//-根据套接字寻找到对应的客户端
	Log(LOG_PROTOCOL, 21, NULL, sock, client->clientID);
	client->ping_outstanding = 0;	//-可能就是设置下标识位就可以知道对于信息了
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * MQTT outgoing subscribe processing for a client
 * @param client the client structure
 * @param topics list of topics
 * @param qoss corresponding list of QoSs
 * @return completion code
 */
int MQTTProtocol_subscribe(Clients* client, List* topics, List* qoss, int msgID)	//-MQTT外向订阅客户端处理
{
	int rc = 0;

	FUNC_ENTRY;
	/* we should stack this up for retry processing too */	//-我们也应该堆栈重试处理这个
	rc = MQTTPacket_send_subscribe(topics, qoss, msgID, 0, &client->net, client->clientID);	//-发送订阅
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Process an incoming suback packet for a socket
 * @param pack pointer to the publish packet
 * @param sock the socket on which the packet was received
 * @return completion code
 */
int MQTTProtocol_handleSubacks(void* pack, int sock)	//-处理一个输入的发布应答帧
{
	Suback* suback = (Suback*)pack;
	Clients* client = NULL;
	int rc = TCPSOCKET_COMPLETE;

	FUNC_ENTRY;
	client = (Clients*)(ListFindItem(bstate->clients, &sock, clientSocketCompare)->content);
	Log(LOG_PROTOCOL, 23, NULL, sock, client->clientID, suback->msgId);
	MQTTPacket_freeSuback(suback);	//-释放存储空间,这里有一个很重要的思想,为什么都要对内存释放
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * MQTT outgoing unsubscribe processing for a client
 * @param client the client structure
 * @param topics list of topics
 * @return completion code
 */
int MQTTProtocol_unsubscribe(Clients* client, List* topics, int msgID)	//-MQTT发出取消订阅帧
{
	int rc = 0;

	FUNC_ENTRY;
	/* we should stack this up for retry processing too? */
	rc = MQTTPacket_send_unsubscribe(topics, msgID, 0, &client->net, client->clientID);	//-发送一个帧到一个套接字
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Process an incoming unsuback packet for a socket
 * @param pack pointer to the publish packet
 * @param sock the socket on which the packet was received
 * @return completion code
 */
int MQTTProtocol_handleUnsubacks(void* pack, int sock)	//-发布帧 帧被接收的套接字
{//?处理没有订阅的帧
	Unsuback* unsuback = (Unsuback*)pack;
	Clients* client = NULL;
	int rc = TCPSOCKET_COMPLETE;

	FUNC_ENTRY;
	client = (Clients*)(ListFindItem(bstate->clients, &sock, clientSocketCompare)->content);
	Log(LOG_PROTOCOL, 24, NULL, sock, client->clientID, unsuback->msgId);
	free(unsuback);
	FUNC_EXIT_RC(rc);
	return rc;
}

