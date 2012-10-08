$(CC)=gcc

all: raw-sender

raw-sender: raw-sender.o
	$(CC) -o raw-sender raw-sender.o
