CFLAGS := -Wall -Iinclude/
LDFLAGS := -pthread

OBJS := \
	main.o \
	ipc.o \
	tcp_posix.o \
	libvnc.o

all: vncserver

clean:
	rm -f vncserver $(OBJS)

vncserver: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o:%.c
	$(CC) $(CFLAGS) -c -o $@ $<
