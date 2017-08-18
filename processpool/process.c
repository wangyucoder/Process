#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#define err_sys(msg) \
    do { perror(msg); exit(-1); } while(0)
#define err_exit(msg) \
    do { fprintf(stderr, msg); exit(-1); } while(0)
#define head200 "HTTP/1.1 200 OK \r\n"
#define head404 "HTTP/1.1 404 Not Found\r\n"
#define head503 "HTTP/1.1 503 Service unabailiable\r\n"
#define MAXCHILD 4
#define BUFFSIZE 1024

typedef struct
{
    pid_t pid;
    char status;
}sReport;

int len200, len404, len503;
int pipe_fd1[2], pipe_fd2[2];

void process_child(int listenfd, char* filename)
{
    int connfd;
    int cnt, len;
    struct sockaddr_in cliaddr;
    socklen_t clilen = sizeof(cliaddr);
    sReport req;
    char comm = '\0';
    int running = 1;
    char head_buf[1024];

    req.pid = getpid();
    while(running)
    {
        connfd = accept(listenfd, (struct sockaddr *)&cliaddr, &clilen);
        if(connfd < 0)
            err_sys("accept");
        req.status = 'n';

        /* send message to parent process that got a new accept */
        if(write(pipe_fd1[1], &req, sizeof(req)) < 0)
            err_sys("write");

        int fd;
        struct stat file_stat;
        if((fd = open(filename, O_RDONLY)) < 0)
            err_sys("open");
        if(stat(filename, &file_stat) < 0)
            err_sys("stat");

        bzero(head_buf, sizeof(head_buf));
        len = 0;
        cnt = snprintf(head_buf, BUFFSIZE - 1, head200);
        len += cnt;
        cnt = snprintf(head_buf + len, BUFFSIZE - 1 - len, "Coontent-Length: %lu\r\n", file_stat.st_size);
        len += cnt;
        cnt = snprintf(head_buf + len, BUFFSIZE - 1 - len, "\r\n");

        char *file_buf;
        struct iovec iv[2];

        if((file_buf = (char *)malloc(file_stat.st_size)) == NULL)
            err_sys("malloc");
        bzero(file_buf, file_stat.st_size);
        if(read(fd, file_buf, file_stat.st_size) < 0)
            err_sys("read");
        iv[0].iov_base = head_buf;
        iv[0].iov_len = strlen(head_buf);
        iv[1].iov_base = file_buf;
        iv[1].iov_len = strlen(file_buf);
        writev(connfd, iv, 2); /* send the file to client host */
        free(file_buf);

        close(fd);
        close(connfd);

        req.status = 'f';
        /* tell parent process that finish the request */
        if(write(pipe_fd1[1], &req, sizeof(req)) < 0)
            err_sys("write");
        /* wait for commond from parent process */
        if(read(pipe_fd2[0], &comm, 1) < 1)
            err_sys("read");

        if('e' == comm)
        {
            printf("[%d] exit\n", req.pid);
            running = 0;
        }
        else if('c' == comm)
            printf("[%d] continue\n", req.pid);
        else
            printf("[%d]: comm: %c illeagle\n", req.pid, comm);
    }
}

void handle_sigchld(int sig)
{
    printf("the child process exit.\n");
}

int main(int argc, char *argv[])
{
    int listenfd;
    struct sockaddr_in servaddr;
    pid_t pid;

    if(argc != 2)
        err_exit("Usage: http-server port\n");

    int port = atoi(argv[1]);
    len200 = strlen(head200);
    len404 = strlen(head404);
    len503 = strlen(head503);

    if(signal(SIGCHLD, handle_sigchld) < 0)
        err_sys("signal");
    if(pipe(pipe_fd1) < 0)
        err_sys("pipe pipe_fd1");
    if(pipe(pipe_fd2) < 0)
        err_sys("pipe piep_fd2");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_sys("socket");
    if(bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
        err_sys("bind");
    if(listen(listenfd, 10) < 0)
        err_sys("listen");
    int i;
    for(i = 0; i < MAXCHILD; i++)
    {
        if((pid = fork()) < 0)
            err_sys("fork");
        else if(0 == pid)
        {
            process_child(listenfd, "test.txt"); //test.txt为测试文件，内容为 Hello World!!!
        }
        else
        {
            printf("have create child %d\n", pid);
        }
    }

    close(pipe_fd1[1]);
    close(pipe_fd2[0]);
    close(listenfd);
    char c = 'c';
    int req_num = 0;
    sReport req;

    while(1)
    {
        if(read(pipe_fd1[0], &req, sizeof(req)) < 0)
            err_sys("read pipe_fd1");
        /* a new request come */
        if(req.status == 'n') //子进程收到一个连接请求
        {
            req_num++;
            printf("parent: %d have receive new request\n", req.pid);
        }
        else if(req.status == 'f') /* just finish a accept */
        {
            req_num--;
            if(write(pipe_fd2[1], &c, sizeof(c)) < sizeof(c))
                err_sys("write");
        }
    }
    printf("Done\n");

    return 0;
}
