#include "_public.h"
using namespace idc;

cpactive pactive;        // 进程的心跳。
//clogfile logfile;       //用于写日志

void EXIT(int sig);

int main(int argc,char* argv[])
{
    if(argc!=4)
    {
        printf("\n");
        printf("Using:/project/tools/bin/deletefiles pathname matchstr timeout\n\n");

        printf("Example:/project/tools/bin/deletefiles /tmp/idc/surfdata \"*CSV,*.Xml,*.Json\" 0.01\n");
        cout << R"(        /project/tools/bin/deletefiles /log/idc "*.log.20*" 0.02)" << endl;
        printf("        /project/tools/bin/procctl 300 /project/tools/bin/deletefiles /log/idc \"*.log.20*\" 0.02\n");
        printf("        /project/tools/bin/procctl 300 /project/tools/bin/deletefiles /tmp/idc/surfdata \"*.xml,*.json\" 0.01\n\n");

        printf("这是一个工具程序，用于删除历史的数据文件或日志文件。\n");
        printf("本程序把pathname目录及子目录中timeout天之前的匹配matchstr文件全部删除，timeout可以是小数。\n");
        printf("本程序不写日志文件，也不会在控制台输出任何信息。\n\n\n");

        return -1;
    }

    //忽略全部的信号和关闭I/O，设置信号处理函数
    closeioandsignal(true);
    signal(SIGTERM,EXIT);
    signal(SIGINT,EXIT);

    pactive.addpinfo(30,"deletefiles");


    // 获取被定义为历史数据文件的时间点。
    string stringtimeout=ltime1("yyyymmddhh24miss",0-(int)(atof(argv[3])*24*60*60));


  //  logfile.open("/myproject/LOGFILE.log");
    // 打开目录。
    cdir dir;
    if(dir.opendir(argv[1],argv[2],10000,true)==false)
    {
        printf("dir.opendir(%s) failed.\n",argv[1]);
        //logfile.write("dir.opendir(%s) failed.\n",argv[1]);
        return -1;
    }

    while(dir.readdir()==true)
    {
        //logfile.write("未超时%s\n",dir.m_ffilename.c_str());
        if(dir.m_mtime<stringtimeout)
        {
            if(remove(dir.m_ffilename.c_str())==0)
            {
                //logfile.write("删除了文件%s\n",dir.m_ffilename.c_str());
                cout << "remove(" << dir.m_ffilename << ") ok.\n";
            }  
            else
            {
                cout << "remove(" << dir.m_ffilename << ") failed.\n"; 
                //logfile.write("删除了失败%s\n",dir.m_ffilename.c_str());
            }
                 
        }

    }
    return 0;

}

void EXIT(int sig)
{
    printf("程序退出，sig=%d\n\n",sig);
    exit(0);
}