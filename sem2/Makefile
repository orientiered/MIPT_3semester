
all: fork101.out fork102.out sleepsort.out timeit

fork101.o: fork101.c
	$(CC) -c $< -o $@

fork102.o: fork102.c
	$(CC) -c $< -o $@

sleepsort.o: sleepsort.c
	$(CC) -c $< -o $@

timeit.o: timeit.c
	$(CC) -c $< -o $@

fork101.out: fork101.o
	$(CC) $^ -o $@

fork102.out: fork102.o
	$(CC) $^ -o $@

sleepsort.out: sleepsort.o
	$(CC) $^ -o $@

timeit: timeit.o
	$(CC) $^ -o $@

