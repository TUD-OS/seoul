CC=g++
CFLAGS=-g -O3 -std=gnu++11 -gdwarf-2 -ggdb3
CFLAGS_NOOPT=-g -O0 -std=gnu++11 -gdwarf-2 -ggdb3
INCLUDES=-I ../include/ -I ../unix/include/
LIBS=-pthread

all: pic lapic ioapic

pic: pic.o logging.o params.o pic8259.o
	$(CC) $(CFLAGS) $(INCLUDES) $(LIBS) -DPICTEST \
		main.cc pic.o \
		params.o logging.o pic8259.o -o pictest.bin

pic.o: pic.cc pic.h
	$(CC) $(CFLAGS) $(INCLUDES) $(LIBS) -DPICTEST \
		pic.cc -c
params.o: ../unix/params.cc
	$(CC) $(CFLAGS) $(INCLUDES) $(LIBS) -DPICTEST \
		../unix/params.cc -c
logging.o: logging.cc
	$(CC) $(CFLAGS) $(INCLUDES) $(LIBS) -DPICTEST \
		logging.cc -c
pic8259.o: ../model/pic8259.cc
	$(CC) $(CFLAGS) $(INCLUDES) $(LIBS) -DPICTEST \
		../model/pic8259.cc -c

runpic: pic
	@./pictest.bin 2> log.txt

ioapic:
	$(CC) $(CFLAGS) $(INCLUDES) $(LIBS) -DIOAPICTEST \
		main.cc ioapic.cc \
		logging.cc ../unix/params.cc ../model/ioapic.cc -o ioapictest.bin

runioapic: ioapic
	./ioapictest.bin 2> log.txt

lapic: logging.o params.o lapic.cc lapic.h
	$(CC) $(CFLAGS) $(INCLUDES) $(LIBS) -DLAPICTEST \
                main.cc lapic.cc \
		../model/ioapic.cc ../model/lapic.cc ../model/vcpu.cc \
                params.o logging.o -o lapictest.bin

runlapic: lapic
	./lapictest.bin 2> log.txt

clean:
	rm -f *.bin *.txt *.o
