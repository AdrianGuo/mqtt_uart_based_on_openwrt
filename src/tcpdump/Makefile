all: tcpdump.a

tcpdump.a: tcpdump.o
	$(AR) rc $@ tcpdump.o

clean:
	rm -f *.o tcpdump.a
