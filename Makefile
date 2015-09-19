
CC=gcc
CFLAGS=-Wall -g

PROGS= minishell

all: $(PROGS)

% : %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(PROGS) *.o *~

