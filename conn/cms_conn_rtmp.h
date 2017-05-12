/*
The MIT License (MIT)

Copyright (c) 2017- cms(hsc)

Author: hsc/kisslovecsh@foxmail.com

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
#ifndef __CMS_CONN_H__
#define __CMS_CONN_H__
#include <interface/cms_read_write.h>
#include <interface/cms_interf_conn.h>
#include <core/cms_buffer.h>
#include <protocol/cms_rtmp.h>
#include <common/cms_var.h>
#include <protocol/cms_amf0.h>
#include <flvPool/cms_flv_pool.h>
#include <protocol//cms_flv_transmission.h>
#include <strategy/cms_jitter.h>
#include <common/cms_type.h>
#include <protocol/cms_flv_pump.h>
#include <string>

class CRtmpProtocol;
class CFlvTransmission;
class CConnRtmp:public Conn,public CStreamInfo
{
public:
	CConnRtmp(RtmpType rtmpType,CReaderWriter *rw,std::string pullUrl,std::string pushUrl);
	~CConnRtmp();

	int doit();
	int handleEv(FdEvents *fe);
	int stop(std::string reason);
	std::string getUrl();
	std::string getPushUrl();
	std::string getRemoteIP();
	int doDecode(){return 0;};
	int doReadData(){return CMS_OK;};
	int doTransmission();
	int sendBefore(const char *data,int len){return 0;};
	void down8upBytes();

	//stream info 接口
	int		firstPlaySkipMilSecond();
	bool	isResetStreamTimestamp();
	bool	isNoTimeout();
	int		liveStreamTimeout();
	int 	noHashTimeout();
	bool	isRealTimeStream();
	int64   cacheTT();
	//std::string getRemoteIP() = 0;
	std::string getHost();
	void    makeOneTask();

	struct ev_loop  *evLoop();
	struct ev_io    *evReadIO();
	struct ev_io    *evWriteIO();

	void setEVLoop(struct ev_loop *loop);
	
	void setUrl(std::string url);		//拉流或者被推流或者被播放的地址
	void setPushUrl(std::string url);	//推流到其它服务的推流地址
	int  decodeMessage(RtmpMessage *msg);
	int  decodeMetaData(amf0::Amf0Block *block);
	int  decodeSetDataFrame(amf0::Amf0Block *block);
	int  setPublishTask();
	int  setPlayTask();
	void tryCreateTask();
private:
	int  decodeVideo(RtmpMessage *msg,bool &isSave);
	int  decodeAudio(RtmpMessage *msg,bool &isSave);
	int  decodeVideoAudio(RtmpMessage *msg);
	int	 doRead(bool isTimeout);
	int	 doWrite(bool isTimeout);
	void makeHash();
	void makePushHash();
	void justTick();

	struct ev_loop	*mloop;			//全局不属于本类
	struct ev_io	*mwatcherReadIO;	//虽然由外面创建 cms_conn_mgr 或者 cms_net_dispatch 但是最终属于本类
	struct ev_io	*mwatcherWriteIO;	//虽然由外面创建 cms_conn_mgr 或者 cms_net_dispatch 但是最终属于本类
	struct ev_timer *mwatcherTimer;

	uint64	mjustTickOld;
	uint64	mjustTick;

	bool		misStop;
	RtmpType	mrtmpType;
	
	int   miFirstPlaySkipMilSecond;
	bool  misResetStreamTimestamp;	
	bool  misNoTimeout;
	int   miLiveStreamTimeout;
	int   miNoHashTimeout;
	bool  misRealTimeStream;
	int64 mllCacheTT;

	bool misChangeMediaInfo;

	CRtmpProtocol	*mrtmp;
	CBufferReader	*mrdBuff;
	CBufferWriter	*mwrBuff;
	CReaderWriter	*mrw;
	std::string		murl;
	std::string		mremoteAddr;
	std::string		mremoteIP;
	std::string		mHost;
	HASH			mHash;
	uint32          mHashIdx;

	std::string		mstrHash;

	std::string		mstrPushUrl;
	HASH			mpushHash;

	bool			misPublish;		//是否是客户端publish
	bool			misPlay;		//是否是客户端播放
	bool            misPushFlv;		//是否往flvPool投递过数据
	bool			misPush;		//是否是push任务
	bool			misDown8upBytes;//是否统计过数据
	bool			misAddConn;		//是否发送数据的连接
	//速度统计
	int32			m10SecdownBytes;
	int32			m10SecUpBytes;
	int32			m10SecTick;

	int64           mllIdx;

	CFlvTransmission *mflvTrans;
	unsigned long  mspeedTick;

	CFlvPump		*mflvPump;
	int64			mcreateTT;

	int64			mtimeoutTick;
};
#endif
