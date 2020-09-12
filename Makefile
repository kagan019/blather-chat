FLAGS = -Wall -g
LIBS = -lpthread
CC = gcc $(FLAGS)

UTILS = simpio.o util.o server_funcs.o client_funcs.o $(LIBS)

all : bl_client bl_server bl_showlog

%.o : %.c blather.h

bl_client : bl_client.o $(UTILS)
	$(CC) -o $@ $^

bl_server : bl_server.o $(UTILS)
	$(CC) -o $@ $^

bl_showlog : bl_showlog.o $(UTILS)
	$(CC) -o $@ $^

clean :
	rm -f bl_client bl_server bl_showlog *.o *.log *.fifo

include test_Makefile