CC := gcc
CFLAGS := -Wall -Wextra

all: server client markdown.o

server:
	$(CC) -o server source/server.c source/markdown.c source/helper_functions.c $(CFLAGS) -lpthread

client:
	$(CC) -o client source/client.c source/markdown.c source/helper_functions.c $(CFLAGS) -lpthread

helper_functions.o:
	$(CC) -c source/helper_functions.c -o helper_functions.o $(CFLAGS)

mark.o:
	$(CC) -c source/markdown.c -o mark.o $(CFLAGS)

markdown.o: helper_functions.o mark.o
	ld -r -o markdown.o helper_functions.o mark.o

clean:
	rm -f *.o