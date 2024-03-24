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
//增量上传工具
map<string,string> mfromok;             // 容器一：存放已成功上传文件，从starg.okfilename参数指定的文件中加载。
list<struct st_fileinfo> vfromnlist;    // 容器二：上传前列出本地所有文件名的容器
list<struct st_fileinfo> vtook;         // 容器三：本次不需要上传的文件的容器。
list<struct st_fileinfo> vupload;       // 容器四：本次需要上传的文件的容器。
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
    int  ptype;                         // 上传后本地文件的处理方式：1-增量模式，不会改变内容；2-删除；3-备份。
    char remotepathbak[256];            // 上传后本地文件的备份目录。
    char okfilename[256];               // 已上传成功文件信息存放的文件。
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




bool loadlistfile();                      //加载所有文件到容器二
bool loadokfile();                        //加载okfile到容器一
bool compmap();                           //比较容器一，二得到容器三，四
bool writetookfile();                     //将容器三中的内容重新写入okfile,放置okfile无限增大
bool appendtookfile(st_fileinfo fileinfo);//追加本次上传的信息到okfile
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

    //进入ftp服务器待上传文件的目录
    if(ftp.chdir(starg.remotepath)==false)
    {
        logfile.write("ftp.chdir(%s) failed.\n",starg.remotepath);
    }

    // // 调用ftpclient.nlist()方法列出服务器目录中的文件名，保存在本地文件中
    // if(ftp.nlist(".",sformat("/tmp/nlist/ftpgetfiles_%d.nlist",getpid()))==false)
    // {
    //     logfile.write("ftp.nlist(%s) failed.\n",starg.remotepath,ftp.response()); 
    //     return -1;
    // }

    if(loadlistfile()==false)
    {
        logfile.write("获取本地文件列表失败.\n",starg.remotepath,ftp.response()); 
        return -1;
    }


    if(starg.ptype==1)
    {
        // // 加载starg.okfilename文件中的数据到容器vfromok中。
        loadokfile();

        //  // 比较vfromnlist和vfromok，得到vtook和vupload。
        compmap();

        //把容器vtook中的数据写入starg.okfilename文件，覆盖之前的旧starg.okfilename文件。
        writetookfile();

    }
    else
        vfromnlist.swap(vupload);   // 为了统一文件下载的代码，把容器二和容器四交换。

     pactive.uptatime();   // 更新进程的心跳。

    string strremotefilename,strlocalfilename;  //临时变量用于记录待下载名字

     for(auto& aa:vupload)
     {
        sformat(strremotefilename,"%s/%s",starg.remotepath,aa.filename.c_str());          // 拼接服务端全路径的文件名。
        sformat(strlocalfilename,"%s/%s",starg.localpath,aa.filename.c_str());    // 拼接本地全路径的文件名。

        // logfile.write("put %s ...",strlocalfilename.c_str());

        if(ftp.put(strlocalfilename,strremotefilename,false)==false)
        {
            logfile << "upload failed: " << ftp.response() << "\n"; 
            return -1;
        }

        pactive.uptatime();   // 更新进程的心跳。

        // 如果ptype==1，把上传成功的文件记录追加到starg.okfilename文件中。
        if (starg.ptype==1) 
            appendtookfile(aa);
        // ptype==2，删除服务端的文件。
        if (starg.ptype==2)
        {
            if(remove(strlocalfilename.c_str()))
            {
                logfile.write("pdeletefile: %s failed.\n",strremotefilename.c_str()); return -1;
            }
        }
         // ptype==3，把服务端的文件移动到备份目录。
        if (starg.ptype==3)
        {
            string strlocalfilenamebak=sformat("%s/%s",starg.remotepathbak,aa.filename.c_str());  // 生成全路径的备份文件名。
            if (renamefile(strlocalfilename,strlocalfilenamebak)==false)
            {
                logfile.write("renamefile: %s to %s) failed.\n",strremotefilename.c_str(),strlocalfilenamebak.c_str()); 
                return -1;
            }
        }

     }
    return 0;
}

void _help()
{
     printf("\n");
    printf("Using:/project/tools/bin/ftpputfiles logfilename xmlbuffer\n\n");

    printf("Sample:/project/tools/bin/procctl 30 /project/tools/bin/ftpputfiles /log/idc/ftpputfiles_surfdata.log "\
              "\"<host>127.0.0.1:21</host><mode>1</mode><username>mysql</username><password>123456</password>"\
              "<localpath>/tmp/idc/surfdata</localpath><remotepath>/idcdata/surfdata</remotepath>"\
              "<matchname>SURF_ZH*.JSON</matchname>"\
              "<ptype>1</ptype><localpathbak>/tmp/idc/surfdatabak</localpathbak>"\
              "<okfilename>/idcdata/ftplist/ftpputfiles_surfdata.xml</okfilename>"\
              "<timeout>80</timeout><pname>ftpputfiles_surfdata</pname>\"\n\n\n");

    printf("本程序是通用的功能模块，用于把本地目录中的文件上传到远程的ftp服务器。\n");
    printf("logfilename是本程序运行的日志文件。\n");
    printf("xmlbuffer为文件上传的参数，如下：\n");
    printf("<host>127.0.0.1:21</host> 远程服务端的IP和端口。\n");
    printf("<mode>1</mode> 传输模式，1-被动模式，2-主动模式，缺省采用被动模式。\n");
    printf("<username>wucz</username> 远程服务端ftp的用户名。\n");
    printf("<password>wuczpwd</password> 远程服务端ftp的密码。\n");
    printf("<remotepath>/tmp/ftpputest</remotepath> 远程服务端存放文件的目录。\n");
    printf("<localpath>/tmp/idc/surfdata</localpath> 本地文件存放的目录。\n");
    printf("<matchname>SURF_ZH*.JSON</matchname> 待上传文件匹配的规则。"\
           "不匹配的文件不会被上传，本字段尽可能设置精确，不建议用*匹配全部的文件。\n");
    printf("<ptype>1</ptype> 文件上传成功后，本地文件的处理方式：1-什么也不做；2-删除；3-备份，如果为3，还要指定备份的目录。\n");
    printf("<localpathbak>/tmp/idc/surfdatabak</localpathbak> 文件上传成功后，本地文件的备份目录，此参数只有当ptype=3时才有效。\n");
    printf("<okfilename>/idcdata/ftplist/ftpputfiles_surfdata.xml</okfilename> 已上传成功文件名清单，此参数只有当ptype=1时才有效。\n");
    printf("<timeout>80</timeout> 上传文件超时时间，单位：秒，视文件大小和网络带宽而定。\n");
    printf("<pname>ftpputfiles_surfdata</pname> 进程名，尽可能采用易懂的、与其它进程不同的名称，方便故障排查。\n\n\n");
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


    //调用Dir方法的加载所有文件到容器二
    cdir dir;
    if(dir.opendir(starg.localpath,"*")==false)
    {
        logfile.write(("读取%s failed"),starg.localpath); 
        return -1;
    }
    while(dir.readdir())
    {
        if(matchstr(dir.m_filename,starg.matchname)==false)
            continue;
        //无需判断是否有必要记录文件下载时间，记录下来损耗不大
        vfromnlist.emplace_back(dir.m_filename,dir.m_mtime);
    }

    return true;
}
//加载okfile到容器一
bool loadokfile()
{
    if (starg.ptype!=1) return true;

    mfromok.clear();

    cifile ifile;

    // 注意：如果程序是第一次运行，starg.okfilename是不存在的，并不是错误，所以也返回true。
    if ( (ifile.open(starg.okfilename))==false )  
        return true;


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
    vupload.clear();

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
                    vupload.push_back(aa);     // 需要重新下载。
			}
			else
			{
				vtook.push_back(aa);   // 不需要重新下载。
			}
        }
        else
        {   // 如果没有找到，把记录放入vupload容器。
            vupload.push_back(aa);
        }
    }
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