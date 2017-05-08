#ifndef __CMS_STATIC_COMMON_H__
#define __CMS_STATIC_COMMON_H__
#include <common/cms_type.h>
#include <string>

#define PACKET_ONE_TASK_DOWNLOAD	0x00
#define PACKET_ONE_TASK_UPLOAD		0x01
#define PACKET_ONE_TASK_MEDA		0x02
#define PACKET_ONE_TASK_MEM			0x03

#define PACKET_CONN_ADD				0x01
#define PACKET_CONN_DEL				0x02
#define PACKET_CONN_DATA			0x03

struct OneTaskPacket
{
	int	packetID;
};

struct OneTaskDownload 
{
	int		packetID;
	HASH	hash;
	int32	downloadBytes;
	bool	isRemove;
};

struct OneTaskUpload 
{
	int		packetID;
	HASH	hash;
	int32	uploadBytes;
	int		connAct;
};

struct OneTaskMeida 
{
	int				packetID;
	HASH			hash;
	int32			videoFramerate;
	int32			audioFramerate;
	int32			audioSamplerate;
	int32			mediaRate;
	std::string		videoType;
	std::string		audioType;
	std::string		remoteAddr;
	std::string		url;
};

struct OneTaskMem 
{
	int		packetID;
	HASH	hash;
	int64	totalMem;
};

struct OneTask
{
	std::string		murl;
	int64			mdownloadTotal;
	int64			mdownloadTick;
	int64			mdownloadSpeed;
	uint64			mdownloadTT;

	int64			muploadTotal;
	int64			muploadTick;
	int64			muploadSpeed;
	uint64			muploadTT;

	int32			mmediaRate;
	int32			mvideoFramerate;
	int32			maudioFramerate;
	int32			maudioSamplerate;
	std::string		mvideoType;
	std::string		maudioType;

	int32			mtotalConn;			//������ǰ������
	std::string		mreferer;			//refer

	int64			mtotalMem;			//��ǰ��������ռ���ڴ�

	time_t			mttCreate;
	std::string		mremoteAddr;
};

struct CpuInfo 
{
	long long user;
	long long nice;
	long long sys;
	long long idle;
};

typedef struct
{
	/** 01 */ char interface_name[128]; /** ����������eth0 */

	/** �������� */
	/** 02 */ unsigned long receive_bytes;             /** ���������յ����ֽ��� */
	/** 03 */ unsigned long receive_packets;
	/** 04 */ unsigned long receive_errors;
	/** 05 */ unsigned long receive_dropped;
	/** 06 */ unsigned long receive_fifo_errors;
	/** 07 */ unsigned long receive_frame;
	/** 08 */ unsigned long receive_compressed;
	/** 09 */ unsigned long receive_multicast;

	/** �������� */
	/** 10 */ unsigned long transmit_bytes;             /** �������ѷ��͵��ֽ��� */
	/** 11 */ unsigned long transmit_packets;
	/** 12 */ unsigned long transmit_errors;
	/** 13 */ unsigned long transmit_dropped;
	/** 14 */ unsigned long transmit_fifo_errors;
	/** 15 */ unsigned long transmit_collisions;
	/** 16 */ unsigned long transmit_carrier;
	/** 17 */ unsigned long transmit_compressed;        
}net_info_t;


OneTask *newOneTask();
//static ����
void makeOneTaskDownload(HASH &hash,int32 downloadBytes,bool isRemove);
void makeOneTaskupload(HASH	&hash,int32 uploadBytes,int connAct);
void makeOneTaskMedia(HASH	&hash,int32 videoFramerate,int32 audioFramerate,
					  int32 audioSamplerate,int32 mediaRate,std::string videoType,
					  std::string audioType,std::string url,std::string remoteAddr);
void makeOneTaskMem(HASH &hash,int64	totalMem);
#endif