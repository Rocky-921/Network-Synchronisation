all:
	gcc syncserver.c -o syncserver
	gcc syncclient.c -o syncclient

clean:
	rm -f syncserver syncclient