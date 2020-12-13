FLAGS=-g -pthread

vip-pull: vip-pull.o json-parse.o
	gcc $(FLAGS) -o vip-pull vip-pull.o json-parse.o

vip-pull.o: vip-pull.c json-parse.o
	gcc $(FLAGS) -c vip-pull.c

json-parse.o: json-parse.c json-parse.h
	gcc $(FLAGS) -c json-parse.c

.PHONY : clean
clean:
	-rm *.o
	-rm *.tmp
	-rm tmp*

