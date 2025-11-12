CC = clang
CFLAGS = -O2 -pipe
LDFLAGS = -lcurl -lpthread
TARGET = nebula
SOURCE = nebula.c

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE) $(LDFLAGS)

clean:
	rm -f $(TARGET)

debug: CFLAGS = -g -DDEBUG -Wall
debug: $(TARGET)

static: LDFLAGS = -lcurl -lpthread -static
static: $(TARGET)

.PHONY: all clean debug static
