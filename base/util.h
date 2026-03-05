#pragma once
#include <csignal>
#include <cerrno>
#include <stdexcept>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/epoll.h>

void addsig(int sig, void (*handler)(int), bool restart = true);

void setnonblocking(int fd);
void addfd(int efd, int fd, bool one_shot);
void delfd(int efd, int fd);
void modfd(int efd, int fd, int ev);