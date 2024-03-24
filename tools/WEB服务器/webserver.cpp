/*
 * 程序名：webserver.cpp，http服务模块
 * 功能：将指定目录下文件指定类型文件发送给http客户端
 * 该服务器访问格式为 域名:端口api?type=<typename>
*/

//接收线程将收到的socket信息和对应收到的数据放入状态机中，直到数据接收完毕，将状态机中的数据放入接收队列
//接受线程与工作线程之间建立生产消费者模型，处理接收队列，工作线程要在数据库中查询信息，故设计多线程处理
//工作线程与发送线程之间通过管道通信和互斥锁同步即可,因为只设立一个发送线程
//发送线程从队列中拿到数据和sock信息后，先保存到状态机sendbuffer，使用io复用异步，故只用一个线程即可


#include "_public.h"
using namespace idc;

void EXIT(int sig);     // 进程退出函数

clogfile logfile;         // 本程序运行的日志
cpactive pactive;       //进程心跳

// 初始化服务端的监听端口
int initserver(const int port);

// 从GET请求中获取参数的值：strget-GET请求报文的内容；name-参数名；value-参数值
bool getvalue(const string &strget,const string &name,string &value);

struct st_client                   // 客户端的结构体
{
    string clientip;                // 客户端的ip地址
    int      clientatime=0;     // 客户端最后一次活动的时间
    string recvbuffer;           // 客户端的接收缓冲区
    string sendbuffer;          // 客户端的发送缓冲区
};

// 接收/发送队列的结构体
struct st_recvmesg
{
    int      sock=0;               // 客户端的socket
    string message;            // 接收/发送的报文

    st_recvmesg(int in_sock,string &in_message):sock(in_sock),message(in_message){ /*logfile.write("构造了报文\n");*/}
};

class AA   // 线程类
{
private:
    queue<shared_ptr<st_recvmesg>> m_rq;            // 接收队列，底层容器用deque
    mutex m_mutex_rq;                                               // 接收队列的互斥锁
    condition_variable m_cond_rq;                              // 接收队列的条件变量

    queue<shared_ptr<st_recvmesg>> m_sq;            // 发送队列，底层容器用deque
    mutex m_mutex_sq;                                               // 发送队列的互斥锁
    int m_sendpipe[2] = {0};                                        // 工作线程通知发送线程的无名管道

    unordered_map<int,struct st_client> clientmap;  // 存放客户端对象的哈希表状态机

    atomic_bool m_exit;                                             // 如果m_exit==true，工作线程和发送线程将退出
public:
    string path;
    int m_recvpipe[2] = {0};                                        // 主进程通知接收线程退出的管道主进程要用到该成员，所以声明为public
    AA() 
    { 
        pipe(m_sendpipe);     // 创建工作线程通知发送线程的无名管道
        pipe(m_recvpipe);      // 创建主进程通知接收线程退出的管道
        m_exit=false;
    }

    // 接收线程主函数，listenport-监听端口
    void recvfunc(int listenport)                  
    {
         // 初始化服务端用于监听的socket
        int listensock=initserver(listenport);
        if (listensock<0)
        {
            logfile.write("接收线程：initserver(%d) failed.\n",listenport);   return;
        }

        // 创建epoll句柄
        int epollfd=epoll_create1(0);

        // 为监听的socket准备读事件
        epoll_event ev;                            //声明事件的数据结构
        ev.events=EPOLLIN;                                //读事件
        ev.data.fd=listensock;                            //指定事件的自定义数据，会随着epoll_wait()返回的事件一并返回
        epoll_ctl(epollfd,EPOLL_CTL_ADD,listensock,&ev);  // 把监听的socket的事件加入epollfd中

        // 把接收主进程通知的管道加入epoll
        ev.data.fd = m_recvpipe[0];
        ev.events = EPOLLIN;
        epoll_ctl(epollfd,EPOLL_CTL_ADD,ev.data.fd,&ev); 

        epoll_event evs[10];      // 存放epoll返回的事件

        while (true)     // 进入事件循环
        {
            // 等待监视的socket有事件发生
            int infds=epoll_wait(epollfd,evs,10,-1);

            // 返回失败
            if (infds < 0) 
            { 
                logfile.write("接收线程：epoll() failed\n"); 
                return; 
            }

            // 遍历epoll返回的已发生事件的数组evs
            for (int ii=0;ii<infds;ii++)
            {
                logfile.write("接收线程：已发生事件的fd=%d(%d)\n",evs[ii].data.fd,evs[ii].events);

                ////////////////////////////////////////////////////////
                // 如果发生事件的是listensock，表示有新的客户端连上来
                if (evs[ii].data.fd==listensock)
                {
                    sockaddr_in client;
                    socklen_t len = sizeof(client);
                    int clientsock = accept(listensock,(struct sockaddr*)&client,&len);

                    fcntl(clientsock,F_SETFL,fcntl(clientsock,F_GETFL,0)|O_NONBLOCK);     // 把socket设置为非阻塞

                    logfile.write("接收线程：accept client(socket=%d) ok.\n",clientsock);

                    clientmap[clientsock].clientip=inet_ntoa(client.sin_addr);    // 保存客户端的ip地址
                    clientmap[clientsock].clientatime=time(0);                           // 客户端的活动时间

                    // 为新的客户端连接准备读事件，并添加到epoll中
                    ev.data.fd=clientsock;
                    ev.events=EPOLLIN;
                    epoll_ctl(epollfd,EPOLL_CTL_ADD,clientsock,&ev);

                    continue;
                }

                // 如果是管道有读事件
                if (evs[ii].data.fd==m_recvpipe[0])
                {
                    logfile.write("接收线程：即将退出\n");

                    m_exit=true;      // 把退出的原子变量置为true

                    m_cond_rq.notify_all();     // 通知全部的工作线程退出

                    write(m_sendpipe[1],(char*)"o",1);   // 通知发送线程退出

                    return;
                }

                // 如果是客户端连接的socke有事件，分两种情况：1）客户端有报文发过来；2）客户端连接已断开
                if (evs[ii].events&EPOLLIN)     // 判断是否为读事件 
                {
                    char buffer[5000];     // 存放从接收缓冲区中读取的数据
                    int  buflen=0;         // 从接收缓冲区中读取的数据的大小

                    // 读取客户端的请求报文
                    if ( (buflen=recv(evs[ii].data.fd,buffer,sizeof(buffer),0)) <= 0 )
                    {
                        // 如果连接已断开
                        logfile.write("接收线程：client(%d) disconnected\n",evs[ii].data.fd);
                        close(evs[ii].data.fd);                      // 关闭客户端的连接
                        clientmap.erase(evs[ii].data.fd);     // 从状态机中删除客户端
                        continue;
                    }
                    
                    // 以下是成功读取了客户端数据的流程
                    logfile.write("接收线程：recv %d,%d bytes\n",evs[ii].data.fd,buflen);
                    // 把读取到的数据追加到socket的recvbuffer中
                    clientmap[evs[ii].data.fd].recvbuffer.append(buffer,buflen);

                    // 如果recvbuffer中的内容以"\r\n\r\n"结束，表示已经是一个完整的http请求报文了
                    if ( clientmap[evs[ii].data.fd].recvbuffer.compare( clientmap[evs[ii].data.fd].recvbuffer.length()-4,4,"\r\n\r\n")==0)
                    {
                        logfile.write("接收线程：接收到了一个完整的请求报文\n");
                        inrq((int)evs[ii].data.fd, clientmap[evs[ii].data.fd].recvbuffer);    // 把完整的请求报文入队，交给工作线程
                        clientmap[evs[ii].data.fd].recvbuffer.clear();  // 清空socket的recvbuffer
                    }
                    else
                    {
                        if (clientmap[evs[ii].data.fd].recvbuffer.size()>1000)
                        {
                            close(evs[ii].data.fd);                      // 关闭客户端的连接
                            clientmap.erase(evs[ii].data.fd);     // 从状态机中删除客户端
                            // 可以考虑增加把客户端的ip加入黑名单
                        }
                    }

                    clientmap[evs[ii].data.fd].clientatime=time(0);   // 更新客户端的活动时间
                }
            }
        }
    }

    // 把客户端的socket和请求报文放入接收队列，sock-客户端的socket，message-客户端的请求报文
    void inrq(int sock,string &message)              
    {
        shared_ptr<st_recvmesg> ptr=make_shared<st_recvmesg>(sock,message);   // 创建接收报文对象

        lock_guard<mutex> lock(m_mutex_rq);   // 申请加锁

        m_rq.push(ptr);                   // 把接收报文对象扔到接收队列中
        m_cond_rq.notify_one();     // 通知工作线程处理接收队列中的报文
    }
    
    // 工作线程主函数，处理接收队列中的请求报文，id-线程编号（仅用于调试和日志，没什么其它的含义）
    void workfunc(int id)       
    {
        while (true)
        {
            shared_ptr<st_recvmesg> ptr;

            {
                unique_lock<mutex> lock(m_mutex_rq);    // 把互斥锁转换成unique_lock<mutex>，并申请加锁

                while (m_rq.empty())                  // 如果队列空，进入循环，否则直接处理数据必须用循环，不能用if
                {
                    m_cond_rq.wait(lock);            // 等待生产者的唤醒信号

                    if (m_exit==true) 
                    {
                        logfile.write("工作线程（%d）：即将退出\n",id);  return;
                    }
                }

                ptr=m_rq.front(); 
                m_rq.pop();    // 出队一个元素
            }//解锁

            // 处理出队的元素，即客户端的请求报文
            logfile.write("工作线程（%d）请求：sock=%d,mesg=%s\n",id,ptr->sock,ptr->message.c_str());

            /////////////////////////////////////////////////////////////
            //这里使用智能指针的好处是，保证了资源不会被delete两次导致系统崩溃
            // 在这里增加处理客户端请求报文的代码（解析请求报文、判断权限、执行查询数据的SQL语句、生成响应报文）
            string fullfilename;
            string type;
            int filesize;
            //寻找文件
            if(getfullfilename(ptr->message,fullfilename,type,filesize)==false)
                logfile.write("getfullfilename() failed");
            //生成报文
            string sendbuf;
            bizmain(fullfilename,type,filesize,sendbuf);
            /////////////////////////////////////////////////////////////

            logfile.write("工作线程（%d）回应：sock=%d,mesg=%s\n",id,ptr->sock,"............");

            // 把客户端的socket和响应报文放入发送队列
            insq(ptr->sock,sendbuf);    
        }
    }

        //解析http报文并返回文件名
        //recvbuf收到的报文 filename是返回的文件名,filesize是文件大小
        //目前只提供选择文件类型的参数
        bool getfullfilename(const string& recvbuf,string& filename,string& type,int& filesize)
        {
            //生成错误报文
            type="none";
            if(getvalue(recvbuf,"type",type)==false);

            //组成匹配规则,打开目录查找文件
            string mttype="*."+type;
            //logfile.write("搜索文件类型%s\n",mttype);

            cdir dir;
            if(dir.opendir(path.c_str(),mttype)==false)
            {
                logfile.write("dir OpenFailed\n");
                return false;
            }

            bool fileok=false;
            while(dir.readdir()!=false)       
            {
                if(dir.m_filesize>0)
                {
                    fileok=true;
                    break;
                }
            }
            if(fileok==false)        // 查找失败
            {
                logfile.write("file FindFailed\n");
                return false;
            }
            filesize=dir.m_filesize;
            filename=path+dir.m_filename;
            return true;
        }

        //提取文件内容构造报文
         bool bizmain(const string& fullfilename,const string& type,int totalsize,string& message)
         {

            message=sformat(
                "HTTP/1.1 400 NotFound\r\n"
                "Server: webserver\r\n"
                "Content-Type: text/text;charset=utf-8\r\n")+sformat("Content-Length:%d\r\n\r\n",10)+"Not Found";

            logfile.write("即将读取%s,大小:%d\n",fullfilename.c_str(),totalsize);
             //读取文件
            cifile ifile;                 
            if(ifile.open(fullfilename,ios::in|ios::binary)==false)
            {
                logfile.write("file ReadFailed\n");
                return false;
            }

            //请空错误信息
            message.clear();
            //读入文件内容
            int count=0;        //防止文件过大
            
            while (count<10000)
            {
                //二进制读取
                // 计算本次应该读取的字节数，如果剩余的数据超过1000字节，就读1000字节
                // if (totalsize-totalbytes>1000) 
                //     onread=1000;
                // else 
                //     onread=totalsize-totalbytes;

                // // 从文件中读取数据
                // ifile.read(buffer,onread);   
                // buffer[onread]='\0';
                // logfile.write("读取到%s\n",buffer);
                // // 把读取到的数据加入集合
                // message+=string(buffer);

                // // 计算文件已读取的字节总数，如果文件已读完，跳出循环
                // totalbytes=totalbytes+onread;

                // if (totalbytes==totalsize) break;

                //文本读取
                string line;
                if(ifile.readline(line)==false)
                {
                    break;
                }

                message+=line+"\n";
                //logfile.write("读取到%s\n",line.c_str());
                ++count;
            }
            message=R"(<?xml-stylesheet type="text/css" href="book.css"?>)"+message;

            //组成报文
            message=sformat(
                "HTTP/1.1 200 OK\r\n"
                "Server: webserver\r\n"
                "Content-Type: text/%s;charset=utf-8\r\n",type.c_str())+sformat("Content-Length:%d\r\n\r\n",message.size())+message;
            
            // 把客户端的socket和响应报文放入发送队列并通知发送线程
            return true;

         }


    // 把客户端的socket和响应报文放入发送队列，sock-客户端的socket，message-客户端的响应报文
    void insq(int sock,string &message)              
    {
        {
            shared_ptr<st_recvmesg> ptr=make_shared<st_recvmesg>(sock,message);

            lock_guard<mutex> lock(m_mutex_sq);   // 申请加锁

            m_sq.push(ptr);
        }//及时解锁

        write(m_sendpipe[1],(char*)"o",1);   // 通知发送线程处理发送队列中的数据
    }

    // 发送线程主函数，把发送队列中的数据发送给客户端
    void sendfunc()           
    {
        // 创建epoll句柄
        int epollfd=epoll_create1(0);
        struct epoll_event ev;  

        // 把发送队列的管道加入epoll
        ev.data.fd = m_sendpipe[0];
        ev.events = EPOLLIN;
        epoll_ctl(epollfd,EPOLL_CTL_ADD,ev.data.fd,&ev); 

        struct epoll_event evs[10];      // 存放epoll返回的事件

        while (true)     // 进入事件循环
        {
            // 等待监视的socket有事件发生
            int infds=epoll_wait(epollfd,evs,10,-1);

            // 返回失败
            if (infds < 0) 
            { 
                logfile.write("发送线程：epoll() failed\n"); 
                return; 
            }

            // 遍历epoll返回的已发生事件的数组evs
            for (int ii=0;ii<infds;ii++)
            {
                logfile.write("发送线程：已发生事件的fd=%d(%d)\n",evs[ii].data.fd,evs[ii].events);

                ////////////////////////////////////////////////////////
                // 如果发生事件的是管道，表示发送队列中有报文需要发送
                if (evs[ii].data.fd==m_sendpipe[0])
                {
                    if (m_exit==true) 
                    {
                        logfile.write("发送线程：即将退出\n");  
                        return;
                    }

                    char cc;
                    read(m_sendpipe[0], &cc, 1);    // 读取管道中的数据，只有一个字符，不关心其内容

                    shared_ptr<st_recvmesg> ptr;

                    lock_guard<mutex> lock(m_mutex_sq);   // 申请加锁

                    while (m_sq.empty()==false)
                    {
                        ptr=m_sq.front(); 
                        //队列只用来暂存数据，要立即出队，通过io复用发送
                        m_sq.pop();   // 出队一个元素（报文）
                        // 把出队的报文保存到socket的发送缓冲区中
                        clientmap[ptr->sock].sendbuffer.append(ptr->message);
                        
                        // 关注客户端socket的写事件
                        ev.data.fd=ptr->sock;
                        ev.events=EPOLLOUT;
                        epoll_ctl(epollfd,EPOLL_CTL_ADD,ev.data.fd,&ev);
                    }

                    continue;
                }
                ////////////////////////////////////////////////////////

                ////////////////////////////////////////////////////////
                 // 判断客户端的socket是否有写事件（发送缓冲区没有满）
                if (evs[ii].events&EPOLLOUT)
                {
                    // 把响应报文发送给客户端
                    int writen=send(evs[ii].data.fd,clientmap[evs[ii].data.fd].sendbuffer.data(),clientmap[evs[ii].data.fd].sendbuffer.length(),0);

                    logfile.write("发送线程：向%d发送了%d字节\n",evs[ii].data.fd,writen);

                    // 删除socket缓冲区中已成功发送的数据
                    clientmap[evs[ii].data.fd].sendbuffer.erase(0,writen);

                    // 如果socket缓冲区中没有数据了，不再关心socket的写件事
                    if (clientmap[evs[ii].data.fd].sendbuffer.length()==0)
                    {
                        ev.data.fd=evs[ii].data.fd;
                        epoll_ctl(epollfd,EPOLL_CTL_DEL,ev.data.fd,&ev);
                    }
                }
                ////////////////////////////////////////////////////////
            }
        }
    }
};

AA aa;

int main(int argc,char *argv[])
{
    if (argc != 4)
    {
        printf("\n");
        printf("Using :/myproject/tools/bin/webserver logfile port filepath\n\n");
        printf("Sample:/myproject/tools/bin/webserver /myproject/webserver.log 5088 /tmp/idc/surfdata\n\n");
        printf("基于HTTP协议的web服务器\n");
        printf("logfile 本程序运行的日是志文件\n");
        printf("port    服务端口，例如：80、8080\n");

        return -1;
    }
    aa.path=argv[3];

    // 关闭全部的信号和输入输出
    // 设置信号,在shell状态下可用 "kill + 进程号" 正常终止些进程
    // 但请不要用 "kill -9 +进程号" 强行终止
    closeioandsignal();  
    signal(SIGINT,EXIT); signal(SIGTERM,EXIT);

    // 打开日志文件
    if (logfile.open(argv[1])==false)
    {
        printf("打开日志文件失败（%s）\n",argv[1]); 
        return -1;
    }

    thread t1(&AA::recvfunc, &aa,atoi(argv[2]));     // 创建接收线程
    thread t2(&AA::workfunc, &aa,1);                      // 创建工作线程1
    thread t3(&AA::workfunc, &aa,2);                      // 创建工作线程2
    thread t4(&AA::workfunc, &aa,3);                      // 创建工作线程3
    thread t5(&AA::sendfunc, &aa);                         // 创建发送线程

    logfile.write("已启动全部的线程\n");

    //我们就不设置join了,因为理论上这个程序会无限运行下去
    while (true)
    {
        sleep(30);

        // 可以执行一些定时任务
    }

    return 0;
}

// 初始化服务端的监听端口
int initserver(const int port)
{
    int sock = socket(AF_INET,SOCK_STREAM,0);
    if (sock < 0)
    {
        logfile.write("socket(%d) failed.\n",port); 
        return -1;
    }

    int opt = 1; 
    unsigned int len = sizeof(opt);
    setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&opt,len);

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if (bind(sock,(struct sockaddr *)&servaddr,sizeof(servaddr)) < 0 )
    {
        logfile.write("bind(%d) failed.\n",port); 
        close(sock); 
        return -1;
    }

    if (listen(sock,5) != 0 )
    {
        logfile.write("listen(%d) failed.\n",port); 
        close(sock); 
        return -1;
    }

    fcntl(sock,F_SETFL,fcntl(sock,F_GETFL,0)|O_NONBLOCK);  // 把socket设置为非阻塞

    return sock;
}

void EXIT(int sig)
{
    signal(sig,SIG_IGN);

    logfile.write("程序退出，sig=%d\n\n",sig);

    write(aa.m_recvpipe[1],(char*)"o",1);   // 通知接收线程退出

    usleep(500);    // 让线程们有足够的时间退出

    exit(0);
}


// 从GET请求中获取参数的值：strget-GET请求报文的内容；name-参数名；value-参数值；len-参数值的长度
bool getvalue(const string &strget,const string &name,string &value)
{
    // http://192.168.150.128:8080/api?type=xml
    // GET /api?type=xml HTTP/1.1
    // Host: 192.168.150.128:8080
    // Connection: keep-alive
    // Upgrade-Insecure-Requests: 1
    // .......

    int startp=strget.find(name);                              // 在请求行中查找参数名的位置

    if (startp==string::npos) return false; 

    int endp=strget.find("&",startp);                         // 从参数名的位置开始，查找&符号
    if (endp==string::npos) endp=strget.find(" ",startp);     // 如果是最后一个参数，没有找到&符号，那就查找空格

    if (endp==string::npos) return false;

    // 从请求行中截取参数的值
    value=strget.substr(startp+(name.length()+1),endp-startp-(name.length()+1));

    return true;
}
