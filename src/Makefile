EXEC = dreamflower_app
OBJS = dreamflower_app.o
libobjs := uart1.o uart_1_app.o gpio.o Daemon.o fdebug.o calendar.o tcpdump.o thread.o mqtt_publish.o mqtt_subscribe.o

#-???????????,????????
LIBOBJSA = mqtt/mqtt_client.a

all: $(EXEC)

mqtt/mqtt_client.a:
	cd mqtt && $(MAKE) mqtt_client.a

$(EXEC): $(OBJS) $(libobjs) $(LIBOBJSA)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(libobjs) $(LIBOBJSA) -lpcap -lpthread

clean:
	rm -f rbcfg *.o $(EXEC)
