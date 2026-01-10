CC = gcc
CFLAGS = -Wall -pthread

all: main kasjer przewodnik turysta

main: main.c common.h
	$(CC) $(CFLAGS) -o main main.c

kasjer: kasjer.c common.h
	$(CC) $(CFLAGS) -o kasjer kasjer.c

przewodnik: przewodnik.c common.h
	$(CC) $(CFLAGS) -o przewodnik przewodnik.c

turysta: turysta.c common.h
	$(CC) $(CFLAGS) -o turysta turysta.c

clean:
	rm -f main kasjer przewodnik turysta park_log.txt
	ipcrm -a

run: all
	./main