CC=g++
PRE=-std=c++98 -pedantic
NAME=ftpclient

build: client.cpp
	$(CC) $(PRE) client.cpp -o $(NAME)


