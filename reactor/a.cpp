#include<iostream>
#include<string.h>
#include<sys/socket.h>  //包含套接字的函数
#include<netinet/in.h>  //包含sockaddr_in结构体及常用的网络操作函数
#include<unistd.h>      //提供POSIX API ，如close,read等
#include<thread>
#include<poll.h>
#include<sys/epoll.h>
using namespace std;

#define POLL 0
#define EPOLL 1

int main(){
    int sockfd = socket(AF_INET,SOCK_STREAM,0);  //套接字
    struct sockaddr_in serveraddr;
    memset(&serveraddr,0,sizeof(struct sockaddr_in));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(2048);

    if(-1 == bind(sockfd,(struct sockaddr*)&serveraddr,sizeof(struct sockaddr))){
        perror("bind");
        return -1;
    }    

    listen(sockfd,10); //10表示排队等待连接的最大连接请求数量

#if 0
//这种方式当连接数比较多的时候，性能较差
//普通方式实现tcp连接，每有一个新的连接，开一个新的线程
    while(1){   
        struct sockaddr clientaddr;
        socklen_t len=sizeof(clientaddr);

        int clientfd=accept(sockfd,&clientaddr,&len);   //通信信道,第二个参数定义的时候使用sockaddr而不是sockaddr_in会有警告
        // pthread_t thid;
        // pthread_create(&thid,NULL,client_thread,&clientfd);
        // pthread_detach(thid); // detach the thread to avoid resource leak
        thread th(client_thread, new int(clientfd));
        th.detach(); // detach the thread to avoid resource leak
    }
#endif

    //reactor网络模型

    //poll
#if POLL
    struct pollfd fds[1024] = {0};
    fds[sockfd].fd = sockfd;  //可以这样理解，现在这个sockfd监听2048端口，并加入poll中管理
    fds[sockfd].events = POLLIN;
    int maxfd = sockfd;

    while(1){
        int nready = poll(fds,maxfd + 1,-1);
        //revents变量里存储具体发生的事件，fds[sockfd].revents & POLLIN表示这个数就是POLLIN
        if(fds[sockfd].revents & POLLIN){
            struct sockaddr clientaddr;
            socklen_t len = sizeof(clientaddr);
            int clientfd = accept(sockfd,(struct sockaddr*)&clientaddr,&len);
            cout <<"sockfd: " << clientfd << endl;
            fds[clientfd].fd = clientfd;
            fds[clientfd].events = POLLIN;
            maxfd = max(maxfd,clientfd);
        }

        for(int i = sockfd + 1;i <= maxfd;i++){
            if(fds[i].revents & POLLIN){
                char buffer[128] = {0};
                int count = recv(i,buffer,128,0);
                //重点！！！！！！！！！！！！！！！
                //这里因为只有客户端断开连接的POLLIN事件触发，这时recv返回<=0的数，才会close(i)
                if(count <= 0){
                    cout << "disconnect: " << i << endl;
                    close(i);
                    fds[i].fd=-1;
                    fds[i].events=0;
                }else{
                    send(i,buffer,count,0);
                    cout<<"clientfd: "<<i<<",count: "<<count<<",buffer: "<<buffer<<endl;
                }
                fds[i].revents = 0;
            }
        }
    }
#endif

    //epoll
#if EPOLL
    int epfd = epoll_create(1);
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = sockfd;

    epoll_ctl(epfd,EPOLL_CTL_ADD,sockfd,&ev);
    struct epoll_event events[1024] = {0};

    while(1){
        int nready = epoll_wait(epfd,events,1024,-1);
        for(int i = 0;i < nready;i++){
            int connfd = events[i].data.fd;
            if(sockfd == connfd){
                //触发事件的是监听描述符，表示新连接进来了
                struct sockaddr clientaddr;
                socklen_t len=sizeof(clientaddr);
                int clientfd=accept(sockfd,(struct sockaddr*)&clientaddr,&len);

                ev.events=EPOLLIN;
                ev.data.fd=clientfd;
                epoll_ctl(epfd,EPOLL_CTL_ADD,clientfd,&ev);
                cout<<"clientfd: "<<clientfd<<endl;
            }else if(events[i].events & EPOLLIN){
                char buffer[128]={0};
                int count=recv(connfd,buffer,128,0);
                if(count<=0){  //close
                    cout<<"disconnect:"<<connfd<<endl;
                    epoll_ctl(epfd,EPOLL_CTL_DEL,connfd,NULL);               
                    close(i);
                }else{
                    send(connfd,buffer,count,0);
                    cout<<"clientfd: "<<connfd<<",count: "<<count<<",buffer: "<<buffer<<endl;
                }
            }
        }
    }

#endif

    close(sockfd);
    return 0;
}
//poll使用的是时间轮询的方式遍历，而epoll使用的是事件通知的方式
