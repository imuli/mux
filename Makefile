CFLAGS=-Wall -Os -g
mux: mux.c
	$(CC) $(CFLAGS) -o $@ `pkg-config fuse --cflags --libs` $^

clean:
	rm -f *.o mux
