
all: cpcp

build/file_copy.o: file_copy.cpp
	$(CC) -c $< -o $@

build/main.o: main.cpp
	$(CC) -c $< -o $@

cpcp: build/file_copy.o build/main.o
	$(CC) $^ -o $@

