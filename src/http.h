/*
 * Copyright (c) 2010, NanChang BangTeng Inc
 *
 * kangle web server              http://www.kangleweb.net/
 * ---------------------------------------------------------------------
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 *  See COPYING file for detail.
 *
 *  Author: KangHongjiu <keengo99@gmail.com>
 */
#ifndef HTTP_H_SADFLKJASDLFKJASDLFKJ234234234
#define HTTP_H_SADFLKJASDLFKJASDLFKJ234234234
#include <string>
#include <vector>
#include <stdlib.h>
#include "global.h"

#include "lib.h"
#include "ksocket.h"

#include "log.h"
#include "KBuffer.h"
#include "KHttpRequest.h"
#include "KTable.h"
#include "utils.h"
#include "KHttpResponseParser.h"
#include "KVirtualHost.h"
#include "KSendable.h"
#include "KEnvInterface.h"
#include "KPipeStream.h"
#include "kselector.h"
#include "KPathRedirect.h"



enum {
	LOAD_HEAD_SUCCESS, LOAD_HEAD_FAILED, LOAD_HEAD_RETRY
};
inline bool is_internal_attr(const char *attr) {
	return *attr == '_';
}
inline bool is_internal_header(KHttpHeader *av) {
	return is_internal_attr(av->attr);
}
class KHttpEnv;
void log_access(KHttpRequest *rq);
void upstream_sign_request(KHttpRequest *rq, KHttpEnv *s);
bool is_val(KHttpHeader *av, const char *val, int val_len);
bool is_attr(KHttpHeader *av, const char *attr);
int attr_casecmp(const char *s1,const char *s2);
kev_result asyncSendHttpObject(KHttpRequest *rq);
kev_result processCacheRequest(KHttpRequest *rq);
kev_result asyncLoadHttpObject(KHttpRequest *rq);
kev_result async_http_start(KHttpRequest *rq);
bool sync_send_http_object(KHttpRequest *rq, KHttpObject *obj);
kev_result sendMemoryObject(KHttpRequest *rq,KHttpObject *obj);
kev_result send_not_modify_from_mem(KHttpRequest *rq,KHttpObject *obj);
kev_result stageDeniedRequest(KHttpRequest *rq) ;
char *url_encode(const char *s, size_t len, size_t *new_length);
std::string url_encode(const char *s, size_t len = 0);
bool parse_url(const char *src, KUrl *url);
kev_result try_send_request(KHttpRequest *rq);
kev_result stage_rdata_end(KHttpRequest *rq,StreamState result);
kev_result stage_prepare(KHttpRequest *rq);
kev_result send_error(KHttpRequest *rq, int code, const char* reason);
void prepare_write_stream(KHttpRequest *rq);
kev_result prepare_load_body(KHttpRequest *rq);
StreamState send_buff(KSendable *socket, kbuf *buf , INT64 &start,INT64 &send_len);
kev_result send_auth(KHttpRequest *rq,KAutoBuffer *body=NULL);
bool stageContentType(KHttpRequest *rq,KHttpObject *obj);
bool build_obj_header(KHttpRequest *rq, KHttpObject *obj,INT64 content_len, INT64 &start, INT64 &send_len);
bool push_redirect_header(KHttpRequest *rq,const char *url,int url_len,int code);
kev_result stageEndRequest(KHttpRequest *rq);
void insert_via(KHttpRequest *rq, KWStream &s, char *old_via = NULL);
bool make_http_env(KHttpRequest *rq, KBaseRedirect *rd,time_t lastModified,KFileName *file,KEnvInterface *env, bool chrooted=false);
KWStream *makeWriteStream(KHttpRequest *rq,KHttpObject *obj,KWStream *st,bool &autoDelete);
bool stored_obj(KHttpObject *obj,int list_state);
bool stored_obj(KHttpRequest *rq, KHttpObject *obj,KHttpObject *old_obj);
bool adjust_range(KHttpRequest *rq,INT64 &len);
kev_result send_http(KHttpRequest *rq, KHttpObject *obj, uint16_t status_code, KAutoBuffer *body = NULL);
kev_result handleUpstreamRecvedHead(KHttpRequest *rq);
kev_result handleError(KHttpRequest *rq,int code,const char *msg) ;
void set_obj_size(KHttpObject *obj, INT64 content_length);
int checkResponse(KHttpRequest *rq,KHttpObject *obj);
KTHREAD_FUNCTION stage_sync(void *param);
inline bool check_need_gzip(KHttpRequest *rq, KHttpObject *obj) {
	if (rq->ctx->connection_upgrade) {
		//透传模式不压缩
		return false;
	}
    //如果obj标记为已经压缩过，或者标记了不用压缩，则不压缩数据
	if (TEST(obj->uk.url->encoding,KGL_ENCODING_YES)) {
			return false;
	}
    //obj有多个引用,不压缩
    if (obj->refs > 1) {
            return false;
    }
    //客户端不支持压缩格式，不压缩
	if (!TEST(rq->raw_url.encoding, KGL_ENCODING_GZIP)) {
		return false;
	}
    //status_code是206，表示是部分内容时也不压缩,或者是200回应，但用了url ranged技术
    //注：这种情况没有经过详细考证
    if (obj->data->status_code == STATUS_CONTENT_PARTIAL 
		|| TEST(rq->raw_url.flags,KGL_URL_RANGED)) {
            return false;
    }
    if (TEST(obj->index.flags,FLAG_DEAD) && conf.only_gzip_cache == 1) {
            return false;
    }
    //标记为需要压缩则返回要压缩
	return obj->need_gzip;
}

//在队列后，处理已经就绪的请求
inline kev_result processReadyedRequest(KHttpRequest *rq)
{
	if (likely(!rq->IsSync())){
		return rq->fetchObj->open(rq);
	}
	if (!kthread_pool_start(stage_sync, rq)) {
		return stageEndRequest(rq);
	}
	return kev_ok;
}
inline kev_result processRequestUseQueue(KHttpRequest *rq, KRequestQueue *queue)
{
#ifdef ENABLE_REQUEST_QUEUE
	if (queue->getMaxWorker() > 0) {
		if (!queue->start(rq)) {
			if (TEST(rq->flags, RQ_SYNC)) {
				send_error(rq, STATUS_SERVICE_UNAVAILABLE, "Server is busy.");
				return stageEndRequest(rq);
			}
			return send_error(rq, STATUS_SERVICE_UNAVAILABLE, "Server is busy.");
		}
		return kev_ok;
	}
#endif
	return processReadyedRequest(rq);
}
//处理请求的队列,根据数据源的特点处理请求是否放入队列
inline kev_result processQueueRequest(KHttpRequest *rq)
{
	if (unlikely(rq->fetchObj->isSync())) {
		rq->AddSync();
	}
#ifdef ENABLE_REQUEST_QUEUE
	if (TEST(rq->GetWorkModel(),WORK_MODEL_MANAGE) || rq->ctx->internal || !rq->fetchObj->needQueue()) {
		//后台管理及内部调用，不用排队
		return processReadyedRequest(rq);
	}
	KRequestQueue *queue = rq->queue;
	if (queue == NULL) {
		queue = &globalRequestQueue;
#ifdef ENABLE_VH_QUEUE
		if (rq->svh && rq->svh->vh->queue) {
			queue = rq->svh->vh->queue;
		}
#endif
	}
	return processRequestUseQueue(rq, queue);
#endif
	return processReadyedRequest(rq);
}
kev_result afterPostHandleForUpstream(KHttpRequest *rq,void *arg);
inline kev_result processNotCacheRequest(KHttpRequest *rq)
{
	return asyncLoadHttpObject(rq);
}
#ifdef ENABLE_TF_EXCHANGE
inline kev_result process_read_post_to_temp_file(KHttpRequest *rq) {
	//有post数据
	if (rq->tf==NULL) {
		rq->tf = new KTempFile;
	}
	rq->tf->init(rq->content_length);
	return stageTempFileReadPost(rq ,afterPostHandleForUpstream, NULL);
}
#endif
//准备好了数据源，根据数据源的特点处理请求，看是否启用临时文件
inline kev_result processRequest(KHttpRequest *rq)
{
	kassert(rq->fetchObj!=NULL);
#ifdef ENABLE_TF_EXCHANGE
	if (unlikely(!rq->ctx->internal && (rq->fetchObj->NeedTempFile(true,rq) || rq->hasInputFilter()))) {
		//post chunked
		return process_read_post_to_temp_file(rq);
	}
#endif
	return processQueueRequest(rq);
}
/*
obj is not expire
*/
inline kev_result async_send_valide_object(KHttpRequest *rq, KHttpObject *obj)
{
	//rq->status_code = STATUS_OK;
	bool not_modifed = false;
	if (TEST(rq->flags,RQ_HAS_IF_MOD_SINCE|RQ_IF_RANGE_DATE)) {
		time_t useTime = obj->index.last_modified;
		if (useTime <= 0) {
			useTime = obj->index.last_verified;
		}
		if (rq->if_modified_since >= useTime) {
			//not change
			if (TEST(rq->flags,RQ_HAS_IF_MOD_SINCE)){
				not_modifed = true;
				//rq->status_code = STATUS_NOT_MODIFIED;
			}
		} else if (TEST(rq->flags,RQ_IF_RANGE_DATE)) {
			CLR(rq->flags,RQ_HAVE_RANGE);
			rq->range_from = 0;
			rq->range_to = -1;
		}
	} else if (TEST(rq->flags,RQ_HAS_IF_NONE_MATCH)) {
		kgl_str_t *if_none_match = rq->ctx->if_none_match;
		if (if_none_match && obj->matchEtag(if_none_match->data, (int)if_none_match->len)) {
			not_modifed = true;
			//rq->status_code = STATUS_NOT_MODIFIED;
		}
		rq->ctx->clean_if_none_match();
	} else if (TEST(rq->flags,RQ_IF_RANGE_ETAG)) {
		kgl_str_t *if_none_match = rq->ctx->if_none_match;
		if (if_none_match==NULL || !obj->matchEtag(if_none_match->data, (int)if_none_match->len)) {
			CLR(rq->flags, RQ_HAVE_RANGE);
			rq->range_from = 0;
			rq->range_to = -1;
		}
		rq->ctx->clean_if_none_match();
	}
	if (!not_modifed || rq->needFilter()) {
		return  asyncSendHttpObject(rq);
	}
	return send_not_modify_from_mem(rq,obj);
}
//检查obj是否过期1
inline bool check_object_expiration(KHttpRequest *rq,KHttpObject *obj) {
	assert(obj);
	if (TEST(obj->index.flags, OBJ_IS_GUEST) && !TEST(rq->filter_flags, RF_GUEST)) {
		//是游客缓存，但该用户不是游客
		return true;
	}
	if (TEST(obj->index.flags,OBJ_MUST_REVALIDATE)) {
		//有must-revalidate,则每次都要从源上验证
		return true;
	}
	if (obj->index.last_verified == 0) {
		return true;
	}
	unsigned freshness_lifetime;
	if (TEST(obj->index.flags ,ANSW_HAS_MAX_AGE|ANSW_HAS_EXPIRES)){
		freshness_lifetime = obj->index.max_age;
	} else {
		freshness_lifetime = conf.refresh_time;
	}
	if (TEST(rq->filter_flags,RF_DOUBLE_CACHE_EXPIRE)) {
		//双倍过期时间，并清除强制刷新
		freshness_lifetime = freshness_lifetime<<1;
		CLR(rq->flags,RQ_HAS_NO_CACHE);
	}
	//debug("current_age=%d,refreshness_lifetime=%d\n",current_age,freshness_lifetime);
	unsigned current_age = obj->getCurrentAge(kgl_current_sec);
	if (current_age >= freshness_lifetime){
		return true;
	}
	return false;
}
inline kev_result processCacheWithCheckExpire(KHttpRequest *rq,KHttpObject *obj)
{
	if (check_object_expiration(rq,obj)) {
#ifdef ENABLE_STATIC_ENGINE
		//如果是静态化的页面,则要重新生成过
		CLR(obj->index.flags, OBJ_IS_STATIC2);
#endif
		goto revalidate;
	}
	if (TEST(rq->flags , RQ_HAS_NO_CACHE)) {
		goto revalidate;
	}
	return async_send_valide_object(rq,obj);
	revalidate:
#ifdef ENABLE_FORCE_CACHE
	if (TEST(obj->index.flags,OBJ_IS_STATIC2)){
		return async_send_valide_object(rq,obj);
	}
#endif
	assert(obj && rq->ctx->old_obj==NULL);
	rq->ctx->old_obj = obj;
	rq->ctx->obj = new KHttpObject(rq);
	rq->ctx->new_object = true;
	return asyncLoadHttpObject(rq);
}
#endif
