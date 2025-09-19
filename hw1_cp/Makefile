
all: cpcp

file_copy.o: file_copy.cpp
	$(CC) -c $< -o $@

main.o: main.cpp
	$(CC) -c $< -o $@

cpcp: file_copy.o main.o
	$(CC) $^ -o $@

