CC = cc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L
LDFLAGS =
OBJS = main.o controller.o worker.o analysis.o ipc.o

all: analyzer

analyzer: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

main.o: main.c controller.h protocol.h
controller.o: controller.c controller.h ipc.h protocol.h worker.h
worker.o: worker.c worker.h analysis.h ipc.h protocol.h
analysis.o: analysis.c analysis.h protocol.h
ipc.o: ipc.c ipc.h

clean:
	rm -f analyzer $(OBJS)

.PHONY: all clean