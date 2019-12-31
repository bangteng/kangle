#include "KDirectoryFetchObject.h"
#include "KHttpObject.h"
#include "http.h"
#include <sstream>
using namespace std;
kev_result KPrevDirectoryFetchObject::open(KHttpRequest *rq)
{
	KHttpObject *obj = rq->ctx->obj;
	obj->data->status_code = STATUS_MOVED;
	KStringBuf new_path2;
	new_path2 << rq->raw_url.path << "/";
	if (rq->raw_url.param && *rq->raw_url.param){
		new_path2 << "?" << rq->raw_url.param;
	}
	rq->closeFetchObject();
	push_redirect_header(rq,new_path2.getString(),new_path2.getSize(),STATUS_FOUND);
	rq->startResponseBody(0);
	return stageEndRequest(rq);
}
KDirectoryFetchObject::KDirectoryFetchObject()
{
#ifdef _WIN32
	dp = INVALID_HANDLE_VALUE;
#else
	dp = NULL;
#endif
	hot = NULL;
}
KDirectoryFetchObject::~KDirectoryFetchObject()
{
#ifdef _WIN32
	if(dp!=INVALID_HANDLE_VALUE){
		FindClose(dp);
	}
#else
	if(dp){
		closedir(dp);
	}
#endif	
}
kev_result KDirectoryFetchObject::open(KHttpRequest *rq)
{
	assert(rq->file->isDirectory());
	rq->ctx->obj->insertHttpHeader(kgl_expand_string("Content-Type"),kgl_expand_string("text/html"));
#ifndef _WIN32
	assert(dp==NULL);
	dp = opendir(rq->file->getName());
	if (dp == NULL) {
		return handleError(rq,STATUS_NOT_FOUND,"cann't open dir");
	}
#else
	SET(rq->ctx->obj->index.flags,ANSW_LOCAL_SERVER);
	assert(dp==INVALID_HANDLE_VALUE);
	stringstream dir;
	dir << rq->file->getName() << "\\*";
	closeExecLock.Lock();
	dp = FindFirstFile(dir.str().c_str(), &FileData);
	if (dp == INVALID_HANDLE_VALUE) {
		closeExecLock.Unlock();
		return handleError(rq,STATUS_NOT_FOUND,"cann't open dir");
	}
	kfile_close_on_exec(dp,true);
	closeExecLock.Unlock();
#endif
	//	KBuffer buffer;
	buffer << "<html><head><title>";
	//	s << file->m_name);
	buffer << "</title></head><body>\n";
	buffer << "<a href='..'>[Parent directory]</a><hr>";
	buffer << "<table><tr><td>Name</td><td>Size</td><td>Last modified</td></tr>\n";
#ifndef _WIN32
	for (;;) {
		dirent *fp = readdir(dp);
		if (fp == NULL) {
			break;
		}
		if (strcmp(fp->d_name, ".") == 0 || strcmp(fp->d_name, "..") == 0) {
			continue;
		}
		browerOneFile(rq,fp->d_name);
	}
#else
	for(;;) {
		// Check the object is a directory or not

		if ((strcmp(FileData.cFileName, ".")==0) 
			|| (strcmp(FileData.cFileName, "..")==0)){
			goto next_file;
		}
		browerOneFile(rq,FileData.cFileName);
		next_file:
		if (!FindNextFile(dp, &FileData)) {
			if (GetLastError() == ERROR_NO_MORE_FILES) {
				break;
			}

		}
	}
#endif
	buffer << "</table><hr>Generated by " << PROGRAM_NAME << "/" << VERSION;
	buffer << "</body></html>";
	hot = buffer.getHead();
	rq->ctx->obj->data->status_code = STATUS_OK;
	return handleUpstreamRecvedHead(rq);
}
void KDirectoryFetchObject::browerOneFile(KHttpRequest *rq,const char *path) {

	//struct stat sbuf;
	struct _stat64 sbuf;
	stringstream f;
	f << rq->file->getName() << "/" << path;
	if (_stati64(f.str().c_str(), &sbuf) != 0) {
	//if (stat(s.str().c_str(), &sbuf) != 0) {
		//		printf("stat result error=%s,errno=%d\n", path, errno);
		return;
	}
	bool isdir = false;
	if (S_ISDIR(sbuf.st_mode)) {
		isdir = true;
	}
	buffer << "<tr><td>";
	if (isdir) {
		buffer << "[";
	}
	buffer << "<a href='" << url_encode(path).c_str();
	if (isdir) {
		buffer << "/";
	}
	buffer << "'>" << path << "</a>";
	if (isdir) {
		buffer << "]";
	}
	buffer << "</td><td>";
	f.str("");
	if (isdir) {
		//s << "<td colspan=2>&lt;DIR&gt;</td>");
		f << "-";
	} else {
		f << sbuf.st_size;
	}
	buffer << f.str().c_str() << "</td><td>";
	char tmp[27];
	makeLastModifiedTime(&sbuf.st_mtime, tmp, 27);
	buffer << tmp << "</td></tr>\n";
}
kev_result KDirectoryFetchObject::readBody(KHttpRequest *rq)
{
	for(;;){
		if (hot==NULL||hot->used==0) {
			return stage_rdata_end(rq,STREAM_WRITE_SUCCESS);
		}
		kbuf *tmp = hot;
		hot = hot->next;
		kev_result result = pushHttpBody(rq,tmp->data,tmp->used);
		if (KEV_HANDLED(result)) {
			return result;
		}
	}
	return kev_err;
}
