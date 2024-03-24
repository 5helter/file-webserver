
// 本程序不需要包含_public.h，没必要依赖那么多头文件。
#include <cstdio>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc,char* argv[])
{
    if(argc<3)
    {
        printf("Using:./procctl timetvl program argv ...\n");
        printf("Example:/project/tools/bin/procctl 10 /usr/bin/tar zcvf /tmp/tmp.tgz /usr/include\n");
  	    printf("Example:/project/tools/bin/procctl 60 /project/idc/bin/crtsurfdata /project/idc/ini/stcode.ini /tmp/idc/surfdata /log/idc/crtsurfdata.log csv,xml,json\n");

        printf("本程序是服务程序的调度程序，周期性启动服务程序或shell脚本。\n");
        printf("timetvl 运行周期，单位：秒。\n");
        printf("        被调度的程序运行结束后，在timetvl秒后会被procctl重新启动。\n");
        printf("        如果被调度的程序是周期性的任务，timetvl设置为运行周期。\n");
        printf("        如果被调度的程序是常驻内存的服务程序，timetvl设置小于5秒。\n");
        printf("program 被调度的程序名，必须使用全路径。\n");
        printf("...     被调度的程序的参数。\n");
        printf("注意，本程序不会被kill杀死，但可以用kill -9强行杀死。\n\n\n");
        return -1;
    }
    // 关闭信号和I/O，本程序不希望被打扰。
    // 注意：1）为了防调度程序被误杀，不处理退出信号；
    //           2）如果忽略和信号和关闭了I/O，将影响被调度的程序（也会忽略和信号和关闭了I/O）。 why？因为被调度的程序取代了子进程，子进程会继承父进程的信号处理方式和I/O。
    for (int ii=0;ii<64;ii++)
    {
        signal(ii,SIG_IGN);  close(ii);
    }

    if(fork()>0) exit(0);   //将子进程交给1号托管，脱离shell控制

    // 把子进程退出的信号SIGCHLD恢复为默认行为，让父进程可以调用wait()函数等待子进程退出。
    signal(SIGCHLD,SIG_DFL);

    char* argp[argc];       //用于存储函数的参数
    for(int i=2;i<argc;++i)
    {
        argp[i-2]=argv[i];
    }

    argp[argc-2]=nullptr;   //最后添加一个空作为参数结束的标志

    while(true)
    {
        if(fork()==0)
        {
            execv(argv[2],argp);    //使用数组传递参数，注意最后是空指针
            exit(0);
        }
        else
        {
            //父进程等待子进程终止（被调度的程序运行结束）。
            int status;         //返回子进程状态
            wait(&status);
            sleep(atoi(argv[1]));   // 休眠timetvl秒，然后回到循环。
        }
    }

}