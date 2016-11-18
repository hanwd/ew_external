

#include "ewa_base.h"
#include "external/httpclient.h"
#include "curl/curl.h"


EW_ENTER


static size_t write_data_to_sb(void *ptr, size_t size, size_t nmemb, void *stream)
{
	(*(StringBuffer<char>*)stream).append((const char*)ptr, nmemb*size);
	return nmemb*size;
}

volatile int g_curl_init_state=0;


CurlHandle::CurlHandle()
{
	if(AtomicOps::exchange(&g_curl_init_state,1)==0)
	{
		CURLcode rc=curl_global_init(CURL_GLOBAL_ALL);
		if(rc==0)
		{
			atexit(curl_global_cleanup);
		}
		else
		{
			System::LogError("libcurl initialization failed (%d)", rc);
		}
	}

}



CurlHandle::~CurlHandle()
{


}

static size_t write_data_to_stream(void *ptr, size_t size, size_t nmemb, void *stream)
{
	size_t sz=nmemb*size;
	if(!((Stream*)stream)->send_all((const char*)ptr, nmemb*size))
	{
		System::LogTrace("write_data_to_stream_data error");
	}
	return nmemb*size;
}

void CurlHandle::Redirect(Stream& stream)
{
	SetDataCallback(write_data_to_stream,&stream);
}

void KO_Policy_curl_handle::destroy(type& o)
{
	if (o == NULL) return;
	curl_easy_cleanup(o);
	o = NULL;
}


HttpClient HttpClient::Duplicate(bool copy_cookie)
{
	HttpClient obj;

	obj.handle.reset(curl_easy_duphandle(handle.get()));

	if (copy_cookie)
	{
		struct curl_slist *list = NULL;
		curl_easy_getinfo(handle.get(), CURLINFO_COOKIELIST, &list);
		for (curl_slist *p = list; p; p = p->next)
		{
			curl_easy_setopt(obj.handle.get(), CURLOPT_COOKIELIST, p->data);
		}		
		curl_slist_free_all(list);
	}

	return obj;
}

bool CurlHandle::EnsureInited()
{
	if (handle.get()) return true;

	CURL* curl = curl_easy_init();

	if (!curl)
	{
		return false;
	}

	curl_easy_setopt(handle.get(), CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_COOKIESESSION, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.9.2.8) Gecko/20100722 Firefox/3.6.8");

	handle.reset(curl);

	return true;
}

arr_1t<String> HttpClient::GetCookie()
{
	arr_1t<String> cookies;
	if (!EnsureInited())
	{
		return cookies;
	}

	struct curl_slist *list=NULL;
	curl_easy_getinfo(handle.get(),CURLINFO_COOKIELIST,&list);
	for (curl_slist *p = list; p;p=p->next)
	{
		cookies.push_back(p->data);
	}
	curl_slist_free_all (list);

	return cookies;
}


void CurlHandle::SetTimeout(int t)
{
	if (!EnsureInited()) return;
	curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, t);
}

void HttpClient::SetUserAgent(const String& agent)
{
	if (!EnsureInited()) return;
	curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, agent.c_str());
}

void HttpClient::SetCookie(const arr_1t<String>& cookies)
{
	for (size_t i = 0; i < cookies.size(); i++)
	{
		SetCookie(cookies[i]);
	}
}

void HttpClient::SetCookie(const String& cookie)
{
	if (!EnsureInited()) return;

	arr_1t<String> sep = string_split(cookie, "\t");
	if (sep.size() > 5||cookie=="ALL"||cookie=="SESS"||cookie=="FLUSH"||cookie=="RELOAD")
	{
		curl_easy_setopt(handle.get(), CURLOPT_COOKIELIST, cookie.c_str());
	}
	else
	{
		curl_easy_setopt(handle.get(), CURLOPT_COOKIE, cookie.c_str());
	}
}

void HttpClient::SetReferer(const String& refer)
{
	if (!EnsureInited()) return;
	curl_easy_setopt(handle.get(), CURLOPT_REFERER, refer.c_str());
}

void CurlHandle::SetUserPwd(const String& userpwd)
{
	if (!EnsureInited()) return;
	curl_easy_setopt(handle.get(), CURLOPT_USERPWD, userpwd.c_str());
}

void CurlHandle::SetDataCallback(curl_handle_data_callback fn, void* stream)
{
	if (!EnsureInited()) return;
	curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, fn);
	curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, stream);
}

void CurlHandle::SetDataCallback(StringBuffer<char>& sb)
{
	SetDataCallback(write_data_to_sb, &sb);
}


HttpClient::HttpClient()
{
	SetCookieFile("http.cookie.txt");
}

void HttpClient::SetCookieFile(const String& file)
{
	if (!EnsureInited()) return;
	curl_easy_setopt(handle.get(), CURLOPT_COOKIEFILE, file.c_str());

}

void HttpClient::SetCookieSave(const String& file)
{
	if (!EnsureInited()) return;
	curl_easy_setopt(handle.get(), CURLOPT_COOKIEJAR, file.c_str());
}

bool HttpClient::Perform(const String& url)
{
	return CurlHandle::Perform(url);
}

bool CurlHandle::Perform(const String& url)
{
	if (!EnsureInited()) return false;

	curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
	curl_easy_setopt(handle.get(), CURLOPT_POST, 0L);

	CURLcode res = curl_easy_perform(handle.get());

	if (res != 0)
	{
		return false;
	}

	return true;
}

bool HttpClient::Perform(const String& url, const String& postdata)
{
	if (!EnsureInited()) return false;

	curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
	curl_easy_setopt(handle.get(), CURLOPT_POST, 1L);
	curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, postdata.c_str());

    curl_easy_setopt(handle.get(), CURLOPT_VERBOSE, 1);    

	CURLcode res = curl_easy_perform(handle.get());

	if (res != 0)
	{
		return false;
	}
	return true;
}


bool HttpClient::Perform(const String& url, const indexer_map<String, String>& data)
{
	if (!EnsureInited()) return false;

	StringBuffer<char> postdata;
	for (indexer_map<String, String>::const_iterator it = data.begin(); it != data.end(); ++it)
	{
		if (!postdata.empty())
		{
			postdata << "&";
		}
		postdata << string_escape((*it).first);
		postdata << "=";
		postdata << string_escape((*it).second);
	}

	curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
	curl_easy_setopt(handle.get(), CURLOPT_POST, 1L);
	curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, postdata.c_str());

	CURLcode res = curl_easy_perform(handle.get());   // Ö´ÐÐ

	if (res != 0)
	{
		return false;
	}

	return true;
}



CallableHttpClient::CallableHttpClient()
{
	

}



class CallableFunctionHttp : public CallableFunction
{
public:
	CallableHttpClient* GetClient(Executor& ewsl)
	{
		return dynamic_cast<CallableHttpClient*>(ewsl.ci1.nbp[StackState1::SBASE_THIS].kptr());
	}

	HttpClient& GetTarget(Executor& ewsl)
	{
		CallableHttpClient* p=dynamic_cast<CallableHttpClient*>(ewsl.ci1.nbp[StackState1::SBASE_THIS].kptr());
		if(!p) ewsl.kerror("invalid this pointer");
		return p->http;
	}
};

class HttpContentToString
{
public:
	static void g(Variant& var,StringBuffer<char>& sb)
	{
		var.reset(IConv::from_unknown(sb.c_str()));
	}
};

class HttpContentToBuffer
{
public:
	static void g(Variant& var,StringBuffer<char>& sb)
	{
		var.reset(sb);
	}
};

template<typename P>
class CallableFunctionHttpGet_t : public CallableFunctionHttp
{
public:
	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.check_pmc(this,pm,1);

		HttpClient& http(GetTarget(ewsl));

		String uri=ewsl.ci0.nbx[1].ref<String>();
		StringBuffer<char> sb;


		http.SetDataCallback(sb);
		bool flag=http.Perform(uri);

		ewsl.ci0.nbx[1].reset(flag);
		if(flag)
		{
			P::g(ewsl.ci0.nbx[2],sb);
		}
		return flag?2:1;
	}
};

template<typename P>
class CallableFunctionHttpPost_t : public CallableFunctionHttp
{
public:
	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.check_pmc(this,pm,2);

		HttpClient& http(GetTarget(ewsl));

		String uri=ewsl.ci0.nbx[1].ref<String>();
		StringBuffer<char> sb;

		indexer_map<String,String> postdata;
		VariantTable& tb(ewsl.ci0.nbx[2].ref<VariantTable>());
		for(size_t i=0;i<tb.size();i++)
		{
			postdata[tb.get(i).first]=variant_cast<String>(tb.get(i).second);
		}


		http.SetDataCallback(sb);
		bool flag=http.Perform(uri,postdata);

		ewsl.ci0.nbx[1].reset(flag);
		if(flag)
		{
			P::g(ewsl.ci0.nbx[2],sb);
		}
		return flag?2:1;
	}
};

class CallableFunctionHttpGet : public CallableFunctionHttpGet_t<HttpContentToString>
{

};

class CallableFunctionHttpGet2 : public CallableFunctionHttpGet_t<HttpContentToBuffer>
{

};

class CallableFunctionHttpPost : public CallableFunctionHttpPost_t<HttpContentToString>
{

};

class CallableFunctionHttpPost2 : public CallableFunctionHttpPost_t<HttpContentToBuffer>
{

};




class CallableFunctionHttpSetCookieFile : public CallableFunctionHttp
{
public:
	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.check_pmc(this,pm,1);
		HttpClient& http(GetTarget(ewsl));

		String file=ewsl.ci0.nbx[1].ref<String>();		
		http.SetCookieFile(file);
		return 0;
	}
};

class CallableFunctionHttpSetCookieSave : public CallableFunctionHttp
{
public:
	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.check_pmc(this,pm,1);
		HttpClient& http(GetTarget(ewsl));
		String file=ewsl.ci0.nbx[1].ref<String>();		
		http.SetCookieSave(file);
		return 0;
	}
};

class CallableFunctionHttpSetUserAgent : public CallableFunctionHttp
{
public:
	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.check_pmc(this,pm,1);
		HttpClient& http(GetTarget(ewsl));
		String val=ewsl.ci0.nbx[1].ref<String>();		
		http.SetUserAgent(val);
		return 0;
	}
};
class CallableFunctionHttpSetReferer : public CallableFunctionHttp
{
public:
	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.check_pmc(this,pm,1);
		HttpClient& http(GetTarget(ewsl));
		String val=ewsl.ci0.nbx[1].ref<String>();		
		http.SetReferer(val);
		return 0;
	}
};

class CallableFunctionHttpSetCookie : public CallableFunctionHttp
{
public:
	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.check_pmc(this,pm,1,2);

		HttpClient& http(GetTarget(ewsl));

		if(pm==1)
		{
			if(String* pval=ewsl.ci0.nbx[1].ptr<String>())
			{
				http.SetCookie(*pval);	
			}
			else if(arr_xt<Variant>* pval=ewsl.ci0.nbx[1].ptr<arr_xt<Variant> >())
			{
				for(size_t i=0;i<pval->size();i++)
				{
					http.SetCookie((*pval)[i].ref<String>());
				}
			}
		}
		else if(pm==2)
		{
			String& key(ewsl.ci0.nbx[1].ref<String>());
			String& val(ewsl.ci0.nbx[2].ref<String>());
			http.SetCookie(key+"="+string_escape(val));
		}

		return 0;
	}
};


class CallableFunctionHttpGetCookie : public CallableFunctionHttp
{
public:
	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.check_pmc(this,pm,0);
		HttpClient& http(GetTarget(ewsl));
		arr_1t<String> arr(http.GetCookie());
		arr_xt<Variant>& res(ewsl.ci0.nbx[1].ref<arr_xt<Variant> >());
		res.resize(arr.size());
		for(size_t i=0;i<arr.size();i++) res[i].reset(arr[i]);
		return 1;
	}
};

class CallableFunctionHttpCreate : public CallableFunction
{
public:

	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.ci0.nbx[1].reset(new CallableHttpClient);
		return 1;
	}
};


template<>
class CallableMetatableT<HttpClient> : public CallableMetatable
{
public:

	CallableMetatableT():CallableMetatable("httpclient")
	{
		value["create"].reset(new CallableFunctionHttpCreate);
		value["get"].reset(new CallableFunctionHttpGet);
		value["post"].reset(new CallableFunctionHttpPost);
		value["get2"].reset(new CallableFunctionHttpGet2);
		value["post2"].reset(new CallableFunctionHttpPost2);

		value["set_cookie_file"].reset(new CallableFunctionHttpSetCookieFile);
		value["set_cookie_save"].reset(new CallableFunctionHttpSetCookieSave);
		value["set_useragent"].reset(new CallableFunctionHttpSetUserAgent);
		value["set_referer"].reset(new CallableFunctionHttpSetReferer);

		value["set_cookie"].reset(new CallableFunctionHttpSetCookie);
		value["get_cookie"].reset(new CallableFunctionHttpGetCookie);


	}

	DECLARE_OBJECT_CACHED_INFO(CallableMetatableT<HttpClient>,ObjectInfo)
};


IMPLEMENT_OBJECT_INFO(CallableMetatableT<HttpClient>,ObjectInfo)

CallableMetatable* CallableHttpClient::GetMetaTable()
{
	return CallableMetatableT<HttpClient>::sm_info.GetCachedInstance();
}


CallableData* CallableHttpClient::DoClone(ObjectCloneState& cs)
{
	if(cs.type>0)
	{
		CallableHttpClient* p=NULL;
		try
		{
			p=new CallableHttpClient;
			p->http=http.Duplicate(true);
		}
		catch(...)
		{
			delete p;
			throw;
		}
		return p;
	}
	else
	{
		return this;
	}

}


EW_LEAVE


