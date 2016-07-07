
#include "ewa_base.h"
#include "external/crypto.h"
#include <openssl/md5.h>

EW_ENTER

class MD5Gen
{
public:
    MD5_CTX ctx; 

	MD5Gen()
	{
		 MD5_Init(&ctx);  
	}

	void reset()
	{
		 MD5_Init(&ctx);  	
	}

	void update(const String& s)
	{
		MD5_Update(&ctx, s.c_str(),s.size());  
	}

	void update(const char* p,size_t s)
	{
		MD5_Update(&ctx, p,s);  
	}

	static inline char to_hex(unsigned char ch)
	{
		return ch<10?'0'+ch:'A'+ch-10;
	}

	String get()
	{
		unsigned char digest[16];
		MD5_Final(digest, &ctx);
		StringBuffer<char> sb;		
		for(size_t i=0;i<16;i++)
		{
			sb<<to_hex(digest[i]>>4);
			sb<<to_hex(digest[i]&0xF);
		}
		return sb;
	}
};


String md5(const String& s)
{
	MD5Gen gen;
	gen.update(s);
	return gen.get();
   
}

EW_LEAVE
