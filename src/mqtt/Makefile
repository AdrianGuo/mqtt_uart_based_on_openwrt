
all: mqtt_client.a

mqtt_client.a: Clients.o Heap.o LinkedList.o Log.o Messages.o MQTTClient.o MQTTPacket.o MQTTPacketOut.o MQTTPersistence.o MQTTPersistenceDefault.o MQTTProtocolClient.o MQTTProtocolOut.o Socket.o SocketBuffer.o SSLSocket.o StackTrace.o Thread.o Tree.o utf-8.o mqtt_client.o
	$(AR) rc $@ Clients.o Heap.o LinkedList.o Log.o Messages.o MQTTClient.o MQTTPacket.o MQTTPacketOut.o MQTTPersistence.o MQTTPersistenceDefault.o MQTTProtocolClient.o MQTTProtocolOut.o Socket.o SocketBuffer.o SSLSocket.o StackTrace.o Thread.o Tree.o utf-8.o mqtt_client.o

clean:
	rm -f *.o mqtt_client.a

.PHONY: all clean
