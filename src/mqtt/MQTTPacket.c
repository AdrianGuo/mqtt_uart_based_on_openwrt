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
 *******************************************************************************/
//-和MQTTPacketOut.c文件模式是一样的
/**
 * @file
 * \brief functions to deal with reading and writing of MQTT packets from and to sockets
 *
 * Some other related functions are in the MQTTPacketOut module
 */

#include "MQTTPacket.h"
#include "Log.h"
#if !defined(NO_PERSISTENCE)
	#include "MQTTPersistence.h"
#endif
#include "Messages.h"
#include "StackTrace.h"

#include <stdlib.h>
#include <string.h>

#include "Heap.h"

#if !defined(min)
#define min(A,B) ( (A) < (B) ? (A):(B))
#endif
/*
0->保留；
1->连接请求：客户端请求连接到服务器；
2->连接确认；
3->发布消息；
4->发布确认；
5->发布信息收到（确保分发的第1部分）；
6->发布信息分发（确保分发的第2部分）；
7->发布完成 （确保分发的第3部分）；
8->客户端订阅请求；
9->订阅确认；
10->客户端取消订阅请求；
11->取消订阅确认；
12->ping请求；
13->ping响应；
14->客户端正在断开连接；
15->保留；
*/
/**
 * List of the predefined MQTT v3 packet names.
 */
static char* packet_names[] =
{
	"RESERVED", "CONNECT", "CONNACK", "PUBLISH", "PUBACK", "PUBREC", "PUBREL",
	"PUBCOMP", "SUBSCRIBE", "SUBACK", "UNSUBSCRIBE", "UNSUBACK",
	"PINGREQ", "PINGRESP", "DISCONNECT"
};

char** MQTTClient_packet_names = packet_names;


/**
 * Converts an MQTT packet code into its name
 * @param ptype packet code
 * @return the corresponding string, or "UNKNOWN"
 */
char* MQTTPacket_name(int ptype)
{
	return (ptype >= 0 && ptype <= DISCONNECT) ? packet_names[ptype] : "UNKNOWN";
}

/**
 * Array of functions to build packets, indexed according to packet code
 */
pf new_packets[] =
{
	NULL,	/**< reserved */
	NULL,	/**< MQTTPacket_connect*/
	MQTTPacket_connack, /**< CONNACK */
	MQTTPacket_publish,	/**< PUBLISH */
	MQTTPacket_ack, /**< PUBACK */
	MQTTPacket_ack, /**< PUBREC */
	MQTTPacket_ack, /**< PUBREL */
	MQTTPacket_ack, /**< PUBCOMP */
	NULL, /**< MQTTPacket_subscribe*/
	MQTTPacket_suback, /**< SUBACK */
	NULL, /**< MQTTPacket_unsubscribe*/
	MQTTPacket_ack, /**< UNSUBACK */
	MQTTPacket_header_only, /**< PINGREQ */
	MQTTPacket_header_only, /**< PINGRESP */
	MQTTPacket_header_only  /**< DISCONNECT */
};


/**
 * Reads one MQTT packet from a socket.
 * @param socket a socket from which to read an MQTT packet
 * @param error pointer to the error code which is completed if no packet is returned
 * @return the packet structure or NULL if there was an error
 */
void* MQTTPacket_Factory(networkHandles* net, int* error)	//-读一个来自套接字的MQTT帧
{
	char* data = NULL;
	static Header header;
	int remaining_length, ptype;
	size_t remaining_length_new;
	void* pack = NULL;
	int actual_len = 0;

	FUNC_ENTRY;
	*error = SOCKET_ERROR;  /* indicate whether an error occurred, or not */

	/* read the packet data from the socket */
#if defined(OPENSSL)
	*error = (net->ssl) ? SSLSocket_getch(net->ssl, net->socket, &header.byte) : Socket_getch(net->socket, &header.byte); 
#else
	*error = Socket_getch(net->socket, &header.byte);	//-里面有最底层的接收库函数
#endif
	if (*error != TCPSOCKET_COMPLETE)   /* first byte is the header byte */
		goto exit; /* packet not read, *error indicates whether SOCKET_ERROR occurred */

	/* now read the remaining length, so we know how much more to read */
	if ((*error = MQTTPacket_decode(net, &remaining_length)) != TCPSOCKET_COMPLETE)	//-去到最底层读TCP数据
		goto exit; /* packet not read, *error indicates whether SOCKET_ERROR occurred */

	/* now read the rest, the variable header and payload */
#if defined(OPENSSL)
	data = (net->ssl) ? SSLSocket_getdata(net->ssl, net->socket, remaining_length, &actual_len) : 
						Socket_getdata(net->socket, remaining_length, &actual_len);
#else
	data = Socket_getdata(net->socket, remaining_length, &actual_len);	//-里面有最底层的接收库函数
#endif
	if (data == NULL)
	{
		*error = SOCKET_ERROR;
		goto exit; /* socket error */
	}

	if (actual_len != remaining_length)
		*error = TCPSOCKET_INTERRUPTED;
	else
	{
		ptype = header.bits.type;
		if (ptype < CONNECT || ptype > DISCONNECT || new_packets[ptype] == NULL)
			Log(TRACE_MIN, 2, NULL, ptype);
		else
		{
			if ((pack = (*new_packets[ptype])(header.byte, data, remaining_length)) == NULL)
				*error = BAD_MQTT_PACKET;
#if !defined(NO_PERSISTENCE)
			else if (header.bits.type == PUBLISH && header.bits.qos == 2)
			{
				int buf0len;
				char *buf = malloc(10);
				buf[0] = header.byte;
				buf0len = 1 + MQTTPacket_encode(&buf[1], remaining_length);
				remaining_length_new = remaining_length;
				*error = MQTTPersistence_put(net->socket, buf, buf0len, 1,
					&data, &remaining_length_new, header.bits.type, ((Publish *)pack)->msgId, 1);	//-把接收到的内容进行持久化存储
				free(buf);
			}
#endif
		}
	}
	if (pack)
		time(&(net->lastReceived));
exit:
	FUNC_EXIT_RC(*error);
	return pack;
}


/**
 * Sends an MQTT packet in one system call write
 * @param socket the socket to which to write the data
 * @param header the one-byte MQTT header
 * @param buffer the rest of the buffer to write (not including remaining length)
 * @param buflen the length of the data in buffer to be written
 * @return the completion code (TCPSOCKET_COMPLETE etc)
 */
int MQTTPacket_send(networkHandles* net, Header header, char* buffer, size_t buflen, int free)
{
	int rc, buf0len;
	char *buf;

	FUNC_ENTRY;
	buf = malloc(10);
	buf[0] = header.byte;
	buf0len = 1 + MQTTPacket_encode(&buf[1], buflen);	//-缓冲区待发送的数据长度,进行格式转化
#if !defined(NO_PERSISTENCE)
	if (header.bits.type == PUBREL)	//-判断是否是发布消息分发
	{
		char* ptraux = buffer;
		int msgId = readInt(&ptraux);
		rc = MQTTPersistence_put(net->socket, buf, buf0len, 1, &buffer, &buflen,
			header.bits.type, msgId, 0);	//-如果发送的消息需要再次发送的话,就先进行持久存储,防止掉电丢失不能再发了
	}
#endif

#if defined(OPENSSL)
	if (net->ssl)
		rc = SSLSocket_putdatas(net->ssl, net->socket, buf, buf0len, 1, &buffer, &buflen, &free);
	else
#endif
		rc = Socket_putdatas(net->socket, buf, buf0len, 1, &buffer, &buflen, &free);	//-这个里面实现了数据发送
		
	if (rc == TCPSOCKET_COMPLETE)
		time(&(net->lastSent));		//-记录最后一次发送的时间
	
	if (rc != TCPSOCKET_INTERRUPTED)
	  free(buf);	//-发送完成了空间就可以释放了

	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Sends an MQTT packet from multiple buffers in one system call write
 * @param socket the socket to which to write the data
 * @param header the one-byte MQTT header
 * @param count the number of buffers
 * @param buffers the rest of the buffers to write (not including remaining length)
 * @param buflens the lengths of the data in the array of buffers to be written
 * @return the completion code (TCPSOCKET_COMPLETE etc)
 */
int MQTTPacket_sends(networkHandles* net, Header header, int count, char** buffers, size_t* buflens, int* frees)
{
	int i, rc, buf0len, total = 0;
	char *buf;

	FUNC_ENTRY;
	buf = malloc(10);
	buf[0] = header.byte;	//-包含Message Type（消息类型）和Flags（DUP，QoS级别，RETAIN）字段
	for (i = 0; i < count; i++)
		total += buflens[i];	//-计算出不同缓冲区内的总字节数
	buf0len = 1 + MQTTPacket_encode(&buf[1], total);	//-后面把总字节数转化为MQTT帧中剩余长度字段,最终表示的是固定头的长度
#if !defined(NO_PERSISTENCE)
	if (header.bits.type == PUBLISH && header.bits.qos != 0)	//-根据固定头中不同帧的类型开始组织数据
	{   /* persist PUBLISH QoS1 and Qo2 */
		char *ptraux = buffers[2];
		int msgId = readInt(&ptraux);
		rc = MQTTPersistence_put(net->socket, buf, buf0len, count, buffers, buflens,
			header.bits.type, msgId, 0);
	}
#endif
#if defined(OPENSSL)
	if (net->ssl)
		rc = SSLSocket_putdatas(net->ssl, net->socket, buf, buf0len, count, buffers, buflens, frees);
	else
#endif
		rc = Socket_putdatas(net->socket, buf, buf0len, count, buffers, buflens, frees);
		
	if (rc == TCPSOCKET_COMPLETE)
		time(&(net->lastSent));
	
	if (rc != TCPSOCKET_INTERRUPTED)
	  free(buf);
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Encodes the message length according to the MQTT algorithm
 * @param buf the buffer into which the encoded data is written
 * @param length the length to be encoded
 * @return the number of bytes written to buffer
 */
int MQTTPacket_encode(char* buf, int length)	//-每个字节的7位用于编码Remaining Length数据，第8位表示在下面还有值。
{
	int rc = 0;

	FUNC_ENTRY;
	do
	{
		char d = length % 128;
		length /= 128;
		/* if there are more digits to encode, set the top bit of this digit */
		if (length > 0)
			d |= 0x80;
		buf[rc++] = d;
	} while (length > 0);
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Decodes the message length according to the MQTT algorithm
 * @param socket the socket from which to read the bytes
 * @param value the decoded length returned
 * @return the number of bytes read from the socket
 */
int MQTTPacket_decode(networkHandles* net, int* value)	//-解码
{
	int rc = SOCKET_ERROR;
	char c;
	int multiplier = 1;
	int len = 0;
#define MAX_NO_OF_REMAINING_LENGTH_BYTES 4

	FUNC_ENTRY;
	*value = 0;
	do
	{
		if (++len > MAX_NO_OF_REMAINING_LENGTH_BYTES)
		{
			rc = SOCKET_ERROR;	/* bad data */
			goto exit;
		}
#if defined(OPENSSL)
		rc = (net->ssl) ? SSLSocket_getch(net->ssl, net->socket, &c) : Socket_getch(net->socket, &c);
#else
		rc = Socket_getch(net->socket, &c);	//-里面从底层直接读取了数据
#endif
		if (rc != TCPSOCKET_COMPLETE)
				goto exit;
		*value += (c & 127) * multiplier;
		multiplier *= 128;
	} while ((c & 128) != 0);
exit:
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Calculates an integer from two bytes read from the input buffer
 * @param pptr pointer to the input buffer - incremented by the number of bytes used & returned
 * @return the integer value calculated
 */
int readInt(char** pptr)	//-计算一个整数,把两个内存单元的数转化为一个整数
{
	char* ptr = *pptr;
	int len = 256*((unsigned char)(*ptr)) + (unsigned char)(*(ptr+1));
	*pptr += 2;
	return len;
}


/**
 * Reads a "UTF" string from the input buffer.  UTF as in the MQTT v3 spec which really means
 * a length delimited string.  So it reads the two byte length then the data according to
 * that length.  The end of the buffer is provided too, so we can prevent buffer overruns caused
 * by an incorrect length.
 * @param pptr pointer to the input buffer - incremented by the number of bytes used & returned
 * @param enddata pointer to the end of the buffer not to be read beyond
 * @param len returns the calculcated value of the length bytes read
 * @return an allocated C string holding the characters read, or NULL if the length read would
 * have caused an overrun.
 *
 */
char* readUTFlen(char** pptr, char* enddata, int* len)
{
	char* string = NULL;

	FUNC_ENTRY;
	if (enddata - (*pptr) > 1) /* enough length to read the integer? */
	{
		*len = readInt(pptr);
		if (&(*pptr)[*len] <= enddata)
		{
			string = malloc(*len+1);
			memcpy(string, *pptr, *len);
			string[*len] = '\0';
			*pptr += *len;
		}
	}
	FUNC_EXIT;
	return string;
}


/**
 * Reads a "UTF" string from the input buffer.  UTF as in the MQTT v3 spec which really means
 * a length delimited string.  So it reads the two byte length then the data according to
 * that length.  The end of the buffer is provided too, so we can prevent buffer overruns caused
 * by an incorrect length.
 * @param pptr pointer to the input buffer - incremented by the number of bytes used & returned
 * @param enddata pointer to the end of the buffer not to be read beyond
 * @return an allocated C string holding the characters read, or NULL if the length read would
 * have caused an overrun.
 */
char* readUTF(char** pptr, char* enddata)
{
	int len;
	return readUTFlen(pptr, enddata, &len);
}


/**
 * Reads one character from the input buffer.
 * @param pptr pointer to the input buffer - incremented by the number of bytes used & returned
 * @return the character read
 */
unsigned char readChar(char** pptr)
{
	unsigned char c = **pptr;
	(*pptr)++;
	return c;
}


/**
 * Writes one character to an output buffer.
 * @param pptr pointer to the output buffer - incremented by the number of bytes used & returned
 * @param c the character to write
 */
void writeChar(char** pptr, char c)
{
	**pptr = c;
	(*pptr)++;
}


/**
 * Writes an integer as 2 bytes to an output buffer.
 * @param pptr pointer to the output buffer - incremented by the number of bytes used & returned
 * @param anInt the integer to write
 */
void writeInt(char** pptr, int anInt)	//-把整数转化为字节保存
{
	**pptr = (char)(anInt / 256);
	(*pptr)++;
	**pptr = (char)(anInt % 256);
	(*pptr)++;
}


/**
 * Writes a "UTF" string to an output buffer.  Converts C string to length-delimited.
 * @param pptr pointer to the output buffer - incremented by the number of bytes used & returned
 * @param string the C string to write
 */
void writeUTF(char** pptr, const char* string)	//-是进行字符编码转换吗
{
	int len = strlen(string);
	writeInt(pptr, len);
	memcpy(*pptr, string, len);
	*pptr += len;
}


/**
 * Function used in the new packets table to create packets which have only a header.
 * @param aHeader the MQTT header byte
 * @param data the rest of the packet
 * @param datalen the length of the rest of the packet
 * @return pointer to the packet structure
 */
void* MQTTPacket_header_only(unsigned char aHeader, char* data, size_t datalen)	//-此函数仅仅实现其中一个模块功能
{
	static unsigned char header = 0;
	header = aHeader;
	return &header;
}


/**
 * Send an MQTT disconnect packet down a socket.
 * @param socket the open socket to send the data to
 * @return the completion code (e.g. TCPSOCKET_COMPLETE)
 */
int MQTTPacket_send_disconnect(networkHandles *net, const char* clientID)	//-客户端向服务器发送断开连接通知帧
{
	Header header;
	int rc = 0;

	FUNC_ENTRY;
	header.byte = 0;
	header.bits.type = DISCONNECT;	//-记录发送帧的类型
	rc = MQTTPacket_send(net, header, NULL, 0, 0);	//-这个网络里面有一个套接字,通过这个套接字把信息发送出去
	Log(LOG_PROTOCOL, 28, NULL, net->socket, clientID, rc);
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Function used in the new packets table to create publish packets.
 * @param aHeader the MQTT header byte
 * @param data the rest of the packet
 * @param datalen the length of the rest of the packet
 * @return pointer to the packet structure
 */
void* MQTTPacket_publish(unsigned char aHeader, char* data, size_t datalen)	//-发布
{
	Publish* pack = malloc(sizeof(Publish));
	char* curdata = data;
	char* enddata = &data[datalen];

	FUNC_ENTRY;
	pack->header.byte = aHeader;
	if ((pack->topic = readUTFlen(&curdata, enddata, &pack->topiclen)) == NULL) /* Topic name on which to publish */
	{
		free(pack);
		pack = NULL;
		goto exit;
	}
	if (pack->header.bits.qos > 0)  /* Msgid only exists for QoS 1 or 2 */
		pack->msgId = readInt(&curdata);
	else
		pack->msgId = 0;
	pack->payload = curdata;
	pack->payloadlen = datalen-(curdata-data);
exit:
	FUNC_EXIT;
	return pack;
}


/**
 * Free allocated storage for a publish packet.
 * @param pack pointer to the publish packet structure
 */
void MQTTPacket_freePublish(Publish* pack)
{
	FUNC_ENTRY;
	if (pack->topic != NULL)
		free(pack->topic);
	free(pack);
	FUNC_EXIT;
}


/**
 * Send an MQTT acknowledgement packet down a socket.
 * @param type the MQTT packet type e.g. SUBACK
 * @param msgid the MQTT message id to use
 * @param dup boolean - whether to set the MQTT DUP flag
 * @param net the network handle to send the data to
 * @return the completion code (e.g. TCPSOCKET_COMPLETE)
 */
int MQTTPacket_send_ack(int type, int msgid, int dup, networkHandles *net)	//-怎么感觉重复了
{
	Header header;
	int rc;
	char *buf = malloc(2);
	char *ptr = buf;

	FUNC_ENTRY;
	header.byte = 0;
	header.bits.type = type;
	header.bits.dup = dup;
	if (type == PUBREL)
	    header.bits.qos = 1;
	writeInt(&ptr, msgid);
	if ((rc = MQTTPacket_send(net, header, buf, 2, 1)) != TCPSOCKET_INTERRUPTED)
		free(buf);
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Send an MQTT PUBACK packet down a socket.
 * @param msgid the MQTT message id to use
 * @param socket the open socket to send the data to
 * @param clientID the string client identifier, only used for tracing
 * @return the completion code (e.g. TCPSOCKET_COMPLETE)
 */
int MQTTPacket_send_puback(int msgid, networkHandles* net, const char* clientID)	//-发布确认
{
	int rc = 0;

	FUNC_ENTRY;
	rc =  MQTTPacket_send_ack(PUBACK, msgid, 0, net);
	Log(LOG_PROTOCOL, 12, NULL, net->socket, clientID, msgid, rc);
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Free allocated storage for a suback packet.
 * @param pack pointer to the suback packet structure
 */
void MQTTPacket_freeSuback(Suback* pack)	//-信息使用过了之后就释放掉
{
	FUNC_ENTRY;
	if (pack->qoss != NULL)
		ListFree(pack->qoss);
	free(pack);
	FUNC_EXIT;
}


/**
 * Send an MQTT PUBREC packet down a socket.
 * @param msgid the MQTT message id to use
 * @param socket the open socket to send the data to
 * @param clientID the string client identifier, only used for tracing
 * @return the completion code (e.g. TCPSOCKET_COMPLETE)
 */
int MQTTPacket_send_pubrec(int msgid, networkHandles* net, const char* clientID)	//-发布信息收到（确保分发的第1部分）
{
	int rc = 0;

	FUNC_ENTRY;
	rc =  MQTTPacket_send_ack(PUBREC, msgid, 0, net);
	Log(LOG_PROTOCOL, 13, NULL, net->socket, clientID, msgid, rc);
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Send an MQTT PUBREL packet down a socket.
 * @param msgid the MQTT message id to use
 * @param dup boolean - whether to set the MQTT DUP flag
 * @param socket the open socket to send the data to
 * @param clientID the string client identifier, only used for tracing
 * @return the completion code (e.g. TCPSOCKET_COMPLETE)
 */
int MQTTPacket_send_pubrel(int msgid, int dup, networkHandles* net, const char* clientID)	//-发布信息分发（确保分发的第2部分）
{
	int rc = 0;

	FUNC_ENTRY;
	rc = MQTTPacket_send_ack(PUBREL, msgid, dup, net);
	Log(LOG_PROTOCOL, 16, NULL, net->socket, clientID, msgid, rc);
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Send an MQTT PUBCOMP packet down a socket.
 * @param msgid the MQTT message id to use
 * @param socket the open socket to send the data to
 * @param clientID the string client identifier, only used for tracing
 * @return the completion code (e.g. TCPSOCKET_COMPLETE)
 */
int MQTTPacket_send_pubcomp(int msgid, networkHandles* net, const char* clientID)	//-发布完成（确保分发的第3部分）
{
	int rc = 0;

	FUNC_ENTRY;
	rc = MQTTPacket_send_ack(PUBCOMP, msgid, 0, net);
	Log(LOG_PROTOCOL, 18, NULL, net->socket, clientID, msgid, rc);
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Function used in the new packets table to create acknowledgement packets.
 * @param aHeader the MQTT header byte
 * @param data the rest of the packet
 * @param datalen the length of the rest of the packet
 * @return pointer to the packet structure
 */
void* MQTTPacket_ack(unsigned char aHeader, char* data, size_t datalen)
{
	Ack* pack = malloc(sizeof(Ack));
	char* curdata = data;

	FUNC_ENTRY;
	pack->header.byte = aHeader;
	pack->msgId = readInt(&curdata);
	FUNC_EXIT;
	return pack;
}


/**
 * Send an MQTT PUBLISH packet down a socket.
 * @param pack a structure from which to get some values to use, e.g topic, payload
 * @param dup boolean - whether to set the MQTT DUP flag
 * @param qos the value to use for the MQTT QoS setting
 * @param retained boolean - whether to set the MQTT retained flag
 * @param socket the open socket to send the data to
 * @param clientID the string client identifier, only used for tracing
 * @return the completion code (e.g. TCPSOCKET_COMPLETE)
 */
int MQTTPacket_send_publish(Publish* pack, int dup, int qos, int retained, networkHandles* net, const char* clientID)	//-发送发布帧
{
	Header header;
	char *topiclen;
	int rc = -1;

	FUNC_ENTRY;
	topiclen = malloc(2);

	header.bits.type = PUBLISH;
	header.bits.dup = dup;
	header.bits.qos = qos;
	header.bits.retain = retained;
	if (qos > 0)
	{
		char *buf = malloc(2);
		char *ptr = buf;
		char* bufs[4] = {topiclen, pack->topic, buf, pack->payload};
		size_t lens[4] = {2, strlen(pack->topic), 2, pack->payloadlen};
		int frees[4] = {1, 0, 1, 0};

		writeInt(&ptr, pack->msgId);
		ptr = topiclen;
		writeInt(&ptr, lens[1]);
		rc = MQTTPacket_sends(net, header, 4, bufs, lens, frees);
		if (rc != TCPSOCKET_INTERRUPTED)
			free(buf);
	}
	else
	{
		char* ptr = topiclen;
		char* bufs[3] = {topiclen, pack->topic, pack->payload};
		size_t lens[3] = {2, strlen(pack->topic), pack->payloadlen};
		int frees[3] = {1, 0, 0};

		writeInt(&ptr, lens[1]);
		rc = MQTTPacket_sends(net, header, 3, bufs, lens, frees);
	}
	if (rc != TCPSOCKET_INTERRUPTED)
		free(topiclen);
	if (qos == 0)
		Log(LOG_PROTOCOL, 27, NULL, net->socket, clientID, retained, rc);
	else
		Log(LOG_PROTOCOL, 10, NULL, net->socket, clientID, pack->msgId, qos, retained, rc,
				min(20, pack->payloadlen), pack->payload);
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Free allocated storage for a various packet tyoes
 * @param pack pointer to the suback packet structure
 */
void MQTTPacket_free_packet(MQTTPacket* pack)	//-释放帧什么意思
{
	FUNC_ENTRY;
	if (pack->header.bits.type == PUBLISH)
		MQTTPacket_freePublish((Publish*)pack);
	/*else if (pack->header.type == SUBSCRIBE)
		MQTTPacket_freeSubscribe((Subscribe*)pack, 1);
	else if (pack->header.type == UNSUBSCRIBE)
		MQTTPacket_freeUnsubscribe((Unsubscribe*)pack);*/
	else
		free(pack);
	FUNC_EXIT;
}
