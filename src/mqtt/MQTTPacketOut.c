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
 *    Ian Craggs - MQTT 3.1.1 support
 *    Rong Xiang, Ian Craggs - C++ compatibility
 *******************************************************************************/
//-处理来自和到套接字的读写MQTT帧
/**
 * @file
 * \brief functions to deal with reading and writing of MQTT packets from and to sockets
 *
 * Some other related functions are in the MQTTPacket module
 */


#include "MQTTPacketOut.h"
#include "Log.h"
#include "StackTrace.h"

#include <string.h>
#include <stdlib.h>

#include "Heap.h"

/**
Fixed header:
		|7	6	5	4	3	2	1	0|
byte 1	|Message Type	DUP flag QoS level RETAIN
Message Type固定为1
DUP, QoS, 和RETAIN flags没有在CONNECT消息中使用.		
byte 2	|Remaining Length
Remaining Length是variable header (12 bytes)的长度和Payload的长度. 这可以是一个多字节的字段.
Variable header:
Protocol Name
byte 1	|Length MSB
byte 2	|Length LSB
byte 3	|
byte 4	|
byte 5	|
byte 6	|
byte 7	|
byte 8	|
byte 9	|Version
byte 10	|Connect Flags
byte 11	|Keep Alive MSB
byte 12	|Keep Alive LSB
Payload:
CONNECT消息的payload包含一个或多个的UTF-8编码的字符串， 这是基于variable header中的
标识（ flags） 。如果有字符串存在，则必须按下列顺序出现:
Client Identifier
Will Topic
Will Message
User Name
Password
Response
*/
/**
 * Send an MQTT CONNECT packet down a socket.
 * @param client a structure from which to get all the required values
 * @param MQTTVersion the MQTT version to connect with
 * @return the completion code (e.g. TCPSOCKET_COMPLETE)
 */
int MQTTPacket_send_connect(Clients* client, int MQTTVersion)	//-这里很可能就是组织MQTT协议内容了,这个协议内容需要根据MQTT来写,属于TCP的有效区域
{
	char *buf, *ptr;
	Connect packet;
	int rc = -1, len;

	FUNC_ENTRY;
	packet.header.byte = 0;
	packet.header.bits.type = CONNECT;	//-说明了帧的类型

	len = ((MQTTVersion == 3) ? 12 : 10) + strlen(client->clientID)+2;
	if (client->will)	//-判断是否有will消息,这是链接命令中的一个东西
		len += strlen(client->will->topic)+2 + strlen(client->will->msg)+2;
	if (client->username)	//-这些标识位同样意味着后面有效区域是否含有这些信息,如果有的话就需要预留空间
		len += strlen(client->username)+2;
	if (client->password)	//-重点是什么是否给这些标识位赋值的
		len += strlen(client->password)+2;

	ptr = buf = malloc(len);
	if (MQTTVersion == 3)
	{
		writeUTF(&ptr, "MQIsdp");
		writeChar(&ptr, (char)3);
	}
	else if (MQTTVersion == 4)
	{
		writeUTF(&ptr, "MQTT");
		writeChar(&ptr, (char)4);
	}
	else
		goto exit;

	packet.flags.all = 0;
	packet.flags.bits.cleanstart = client->cleansession;
	packet.flags.bits.will = (client->will) ? 1 : 0;
	if (packet.flags.bits.will)
	{
		packet.flags.bits.willQoS = client->will->qos;
		packet.flags.bits.willRetain = client->will->retained;
	}

	if (client->username)
		packet.flags.bits.username = 1;
	if (client->password)
		packet.flags.bits.password = 1;

	writeChar(&ptr, packet.flags.all);	//-前面经过组织,这里实际向缓冲区填写内容了
	writeInt(&ptr, client->keepAliveInterval);
	writeUTF(&ptr, client->clientID);
	if (client->will)
	{
		writeUTF(&ptr, client->will->topic);
		writeUTF(&ptr, client->will->msg);
	}
	if (client->username)
		writeUTF(&ptr, client->username);
	if (client->password)
		writeUTF(&ptr, client->password);
	//-上面填写的协议内容,应该可以从MQTT协议文本中找到依据
	rc = MQTTPacket_send(&client->net, packet.header, buf, len, 1);
	Log(LOG_PROTOCOL, 0, NULL, client->net.socket, client->clientID, client->cleansession, rc);
exit:
	if (rc != TCPSOCKET_INTERRUPTED)
		free(buf);
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Function used in the new packets table to create connack packets.
 * @param aHeader the MQTT header byte
 * @param data the rest of the packet
 * @param datalen the length of the rest of the packet
 * @return pointer to the packet structure
 */
void* MQTTPacket_connack(unsigned char aHeader, char* data, size_t datalen)	//-连接确认
{//-这条指令肯定是不需要客户端发送出去的,这里有说明服务器和客户端使用的是同一个库
	Connack* pack = malloc(sizeof(Connack));
	char* curdata = data;

	FUNC_ENTRY;
	pack->header.byte = aHeader;
	pack->flags.all = readChar(&curdata);
	pack->rc = readChar(&curdata);
	FUNC_EXIT;
	return pack;
}

/**
Fixed header:
		|7	6	5	4	3	2	1	0|
byte 1	|Message Type	DUP flag QoS level RETAIN
Message Type固定为1
DUP, QoS, 和RETAIN flags没有在CONNECT消息中使用.		
byte 2	|Remaining Length
Remaining Length是variable header (12 bytes)的长度和Payload的长度. 这可以是一个多字节的字段.这里=0
Variable header:
无
Payload:
无
*/
/**
 * Send an MQTT PINGREQ packet down a socket.
 * @param socket the open socket to send the data to
 * @param clientID the string client identifier, only used for tracing
 * @return the completion code (e.g. TCPSOCKET_COMPLETE)
 */
int MQTTPacket_send_pingreq(networkHandles* net, const char* clientID)	//-ping请求
{
	Header header;
	int rc = 0;
	size_t buflen = 0;	//-表示有效缓冲区尺寸

	FUNC_ENTRY;
	header.byte = 0;	//-表示字节长度为0
	header.bits.type = PINGREQ;	//-发送报文的类型
	rc = MQTTPacket_send(net, header, NULL, buflen,0);	//-有足够的标识位就可以发送
	Log(LOG_PROTOCOL, 20, NULL, net->socket, clientID, rc);
	FUNC_EXIT_RC(rc);
	return rc;
}

/**
Fixed header:
		|7	6	5	4	3	2	1	0|
byte 1	|Message Type	DUP flag	QoS level	RETAIN
Message Type固定为8
使用QoS级别1来确认多个订阅请求，响应的SUBACK通过匹配Message ID来识别。
DUP flag设置为零（ 0）。这意味着这个消息正在被第一次发送。
byte 2	|Remaining Length
Remaining Length是variable header (12 bytes)的长度和Payload的长度. 这可以是一个多字节的字段.
Variable header:
variable header包含一个Message ID，因为SUBSCRIBE消息的QoS级别为1。
Payload:
包含一个客户端希望订阅的主题名列表和客户端要接收消息的QoS级别。这些 topic/QoS成对连续包装
*/
/**
 * Send an MQTT subscribe packet down a socket.
 * @param topics list of topics
 * @param qoss list of corresponding QoSs
 * @param msgid the MQTT message id to use
 * @param dup boolean - whether to set the MQTT DUP flag
 * @param socket the open socket to send the data to
 * @param clientID the string client identifier, only used for tracing
 * @return the completion code (e.g. TCPSOCKET_COMPLETE)
 */
int MQTTPacket_send_subscribe(List* topics, List* qoss, int msgid, int dup, networkHandles* net, const char* clientID)	//-发送一个MQTT订阅帧到一个套接字
{
	Header header;
	char *data, *ptr;
	int rc = -1;
	ListElement *elem = NULL, *qosElem = NULL;
	int datalen;

	FUNC_ENTRY;
	header.bits.type = SUBSCRIBE;	//-客户端订阅请求
	header.bits.dup = dup;
	header.bits.qos = 1;
	header.bits.retain = 0;
	//-上面是根据帧类型给帧填内容
	datalen = 2 + topics->count * 3; // utf length + char qos == 3,,乘以3的原因是每个主题固定有长度2个字节品质1个字节,其它主题名称是不固定的
	while (ListNextElement(topics, &elem))	//-订阅的主题被形成了一个链表,这里一个个检索出来
		datalen += strlen((char*)(elem->content));
	ptr = data = malloc(datalen);	//-计算出了整个报文的字节数

	writeInt(&ptr, msgid);	//-开始填写内容
	elem = NULL;	//-为了从头开始找
	while (ListNextElement(topics, &elem))
	{
		ListNextElement(qoss, &qosElem);
		writeUTF(&ptr, (char*)(elem->content));
		writeChar(&ptr, *(int*)(qosElem->content));
	}
	rc = MQTTPacket_send(net, header, data, datalen, 1);	//-最终把组织好的内容发送出去
	Log(LOG_PROTOCOL, 22, NULL, net->socket, clientID, msgid, rc);
	if (rc != TCPSOCKET_INTERRUPTED)
		free(data);
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Function used in the new packets table to create suback packets.
 * @param aHeader the MQTT header byte
 * @param data the rest of the packet
 * @param datalen the length of the rest of the packet
 * @return pointer to the packet structure
 */
void* MQTTPacket_suback(unsigned char aHeader, char* data, size_t datalen)	//-订阅确认
{//-这里很有可能也包含了服务器的程序,只是上面都是按照客户端的来的,但是库是统一的
	Suback* pack = malloc(sizeof(Suback));
	char* curdata = data;

	FUNC_ENTRY;
	pack->header.byte = aHeader;
	pack->msgId = readInt(&curdata);
	pack->qoss = ListInitialize();	//-为了应答产生了一个新的帧
	while ((size_t)(curdata - data) < datalen)
	{
		int* newint;
		newint = malloc(sizeof(int));
		*newint = (int)readChar(&curdata);	//?读一个字节什么意思
		ListAppend(pack->qoss, newint, sizeof(int));	//?一个字节一个字节增加有什么意思啊
	}
	FUNC_EXIT;
	return pack;
}

/**
Fixed header:
		|7	6	5	4	3	2	1	0|
byte 1	|Message Type	DUP flag	QoS level	RETAIN
Message Type固定为10
用QoS level 1来确认多个取消订阅请求。相应的UNSUBACK消息使用这个MessageID来进行识别。
DUP flag设置为零（ 0）。这意味着这个消息正在被第一次发送。
byte 2	|Remaining Length
Remaining Length是variable header 的长度和Payload的长度. 这可以是一个多字节的字段.
Variable header:
variable header包含一个Message ID，因为SUBSCRIBE消息的QoS级别为1。
Payload:
客户端在payload里取消对一系列主题的取消订阅。字符串都是UTF编码格式，且紧挨着一起打包。
*/
/**
 * Send an MQTT unsubscribe packet down a socket.
 * @param topics list of topics
 * @param msgid the MQTT message id to use
 * @param dup boolean - whether to set the MQTT DUP flag
 * @param socket the open socket to send the data to
 * @param clientID the string client identifier, only used for tracing
 * @return the completion code (e.g. TCPSOCKET_COMPLETE)
 */
int MQTTPacket_send_unsubscribe(List* topics, int msgid, int dup, networkHandles* net, const char* clientID)	//-发送一个MQTT取消订阅帧到一个套接字
{//-这里就是发送一个订阅取消帧,通知服务器什么主题被取消了
	Header header;
	char *data, *ptr;
	int rc = -1;
	ListElement *elem = NULL;
	int datalen;

	FUNC_ENTRY;
	header.bits.type = UNSUBSCRIBE;	//-客户端取消订阅请求
	header.bits.dup = dup;
	header.bits.qos = 1;
	header.bits.retain = 0;

	datalen = 2 + topics->count * 2; // utf length == 2
	while (ListNextElement(topics, &elem))
		datalen += strlen((char*)(elem->content));
	ptr = data = malloc(datalen);

	writeInt(&ptr, msgid);
	elem = NULL;
	while (ListNextElement(topics, &elem))
		writeUTF(&ptr, (char*)(elem->content));
	rc = MQTTPacket_send(net, header, data, datalen, 1);	//-组织好内容发送出去
	Log(LOG_PROTOCOL, 25, NULL, net->socket, clientID, msgid, rc);
	if (rc != TCPSOCKET_INTERRUPTED)
		free(data);
	FUNC_EXIT_RC(rc);
	return rc;
}
