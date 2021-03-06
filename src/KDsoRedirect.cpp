#include "KDsoRedirect.h"
#include "KDsoAsyncFetchObject.h"

KFetchObject *KDsoRedirect::makeFetchObject(KHttpRequest *rq, KFileName *file)
{
	if (TEST(us->flags, KF_UPSTREAM_SYNC)) {
		//not support
		return NULL;
	}
	return new KDsoAsyncFetchObject();
}
KFetchObject *KDsoRedirect::makeFetchObject(KHttpRequest *rq, void *model_ctx)
{
	if (TEST(us->flags, KF_UPSTREAM_SYNC)) {
		//not support
		return NULL;
	}
	return new KDsoAsyncFetchObject(model_ctx);
}