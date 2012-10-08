$(CC)=gcc

all: raw-sender raw-receiver

raw-sender: raw-sender.o
	$(CC) -o raw-sender raw-sender.o

raw-receiver: raw-receiver.o
	$(CC) -o raw-receiver raw-receiver.o
