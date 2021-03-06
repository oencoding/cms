/*
The MIT License (MIT)

Copyright (c) 2017- cms(hsc)

Author: 天空没有乌云/kisslovecsh@foxmail.com

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include <config/cms_config.h>
#include <log/cms_log.h>
#include <conn/cms_conn_mgr.h>
#include <dispatch/cms_net_dispatch.h>
#include <dnscache/cms_dns_cache.h>
#include <ts/cms_hls_mgr.h>
#include <common/cms_shmmgr.h>
#include <common/cms_url.h>
#include <enc/cms_sha1.h>
#include <app/cms_server.h>
#include <flvPool/cms_flv_pool.h>
#include <taskmgr/cms_task_mgr.h>
#include <static/cms_static.h>
#include <net/cms_net_mgr.h>
#include <common/cms_time.h>
#include <app/cms_app_info.h>
#include <map>
#include <string>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/resource.h>
using namespace std;

map<string,string> mapAgrv;
#define ParamConfig		"-c"
#define ParamDaemon		"-d"
#define ParamTestServer "-t"
#define ParamTestUrl	"-i"
#define ParamTestNum	"-n"

void sample(char *app)
{
	printf("##### useage: %s -d [debug/nodebug] -c config.json [-t true -n 1000 -i rtmp://test.cms.com/live/test]\n", app);
}

void inorgSignal()
{
	signal(SIGPIPE, SIG_IGN);;
}

int daemon()
{
	int fd;
	int pid;

	switch(fork()) 
	{
	case -1:
		printf("***** %s(%d)-%s: fork() error: %s *****\n",
			__FILE__, __LINE__, __FUNCTION__, strerror(errno));
		return -1;

	case 0:
		break;

	default:
		printf("Daemon exit\n");
		exit(0);
	}

	pid = getpid();

	if (setsid() == -1) {
		printf("***** %s(%d)-%s: setsid() failed *****\n",
			__FILE__, __LINE__, __FUNCTION__);
		return -1;
	}

	umask(0);

	fd = open("/dev/null", O_RDWR);
	if (fd == -1) {
		printf("***** %s(%d)-%s: open(\"/dev/null\") failed *****\n",
			__FILE__, __LINE__, __FUNCTION__);
		return -1;
	}

	if (dup2(fd, STDIN_FILENO) == -1) {
		printf( "***** %s(%d)-%s: dup2(STDIN) failed *****\n",
			__FILE__, __LINE__, __FUNCTION__);
		return -1;
	}

	if (dup2(fd, STDOUT_FILENO) == -1) {
		printf("***** %s(%d)-%s: dup2(STDOUT) failed *****\n",
			__FILE__, __LINE__, __FUNCTION__);
		return -1;
	}

	if (fd > STDERR_FILENO) {
		if (close(fd) == -1) {
			printf( "***** %s(%d)-%s: close() failed ******\n",
				__FILE__, __LINE__, __FUNCTION__);
			return -1;
		}
	}
	return 0;
}

void initInstance()
{
	CConfig::instance();
	CFlvPool::instance();
	CConnMgrInterface::instance();
	CNetDispatch::instance();
	CDnsCache::instance();
	CShmMgr::instance();
	CServer::instance();
	CTaskMgr::instance();
	CStatic::instance();
	CNetMgr::instance();
	CMissionMgr::instance();
}

void parseVar(int num,char **argv)
{
	for (int i = 0; i < num; )
	{
		printf("param: %s %s\n",argv[i],argv[i+1]);
		mapAgrv.insert(make_pair(argv[i],argv[i+1]));
		i += 2;
	}
}

string getVar(string key)
{
	map<string,string>::iterator it = mapAgrv.find(key.c_str());
	if (it != mapAgrv.end())
	{
		return it->second;
	}
	return "";
}

void cycleServer()
{
#ifdef just_test__CMS_APP_DEBUG_SP__
	cmsSleep(100000);
#else
	do
	{
		cmsSleep(1000);
	} while (1);
#endif
	cms_timer_udp_thread_stop();
	cms_ev_timer_thread_stop();
	CNetMgr::instance()->stop();
	CStatic::instance()->stop();
	CFlvPool::instance()->stop();
	CMissionMgr::instance()->stop();
	CConnMgrInterface::instance()->stop();
	CTaskMgr::instance()->stop();
#ifdef _CMS_APP_USE_TIME_
	cmsTimeStop();
#endif
	//CServer::instance()->stop();
	cmsSleep(1000);
	cmsLogStop();	
}

void setRlimit()
{
	struct rlimit rlim;
	rlim.rlim_cur = 300000;
	rlim.rlim_max = 300000;
	setrlimit(RLIMIT_NOFILE, &rlim);

	rlim.rlim_cur = 0;
	rlim.rlim_max = 0;
	getrlimit(RLIMIT_NOFILE, &rlim);
	logs->debug("+++ open file cur %d,open file max %d +++\n",rlim.rlim_cur,rlim.rlim_max);
}

void createTestTask()
{
	for (int i = 0; i < gcmsTestNum; i++)
	{
		char szNum[20] = { 0 };
		snprintf(szNum, sizeof(szNum), "_%d", i);
		string hashUrl = gcmsTestUrl + szNum;
		printf(">>>>createTestTask hash url %s\n", hashUrl.c_str());
		CSHA1 sha;
		sha.write(hashUrl.c_str(), hashUrl.length());
		string strHash = sha.read();
		HASH hash = HASH((char *)strHash.c_str());
		CTaskMgr::instance()->createTask(hash, gcmsTestUrl, "", gcmsTestUrl, "", CREATE_ACT_PULL, false, false);
	}
}

int main(int argc,char *argv[])
{	
	if (argc % 2 != 1)
	{
		printf("***** main argc is error,should argc %% 2 == 1*****\n");
		return 0;
	}

#ifdef _CMS_APP_USE_TIME_
	cmsTimeRun();
#endif
	parseVar(argc-1,argv+1);
	string config = getVar(ParamConfig);
	if (config.empty())
	{
		sample(argv[0]);
		return 0;
	}
	string daemons = getVar(ParamDaemon);
	if (daemons != "debug")
	{
		daemon();
	}	
	string testServer = getVar(ParamTestServer);
	if (testServer == "true")
	{
		gcmsTestUrl = getVar(ParamTestUrl);
		if (gcmsTestUrl.empty())
		{
			printf("***** app as test server.but url is empty *****\n");
			return 0;
		}
		string testNum =  getVar(ParamTestNum);
		if (!testNum.empty())
		{
			gcmsTestNum = atoi(testNum.c_str());
		}
		if (gcmsTestNum <= 0)
		{
			gcmsTestNum = 100;
		}		
		gcmsTestServer = true;
		printf("##### app as test server.url %s,make task num %d #####\n", gcmsTestUrl.c_str(), gcmsTestNum);
	}

	inorgSignal();	
	if (!CConfig::instance()->init(config.c_str()))
	{
		return 0;
	}
	cmsLogInit(CConfig::instance()->clog()->path(),CConfig::instance()->clog()->level(),
		CConfig::instance()->clog()->console(),CConfig::instance()->clog()->size());
	setRlimit();
	//必须先初始化日志，否则会崩溃
	initInstance();	
	char *pPos = strrchr(argv[0],'/');
	if (pPos)
	{
		++pPos;
		CStatic::instance()->setAppName(pPos);
	}
	if (!CStatic::instance()->run())
	{
		logs->error("*** CStatic::instance()->run() fail ***");
		cmsSleep(1000*3);
		return 0;
	}
	if (!CFlvPool::instance()->run())
	{
		logs->error("*** CFlvPool::instance()->run() fail ***");
		cmsSleep(1000*3);
		return 0;
	}
	if (!CMissionMgr::instance()->run())
	{
		logs->error("*** CMissionMgr::instance()->run() fail ***");
		cmsSleep(1000*3);
		return 0;
	}
	if (!CConnMgrInterface::instance()->run())
	{
		logs->error("*** CConnMgrInterface::instance()->run() fail ***");
		cmsSleep(1000*3);
		return 0;
	}
	if (!CTaskMgr::instance()->run())
	{
		logs->error("*** CTaskMgr::instance()->run() fail ***");
		cmsSleep(1000*3);
		return 0;
	}
	if (!CServer::instance()->listenAll())
	{
		logs->error("*** CServer::instance()->listenAll() fail ***");
		cmsSleep(1000*3);
		return 0;
	}
	//作为压测服务
	if (gcmsTestServer)
	{
		LinkUrl linkUrl;
		if (!parseUrl(gcmsTestUrl, linkUrl))
		{
			printf("***** app as test server.but url %s parse error *****\n", gcmsTestUrl.c_str());
			return 0;
		}
		createTestTask();
	}
	cycleServer();
	logs->debug("cms app exit.");
}