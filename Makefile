all: myshell

myshell: myshell.c
	gcc myshell.c -o myshell
	
clean:
	rm -f myshell *.o
