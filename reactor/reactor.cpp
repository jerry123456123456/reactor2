#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <sys/time.h>
#include <fcntl.h>   //文件控制
#include <sys/stat.h>  
#include <sys/types.h>

int client_count = 0;  //当前连接的客户端的数量

#define BUFFER_LENGTH 1024
#define EPOLL_SIZE 1024

int epfd = 0;
typedef int (*RCALLBACK)(int fd);

//存储每个连接相关的信息
struct conn_item{
    int fd;
    char buffer[BUFFER_LENGTH];

    union{
        RCALLBACK accept_callback;
        RCALLBACK recv_callback;
    }recv_t;
    RCALLBACK send_callback;
};

struct conn_item connlist[BUFFER_LENGTH * BUFFER_LENGTH];

//函数声明
//listenfd
int accept_cb(int fd);
//clientfd
int recv_cb(int fd);
int send_cb(int fd);

void set_event(int fd,int event,int flag);

//这个函数用于设置epoll事件，它接收三个参数文件描述符fd,要设置的事件类型event,以及标志flag
void set_event(int fd,int event,int flag){
    if(flag){    //1 add 0 mod
        struct epoll_event ev;
        ev.events = event;
        ev.data.fd = fd;
        epoll_ctl(epfd,EPOLL_CTL_ADD,fd,&ev);
    }else{
        struct epoll_event ev;
        ev.events = event;
        ev.data.fd = fd;
        epoll_ctl(epfd,EPOLL_CTL_MOD,fd,&ev);
    }
}

//处理新连接
int accept_cb(int fd){
    struct sockaddr_in client_addr;
    memset(&client_addr,0,sizeof(struct sockaddr_in));
    socklen_t client_len = sizeof(client_addr);
    int clientfd = accept(fd, (struct sockaddr*)&client_addr, &client_len);

    set_event(clientfd,EPOLLIN,1);

    connlist[clientfd].fd = clientfd;
    memset(connlist[clientfd].buffer,0,BUFFER_LENGTH);
    connlist[clientfd].recv_t.recv_callback=recv_cb;
    connlist[clientfd].send_callback=send_cb;
    //设置新连接的套接字为非阻塞
    int flags = fcntl(clientfd,F_GETFL,0);
    fcntl(clientfd, F_SETFL, flags | O_NONBLOCK);
    client_count++;
    return clientfd;
}

int recv_cb(int fd){
    char *buffer = connlist[fd].buffer;
    while(1){
        int count = recv(fd,buffer,BUFFER_LENGTH,0);
        if(count < 0){  //系统调用错误（非阻塞套接字暂时无数据可读）
            //由于是非阻塞，所以退出两种情况，没数据EAGAIN，另一种是客户端退出
            if(errno == EAGAIN){
                printf("数据已经接收完毕...\n");
                break;
            }else{
                close(fd);
                epoll_ctl(epfd,EPOLL_CTL_DEL,fd,NULL);
                break;
            }
        }else if(count == 0 ){   //对端正常关闭连接
            printf("客户端已经断开连接! \n");
            close(fd);
            epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
            client_count--;
            if(client_count<=0){
                break;
            }
        }else{
            printf("Recv: %s, %d byte(s)\n", buffer, count);
            set_event(fd,EPOLLOUT,0);
        }
    }
    if(client_count<=0){
        close(epfd);
        close(fd);
        exit(0);
    } 
    return 0;
}

int send_cb(int fd){   //发送数据
    char *buffer = connlist[fd].buffer;

	int count = send(fd, buffer, BUFFER_LENGTH, 0);
    //printf("Recv: %s, %d byte(s)\n", buffer,count);

	set_event(fd, EPOLLIN, 0);

	return count;
}

int init_server(unsigned short port) {

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in serveraddr;
	memset(&serveraddr, 0, sizeof(struct sockaddr_in));

	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(port);

	if (-1 == bind(sockfd, (struct sockaddr*)&serveraddr, sizeof(struct sockaddr))) {
		perror("bind");
		return -1;
	}
	listen(sockfd, 10);
	return sockfd;
}

int main(int argc,char *argv[]){
    //同时监听5个端口
    int port_count = 5;
    unsigned short port = 2048;

    epfd = epoll_create(1);

    for (int i = 0;i < port_count;i ++) {
		int sockfd = init_server(port + i);  // 2048, 2049, 2050, 2051 ... 2057
		connlist[sockfd].fd = sockfd;
		connlist[sockfd].recv_t.accept_callback = accept_cb;
		set_event(sockfd, EPOLLIN, 1);
	}

    struct epoll_event events[EPOLL_SIZE] = {0};

    while(1){
        int nready = epoll_wait(epfd,events,1024,-1);
        for(int i = 0;i < nready;i++){
            int connfd = events[i].data.fd;
            if(events[i].events & EPOLLIN){
                int count = connlist[connfd].recv_t.recv_callback(connfd);
            }else if(events[i].events & EPOLLOUT){
                int count = connlist[connfd].send_callback(connfd);
            }
        }
    }

    return 0;
}