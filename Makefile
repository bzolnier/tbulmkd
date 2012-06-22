
CC=arm-linux-gnueabi-gcc

all: tbulmkd proxy_shm

tbulmkd: tbulmkd.c common.c
	$(CC) -o $@ $< common.c -lpthread -lrt

proxy_shm: proxy_shm.c common.c
	$(CC) -o $@ $< common.c -lpthread -lrt

clean:
	rm -f tbulmkd proxy_shm
