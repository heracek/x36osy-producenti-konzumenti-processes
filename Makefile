# all after symbol '#' is comment

# === which communication library to use ===
CC	=	g++
CFLAGS	=      
LIBS	=	-lpthread
# -lrt

default:	main

main:main.cpp
	$(CC) $(CFLAGS) -o main main.cpp $(LIBS)

run:main
	./main 5 3 4 10

clear:
	\rm main 

