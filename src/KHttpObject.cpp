#include <string.h>
#include <stdlib.h>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#endif
#include <errno.h>
#include "KDiskCache.h"
#include "do_config.h"
#include "utils.h"
#include "cache.h"
#include "KHttpObject.h"
#include "KHttpObjectHash.h"
#include "KCache.h"
#ifdef ENABLE_DB_DISK_INDEX
#include "KDiskCacheIndex.h"
#endif
#include "kforwin32.h"
#include "kmalloc.h"
KMutex obj_lock[HASH_SIZE+1];
static unsigned file_index = (rand() & CACHE_DIR_MASK2);
static KMutex indexLock;
KHttpObjectBody::KHttpObjectBody(KHttpObjectBody *data)
{
	memset(this, 0, sizeof(KHttpObjectBody));
	status_code = data->status_code;	
	KHttpHeader *hot = headers;
	KHttpHeader *tmp = data->headers;
	while (tmp) {
		KHttpHeader *new_t = (KHttpHeader *)xmalloc(sizeof(KHttpHeader));
		new_t->attr = xstrdup(tmp->attr);
		if(tmp->val){
			new_t->val = xstrdup(tmp->val);
		}else{
			new_t->val = NULL;
		}
		new_t->attr_len = tmp->attr_len;
		new_t->val_len = tmp->val_len;
		new_t->next = NULL;
		if(hot==NULL){
			headers = new_t;
		}else{
			hot->next = new_t;
		}
		hot = new_t;
		tmp = tmp->next;
	}
}
bool KHttpObject::AddVary(KHttpRequest *rq,const char *val,int val_len)
{
	SET(index.flags, OBJ_HAS_VARY);
	if (uk.vary == NULL) {
		char *vary_val = rq->BuildVary(val);
		if (vary_val != NULL) {
			uk.vary = new KVary;
			uk.vary->val = vary_val;
			uk.vary->key = strdup(val);
			return true;
		}
	}
	return false;
}
char *KHttpObject::BuildVary(KHttpRequest *rq)
{
	kassert(uk.vary);
	kassert(uk.vary->key);
	return rq->BuildVary(uk.vary->key);
}
KHttpObject::KHttpObject(KHttpRequest *rq,KHttpObject *obj)
{
	init(rq->url);
	uk.url->encoding = rq->raw_url.encoding;
	index.flags = obj->index.flags;
	CLR(index.flags,FLAG_IN_DISK|FLAG_URL_FREE|OBJ_IS_READY|OBJ_IS_STATIC2|FLAG_NO_BODY|ANSW_HAS_CONTENT_LENGTH|ANSW_HAS_CONTENT_RANGE);
	SET(index.flags,FLAG_IN_MEM);
	index.last_verified = obj->index.last_verified;
	index.last_modified = obj->index.last_modified;
	index.max_age = obj->index.max_age;
	data = new KHttpObjectBody(obj->data);
}
KHttpObject::~KHttpObject() {
#ifdef ENABLE_DISK_CACHE
	unlinkDiskFile();
#endif
	if (data) {
		delete data;
	}
	if (uk.url && TEST(index.flags,FLAG_URL_FREE)) {
		//url由obj负责，则清理
		uk.url->destroy();
		delete uk.url;
	}
	if (uk.vary) {
		delete uk.vary;
	}
}
void KHttpObject::Dead()
{
	SET(index.flags, FLAG_DEAD);
	dc_index_update = 1;
}
void KHttpObject::UpdateCache(KHttpObject *obj)
{
	kassert(!obj->in_cache);
	kassert(in_cache);
	if (cmp_vary_key(uk.vary, obj->uk.vary)) {
		cache.UpdateVary(this, obj->uk.vary);
	}
	//TODO:更新vary,或max_age等缓存控制	
	
	return;
}
int KHttpObject::GetHeaderSize(int url_len)
{
	if (index.head_size > 0) {
		return index.head_size;
	}
	int len = sizeof(KHttpObjectFileHeader);
	if (url_len == 0) {
		char *u = get_url_key(&uk,&url_len);
		if (u == NULL) {
			return 0;
		}
		free(u);
	}
	len += url_len + sizeof(int);
	KHttpHeader *header = data->headers;
	while (header) {
		len += header->attr_len + header->val_len + 2 * sizeof(int);
		header = header->next;
	}
	len += sizeof(int);
	index.head_size = kgl_align(len, kgl_aio_align_size);
	return index.head_size;
}
#ifdef ENABLE_DISK_CACHE
void KHttpObjectBody::create_type(HttpObjectIndex *index)
{
	
	if (index->content_length >= conf.max_cache_size) {
		this->type = BIG_OBJECT;
		return;
	}
	this->type = MEMORY_OBJECT;
}
bool KHttpObjectBody::restore_header(KHttpObject *obj, char *buffer, int len)
{
	KHttpObjectFileHeader *fileHeader = (KHttpObjectFileHeader *)buffer;
	if (!is_valide_dc_sign(fileHeader)) {
		return false;
	}
	if (len != (int)fileHeader->index.head_size) {
		//head_size不对
		return false;
	}
	int hotlen = len - sizeof(KHttpObjectFileHeader);
	char *hot = (char *)(fileHeader + 1);
	//skip url
	skipString(&hot, hotlen);
	this->status_code = fileHeader->status_code;
	if (this->status_code == 0) {
		this->status_code = STATUS_OK;
	}
	return read_obj_head(this, &hot, hotlen);
}
void KHttpObject::unlinkDiskFile()
{
	if (TEST(index.flags,FLAG_IN_DISK)) {
#ifdef ENABLE_DB_DISK_INDEX
		if (dci) {
			dci->start(ci_del,this);
		}
#endif
		char *name = getFileName();
		int ret = unlink(name);
		char *url = this->uk.url->getUrl();
		assert(url);
		if (url) {
			klog(KLOG_INFO, "unlink disk cache obj=[%p %x " INT64_FORMAT_HEX "] url=[%s] file=[%s] ret=[%d] errno=[%d]\n",
				this,
				this->index.flags,
				index.last_modified,
				url,
				name,
				ret,
				errno);
			free(url);
		}
		
		free(name);
	}
}
char *KHttpObject::getFileName(bool part)
{	
	KStringBuf s;
	get_disk_base_dir(s);
	if (dk.filename1==0) {
		dk.filename1 = (unsigned) kgl_current_sec;
		indexLock.Lock();
		dk.filename2 = file_index++;
		indexLock.Unlock();
	}
	s.addHex((dk.filename1 & CACHE_DIR_MASK1));
	s << PATH_SPLIT_CHAR;
	s.addHex((dk.filename2 & CACHE_DIR_MASK2));
	s << PATH_SPLIT_CHAR;
	s.addHex(dk.filename1);
	s << "_";
	s.addHex(dk.filename2);
	if (part) {
		s << ".part";
	}
	return s.stealString();
}
bool KHttpObject::swapinBody(KFile *fp, KHttpObjectBody *data)
{
	assert(data->bodys == NULL && data->type == MEMORY_OBJECT);
	INT64 left_read = index.content_length;
	kbuf *last = NULL;
	while (left_read>0) {
		int this_read = (int)MIN(left_read, 16384);
		char *buf = (char *)xmalloc(this_read);
		if (buf == NULL) {
			return false;
		}
		if (fp->read(buf, this_read) != this_read) {
			free(buf);
			return false;
		}
		kbuf *tmp = (kbuf *)malloc(sizeof(kbuf));
		tmp->used = this_read;
		tmp->data = buf;
		tmp->flags = 0;
		tmp->next = NULL;
		if (last == NULL) {
			data->bodys = tmp;
		}
		else {
			last->next = tmp;
		}
		last = tmp;
		left_read -= this_read;
	}
	return true;
}
bool KHttpObject::swapin(KHttpObjectBody *data)
{
	bool result = false;
	assert(0 == TEST(index.flags, FLAG_IN_MEM));
	assert(data->bodys == NULL);
	char *filename = getFileName();
	if (filename == NULL) {
		return false;
	}
	if (!is_valide_dc_head_size(index.head_size)) {
		return false;
	}

	KFile fp;
	if (!fp.open(filename, fileRead, 0)) {
		free(filename);
		return false;
	}
	char *buffer = (char *)malloc(index.head_size);
	if (fp.read(buffer, index.head_size) != (int)index.head_size) {
		goto failed;
	}
	data->create_type(&index);
	if (!data->restore_header(this, buffer, index.head_size)) {
		goto failed;
	}
	switch (data->type) {
	case MEMORY_OBJECT:
		if (!swapinBody(&fp, data)) {
			goto failed;
		}
		break;
		
	default:
		break;
	}
	result = true;
	goto success;
failed:
	fp.close();
	if (unlink(filename) != 0) {
		klog(KLOG_WARNING, "cann't unlink corrupt object file [%s]\n", filename);
	}
	if (data->headers) {
		free_header(data->headers);
		data->headers = NULL;
	}
success:
	if (buffer) {
		free(buffer);
	}
	free(filename);
	return result;
}

void KHttpObject::write_file_header(KHttpObjectFileHeader *fileHeader)
{
	memset(fileHeader, 0, sizeof(KHttpObjectFileHeader));
	kgl_memcpy(fileHeader->fix_str, CACHE_FIX_STR, sizeof(CACHE_FIX_STR));
	kgl_memcpy(&fileHeader->index, &index, sizeof(HttpObjectIndex));
	fileHeader->version = CACHE_DISK_VERSION;
	fileHeader->url_flag_encoding = uk.url->flag_encoding;
	fileHeader->status_code = data->status_code;
	fileHeader->body_complete = 1;
	
}
bool KHttpObject::save_dci_header(KBufferFile *fp)
{
	KHttpObjectFileHeader fileHeader;
	write_file_header(&fileHeader);
	return fp->write((char *)&fileHeader, sizeof(KHttpObjectFileHeader)) == sizeof(KHttpObjectFileHeader);
}
/*
int KHttpObject::saveIndex(KFile *fp)
{
	assert(TEST(index.flags,FLAG_IN_DISK));
	//assert(index.file_name>0);
	int len = sizeof(index);
	if(fp->write((char *)&index,len)!=len){
		return -1;
	}
	char *urlstr = url->getUrl();
	int ret = writeString(fp,urlstr);
	xfree(urlstr);
	if (ret<0) {
		return -1;
	}
	len += ret;
	return len;
}
*/
char *KHttpObject::build_aio_header(int &len)
{
	int url_len;
	char *u = get_url_key(&uk, &url_len);
	if (u == NULL) {
		return NULL;
	}
	len = GetHeaderSize(url_len);
	char *buf = (char *)aio_alloc_buffer(len);
	char *hot = buf;
	write_file_header((KHttpObjectFileHeader *)hot);
	hot += sizeof(KHttpObjectFileHeader);
	hot += write_string(hot, u, url_len);
	free(u);
	KHttpHeader *header = data->headers;
	while (header) {
		hot += write_string(hot, header->attr, header->attr_len);
		hot += write_string(hot, header->val, header->val_len);
		header = header->next;
	}
	hot += write_string(hot, NULL, 0);
	kassert(hot - buf <= (INT64)index.head_size);
	return buf;
}
bool KHttpObject::save_header(KBufferFile *fp,const char *url,int url_len)
{
	save_dci_header(fp);
	writeString(fp,url,url_len);
	KHttpHeader *header = data->headers;
	while (header) {
		writeString(fp,header->attr,header->attr_len);
		writeString(fp,header->val,header->val_len);
		header = header->next;
	}
	writeString(fp,NULL);
	int pad_len = index.head_size - (int)fp->get_total_write();
	kassert(pad_len >= 0);
	if (pad_len > 0) {
		fp->write(NULL, pad_len);
	}
	return true;
}

bool KHttpObject::swapout(KBufferFile *file,bool fast_model)
{
	if (fast_model && !TEST(index.flags, FLAG_IN_DISK)) {
		return false;
	}
	kbuf *tmp;
	char *filename = NULL;
	assert(data);
	if (TEST(index.flags,FLAG_IN_DISK)) {
		if (dc_index_update == 0) {
			return true;
		}
#ifdef ENABLE_DB_DISK_INDEX
		if (dci) {
			dci->start(ci_update,this);
		}
#endif
	}
	dc_index_update = 0;
	if (conf.disk_cache <= 0 || cache.is_disk_shutdown()) {
		return false;
	}
	int url_len = 0;
	char *url = get_url_key(&uk, &url_len);
	if (url == NULL) {
		return false;
	}
	GetHeaderSize(url_len);
	INT64 buffer_size = index.content_length + index.head_size;
	if (buffer_size > KGL_MAX_BUFFER_FILE_SIZE) {
		buffer_size = KGL_MAX_BUFFER_FILE_SIZE;
	}
	filename = getFileName();
	klog(KLOG_INFO, "swap out obj=[%p %x %x] url=[%s] to file [%s]\n",
		this,
		index.flags,
		index.last_modified,
		url,
		filename);
	kassert(!file->opened());
	file->init();
	if (!file->open(filename,fileModify, KFILE_DSYNC)) {
		int err = errno;
		klog(KLOG_WARNING,"cann't open file [%s] to write. errno=[%d %s]\n",filename,err,strerror(err));
		goto swap_out_failed;
	}
	if (!save_header(file, url, url_len)) {
		goto swap_out_failed;
	}
	
	if (TEST(index.flags, FLAG_IN_DISK)) {
		//内容已经有，无需
		goto swap_out_success;
	}
	if (data->type != MEMORY_OBJECT) {
		klog(KLOG_ERR, "swapout failed obj type=[%d],file=[%s].\n", data->type,filename);
		goto swap_out_failed;
	}
	tmp = data->bodys;
	while (tmp) {
		if (file->write(tmp->data, tmp->used)<(int)tmp->used) {
			klog(KLOG_ERR,"cann't write cache to disk file=[%s].\n",filename);
			goto swap_out_failed;
		}
		tmp=tmp->next;
	}
	cache.getHash(h)->IncDiskObjectSize(this);
#ifdef ENABLE_DB_DISK_INDEX
	if (dci) {		
		dci->start(ci_add,this);
	}
#endif
swap_out_success:
	if (url) {
		free(url);
	}
	if (filename) {
		free(filename);
	}
	file->close();
	return true;
swap_out_failed:
	if (url) {
		free(url);
	}
	if (file->opened()) {
		file->close();
		unlink(filename);
	}
	if (filename) {
		free(filename);
	}
	return false;
}
#endif
