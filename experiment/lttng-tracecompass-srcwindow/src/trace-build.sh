#!/bin/bash
sudo rm -rf hello app
sudo gcc -c -g -I. hello-tp.c
sudo gcc -c -g hello.c
sudo gcc -o hello hello.o hello-tp.o -llttng-ust -ldl

sudo cc -c -g -I. tp.c
sudo cc -c -g app.c
sudo cc -o app tp.o app.o -llttng-ust -ldl