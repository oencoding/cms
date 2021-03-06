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
#ifndef __CMS_HLS_MGR_H__
#define __CMS_HLS_MGR_H__
#include <common/cms_type.h>
#include <common/cms_var.h>
#include <core/cms_thread.h>
#include <ts/cms_ts.h>
#include <ev/cms_ev.h>
#include <core/cms_lock.h>
#include <strategy/cms_duration_timestamp.h>
#include <app/cms_app_info.h>
#include <string>
#include <vector>
#include <map>
#include <queue>


typedef struct _SSlice {
	int		mionly;		  //0 表示没被使用，大于0表示正在被使用次数
	float	msliceRange;  //切片时长
	int		msliceLen;    //切片大小
	int64	msliceIndex;  //切片序号
	uint64	msliceStart;  //切片开始时间戳
	std::vector<TsChunkArray *> marray;	  //切片数据
}SSlice;

SSlice *newSSlice();
void atomicInc(SSlice *s);
void atomicDec(SSlice *s);

void cmsTagReadTimer(void *t);

class CMission 
{
public:
	CMission(HASH &hash,uint32 hashIdx,std::string url,
		int tsDuration,int tsNum,int tsSaveNum);
	~CMission();

	int  doFirstVideoAudio(bool isVideo);
	int  doit(cms_timer *t = NULL);
	void stop();
	int  pushData(TsChunkArray *tca,byte frameType,uint64 timestamp);
	int  getTS(int64 idx,SSlice **s);
	int  getM3U8(std::string addr,std::string &outData);
	int64 getLastTsTime();
	int64 getUid();
private:
	int     mcmsReadTimeOutDo;
	cms_timer *mreadTimer;
	int64      muid;

	HASH	mhash;			//用来识别任务的hash值
	uint32  mhashIdx;		//
	std::string murl;		//拼接用的URL

	int		mtsDuration;    //单个切片时长
	int		mtsNum;         //切片上限个数
	int		mtsSaveNum;     //缓存保留的切片个数
	std::vector<SSlice *> msliceList; //切片列表
	int		msliceCount;    //切片计数
	int64	msliceIndx;     //当前切片的序号

	bool	misStop;		//用来控制任务协程

	int64	mreadIndex;		//读取的帧的序号

	int64	mreadFAIndex;	//读取的音频首帧的序号
	int64	mreadFVIndex;	//读取的视频首帧的序号

	bool	mFAFlag;	//是否读到首帧音频
	bool	mFVFlag;	//是否读到首帧视频(SPS/PPS)
	int64	mbTime;		//最后一个切片的生成时间
	CSMux	*mMux;      //转码器
	TsChunkArray *mlastTca;//节省空间

	uint64  mullTransUid;

	CDurationTimestamp *mdurationtt;
};

class CMissionMgr
{
public:
	CMissionMgr();
	~CMissionMgr();

	static void *routinue(void *param);
	void thread(uint32 i);
	bool run();
	void stop();

	static CMissionMgr *instance();
	static void freeInstance();
	/*创建一个任务
	-- idx hash对应的索引号,切片内部需要保存
	-- hash 任务哈希
	-- url任务url
	-- tsDuration 一个切片ts的时长
	-- tsNum 该m3u8的ts片数
	-- tsSaveNum 切片模块缓存ts片数
	*/
	int	 create(uint32 i,HASH &hash,std::string url,int tsDuration,int tsNum,int tsSaveNum);
	/*销毁一个任务
	-- hash 任务哈希
	*/
	void destroy(uint32 i,HASH &hash);
	/*根据任务读取m3u8或ts
	-- hash 任务哈希
	-- url m3u8或者ts的地址
	*/
	int  readM3U8(uint32 i,HASH &hash,std::string url,std::string addr,std::string &outData,int64 &tt);
	int  readTS(uint32 i,HASH &hash,std::string url,std::string addr,SSlice **ss,int64 &tt);
	/*管理器的释放，管理器会有一些超时数据的缓存*/
	void release();
	void tick(uint32 i,cms_timer *t);
	void push(uint32 i,cms_timer *t);
	bool pop(uint32 i,cms_timer **t);
private:
	static CMissionMgr *minstance;
	cms_thread_t mtid[APP_ALL_MODULE_THREAD_NUM];	
	bool misRunning[APP_ALL_MODULE_THREAD_NUM];
	std::map<HASH,CMission *> mMissionMap[APP_ALL_MODULE_THREAD_NUM];			//任务列表
	std::map<int64,CMission *> mMissionUidMap[APP_ALL_MODULE_THREAD_NUM];			//任务列表
	CRWlock					  mMissionMapLock[APP_ALL_MODULE_THREAD_NUM];

	std::map<HASH,int64>	  mMissionSliceCount[APP_ALL_MODULE_THREAD_NUM];	//任务的切片记录
	CRWlock					  mMissionSliceCountLock[APP_ALL_MODULE_THREAD_NUM];

	//超时
	std::queue<cms_timer *> mqueueRT[APP_ALL_MODULE_THREAD_NUM];
	CLock mqueueWL[APP_ALL_MODULE_THREAD_NUM];
};

struct HlsMgrThreadParam 
{
	CMissionMgr *pinstance;
	uint32 i;
};

#endif
