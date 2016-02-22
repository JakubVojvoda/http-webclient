#####################################################
#
#  Simple HTTP client
#  by Jakub Vojvoda [vojvoda@swdeveloper.sk]
#  2013
#
#####################################################

NAME=webclient
CC=gcc
CFLAGS=-Wall -g -O

all:
	gcc $(NAME).c $(CFLAGS) -o $(NAME)

clean:
	rm -rf $(NAME).o $(NAME) *~
