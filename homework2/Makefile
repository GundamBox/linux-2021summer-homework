CFLAG=
INCL_DEPS = -I.


harris:
	gcc $(CFLAG) -DUSE_HARRIS_METHOD -Wall -o benchmark benchmark.c lf_list.c hazard_pointer.c $(INCL_DEPS) -lpthread -g -fsanitize=thread 

origin:
	gcc $(CFLAG) -Wall -o benchmark benchmark.c lf_list.c hazard_pointer.c  -lpthread -g -fsanitize=thread 

.PHONY: default
default: origin ;