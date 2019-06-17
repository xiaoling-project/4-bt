$ make
Makefile
	CC=gcc
	CFLAGS= -Linclude -Wll -g -DDEBUG
	LDFLAGS=-L.lib -Wl,-rpath=.lib -Wl,-rpath=/usr/local/lib

	ttorrent; main.o parse_metafile.o tracker.o bitfield.o shal.o message.o peer.o data.o policy.o torrent.o bterror.olog.o signal_hander.o
		$(CC) -o $@ $(LDFLAGS) $^ -ldl

	clean;
		rm -rf *.o ttorrent
