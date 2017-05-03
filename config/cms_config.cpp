#include <config/cms_config.h>
#include <json/json.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string>
#include <algorithm>
using namespace std;


CAddr::CAddr(char *addr,int defaultPort)
{	
	assert(strlen(addr) < 128);
	mAddr = new char[128];
	memset(mAddr,0,128);
	mHost = new char[128];
	memset(mHost,0,128);
	strcpy(mAddr,addr);
	char *p = strstr(addr,":");
	if (p == NULL)
	{
		strcpy(mHost,addr);
		mPort = defaultPort;
	}
	else
	{
		memcpy(mHost,addr,p-addr);
		mPort = atoi(p+1);
	}
	printf("### CAddr addr=%s,defaultPort=%d,host=%s,port=%d ###\n",mAddr,defaultPort,mHost,mPort);
}

CAddr::~CAddr()
{
	if (mAddr)
	{
		delete []mAddr;
		mAddr = NULL;
	}
	if (mHost)
	{
		delete []mHost;
		mAddr = NULL;
	}
}

char *CAddr::addr()
{
	printf("### CAddr get addr=%s ###\n",mAddr);
	return mAddr;
}

char *CAddr::host()
{
	return mHost;
}

int  CAddr::port()
{
	return mPort;
}

LogLevel getLevel(string level) 
{
	transform(level.begin(), level.end(), level.begin(), (int (*)(int))tolower);
	if (level == "off")
	{
		return OFF;
	}
	else if (level == "debug")
	{
		return DEBUG;
	}
	else if (level == "info")
	{
		return INFO;
	}
	else if (level == "warn")
	{
		return WARN;
	}
	else if (level == "error")
	{
		return ERROR1;
	}
	else if (level == "fatal")
	{
		return FATAL;
	}
	else if (level == "all_level")
	{
		return ALL_LEVEL;
	}
	return DEBUG;
}

Clog::Clog(char *path,char *level,bool console,int size)
{
	assert(path!=NULL);
	int len = strlen(path);
	mpath = new char[len+1];
	memcpy(mpath,path,len);
	mpath[len] = '\0';
	if (size < 500*1024*1024)
	{
		msize = 500*1024*1024;
	}
	msize = size;
	mlevel = getLevel(level);
	mconsole = console;
}

Clog::~Clog()
{
	if (mpath)
	{
		delete []mpath;
		mpath = NULL;
	}
}

char *Clog::path()
{
	return mpath;
}

int	Clog::size()
{
	return msize;
}

LogLevel Clog::level()
{
	return mlevel;
}

bool Clog::console()
{
	return mconsole;
}

CConfig *CConfig::minstance = NULL;
CConfig *CConfig::instance()
{
	if (minstance == NULL)
	{
		minstance = new CConfig();
	}
	return minstance;
}

void CConfig::freeInstance()
{
	if (minstance)
	{
		delete minstance;
		minstance = NULL;
	}
}

CConfig::CConfig()
{

}

CConfig::~CConfig()
{
	if (mHttp)
	{
		delete mHttp;
	}
	if (mHttps)
	{
		delete mHttps;
	}
	if (mRtmp)
	{
		delete mRtmp;
	}
	if (mQuery)
	{
		delete mQuery;
	}
	if (mlog)
	{
		delete mlog;
	}
}

bool	CConfig::init(const char *configPath)
{
	FILE *fp = fopen(configPath,"rb");
	if (fp == NULL)
	{
		printf("*** [CConfig::init] open file %s fail,errno=%d,strerrno=%s ***\n",
			configPath,errno,strerror(errno));
		return false;
	}
	fseek(fp,0,SEEK_END);
	long size = ftell(fp);
	if (size <= 0)
	{
		printf("*** [CConfig::init] file %s is empty=%ld ***\n",
			configPath,size);
		fclose(fp);
		return false;
	}
	char *data = new char[size+1];
	fseek(fp,0,SEEK_SET);
	int n = fread(data,1,size,fp);
	if (n != size)
	{
		printf("*** [CConfig::init] fread file %s fail,errno=%d,strerrno=%s ***\n",
			configPath,errno,strerror(errno));
		fclose(fp);
		return false;
	}
	data[size] = '\0';
	fclose(fp);
	Json::Reader reader;
	Json::Value  root;
	if (!reader.parse(data,root,false))
	{
		printf("*** [CConfig::init] config file %s parse json fail %s ***\n",
			configPath,data);
		return false;
	}
	//�����˿�
	Json::Value value = root["listen"];
	if (!value.isObject())
	{
		printf("*** [CConfig::init] config file %s do not have [listen] ***\n",
			configPath);
		return false;
	}
	if (!value["http"].isString() || 
		!value["https"].isString() ||
		!value["rtmp"].isString() ||
		!value["query"].isString())
	{
		printf("*** [CConfig::init] config file %s listen term do not have http/https/rtmp/query ***\n",
			configPath);
		return false;
	}
	mHttp = new CAddr((char *)value["http"].asString().c_str(),80);
	mHttps = new CAddr((char *)value["https"].asString().c_str(),443);
	mRtmp = new CAddr((char *)value["rtmp"].asString().c_str(),1935);
	mQuery = new CAddr((char *)value["query"].asString().c_str(),8981);
	//��־
	value = root["log"];
	if (!value.isObject())
	{
		printf("*** [CConfig::init] config file %s do not have [log] ***\n",
			configPath);
		return false;
	}
	if (!value["file"].isString() ||
		!value["size"].isNumeric() ||
		!value["level"].isString())
	{
		printf("*** [CConfig::init] config file %s [log] term path/size error ***\n",
			configPath);
		return false;
	}
	mlog = new Clog((char *)value["file"].asString().c_str(),
		(char *)value["level"].asString().c_str(),
		value["console"].isBool()?value["console"].asBool():false,
		value["size"].asInt());
	return true;
}

CAddr   *CConfig::addrHttp()
{
	return mHttp;
}

CAddr	*CConfig::addrHttps()
{
	return mHttps;
}

CAddr	*CConfig::addrRtmp()
{
	return mRtmp;
}

CAddr	*CConfig::addrQuery()
{
	return mQuery;
}

Clog	*CConfig::clog()
{
	return mlog;
}
