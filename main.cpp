#include<stdio.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<stdlib.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<vector>
#include<string.h>
#include<string>

using namespace std;

string ip;
string port;
string directory;


static const char* templ = "HTTP/1.0 200 OK\r\n"
		           "Content-length: %d\r\n"
		       	   "Content-Type: text/html\r\n"
		       	   "\r\n"
		       	   "%s";

static const char not_found[] = "HTTP/1.0 404 NOT FOUND\r\n"
				"Content-Type: text/html\r\n\r\n";


ssize_t sock_fd_write(int sock, void *buf, ssize_t buflen, int fd)
{
    ssize_t     size;
    struct msghdr   msg;
    struct iovec    iov;
    union {
        struct cmsghdr  cmsghdr;
        char        control[CMSG_SPACE(sizeof (int))];
    } cmsgu;
    struct cmsghdr  *cmsg;

    iov.iov_base = buf;
    iov.iov_len = buflen;

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    if (fd != -1) {
        msg.msg_control = cmsgu.control;
        msg.msg_controllen = sizeof(cmsgu.control);

        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_len = CMSG_LEN(sizeof (int));
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;

        printf ("passing fd %d\n", fd);
        *((int *) CMSG_DATA(cmsg)) = fd;
    } else {
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
        printf ("not passing fd\n");
    }

    size = sendmsg(sock, &msg, 0);

    if (size < 0)
        perror ("sendmsg");
    return size;
}


ssize_t sock_fd_read(int sock, void *buf, ssize_t bufsize, int *fd)
{
    ssize_t     size;

    if (fd) {
        struct msghdr   msg;
        struct iovec    iov;
        union {
            struct cmsghdr  cmsghdr;
            char        control[CMSG_SPACE(sizeof (int))];
        } cmsgu;
        struct cmsghdr  *cmsg;

        iov.iov_base = buf;
        iov.iov_len = bufsize;

        msg.msg_name = NULL;
        msg.msg_namelen = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = cmsgu.control;
        msg.msg_controllen = sizeof(cmsgu.control);
        size = recvmsg (sock, &msg, 0);
        if (size < 0) {
            perror ("recvmsg");
            exit(1);
        }
        cmsg = CMSG_FIRSTHDR(&msg);
        if (cmsg && cmsg->cmsg_len == CMSG_LEN(sizeof(int))) {
            if (cmsg->cmsg_level != SOL_SOCKET) {
                fprintf (stderr, "invalid cmsg_level %d\n",
                     cmsg->cmsg_level);
                exit(1);
            }
            if (cmsg->cmsg_type != SCM_RIGHTS) {
                fprintf (stderr, "invalid cmsg_type %d\n",
                     cmsg->cmsg_type);
                exit(1);
            }

            *fd = *((int *) CMSG_DATA(cmsg));
            printf ("received fd %d\n", *fd);
        } else
            *fd = -1;
    } else {
        size = read (sock, buf, bufsize);
        if (size < 0) {
            perror("read");
            exit(1);
        }
    }
    return size;
}


void worker(int d)
{
	while(1)
	{
	char buf[1000];
	int fd;
	sock_fd_read(d,buf,10,&fd);
	int cnt=recv(fd,buf,sizeof(buf),O_NONBLOCK);
	string path=directory;
	for(int i=4; buf[i]!=' '&&buf[i]!='?'; i++)
	{
		path.push_back(buf[i]);
	}
	FILE *file=fopen(path.c_str()+1,"r");
	if(file==NULL)
	{
		printf(not_found);
		send(fd,not_found,strlen(not_found),0);
	}
	else
	{
		fseek(file, 0, SEEK_END);
		int size = ftell(file);
		fseek(file, 0, SEEK_SET);
		fread(buf, sizeof(char), size, file);
		buf[size]=0;
		char ans[1000];
		sprintf(ans,templ,strlen(buf),buf);
		printf("%s",ans);
		send(fd,ans,strlen(ans),O_NONBLOCK);
	}
	shutdown(fd,SHUT_RDWR);
	close(fd);
	}
}

int toInt(string &s)
{
	int res=0;
	for(int i=0; i<s.size(); i++)
	{
		res=(res*10)+s[i]-'0';
	}
	return res;
}

int main(int argc, char *argv[])
{
	for(int i=0; i<argc; i++)
	{
		if(strcmp(argv[i],"-h")==0)
		{
			ip=string(argv[i+1]);
		}
		else if(strcmp(argv[i],"-p")==0)
		{
			port=string(argv[i+1]);
		}
		else if(strcmp(argv[i],"-d")==0)
		{
			directory=string(argv[i+1]);
		}
	}
	daemon(1,0);
	vector<int> workers;
	int sockPair[2];
	int cntWorkers=8;
	for(int i=0; i<cntWorkers; i++)
	{
		socketpair(AF_UNIX,SOCK_STREAM,0,sockPair);
		if(!fork())
		{
			worker(sockPair[1]);
		}
		workers.push_back(sockPair[0]);
	}
	int MasterSocket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
	struct sockaddr_in SockAddr;
	SockAddr.sin_family = AF_INET;
	SockAddr.sin_port = htons(toInt(port));
	SockAddr.sin_addr.s_addr=inet_addr(ip.c_str());
	bind(MasterSocket, (struct sockaddr*)(&SockAddr), sizeof(SockAddr));
	listen(MasterSocket, SOMAXCONN);
	int it=0;
	while(1)
	{
		int fd=accept(MasterSocket,0,0);
		char buf[10];
		sock_fd_write(workers[it],buf,1,fd);
		it++;
		if(it==cntWorkers) it=0;
	}
	return 0;
}
