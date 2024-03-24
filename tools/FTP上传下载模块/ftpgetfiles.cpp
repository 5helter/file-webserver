#include "_ftp.h"
#include "_public.h"
using namespace idc;

//程序退出和信号2，15的处理函数
void EXIT(int sig);

//创建ftp客户端对象，服务器端vsftpd可以提供完成的ftp服务支持，因此不需要，客户端使用ftp自动化处理
cftpclient ftp; 
// 日志文件对象。  
clogfile logfile;
// 进程心跳的对象。
cpactive pactive;
//增量下载工具
map<string,string> mfromok;             // 容器一：存放已下载成功文件，从starg.okfilename参数指定的文件中加载。
list<struct st_fileinfo> vfromnlist;    // 容器二：下载前列出服务端文件名的容器，从nlist文件中加载。
list<struct st_fileinfo> vtook;         // 容器三：本次不需要下载的文件的容器。
list<struct st_fileinfo> vdownload;     // 容器四：本次需要下载的文件的容器。
//程序运行的参数结构体
struct st_arg
{
    char host[31];                      // 远程服务端的IP和端口。
    int  mode;                          // 传输模式，1-被动模式，2-主动模式，缺省采用被动模式。
    char username[31];                  // 远程服务端ftp的用户名。
    char password[31];                  // 远程服务端ftp的密码。
    char remotepath[256];               // 远程服务端存放文件的目录。
    char localpath[256];                // 本地文件存放的目录。
    char matchname[256];                // 待下载文件匹配的规则。
    int  ptype;                         // 下载后服务端文件的处理方式：1-增量模式，不会改变服务器端内容；2-删除；3-备份。
    char remotepathbak[256];            // 下载后服务端文件的备份目录。
    char okfilename[256];               // 已下载成功文件信息存放的文件。
    bool checkmtime;                    // 是否需要检查服务端文件的时间，true-需要，false-不需要，缺省为false。
    int  timeout;                       // 进程心跳超时的时间。
    char pname[51];                     // 进程名，建议用"ftpgetfiles_后缀"的方式。

} starg;

struct st_fileinfo              // 文件信息的结构体。
{
    string filename;           // 文件名。
    string mtime;              // 文件时间。
    st_fileinfo()=default;
    st_fileinfo(const string &in_filename,const string &in_mtime):filename(in_filename),mtime(in_mtime) {}
    void clear() { filename.clear(); mtime.clear(); }
}; 

bool _xmltoargs(const char* strxmlbuffer);// 把xml解析到参数starg结构中。




bool loadlistfile();                      //加载服务端所有文件到容器二
bool loadokfile();                        //加载okfile到容器一
bool compmap();                           //比较容器一，二得到容器三，四
bool writetookfile();                     //将容器三中的内容重新写入okfile,因为可能有文件时间更新
bool appendtookfile(st_fileinfo fileinfo);//追加本次下载的信息到okfile

void _help();
int main(int argc,char* argv[])
{
    //从服务器下载符合匹配规则的文件
    if(argc!=3)
    {
        _help();
        return -1;
    }

    //处理退出信息
    signal(SIGINT,EXIT);
    signal(SIGTERM,EXIT);

    if(logfile.open(argv[1])==false)
    {
        printf("打开日志文件失败(%S)",argv[1]);
        return -1;
    }
    //解析xml，得到程序运行参数
    if(_xmltoargs(argv[2])==false)
        return -1;

    //设置进程心跳
    pactive.addpinfo(starg.timeout,starg.pname);

    //登录ftp服务器
    if(ftp.login(starg.host,starg.username,starg.password,starg.mode)==false)
    {
        logfile<<"failed.\n"<<ftp.response()<<"\n";
        return -1;
    }

    //进入ftp服务器存放文件的目录
    if(ftp.chdir(starg.remotepath)==false)
    {
        logfile.write("ftp.chdir(%s) failed.\n",starg.remotepath);
    }

    // 调用ftpclient.nlist()方法列出服务器目录中的文件名，保存在本地文件中
    if(ftp.nlist(".",sformat("/tmp/nlist/ftpgetfiles_%d.nlist",getpid()))==false)
    {
        logfile.write("ftp.nlist(%s) failed.\n",starg.remotepath,ftp.response()); 
        return -1;
    }

    pactive.uptatime();     //更新进程心跳


    // 把ftpclient.nlist()方法获取到的list文件加载到容器vfromnlist中。加载所有文件名
    if(loadlistfile()==false)
    {
        logfile.write("loadlistfile() failed.\n");  
        return -1; 
    }

    if(starg.ptype==1)
    {
        // // 加载starg.okfilename文件中的数据到容器vfromok中。
        loadokfile();

        //  // 比较vfromnlist和vfromok，得到vtook和vdownload。
        compmap();

        // 把容器vtook中的数据写入starg.okfilename文件，覆盖之前的旧starg.okfilename文件。
        writetookfile();

    }
    else
        vfromnlist.swap(vdownload);   // 为了统一文件下载的代码，把容器二和容器四交换。

    pactive.uptatime();   // 更新进程的心跳。

    string strremotefilename,strlocalfilename;  //临时变量用于记录待下载名字

    //logfile << "开始下载.\n"; 
    for(auto& aa:vdownload)
    {
        sformat(strremotefilename,"%s/%s",starg.remotepath,aa.filename.c_str());          // 拼接服务端全路径的文件名。
        sformat(strlocalfilename,"%s/%s",starg.localpath,aa.filename.c_str());    // 拼接本地全路径的文件名。

        if(ftp.get(strremotefilename,strlocalfilename,false)==false)
        {
            logfile << "failed." << ftp.response() << "\n"; 
            return -1;
        }

         pactive.uptatime();   // 更新进程的心跳。

         // 如果ptype==1，把下载成功的文件记录追加到starg.okfilename文件中。
         if (starg.ptype==1) 
             appendtookfile(aa);

          // ptype==2，删除服务端的文件。
        if (starg.ptype==2)
        {
            if (ftp.ftpdelete(strremotefilename)==false)
            {
                logfile.write("ftp.ftpdelete(%s) failed.\n%s\n",strremotefilename.c_str(),ftp.response()); return -1;
            }
        }

         // ptype==3，把服务端的文件移动到备份目录。
        if (starg.ptype==3)
        {
            string strremotefilenamebak=sformat("%s/%s",starg.remotepathbak,aa.filename.c_str());  // 生成全路径的备份文件名。
            if (ftp.ftprename(strremotefilename,strremotefilenamebak)==false)
            {
                logfile.write("ftp.ftprename(%s,%s) failed.\n%s\n",strremotefilename.c_str(),strremotefilenamebak.c_str(),ftp.response()); return -1;
            }
        }

    }
    //logfile << "下载完成.\n"; 

    return 0;
}

void EXIT(int sig)
{
    printf("程序退出，sig=%d",sig);

    exit(0);
}

bool _xmltoargs(const char* strxmlbuffer)
{
    memset(&starg,0,sizeof(st_arg));

    getxmlbuffer(strxmlbuffer,"host",starg.host,30);    //远程服务器ip和端口
    if(strlen(starg.host)==0)
    {
        logfile.write("host is null.\n");
        return false;
    }
    getxmlbuffer(strxmlbuffer,"mode",starg.mode);   // 传输模式，1-被动模式，2-主动模式，缺省采用被动模式。
    if (starg.mode!=2)  starg.mode=1;

    getxmlbuffer(strxmlbuffer,"username",starg.username,30);   // 远程服务端ftp的用户名。
    if (strlen(starg.username)==0)
    { logfile.write("username is null.\n");  return false; }

    getxmlbuffer(strxmlbuffer,"password",starg.password,30);   // 远程服务端ftp的密码。
    if (strlen(starg.password)==0)
    { logfile.write("password is null.\n");  return false; }

    getxmlbuffer(strxmlbuffer,"remotepath",starg.remotepath,255);   // 远程服务端存放文件的目录。
    if (strlen(starg.remotepath)==0)
    { logfile.write("remotepath is null.\n");  return false; }

    getxmlbuffer(strxmlbuffer,"localpath",starg.localpath,255);   // 本地文件存放的目录。
    if (strlen(starg.localpath)==0)
    { logfile.write("localpath is null.\n");  return false; }

    getxmlbuffer(strxmlbuffer,"matchname",starg.matchname,100);   // 待下载文件匹配的规则。
    if (strlen(starg.matchname)==0)
    { logfile.write("matchname is null.\n");  return false; }  

    // 下载后服务端文件的处理方式：1-什么也不做；2-删除；3-备份。
    getxmlbuffer(strxmlbuffer,"ptype",starg.ptype);   
    if ( (starg.ptype!=1) && (starg.ptype!=2) && (starg.ptype!=3) )
    { logfile.write("ptype is error.\n"); return false; }

    // 下载后服务端文件的备份目录。
    if (starg.ptype==3) 
    {
        getxmlbuffer(strxmlbuffer,"remotepathbak",starg.remotepathbak,255); 
        if (strlen(starg.remotepathbak)==0) { logfile.write("remotepathbak is null.\n");  return false; }
    }

    // 增量下载文件。
    if (starg.ptype==1) 
    {
        getxmlbuffer(strxmlbuffer,"okfilename",starg.okfilename,255); // 已下载成功文件名清单。
        if ( strlen(starg.okfilename)==0 ) { logfile.write("okfilename is null.\n");  return false; }

        // 是否需要检查服务端文件的时间，true-需要，false-不需要，此参数只有当ptype=1时才有效，缺省为false。
        getxmlbuffer(strxmlbuffer,"checkmtime",starg.checkmtime);
    }

    getxmlbuffer(strxmlbuffer,"timeout",starg.timeout);   // 进程心跳的超时时间。
    if (starg.timeout==0) { logfile.write("timeout is null.\n");  return false; }

    getxmlbuffer(strxmlbuffer,"pname",starg.pname,50);     // 进程名。
    // if (strlen(starg.pname)==0) { logfile.write("pname is null.\n");  return false; }

    return true;
}

 //加载服务端所有文件到容器二
bool loadlistfile() 
{
    vfromnlist.clear();
    
    cifile ifile;
    if(ifile.open(sformat("/tmp/nlist/ftpgetfiles_%d.nlist",getpid()))==false)
    {
        logfile.write("ifile.open(%s) 失败。\n",sformat("/tmp/nlist/ftpgetfiles_%d.nlist",getpid())); 
        return false;
    }
    string strfilename; //除了直接把string当指针用否则不需要使用resize


    while(true)
    {
        if(ifile.readline(strfilename)==false)
            break;
        if(matchstr(strfilename,starg.matchname)==false)
            continue;

        //判断是否有必要记录文件下载时间
        if ( (starg.ptype==1) && (starg.checkmtime==true) )
        {
            if(ftp.mtime(strfilename)==false)
            {
                logfile.write("ftp.mtime(%s) failed.\n",strfilename.c_str()); 
                return false;
            }
        }
        vfromnlist.emplace_back(strfilename,ftp.m_mtime);
    }

    ifile.closeandremove();


    // for(auto& aa:vfromnlist)
    // {
    //     logfile.write("读取到file：%s  ,时间：%s\n",aa.filename.c_str(),aa.mtime.c_str());
    // }



    return true;
}
//加载okfile到容器一
bool loadokfile()
{

    if (starg.ptype!=1) return true;

    mfromok.clear();

    cifile ifile;

    // 注意：如果程序是第一次运行，starg.okfilename是不存在的，并不是错误，所以也返回true。
    if ( (ifile.open(starg.okfilename))==false )  return true;

    string strbuffer;

    struct st_fileinfo stfileinfo;

    while (true)
    {
        stfileinfo.clear();

        if (ifile.readline(strbuffer)==false) break;

        getxmlbuffer(strbuffer,"filename",stfileinfo.filename);
        getxmlbuffer(strbuffer,"mtime",stfileinfo.mtime);

        mfromok[stfileinfo.filename]=stfileinfo.mtime;
    }

    ifile.close();

    return true;
} 
//比较容器一，二得到容器三，四                  
bool compmap()
{
    if (starg.ptype!=1) 
        return true;

    vtook.clear(); 
    vdownload.clear();

    // 遍历vfromnlist。
    for (auto &aa:vfromnlist)
    {
        auto it=mfromok.find(aa.filename);           // 在容器一中用文件名查找。
        if (it !=mfromok.end())
        {   // 如果找到了，再判断文件时间。
            if (starg.checkmtime==true)
			{
				// 如果时间也相同，不需要下载，否则需要重新下载。
				if (it->second==aa.mtime) 
                    vtook.push_back(aa);    // 文件时间没有变化，不需要下载。
				else 
                    vdownload.push_back(aa);     // 需要重新下载。
			}
			else
			{
				vtook.push_back(aa);   // 不需要重新下载。
			}
        }
        else
        {   // 如果没有找到，把记录放入vdownload容器。
            vdownload.push_back(aa);
        }
    }

    // for(auto& aa:vdownload)
    // {
    //     logfile.write("带下载文件file：%s  ,时间：%s\n",aa.filename.c_str(),aa.mtime.c_str());
    // }

    return true;
}
//将容器三中的内容重新写入okfile,因为可能有文件时间更新                         
bool writetookfile()
{
    cofile ofile;    

    if (ofile.open(starg.okfilename)==false)
    {
      logfile.write("file.open(%s) failed.\n",starg.okfilename); return false;
    }

    for (auto &aa:vtook)
        ofile.writeline("<filename>%s</filename><mtime>%s</mtime>\n",aa.filename.c_str(),aa.mtime.c_str());

    ofile.closeandrename();

    return true;
}
//追加本次下载的信息到okfile                 
bool appendtookfile(st_fileinfo fileinfo)
{
    cofile ofile;

    // 以追加的方式打开文件，注意第二个参数一定要填false。
    if (ofile.open(starg.okfilename,false,ios::app)==false)
    {
      logfile.write("file.open(%s) failed.\n",starg.okfilename); return false;
    }

    ofile.writeline("<filename>%s</filename><mtime>%s</mtime>\n",fileinfo.filename.c_str(),fileinfo.mtime.c_str());

    return true;
}
void _help()
{
    printf("\n");
    printf("Using:/myproject/tools/bin/ftpgetfiles logfilename xmlbuffer\n\n");
    printf("Sample:/myproject/tools/bin/procctl 30 /myproject/tools/bin/ftpgetfiles /myproject/ftpgetfiles.log " \
              "\"<host>192.168.150.128:21</host><mode>1</mode>"\
              "<username>mysql</username><password>123456</password>"\
              "<remotepath>/tmp/ftp/server</remotepath><localpath>/tmp/ftp/client</localpath>"\
              "<matchname>*.TXT</matchname>"\
              "<ptype>1</ptype><okfilename>/idcdata/ftplist/ftpgetfiles_test.xml</okfilename>"\
              "<checkmtime>true</checkmtime>"\
              "<timeout>30</timeout><pname>ftpgetfiles_test</pname>\"\n\n\n");

    printf("本程序是通用的功能模块，用于把远程ftp服务端的文件下载到本地目录。\n");
    printf("logfilename是本程序运行的日志文件。\n");
    printf("xmlbuffer为文件下载的参数，如下：\n");
    printf("<host>192.168.150.128:21</host> 远程服务端的IP和端口。\n");
    printf("<mode>1</mode> 传输模式，1-被动模式，2-主动模式，缺省采用被动模式。\n");
    printf("<username>wucz</username> 远程服务端ftp的用户名。\n");
    printf("<password>oraccle</password> 远程服务端ftp的密码。\n");
    printf("<remotepath>/tmp/idc/surfdata</remotepath> 远程服务端存放文件的目录。\n");
    printf("<localpath>/idcdata/surfdata</localpath> 本地文件存放的目录。\n");
    printf("<matchname>SURF_ZH*.XML,SURF_ZH*.CSV</matchname> 待下载文件匹配的规则。"\
              "不匹配的文件不会被下载，本字段尽可能设置精确，不建议用*匹配全部的文件。\n");
    printf("<ptype>1</ptype> 文件下载成功后，远程服务端文件的处理方式："\
              "1-什么也不做；2-删除；3-备份，如果为3，还要指定备份的目录。\n");
    printf("<remotepathbak>/tmp/idc/surfdatabak</remotepathbak> 文件下载成功后，服务端文件的备份目录，"\
              "此参数只有当ptype=3时才有效。\n");
    printf("<okfilename>/idcdata/ftplist/ftpgetfiles_test.xml</okfilename> 已下载成功文件名清单，"\
              "此参数只有当ptype=1时才有效。\n");
    printf("<checkmtime>true</checkmtime> 是否需要检查服务端文件的时间，true-需要，false-不需要，"\
              "此参数只有当ptype=1时才有效，缺省为false。\n");
    printf("<timeout>30</timeout> 下载文件超时时间，单位：秒，视文件大小和网络带宽而定。\n");
    printf("<pname>ftpgetfiles_test</pname> 进程名，尽可能采用易懂的、与其它进程不同的名称，方便故障排查。\n\n\n");
}