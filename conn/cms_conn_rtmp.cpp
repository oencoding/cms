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
#include <conn/cms_conn_rtmp.h>
#include <log/cms_log.h>
#include <common/cms_utility.h>
#include <ev/cms_ev.h>
#include <protocol/cms_flv.h>
#include <common/cms_char_int.h>
#include <taskmgr/cms_task_mgr.h>
#include <static/cms_static.h>
#include <net/cms_net_mgr.h>
#include <enc/cms_sha1.h>
#include <ts/cms_hls_mgr.h>
#include <app/cms_app_info.h>
#include <assert.h>
#include <stdlib.h>
using namespace std;

CConnRtmp::CConnRtmp(HASH &hash,RtmpType rtmpType,CReaderWriter *rw,std::string pullUrl,std::string pushUrl)
{
	char remote[23] = {0};
	rw->remoteAddr(remote,sizeof(remote));
	mremoteAddr = remote;
	size_t pos = mremoteAddr.find(":");
	if (pos == string::npos)
	{
		mremoteIP = mremoteAddr;
	}
	else
	{
		mremoteIP = mremoteAddr.substr(0,pos);
	}	
	mrdBuff = new CBufferReader(rw,DEFAULT_BUFFER_SIZE);
	assert(mrdBuff);
	mwrBuff = new CBufferWriter(rw,DEFAULT_BUFFER_SIZE);
	assert(mwrBuff);
	mrw = rw;
	mrtmp = new CRtmpProtocol(this,rtmpType,mrdBuff,mwrBuff,rw,mremoteAddr);
	murl = pullUrl;
	mwatcherReadIO = NULL;
	mwatcherWriteIO = NULL;
	misChangeMediaInfo = false;
	miFirstPlaySkipMilSecond = 3000;
	misResetStreamTimestamp = false;	
	misNoTimeout = false;
	miLiveStreamTimeout = 1000*60*10;
	miNoHashTimeout = 1000*3;
	misRealTimeStream = false;
	mllCacheTT = 1000*10;
	misPublish = false;
	misPlay = false;
	mllIdx = 0;
	misPushFlv = false;
	misDown8upBytes = false;
	misAddConn = false;
	mflvTrans = new CFlvTransmission(mrtmp, mrtmpType == RtmpClient2Publish);
	misStop = false;
	mjustTickOld = 0;
	mjustTick = 0;
	mrtmpType = rtmpType;
	misPush = false;
	mspeedTick = 0;
	mcreateTT = getTimeUnix();
	mflvPump = NULL;
	mtimeoutTick = getTimeUnix();

	//速度统计
	mxSecdownBytes = 0;
	mxSecUpBytes = 0;
	mxSecTick = 0;
	//要么是推流任务 要么是拉流任务
	if (gcmsTestServer)
	{
		mHash = hash;
	}

	if (!pullUrl.empty())
	{
		makeHash();
		LinkUrl linkUrl;
		if (parseUrl(pullUrl,linkUrl))
		{
			mHost = linkUrl.host;
		}		
	}
	if (!pushUrl.empty())
	{
		setPushUrl(pushUrl);
	}
	misCreateHls = false;
}

CConnRtmp::~CConnRtmp()
{	
	logs->debug("######### %s [CConnRtmp::~CConnRtmp] %s rtmp %s enter ",
		mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str());
	if (mwatcherReadIO)
	{
		if (mrw->netType() == NetTcp || (mrw->netType() == NetUdp && mrw->fd() > 0))//listen 返回的 udp 不调用
		{
			CNetMgr::instance()->cneStop(mwatcherReadIO);
		}
		freeCmsNetEv(mwatcherReadIO);
		logs->debug("######### %s [CConnRtmp::~CConnRtmp] %s rtmp %s stop read io ",
			mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str());
	}
	if (mwatcherWriteIO)
	{
		if (mrw->netType() == NetTcp || (mrw->netType() == NetUdp && mrw->fd() > 0))//listen 返回的 udp 不调用
		{
			CNetMgr::instance()->cneStop(mwatcherWriteIO);
		}		
		freeCmsNetEv(mwatcherWriteIO);
		logs->debug("######### %s [CConnRtmp::~CConnRtmp] %s rtmp %s stop write io ",
			mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str());
	}
	delete mflvTrans;
	delete mrtmp;
	delete mrdBuff;
	delete mwrBuff;
	if (mflvPump)
	{
		delete mflvPump;
	}
	mrw->close();
	if (mrw->netType() == NetTcp)//udp 不调用
	{
		//udp 连接由udp模块自身管理 不需要也不能由外部释放
		delete mrw;
	}
}

int CConnRtmp::doit()
{
	if (mrtmpType == RtmpClient2Publish)
	{
		if (!CTaskMgr::instance()->pushTaskAdd(mpushHash,this))
		{
			logs->warn("######### %s [CConnRtmp::doit] %s rtmp %s push task is exist %s ",
				mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str(),mstrPushUrl.c_str());
			return CMS_ERROR;
		}
		else
		{
			misPush = true;
		}
	}
	else if (mrtmpType == RtmpClient2Play)
	{
		if (/*!CTaskMgr::instance()->pullTaskAdd(mHash,this)*/setPlayTask() != CMS_OK)
		{
			logs->warn("######### %s [CConnRtmp::doit] %s rtmp %s pull task is exist %s ",
				mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str(),mstrPushUrl.c_str());
			return CMS_ERROR;
		}
	}
	mrtmp->run();
	return CMS_OK;
}

int CConnRtmp::stop(std::string reason)
{	
	//可能会被调用两次,任务断开时,正常调用一次 reason 为空,
	//主动断开时,会调用,reason 是调用原因
	if (reason.empty())
	{
		logs->debug("%s [CConnRtmp::stop] %s rtmp %s has been stop,is push task %s ",
			mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str(), misPush?"true":"false");
		if (misPushFlv)
		{
			mflvPump->stop();
		}
		if (misPlay || misPublish)
		{
			CTaskMgr::instance()->pullTaskDel(mHash);
		}
		if (misPush)
		{
			CTaskMgr::instance()->pushTaskDel(mpushHash);
			tryCreatePushTask(true);
		}
		if (misDown8upBytes)
		{
			down8upBytes();
			makeOneTaskDownload(mHash,0,true);
		}
		if (misAddConn)
		{
			makeOneTaskupload(mHash,0,PACKET_CONN_DEL);
		}		
	}
	if (!reason.empty())
	{
		logs->error("%s [CConnRtmp::stop] %s rtmp %s stop with reason: %s ***",
			mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str(),reason.c_str());
	}
	misStop = true;

	if (misCreateHls)
	{
		CMissionMgr::instance()->destroy(mHashIdx,mHash);
	}

	return CMS_OK;
}

int CConnRtmp::handleEv(FdEvents *fe)
{
	if (misStop)
	{
		return CMS_ERROR;
	}
	
	if (fe->events & EventWrite || fe->events & EventWait2Write)
	{
		if (fe->events & EventWait2Write && fe->watcherWCmsTimer !=  mrtmp->cmsTimer2Write())
		{
			//应该是旧的socket号的消息
			return CMS_OK;
		}
		else if (fe->events & EventWrite && mwatcherWriteIO != fe->watcherWriteIO)
		{
			//应该是旧的socket号的消息
			return CMS_OK;
		}
		return doWrite(fe->events & EventWait2Write);
	}
	if (fe->events & EventRead || fe->events & EventWait2Read)
	{
		if (fe->events & EventWait2Read && fe->watcherRCmsTimer !=  mrtmp->cmsTimer2Read())
		{
			//应该是旧的socket号的消息
			return CMS_OK;
		}
		else if (fe->events & EventRead && mwatcherReadIO != fe->watcherReadIO)
		{
			//应该是旧的socket号的消息
			return CMS_OK;
		}
		return doRead(fe->events & EventWait2Read);
	}
	if (fe->events & EventJustTick)
	{
		justTick();
	}
	if (fe->events & EventErrot)
	{
		logs->error("%s [CConnRtmp::handleEv] %s rtmp %s handlEv recv event error ***",
			mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str());
		return CMS_ERROR;
	}
	return CMS_OK;
}

int CConnRtmp::doRead(bool isTimeout)
{
// 	logs->debug("%s [CConnRtmp::doRead] rtmp %s doRead",
// 		mremoteAddr.c_str(),mrtmp->getRtmpType().c_str());
	if (isTimeout)
	{
		int64 tn = getTimeUnix();
		if (tn - mtimeoutTick > 10)
		{
			logs->error("%s [CConnRtmp::doRead] %s rtmp %s is timeout ***",
				mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str());
			return CMS_ERROR;
		}
	}	

	int ret = mrtmp->want2Read(isTimeout);

	return ret;
}

int CConnRtmp::doWrite(bool isTimeout)
{
	mjustTick++;
	int ret = mrtmp->want2Write(isTimeout);
	mjustTick--;	
	return ret;
}

void CConnRtmp::justTick()
{
	if (mjustTick == 0)
	{
		mjustTickOld = mjustTick;
	}
	else
	{
		logs->debug("%s [CConnRtmp::justTick] rtmp %s o no,mjustTick=%llu,mjustTickOld=%llu",
				mremoteAddr.c_str(),mrtmp->getRtmpType().c_str(),mjustTick,mjustTickOld);
	}
}

cms_net_ev *CConnRtmp::evReadIO(cms_net_ev *ev)
{
	if (mwatcherReadIO == NULL)
	{
		if (ev != NULL)
		{
			//自定义的socket 不创建
			atomicInc(ev);		//计数器加1
			mwatcherReadIO = ev;
		}
		else
		{			
			logs->debug("%s [CConnRtmp::evReadIO] rtmp %s set read event %d",
				mremoteAddr.c_str(),mrtmp->getRtmpType().c_str(),mrw->fd());

			mwatcherReadIO = mallcoCmsNetEv();
			initCmsNetEv(mwatcherReadIO,readEV,mrw->fd(),EventRead);
			CNetMgr::instance()->cneStart(mwatcherReadIO);
		}
	}
	return mwatcherReadIO;
}

cms_net_ev *CConnRtmp::evWriteIO(cms_net_ev *ev)
{
	if (mwatcherWriteIO == NULL)
	{
		if (ev != NULL)
		{
			//自定义的socket 不创建
			atomicInc(ev);		//计数器加1
			mwatcherWriteIO = ev;
			//if (mrw->netType() == NetUdp)
			//{
			//	writeEV(mwatcherWriteIO,EventWrite); //对于udp首次需要投递写事件,理论上不会进来
			//}
		}
		else
		{			
			mwatcherWriteIO = mallcoCmsNetEv();
			initCmsNetEv(mwatcherWriteIO,writeEV,mrw->fd(),EventWrite);
			CNetMgr::instance()->cneStart(mwatcherWriteIO);			
		}
	}
	return mwatcherWriteIO;
}

int CConnRtmp::decodeMessage(RtmpMessage *msg)
{
	bool isSave = false;
	int ret = CMS_OK;
	assert(msg);
	if (msg->dataLen == 0 || msg->buffer == NULL)
	{
		return CMS_OK;
	}
	switch (msg->msgType)
	{
	case MESSAGE_TYPE_CHUNK_SIZE:
		{
			ret = mrtmp->decodeChunkSize(msg);
		}
		break;
	case MESSAGE_TYPE_ABORT:
		{
			logs->debug("%s [CConnRtmp::decodeMessage] %s rtmp %s received abort message,discarding.",
				mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str());
		}
		break;
	case MESSAGE_TYPE_ACK:
		{
			//logs->debug("%s [CConnRtmp::decodeMessage] rtmp %s received ack message,discarding.",
			//	mremoteAddr.c_str(),mrtmp->getRtmpType().c_str());
		}
		break;
	case MESSAGE_TYPE_USER_CONTROL:
		{
			ret = mrtmp->handleUserControlMsg(msg);
		}
		break;
	case MESSAGE_TYPE_WINDOW_SIZE:
		{
			ret = mrtmp->decodeWindowSize(msg);
		}
		break;
	case MESSAGE_TYPE_BANDWIDTH:
		{
			ret = mrtmp->decodeBandWidth(msg);
		}
		break;
	case MESSAGE_TYPE_DEBUG:
		{
			logs->debug("%s [CConnRtmp::decodeMessage] %s rtmp %s received debug message,discarding.",
				mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str());
		}
		break;	
	case MESSAGE_TYPE_AMF3_SHARED_OBJECT:
		{
			logs->debug("%s [CConnRtmp::decodeMessage] %s rtmp %s received amf3 share object message,discarding.",
				mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str());
		}
		break;	
	case MESSAGE_TYPE_INVOKE:
		{
			ret = mrtmp->decodeAmf03(msg,false);
		}
		break;
	case MESSAGE_TYPE_AMF0_SHARED_OBJECT:
		{
			logs->debug("%s [CConnRtmp::decodeMessage] %s rtmp %s received amf0 share object message,discarding.",
				mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str());
		}
		break;
	case MESSAGE_TYPE_AMF0:
		{
			ret = mrtmp->decodeAmf03(msg,false);
		}
		break;
	case MESSAGE_TYPE_AMF3:
		{
			ret = mrtmp->decodeAmf03(msg,true);
		}
	case MESSAGE_TYPE_FLEX:
		{
			logs->debug("%s [CConnRtmp::decodeMessage] %s rtmp %s received type flex message,discarding.",
				mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str());
		}
		break;
	case MESSAGE_TYPE_AUDIO:
		{
			//logs->debug("%s [CConnRtmp::decodeMessage] rtmp %s received audio message,discarding.",
			//	mremoteAddr.c_str(),mrtmp->getRtmpType().c_str());
			ret = decodeAudio(msg,isSave);
		}
		break;
	case MESSAGE_TYPE_VIDEO:
		{
			//logs->debug("%s [CConnRtmp::decodeMessage] rtmp %s received video message,discarding.",
			//	mremoteAddr.c_str(),mrtmp->getRtmpType().c_str());
			ret = decodeVideo(msg,isSave);
		}
		break;	
	case MESSAGE_TYPE_STREAM_VIDEO_AUDIO:
		{
			//logs->debug("%s [CConnRtmp::decodeMessage] rtmp %s received video audio message,discarding.",
			//	mremoteAddr.c_str(),mrtmp->getRtmpType().c_str());
			ret = decodeVideoAudio(msg);
		}
		break;
	default:
		logs->error("*** %s [CConnRtmp::decodeMessage] %s rtmp %s received unkown message type %d ***",
			mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str(),msg->msgType);
	}
	if (!isSave)
	{
		delete[] msg->buffer;
		msg->buffer = NULL;
		msg->bufLen = 0;
	}
	return ret;
}

int  CConnRtmp::decodeVideo(RtmpMessage *msg,bool &isSave)
{
	mtimeoutTick = getTimeUnix();

	mrtmp->shouldCloseNodelay();
	misChangeMediaInfo = false;
	int ret = mflvPump->decodeVideo(msg->buffer,msg->dataLen,msg->absoluteTimestamp,misChangeMediaInfo);
	if (ret == 1)
	{
		isSave = true;
		misPushFlv = true;
	}
	if (!misCreateHls && false)
	{
		misCreateHls = true;
		LinkUrl linkUrl;
		parseUrl(murl,linkUrl);
		std::string hlsUrl;
		hlsUrl = "http://" + linkUrl.host;
		hlsUrl += "/" + linkUrl.app;
		hlsUrl += "/";
		size_t pos = linkUrl.instanceName.find("?");
		if (pos != std::string::npos)
		{
			hlsUrl += linkUrl.instanceName.substr(0,pos);
		}
		else
		{
			hlsUrl += linkUrl.instanceName;
		}
		hlsUrl += "/online.m3u8";
		logs->debug("%s [CConnRtmp::decodeVideoAudio] %s rtmp %s m3u8 url %s",
			mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str(),hlsUrl.c_str());
		CMissionMgr::instance()->create(mHashIdx,mHash,hlsUrl,3,3,5);
	}
	return CMS_OK;
}

int  CConnRtmp::decodeAudio(RtmpMessage *msg,bool &isSave)
{
	mtimeoutTick = getTimeUnix();

	mrtmp->shouldCloseNodelay();
	misChangeMediaInfo = false;
	int ret = mflvPump->decodeAudio(msg->buffer,msg->dataLen,msg->absoluteTimestamp,misChangeMediaInfo);
	if (ret == 1)
	{
		isSave = true;
		misPushFlv = true;
	}

	if (!misCreateHls && false)
	{
		misCreateHls = true;
		LinkUrl linkUrl;
		parseUrl(murl,linkUrl);
		std::string hlsUrl;
		hlsUrl = "http://" + linkUrl.host;
		hlsUrl += "/" + linkUrl.app;
		hlsUrl += "/";
		size_t pos = linkUrl.instanceName.find("?");
		if (pos != std::string::npos)
		{
			hlsUrl += linkUrl.instanceName.substr(0,pos);
		}
		else
		{
			hlsUrl += linkUrl.instanceName;
		}
		hlsUrl += "/online.m3u8";
		logs->debug("%s [CConnRtmp::decodeVideoAudio] %s rtmp %s m3u8 url %s",
			mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str(),hlsUrl.c_str());
		CMissionMgr::instance()->create(mHashIdx,mHash,hlsUrl,3,3,5);
	}
	return CMS_OK;
}

int  CConnRtmp::decodeVideoAudio(RtmpMessage *msg)
{	
	uint32 uiHandleLen = 0;
	uint32 uiOffset;
	uint32 tagLen;
	uint32 frameLen;
	char pp[4];
	char *p;
	bool isSave;
	while (uiHandleLen < msg->dataLen)
	{
		uint32 dataType = (uint32)msg->buffer[0];
		tagLen = bigUInt24(msg->buffer+uiHandleLen+1);
		if (msg->dataLen < uiHandleLen+11+tagLen+4)
		{
			logs->error("%s [CConnRtmp::decodeVideoAudio] %s rtmp %s video audio check fail ***",
				mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str());
			return CMS_ERROR;
		}
		//时间戳
		p = msg->buffer+uiHandleLen+1+3;
		pp[2] = p[0];
		pp[1] = p[1];
		pp[0] = p[2];
		pp[3] = p[3];
		
		uiOffset = uiHandleLen+11+tagLen;
		frameLen = bigUInt32(msg->buffer+uiOffset);
		if (frameLen != tagLen+11)
		{
			logs->error("%s [CConnRtmp::decodeVideoAudio] %s rtmp %s video audio tagLen=%u,frameLen=%u ***",
				mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str(),tagLen,frameLen);
			return CMS_ERROR;
		}
		if (tagLen == 0)
		{
			uiHandleLen += (11+tagLen+1);
			continue;
		}
		RtmpMessage *rm = new RtmpMessage;
		rm->buffer = new char[tagLen];
		memcpy(rm->buffer,msg->buffer+uiHandleLen+11,tagLen);
		rm->dataLen = tagLen;
		rm->msgType = dataType;
		rm->streamId = msg->streamId;
		rm->absoluteTimestamp = littleInt32(pp);
		isSave = false;
		if (dataType == MESSAGE_TYPE_VIDEO)
		{
			if (decodeVideo(rm,isSave) == CMS_ERROR)
			{
				delete[] rm->buffer;
				delete rm;
				return CMS_ERROR;
			}
		}
		else if (dataType == MESSAGE_TYPE_AUDIO)
		{
			if (decodeAudio(rm,isSave) == CMS_ERROR)
			{
				delete[] rm->buffer;
				delete rm;
				return CMS_ERROR;
			}
		}
		if (!isSave)
		{
			delete[] rm->buffer;
		}
		delete rm;
		uiHandleLen += (11+tagLen+1);
	}
	return CMS_OK;
}


int CConnRtmp::decodeMetaData(amf0::Amf0Block *block)
{
	mtimeoutTick = getTimeUnix();

	string strMetaData = amf0::amf0Block2String(block);
	int len = strMetaData.length();
	char *data = new char[len];
	memcpy(data,strMetaData.c_str(),len);

	int ret = mflvPump->decodeMetaData(data,len,misChangeMediaInfo);
	if (ret == 1)
	{
		misPushFlv = true;
	}
	delete[] data;
	return CMS_OK;
}

int CConnRtmp::decodeSetDataFrame(amf0::Amf0Block *block)
{
	amf0::amf0BlockRemoveNode(block,0);	
	amf0::Amf0Data *data = amf0::amf0BlockGetAmf0Data(block,1);
	amf0::Amf0Data *objectEcma = amf0::amf0EcmaArrayNew();
	amf0::Amf0Node *node = amf0::amf0ObjectFirst(data);
	while (node)
	{
		amf0::Amf0Data *nodeName = amf0ObjectGetName(node);
		amf0::Amf0Data *nodeData = amf0ObjectGetData(node);
		amf0::Amf0Data *cloneData = amf0::amf0DataClone(nodeData);
		amf0::amf0ObjectAdd(objectEcma,(const char *)nodeName->string_data.mbstr,cloneData);
		node = amf0::amf0ObjectNext(node);
	}

	amf0::Amf0Block *blockMetaData = amf0::amf0BlockNew();
	amf0::amf0BlockPush(blockMetaData,amf0::amf0StringNew((amf0::uint8 *)"onMetaData",10));
	amf0::amf0BlockPush(blockMetaData,objectEcma);

	std::string strMetaData = amf0::amf0Block2String(blockMetaData);

	amf0::amf0BlockRelease(blockMetaData);

	int len = strMetaData.length();
	char *dm = new char[len];
	memcpy(dm,strMetaData.c_str(),len);
	int ret = mflvPump->decodeMetaData(dm,len,misChangeMediaInfo);
	if (ret == 1)
	{
		misPushFlv = true;
	}
	delete[] dm;
	return CMS_OK;
}

int CConnRtmp::doTransmission()
{
	bool isSendData = false;
	int ret = mflvTrans->doTransmission(isSendData);
	if (!misAddConn && (ret == 1 || ret == 0))
	{
		misAddConn = true;
		makeOneTaskupload(mHash,0,PACKET_CONN_ADD);
	}
	if (isSendData)
	{
		//logs->debug("%s [CConnRtmp::doTransmission] %s rtmp do send data",
		//	mremoteAddr.c_str(), murl.c_str());
		mtimeoutTick = getTimeUnix();
	}
	else
	{
		//logs->debug("%s [CConnRtmp::doTransmission] %s rtmp not send data",
		//	mremoteAddr.c_str(), murl.c_str());
	}
	return ret;
}

std::string CConnRtmp::getUrl()
{
	if (!mstrPushUrl.empty())
	{
		return mstrPushUrl;
	}
	return murl;
}

std::string CConnRtmp::getPushUrl()
{
	return mstrPushUrl;
}

std::string CConnRtmp::getRemoteIP()
{
	return mremoteIP;
}

void CConnRtmp::setUrl(std::string url)
{
	if (!url.empty())
	{
		LinkUrl linkUrl;
		if (parseUrl(url,linkUrl))
		{
			mHost = linkUrl.host;
		}
		murl = url;
		makeHash();
	}
	else
	{
		logs->error("***** %s [CConnRtmp::setUrl] %s rtmp %s url is empty *****",
			mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str());
	}
}

void CConnRtmp::setPushUrl(std::string url)
{
	if (!url.empty())
	{
		mstrPushUrl = url;
		makePushHash();
	}
	else
	{
		logs->error("***** %s [CConnRtmp::setPushUrl] %s rtmp %s url is empty *****",
			mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str());
	}
}

int CConnRtmp::setPublishTask()
{	
	if (!CTaskMgr::instance()->pullTaskAdd(mHash,this))
	{
		logs->error("***** %s [CConnRtmp::setPublishTask] %s rtmp %s publish task is exist *****",
			mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str());
		return CMS_ERROR;
	}
	misPublish = true;
	mrw->setReadBuffer(1024*32);

	std::string modeName = "CConnRtmp "+mrtmp->getRtmpType();
	mflvPump = new CFlvPump(this,mHash,mHashIdx,mremoteAddr,modeName,murl);
	mflvPump->setPublish();
	tryCreatePushTask();
	return CMS_OK;
}

int CConnRtmp::setPlayTask()
{	
	if (!CTaskMgr::instance()->pullTaskAdd(mHash,this))
	{
		logs->error("***** %s [CConnRtmp::setPlayTask] %s rtmp %s task is exist *****",
			mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str());
		return CMS_ERROR;
	}
	misPlay = true;
	mrw->setReadBuffer(1024*32);
	std::string modeName = "CConnRtmp "+mrtmp->getRtmpType();
	mflvPump = new CFlvPump(this,mHash,mHashIdx,mremoteAddr,modeName,murl);
	return CMS_OK;
}

void CConnRtmp::tryCreatePullTask()
{
	if (!CTaskMgr::instance()->pullTaskIsExist(mHash))
	{
		CTaskMgr::instance()->createTask(mHash,murl,"",murl,"",CREATE_ACT_PULL,false,false);
	}
}

void CConnRtmp::tryCreatePushTask(bool isRetry/* = false*/)
{
	if (isRetry)
	{
		if (CTaskMgr::instance()->pullTaskIsExist(mHash))
		{
			if (!CTaskMgr::instance()->pushTaskIsExist(mpushHash))
			{
				logs->debug("%s [CConnRtmp::tryCreatePushTask] %s rtmp %s need to retry to publish.",
					mremoteAddr.c_str(), murl.c_str(), mrtmp->getRtmpType().c_str());
				CTaskMgr::instance()->createTask(mHash, murl, murl, murl, "", CREATE_ACT_PUSH, false, false);
			}
			else
			{
				logs->debug("%s [CConnRtmp::tryCreatePushTask] %s rtmp %s publish task is exist.no need to publish.",
					mremoteAddr.c_str(), murl.c_str(), mrtmp->getRtmpType().c_str());
			}
		}
		else
		{
			logs->debug("%s [CConnRtmp::tryCreatePushTask] %s rtmp %s pull task is exist.no need to publish.",
				mremoteAddr.c_str(), murl.c_str(), mrtmp->getRtmpType().c_str());
		}
	}
	else
	{
		logs->debug("%s [CConnRtmp::tryCreatePushTask] %s rtmp %s need to be published.",
			mremoteAddr.c_str(), murl.c_str(), mrtmp->getRtmpType().c_str());
		CTaskMgr::instance()->createTask(mHash, murl, murl, murl, "", CREATE_ACT_PUSH, false, false);
	}
}

void CConnRtmp::makeHash()
{
	HASH tmpHash;
	if (mHash == tmpHash)
	{
		string hashUrl = readHashUrl(murl);
		CSHA1 sha;
		sha.write(hashUrl.c_str(), hashUrl.length());
		string strHash = sha.read();
		mHash = HASH((char *)strHash.c_str());
		mstrHash = hash2Char(mHash.data);
		mHashIdx = CFlvPool::instance()->hashIdx(mHash);
		logs->debug("%s [CConnRtmp::makeHash] %s rtmp %s hash url %s,hash=%s",
			mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str(),hashUrl.c_str(),mstrHash.c_str());
	}
	else
	{
		mHashIdx = CFlvPool::instance()->hashIdx(mHash);
	}
	mflvTrans->setHash(mHashIdx,mHash);
}

void CConnRtmp::makePushHash()
{
	string hashUrl = readMajorUrl(mstrPushUrl);
	CSHA1 sha;
	sha.write(hashUrl.c_str(), hashUrl.length());
	string strHash = sha.read();
	mpushHash = HASH((char *)strHash.c_str());
	mstrHash = hash2Char(mpushHash.data);
	logs->debug("%s [CConnRtmp::makePushHash] %s rtmp %s push hash url %s,hash=%s",
		mremoteAddr.c_str(),mstrPushUrl.c_str(),mrtmp->getRtmpType().c_str(),hashUrl.c_str(),mstrHash.c_str());
}

void CConnRtmp::down8upBytes()
{
	unsigned long tt = getTickCount();
	if (tt - mspeedTick > 1000)
	{
		mspeedTick = tt;
		int32 bytes = mrdBuff->readBytesNum();
		if (bytes > 0 && misPushFlv)
		{
			misDown8upBytes = true;
			makeOneTaskDownload(mHash,bytes,false);
		}

		mxSecdownBytes += bytes;

		bytes = mwrBuff->writeBytesNum();
		if (bytes > 0)
		{
			makeOneTaskupload(mHash,bytes,PACKET_CONN_DATA);
		}
		
		mxSecUpBytes += bytes;
		mxSecTick++;
		if (((mxSecTick+(0x0F-(CMS_SPEED_DURATION>=0x0F?10:CMS_SPEED_DURATION)+1)) & 0x0F) == 0)
		{
			logs->debug("%s [CConnRtmp::down8upBytes] %s rtmp %s download speed %s,upload speed %s",
				mremoteAddr.c_str(),murl.c_str(),mrtmp->getRtmpType().c_str(),
				parseSpeed8Mem(mxSecdownBytes/mxSecTick,true).c_str(),
				parseSpeed8Mem(mxSecUpBytes/mxSecTick,true).c_str());
			mxSecTick = 0;
			mxSecdownBytes = 0;
			mxSecUpBytes = 0;
		}
	}
}

int		CConnRtmp::firstPlaySkipMilSecond()
{
	return miFirstPlaySkipMilSecond;
}

bool	CConnRtmp::isResetStreamTimestamp()
{
	return misResetStreamTimestamp;
}

bool	CConnRtmp::isNoTimeout()
{
	return misNoTimeout;
}

int		CConnRtmp::liveStreamTimeout()
{
	return miLiveStreamTimeout;
}

int	CConnRtmp::noHashTimeout()
{
	return miNoHashTimeout;
}

bool	CConnRtmp::isRealTimeStream()
{
	return misRealTimeStream;
}

int64   CConnRtmp::cacheTT()
{
	return mllCacheTT;
}

std::string CConnRtmp::getHost()
{
	return mHost;
}

void CConnRtmp::makeOneTask()
{
	makeOneTaskDownload(mHash,0,false);
	makeOneTaskMedia(mHash,mflvPump->getVideoFrameRate(),mflvPump->getAudioFrameRate(),mflvPump->getWidth(),mflvPump->getHeight(),
		mflvPump->getAudioSampleRate(),mflvPump->getMediaRate(),getVideoType(mflvPump->getVideoType()),
		getAudioType(mflvPump->getAudioType()),murl,mremoteAddr, mrw->netType() == NetUdp);
}

CReaderWriter *CConnRtmp::rwConn()
{
	return mrw;
}
