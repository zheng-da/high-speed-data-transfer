CC=gcc
LDFLAGS=-lpthread

all: raw-sender raw-receiver mmap-sender

raw-sender: raw-sender.o
	$(CC) -o raw-sender raw-sender.o

raw-receiver: raw-receiver.o
	$(CC) -o raw-receiver raw-receiver.o

mmap-sender: mmap-sender.o
	$(CC) -o mmap-sender mmap-sender.o $(LDFLAGS)
