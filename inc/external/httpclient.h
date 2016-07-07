#ifndef __H_EW_HTTPCLIENT__
#define __H_EW_HTTPCLIENT__


#include "ewa_base.h"
#include "external/config.h"

#if defined(_MSC_VER) && (!defined(EWA_EXTERNAL_DLL) || defined(EWA_EXTERNAL_BUILDING))
#pragma comment(lib, "libcurl.lib")
#pragma comment(lib, "wldap32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "libeay32.lib")
#pragma comment(lib, "ssleay32.lib")
#pragma comment(lib, "libssh2.lib")
#endif

EW_ENTER

class DLLIMPEXP_EWA_EXTERNAL KO_Policy_curl_handle
{
public:
	typedef void* type;
	typedef type const_reference;
	static type invalid_value(){return NULL;}
	static void destroy(type& o);
};


typedef size_t (*curl_handle_data_callback)(void *ptr, size_t size, size_t nmemb, void *stream);

class DLLIMPEXP_EWA_EXTERNAL CurlHandle
{
public:
	typedef KO_Handle<KO_Policy_curl_handle> impl_type;

	CurlHandle();
	~CurlHandle();

	void SetUserPwd(const String& userpwd);

	void SetTimeout(int t);

	void SetDataCallback(curl_handle_data_callback fn, void* stream);
	void SetDataCallback(StringBuffer<char>& sb);

	void Redirect(Stream& stream);

	bool Perform(const String& url);

	bool EnsureInited();

	void Close(){ handle.reset(); }

	void swap(CurlHandle& other){ handle.swap(other.handle); }

protected:

	impl_type handle;

};

class DLLIMPEXP_EWA_EXTERNAL HttpClient : public CurlHandle
{
public:

	HttpClient();

	bool Perform(const String& url);
	bool Perform(const String& url,const String& postdata);
	bool Perform(const String& url,const indexer_map<String,String>& postdata);

	HttpClient Duplicate(bool copy_cookie=false);

	void SetUserAgent(const String& agent);
	void SetReferer(const String& refer);
	void SetCookieFile(const String& file);
	void SetCookieSave(const String& file);

	void SetCookie(const String& cookie);
	void SetCookie(const arr_1t<String>& cookies);
	arr_1t<String> GetCookie();
};

class DLLIMPEXP_EWA_EXTERNAL CallableHttpClient : public CallableObject
{
public:
	HttpClient http;
	CallableHttpClient();

	CallableData* DoClone(ObjectCloneState&);

	virtual CallableMetatable* GetMetaTable();

};

EW_LEAVE

#endif

