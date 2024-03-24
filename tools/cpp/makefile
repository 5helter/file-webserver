########################
# 开发框架头,ftp工具等文件路径。
PUBINCL = -I/myproject/public

# 开发框架cpp文件名，直接和程序的源代码文件一起编译，没有采用链接库，是为了方便调试。
PUBCPP = /myproject/public/_public.cpp
#ftp工具
FTPCPP = /myproject/public/_ftp.cpp /myproject/public/libftp.a
#编译参数
PARAM=-g
#调度程序
all:procctl checkproc deletefiles gzipfiles ftpgetfiles ftpputfiles\
	tcpgetfiles tcpputfiles tcpfileserver inted webserver #httpservertest

procctl:procctl.cpp
	g++ $(PARAM) -o procctl procctl.cpp -lc
	cp -f procctl ../bin/.

checkproc:checkproc.cpp
	g++ $(PARAM) -o checkproc checkproc.cpp $(PUBCPP) $(PUBINCL) -lc
	cp -f checkproc ../bin/.

deletefiles:deletefiles.cpp
	g++ $(PARAM) -o deletefiles deletefiles.cpp $(PUBCPP) $(PUBINCL) -lc
	cp -f deletefiles ../bin/.

gzipfiles:gzipfiles.cpp
	g++ $(PARAM) -o gzipfiles gzipfiles.cpp $(PUBCPP) $(PUBINCL) -lc
	cp -f gzipfiles ../bin/.

ftpgetfiles:ftpgetfiles.cpp
	g++ $(PARAM) -o ftpgetfiles ftpgetfiles.cpp $(FTPCPP) $(PUBCPP) $(PUBINCL) -lc
	cp -f ftpgetfiles ../bin/.

ftpputfiles:ftpputfiles.cpp
	g++ $(PARAM) -o ftpputfiles ftpputfiles.cpp $(PUBCPP) $(FTPCPP) $(PUBINCL) -lc
	cp -f ftpputfiles ../bin/.
tcpgetfiles:tcpgetfiles.cpp
	g++ $(PARAM) -o tcpgetfiles tcpgetfiles.cpp $(PUBCPP) $(PUBINCL) -lc
	cp -f tcpgetfiles ../bin/.

tcpputfiles:tcpputfiles.cpp
	g++ $(PARAM) -o tcpputfiles tcpputfiles.cpp $(PUBCPP) $(PUBINCL) -lc
	cp -f tcpputfiles ../bin/.

tcpfileserver:tcpfileserver.cpp
	g++ $(PARAM) -o tcpfileserver tcpfileserver.cpp $(PUBCPP) $(PUBINCL) -lc
	cp -f tcpfileserver ../bin/.
inted:inted.cpp
	g++ $(PARAM) -o inted inted.cpp $(PUBCPP) $(PUBINCL) -lc
	cp -f inted ../bin/.

webserver:webserver.cpp
	g++ $(PARAM) -o webserver webserver.cpp $(PUBCPP) $(PUBINCL) -lc -lpthread --std=c++11
	cp -f webserver ../bin/. 

#用于测试的代码
# httpservertest:../test/httpservertest.cpp
# 	g++ $(PARAM) -o ../test/httpservertest ../test/httpservertest.cpp $(PUBCPP) $(PUBINCL) -lc

clean:
	rm -rf procctl ../bin/peocctl ../bin/checkproc ../bin/deletefiles ../bin/ftpgetfiles ../bin/ftpputfiles ../bin/tcpgetfiles ../tcpputfiles\
	../bin/tcpfileserver ../bin/inted ../bin/webserver
