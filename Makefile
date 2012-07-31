
CC=arm-linux-gnueabi-gcc
#CFLAGS += -DDEBUG=0
CFLAGS += -DDEBUG=1

all: tbulmkd proxy_shm m

tbulmkd: tbulmkd.c common.c cgroups.c
	$(CC) -o $@ $< common.c cgroups.c $(CFLAGS) -lpthread -lrt

proxy_shm: proxy_shm.c common.c
	$(CC) -o $@ $< common.c $(CFLAGS) -lpthread -lrt

m: m.c
	$(CC) -o $@ $< $(CFLAGS)

clean:
	rm -f tbulmkd proxy_shm m
