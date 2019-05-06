PORT = 54856
FLAGS = -DPORT=$(PORT) -Wall -g -std=gnu99 

all: wordsrv

wordsrv : wordsrv.o socket.o gameplay.o
	gcc $(FLAGS) -o $@ $^

%.o : %.c socket.h gameplay.h
	gcc $(FLAGS) -c $<

clean : 
	rm *.o wordsrv
