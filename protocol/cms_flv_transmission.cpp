#include <protocol/cms_flv_transmission.h>
#include <flvPool/cms_flv_pool.h>
#include <log/cms_log.h>
#include <assert.h>


CFlvTransmission::CFlvTransmission(CProtocol *protocol)
{
	mllMetaDataIdx = -1;
	mllFirstVideoIdx = -1;
	mllFirstAudioIdx = -1;
	mllTransIdx = -1;
	misChangeFirstVideo = false;
	mchangeFristVideoTimes = 0;
	mprotocol = protocol;
	misWaterMark = false;
	mwaterMarkOriHashIdx = 0;
	mcacheTT = 0;
	msliceFrameRate = 0;
	mfastBitRate = new CFastBitRate;
}

CFlvTransmission::~CFlvTransmission()
{
	if (mfastBitRate)
	{
		delete mfastBitRate;
	}
}

void CFlvTransmission::setHash(uint32 hashIdx,HASH &hash)
{
	mreadHashIdx = hashIdx;
	mreadHash = hash;
}

void CFlvTransmission::setWaterMarkHash(uint32 hashIdx,HASH &hash)
{
	misWaterMark = true;
	mwaterMarkOriHashIdx = hashIdx;
	mwaterMarkOriHash = hash;
}

int CFlvTransmission::doMetaData()
{
	int ret = CMS_ERROR;
	Slice *s = NULL;
	if (CFlvPool::instance()->readMetaData(mreadHashIdx,mreadHash,&s) == FlvPoolCodeError)
	{
		return ret;
	}
	assert(s != NULL);
	mllMetaDataIdx = s->mllIndex;
	ret =  mprotocol->sendMetaData(s);
	atomicDec(s);
	logs->debug(">>>>>%s [CFlvTransmission::doTransmission] %s doMetaData send metaData",
		mprotocol->remoteAddr().c_str(),mprotocol->getUrl().c_str());
	return ret;
}

int CFlvTransmission::doFirstVideoAudio(bool isVideo)
{
	int ret = CMS_ERROR;
	Slice *s = NULL;
	if (CFlvPool::instance()->readRirstVideoAudioSlice(mreadHashIdx,mreadHash,&s,isVideo) == FlvPoolCodeError)
	{
		return ret;
	}
	assert(s != NULL);
	if (isVideo)
	{
		misChangeFirstVideo = true;
		mchangeFristVideoTimes++;

		mllFirstVideoIdx = s->mllIndex;
		logs->debug(">>>>>%s [CFlvTransmission::doTransmission] %s doFirstVideoAudio send first video",
			mprotocol->remoteAddr().c_str(),mprotocol->getUrl().c_str());
	}
	else
	{
		mllFirstAudioIdx = s->mllIndex;
		logs->debug(">>>>>%s [CFlvTransmission::doTransmission] %s doFirstVideoAudio send first audio",
			mprotocol->remoteAddr().c_str(),mprotocol->getUrl().c_str());
	}
	ret = mprotocol->sendVideoOrAudio(s,0);
	atomicDec(s);
	return ret;
}

void CFlvTransmission::getSliceFrameRate()
{
	if (msliceFrameRate == 0)
	{
		int videoFrameRate = CFlvPool::instance()->getVideoFrameRate(mreadHashIdx,mreadHash);
		int audioFrameRate = CFlvPool::instance()->getAudioFrameRate(mreadHashIdx,mreadHash);
		if (videoFrameRate > 0 && audioFrameRate > 0)
		{
			msliceFrameRate = videoFrameRate = audioFrameRate;
		}
	}
}

int CFlvTransmission::doTransmission()
{
	int ret = 1;
	Slice *s = NULL;
	Slice *ss = NULL;
	int flvPoolCode;
	uint32 uiTimestamp = 0;	
	int	sliceNum = 0;
	bool needSend = false;
	int  dropPer = 0;
	getSliceFrameRate();
	uint32 tt = getTickCount();
	do 
	{
		if (!CFlvPool::instance()->isExist(mreadHashIdx,mreadHash))
		{			
			ret = 2;
			break;
		}
		if (CFlvPool::instance()->isMetaDataChange(mreadHashIdx,mreadHash,mllMetaDataIdx))
		{
			if (doMetaData() == CMS_ERROR)
			{
				ret = -1;
				break;
			}
		}
		if (CFlvPool::instance()->isFirstVideoChange(mreadHashIdx,mreadHash,mllFirstVideoIdx))
		{
			if (doFirstVideoAudio(true) == CMS_ERROR)
			{
				ret = -1;
				break;
			}
		}
		if (CFlvPool::instance()->isFirstAudioChange(mreadHashIdx,mreadHash,mllFirstAudioIdx))
		{
			if (doFirstVideoAudio(false) == CMS_ERROR)
			{
				ret = -1;
				break;
			}
		}
		sliceNum = 0;
		flvPoolCode = CFlvPool::instance()->readSlice(mreadHashIdx,mreadHash,mllTransIdx,&s,sliceNum);
		if (flvPoolCode == FlvPoolCodeError)
		{
			logs->error("*** %s [CFlvTransmission::doTransmission] %s doTransmission task is missing ***",
				mprotocol->remoteAddr().c_str(),mprotocol->getUrl().c_str());
			ret = -1;
			break;
		}
		else if (flvPoolCode == FlvPoolCodeOK)
		{
			if (!mfastBitRate->isInit())
			{
				mfastBitRate->init(mprotocol->remoteAddr(),"flv",mprotocol->getUrl(),misWaterMark,
					mwaterMarkOriHashIdx,mreadHashIdx,mwaterMarkOriHash,mreadHash);
				mfastBitRate->setChangeBitRate();
			}
			if (s)
			{
				needSend = true;
				uiTimestamp = s->muiTimestamp;
				bool isMergerFrame = false;
				if (mfastBitRate->isChangeBitRate() ||
					(misChangeFirstVideo && mchangeFristVideoTimes > 1))
				{
					Slice *fs = NULL;
					if (CFlvPool::instance()->readRirstVideoAudioSlice(mreadHashIdx,mreadHash,&fs,true) == FlvPoolCodeError)
					{
						logs->info("*** %s [CFlvTransmission::doTransmission] %s merger key frame but not found first video ***",
							mprotocol->remoteAddr().c_str(),mprotocol->getUrl().c_str());
						atomicDec(s);
						ret = -1;
						break;
					}
					char *d = NULL;
					int32 dLen = 0;
					if (CFlvPool::instance()->mergeKeyFrame(fs->mData,fs->miDataLen,s->mData,s->miDataLen,d,dLen,mprotocol->getUrl()))
					{
						ss = new Slice;
						ss->mData = d;
						ss->miDataLen = dLen;
						ss->miDataType = s->miDataType;
						ss->mllIndex = s->mllIndex;
						ss->muiTimestamp = s->muiTimestamp;
						ss->misKeyFrame = s->misKeyFrame;
						isMergerFrame = true;
						atomicDec(s);

						s = ss;
					}
				}
				//如果切换码率了,需要修改时间戳
				uiTimestamp = mfastBitRate->changeBitRateSetTimestamp(s->miDataType,uiTimestamp);
				//如果切换码率了,需要修改时间戳 结束
				//动态丢帧
				if (mfastBitRate->needResetFlags(s->miDataType,uiTimestamp))
				{
					//时间戳变小了重设标志
					mfastBitRate->resetDropFrameFlags();
				}
				if (s->miDataType == DATA_TYPE_AUDIO)
				{
					mfastBitRate->setNo1VideoAudioTimestamp(false, uiTimestamp);
				}
				else if (s->miDataType == DATA_TYPE_VIDEO)
				{
					mfastBitRate->setNo1VideoAudioTimestamp(true, uiTimestamp);
				}
				if (((mfastBitRate->getAutoBitRateMode() == AUTO_DROP_CHANGE_BITRATE_OPEN ||
					mfastBitRate->getAutoBitRateMode() == AUTO_DROP_BITRATE_OPEN) && 
					muiKeyFrameDistance < DropVideoKeyFrameLen) ||
					mfastBitRate->getAutoBitRateMode() == AUTO_CHANGE_BITRATE_OPEN)
				{
					if (mcacheTT == 0)
					{
						mcacheTT = CFlvPool::instance()->getCacheTT(mreadHashIdx,mreadHash);
						muiKeyFrameDistance = CFlvPool::instance()->getKeyFrameDistance(mreadHashIdx,mreadHash);
						mfastBitRate->setAutoBitRateFactor(CFlvPool::instance()->getAutoBitRateFactor(mreadHashIdx,mreadHash));
						mfastBitRate->setAutoFrameFactor(CFlvPool::instance()->getAutoFrameFactor(mreadHashIdx,mreadHash));
						logs->debug("%s [CFlvTransmission::doTransmission] %s cache=%lld,keyFrameDistance=%lu,autoBitRateFactor=%d,autoFrameFactor=%d",
							mprotocol->remoteAddr().c_str(),mprotocol->getUrl().c_str(),mcacheTT,muiKeyFrameDistance,
							mfastBitRate->getAutoBitRateFactor(),mfastBitRate->getAutoFrameFactor());
					}
					mfastBitRate->dropVideoFrame(mcacheTT,s->miDataType,msliceFrameRate,tt,uiTimestamp,sliceNum);
				}
				if (s->miDataType == DATA_TYPE_VIDEO)
				{
					if (mfastBitRate->getTransCodeNeedDropVideo())
					{
						if (s->misKeyFrame && mfastBitRate->isDropEnoughTime(uiTimestamp))
						{
							if (mfastBitRate->getLoseBufferTimes() <= 0)
							{
								mfastBitRate->setTransCodeNeedDropVideo(false);
							}
						}
						else 
						{
							needSend = false;
							mfastBitRate->dropOneFrame();
						}
					}
					if (mfastBitRate->getTransCodeNoNeedDropVideo())
					{
						if (s->misKeyFrame)
						{
							if (mfastBitRate->getLoseBufferTimes() <= 0)
							{
								mfastBitRate->setTransCodeNoNeedDropVideo(false);
							}
						} 
						else 
						{
							mfastBitRate->dropOneFrame();
						}
					}
				}
				else if (s->miDataType == DATA_TYPE_AUDIO)
				{
					
				}
				dropPer = mfastBitRate->dropFramePer(tt, msliceFrameRate);
				//动态丢帧 结束
				if (needSend)
				{
					ret = mprotocol->sendVideoOrAudio(s,uiTimestamp);
				}
				if (mllTransIdx != -1 &&
					mllTransIdx+1 != s->mllIndex)
				{
					logs->info("*** %s [CFlvTransmission::doTransmission] %s doTransmission drop slice ***",
						mprotocol->remoteAddr().c_str(),mprotocol->getUrl().c_str());
				}	

				if (!mfastBitRate->isChangeVideoBit()) 
				{
					mllTransIdx = s->mllIndex;
				}

				if (!isMergerFrame)
				{
					atomicDec(s);
				}
				else
				{
					//因为是在该函数开辟，释放
					delete s->mData;
					delete s;
					ss = NULL;
				}
				if (ret == CMS_ERROR)
				{
					ret = -1;
					break;
				}
				ret = 1;
				if (mprotocol->writeBuffSize() > 0)
				{					
					break;
				}
			}
			else
			{
				break;
			}
		}
		else if (flvPoolCode == FlvPoolCodeNoData)
		{
			ret = 0;
			break;
		}
		else if (flvPoolCode == FlvPoolCodeRestart)
		{
			mllTransIdx = -1;
			mllMetaDataIdx = -1;
			mllFirstVideoIdx = -1;
			mllFirstAudioIdx = -1;

			//还原丢帧转码状态
			mfastBitRate->resetDropFrameFlags();

			logs->debug("%s [CFlvTransmission::doTransmission] %s doTransmission task is been restart",
				mprotocol->remoteAddr().c_str(),mprotocol->getUrl().c_str());
			ret = 0;
			break;
		}
		else
		{
			logs->error("*** %s [CFlvTransmission::doTransmission] %s doTransmission unknow FlvPoolCode=%d ***",
				mprotocol->remoteAddr().c_str(),mprotocol->getUrl().c_str(),flvPoolCode);
			ret = -1;
			break;
		}
	} while (true);
	mprotocol->syncIO();
	return ret;
}
