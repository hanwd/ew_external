#ifndef __H_EW_SSH2CLIENT__
#define __H_EW_SSH2CLIENT__

#include "ewa_base.h"
#include "external/config.h"

EW_ENTER


class DLLIMPEXP_EWA_EXTERNAL KO_Policy_ssh2_channel
{
public:
	typedef void* type;
	typedef type const_reference;
	static inline type invalid_value(){return NULL;}
	static void destroy(type& o);
};

class DLLIMPEXP_EWA_EXTERNAL KO_Policy_ssh2_session
{
public:
	typedef void* type;
	typedef type const_reference;
	static inline type invalid_value(){return NULL;}
	static void destroy(type& o);
};


class DLLIMPEXP_EWA_EXTERNAL KO_Policy_sftp_handle
{
public:
	typedef void* type;
	typedef type const_reference;
	static inline type invalid_value(){return NULL;}
	static void destroy(type& o);
};

class DLLIMPEXP_EWA_EXTERNAL KO_Policy_sftp_session
{
public:
	typedef void* type;
	typedef type const_reference;
	static inline type invalid_value(){return NULL;}
	static void destroy(type& o);
};

class DLLIMPEXP_EWA_EXTERNAL Ssh2Object
{
public:

	enum
	{
		FLAG_ENCODING_GBK	=1<<0,
		FLAG_NON_BLOCKING	=1<<1,
		FLAG_SHOW_MESSAGE	=1<<2,
	};

	BitFlags flags;
	mutable Mutex mutex;

protected:

	KO_Handle<KO_Policy_ssh2_session> session;
	Socket socket;

	void _do_log(int level,const String& msg);
	int _waitsocket();
	KO_Handle<KO_Policy_ssh2_channel> _open_channel();
	bool _read_channel(KO_Handle<KO_Policy_ssh2_channel>& channel,StringBuffer<char>& sb);
};

class Ssh2Client;


class DLLIMPEXP_EWA_EXTERNAL SftpSession : public Ssh2Object, public FSObject
{
public:

	SftpSession(const Ssh2Client& o);
	SftpSession(const SftpSession& o);


	bool FindFiles(const String& sftppath,arr_1t<FileItem>& items,const String& pattern);

	bool Mkdir(const String& dir);
	bool Rmdir(const String& dir,int flag);

	virtual bool Rename(const String& fp_old,const String& fp_new,int flag=0);
	virtual bool Remove(const String& fp);


	virtual Stream Download(const String& fp);
	virtual Stream Upload(const String& fp,int flag);

protected:
	KO_Handle<KO_Policy_sftp_session> impl;

};

class DLLIMPEXP_EWA_EXTERNAL Ssh2Channel : public Ssh2Object
{
public:
	Ssh2Channel(const Ssh2Client& o);
	Ssh2Channel(const Ssh2Channel& o);

	bool Execute(const String& commandline)
	{
		StringBuffer<char> sb;
		if(!Execute(commandline,sb)) return false;
		Console::WriteLine(sb);
		return true;
	}

	bool Execute(const String& commandline,StringBuffer<char>& sb);

	bool Read(StringBuffer<char>& sb);
	void Write(const String& cmdline);

protected:
	KO_Handle<KO_Policy_ssh2_channel> impl;

};

class DLLIMPEXP_EWA_EXTERNAL Ssh2Client : public Ssh2Object
{
public:

	Ssh2Client();
	~Ssh2Client();

	bool Connect(const String& ip,int port=22);

	bool Login(const String& username,const String& password,const String& fp_pub="",const String& fp_priv="");


	Ssh2Channel OpenChannel();

	bool Execute(const String& commandline)
	{
		StringBuffer<char> sb;
		if(!Execute(commandline,sb)) return false;
		Console::WriteLine(sb);
		return true;
	}

	bool Execute(const String& commandline,StringBuffer<char>& sb);

	void Close();


};



class DLLIMPEXP_EWA_EXTERNAL CallableSsh2Client : public CallableObject
{
public:
	Ssh2Client ssh2;
	CallableSsh2Client();

	virtual CallableMetatable* GetMetaTable();

};

EW_LEAVE

#endif
