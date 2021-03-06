/*
 * KVirtualProcess.h
 *
 *  Created on: 2010-10-24
 *      Author: keengo
 */

#ifndef KVIRTUALPROCESS_H
#define KVIRTUALPROCESS_H
#include <sstream>
#include "global.h"
#include "KExtendProgram.h"
#include "KTcpUpstream.h"
#include "KPoolableSocketContainer.h"
#include "KHttpRequest.h"
#include "KProcess.h"
#include "time_utils.h"

enum VProcess_Status
{	
	VProcess_Poweroff,	
	VProcess_Inprogress,
	VProcess_Poweron,
	VProcess_Close
};

/*
 * 一个虚拟主机进程模型
 */
class KVirtualHostProcess : public KPoolableSocketContainer{
public:
	KVirtualHostProcess() {
		//closed = false;
		lastActive = kgl_current_sec;
		idleTime = 0;
		max_request = 0;
		max_error_count = 0;
		error_count = 0;
		status = VProcess_Poweroff;
		lastCheckTime = 0;
	}
	virtual ~KVirtualHostProcess() {
		killProcess(0);
	}
	void isBad(KUpstream *st,BadStage stage)
	{
		if (stage == BadStage_Connect || stage == BadStage_TrySend){
			error_count++;
			if (kgl_current_sec - lastCheckTime > 5) {
				lastCheckTime = kgl_current_sec;
				if(!isProcessActive() || (max_error_count>0 && error_count>=max_error_count)) {
					clean();
					status = VProcess_Close;
					//reset the error_count
					error_count = 0;
					klog(KLOG_ERR,"restart the virtual process error_count=%d\n",error_count);
				}
			}
		}
	}
	void isGood(KUpstream *st)
	{
		//reset the error_count
		error_count = 0;
	}
	virtual kev_result handleRequest(KHttpRequest *rq,KExtendProgram *rd);
	KUpstream *connect(KHttpRequest *rq,KExtendProgram *rd,bool &isHalf) {
		lastActive = kgl_current_sec;
		KUpstream *ps = getPoolSocket(rq);
		if(ps){
			isHalf = false;
			return ps;
		}
		isHalf = true;
#ifdef KSOCKET_UNIX
		if (!unix_path.empty()) {
			struct sockaddr_un un_addr;
			ksocket_unix_addr(unix_path.c_str(),&un_addr);
			SOCKET fd = ksocket_half_connect((sockaddr_i *)&un_addr,NULL,0);
			if (!ksocket_opened(fd)) {
				return NULL;
			}
			kconnection *cn = kconnection_new(&addr);
			cn->st.fd = fd;
			return new KTcpUpstream(cn);
		}
#endif
		kconnection *cn = kconnection_new(&addr);
		bool result = kconnection_half_connect(cn,NULL,0);		
		if (!result) {
			kconnection_destroy(cn);
			return NULL;
		}
		return new KTcpUpstream(cn);
	}

	virtual KTcpUpstream *poweron(KVirtualHost *vh,KExtendProgram *rd,bool &success) = 0;
	virtual void getProcessInfo(const USER_T &user, const std::string &name,
			std::stringstream &s,int &count) {
	}
	
	virtual bool canDestroy(time_t nowTime)
	{
		if (idleTime>0 && nowTime - lastActive > idleTime) {
			return true;
		}
		return false;
	}
	/*
	杀掉指定进程，pid=0，杀掉全部进程。
	返回true,则表示全部进程已杀掉，
	返回false,则表示还有进程(针对多进程)
	*/
	virtual bool killProcess(int pid) {
		return false;
	}
	/*
	 标识是否已经结束
	 */
	//bool closed;
	int idleTime;
	VProcess_Status status;
	pthread_t workThread;
	KMutex lock;
	int error_count;
	int max_error_count;
	unsigned max_request;
	//u_short port;
	sockaddr_i addr;
	std::list<KHttpRequest *> queue;
	time_t lastCheckTime;
protected:
	time_t lastActive;
	virtual bool isProcessActive()
	{
		return true;
	}
	std::string unix_path;
};
struct VProcessPowerParam
{
	KHttpRequest *rq;
	KVirtualHost *vh;
	KVirtualHostProcess *process;
	KExtendProgram *rd;
};
void getProcessInfo(const USER_T &user,const std::string &name,KProcess *process,KPoolableSocketContainer *ps,std::stringstream &s);
KTHREAD_FUNCTION VProcessPowerWorker(void *param);
#endif /* KVIRTUALPROCESS_H_ */
