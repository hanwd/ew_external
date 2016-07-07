

#include "ewa_base.h"
#include "external/ssh2client.h"

#include "libssh2.h"
#include "libssh2_sftp.h"


EW_ENTER


void KO_Policy_ssh2_session::destroy(type& o)
{
	if(o==NULL) return;
	libssh2_session_disconnect((LIBSSH2_SESSION*)o,"Normal Shutdown, Thank you for playing");
	libssh2_session_free((LIBSSH2_SESSION*)o);
	o=NULL;
}

void KO_Policy_ssh2_channel::destroy(type& o)
{
	if(o==NULL) return;

	int exitcode = 127;
	int rc;
	while( (rc = libssh2_channel_close((LIBSSH2_CHANNEL*)o)) == LIBSSH2_ERROR_EAGAIN )
	{
		Thread::yield();
	}

	//char *exitsignal=(char *)"none";
	//if( rc == 0 )
	//{
	//	exitcode = libssh2_channel_get_exit_status( (LIBSSH2_CHANNEL*)o );
	//	libssh2_channel_get_exit_signal((LIBSSH2_CHANNEL*)o, &exitsignal,
	//									NULL, NULL, NULL, NULL, NULL);
	//}

	libssh2_channel_free((LIBSSH2_CHANNEL*)o);
	o = NULL;
}


volatile int g_ssh2_init_state=0;

void Ssh2Object::_do_log(int level,const String& msg)
{
	System::DoLog(level,msg.c_str());
	if(flags.get(FLAG_SHOW_MESSAGE))
	{
		this_logger().DoLog(level,msg);
	}
}

Ssh2Client::Ssh2Client()
{
	if(AtomicOps::exchange(&g_ssh2_init_state,1)==0)
	{
		int rc=libssh2_init(0);
		if(rc==0)
		{
			atexit(libssh2_exit);
		}
		else
		{
			_do_log(LOGLEVEL_ERROR,String::Format("libssh2 initialization failed (%d)", rc));
		}
	}

}

Ssh2Client::~Ssh2Client()
{
	Close();
}


int Ssh2Object::_waitsocket()
{

	int socket_fd=socket.native_handle();

	struct timeval timeout;
	int rc;
	fd_set fd;
	fd_set *writefd = NULL;
	fd_set *readfd = NULL;
	int dir;

	timeout.tv_sec = 1;
	timeout.tv_usec = 0;

	FD_ZERO(&fd);

	FD_SET(socket_fd, &fd);

	/* now make sure we wait in the correct direction */
	dir = libssh2_session_block_directions((LIBSSH2_SESSION*)session.get());

	if(dir & LIBSSH2_SESSION_BLOCK_INBOUND)
		readfd = &fd;

	if(dir & LIBSSH2_SESSION_BLOCK_OUTBOUND)
		writefd = &fd;

	rc = select(socket_fd + 1, readfd, writefd, NULL, &timeout);

	return rc;
}


void KO_Policy_sftp_session::destroy(type& o)
{
	if(!o) return;
	libssh2_sftp_shutdown((LIBSSH2_SFTP*)o);
	o=NULL;
}

void KO_Policy_sftp_handle::destroy(type& o)
{
	if(!o) return;
	while(libssh2_sftp_close((LIBSSH2_SFTP_HANDLE*)o)==LIBSSH2_ERROR_EAGAIN);
	o=NULL;
}

SftpSession::SftpSession(const Ssh2Client& o):Ssh2Object(o)
{

	if(!session.get())
	{
		return;
	}

	LIBSSH2_SFTP* sftp_session=NULL;

	while(!sftp_session)
	{
		sftp_session = libssh2_sftp_init((LIBSSH2_SESSION*)session.get());

		if(sftp_session) break;
		
		int rc=libssh2_session_last_errno((LIBSSH2_SESSION*)session.get());
		if(rc == LIBSSH2_ERROR_EAGAIN)
		{
			_waitsocket();
			continue;
		}

		_do_log(LOGLEVEL_ERROR,"Unable to init SFTP session");
		return;
			
	}

	impl.reset(sftp_session);

}

SftpSession::SftpSession(const SftpSession& o):Ssh2Object(o),impl(o.impl)
{

}

bool SftpSession::Mkdir(const String& dir)
{
	do
	{
		int rc = libssh2_sftp_mkdir((LIBSSH2_SFTP*)impl.get(), dir.c_str(),
								LIBSSH2_SFTP_S_IRWXU|
								LIBSSH2_SFTP_S_IRGRP|LIBSSH2_SFTP_S_IXGRP|
								LIBSSH2_SFTP_S_IROTH|LIBSSH2_SFTP_S_IXOTH);

		if(rc==LIBSSH2_ERROR_EAGAIN)
		{
			_waitsocket();
			continue;
		}

		if(rc)
		{
			_do_log(LOGLEVEL_ERROR,String::Format("libssh2_sftp_mkdir failed: %d", rc));
			return false;
		}


	}while(0);

	return true;

}



bool SftpSession::Rmdir(const String& dir,int flag)
{
	while(1)
	{
		int rc = libssh2_sftp_rmdir((LIBSSH2_SFTP*)impl.get(), dir.c_str());

		if(rc==LIBSSH2_ERROR_EAGAIN)
		{
			_waitsocket();
			continue;
		}

		if(rc)
		{
			_do_log(LOGLEVEL_ERROR,String::Format("libssh2_sftp_rmdir failed: %d", rc));
			return false;
		}

		break;
	};

	return true;
}



bool SftpSession::FindFiles(const String& sftppath,arr_1t<FileItem>& items,const String& pattern)
{

	items.clear();

	if(!impl.ok())
	{
		return false;
	}

	LIBSSH2_SFTP_HANDLE *sftp_handle=NULL;

	do 
	{
		sftp_handle = libssh2_sftp_opendir((LIBSSH2_SFTP*)impl.get(), sftppath.c_str());

		if (!sftp_handle)
		{
			if (libssh2_session_last_errno((LIBSSH2_SESSION*)session.get()) != LIBSSH2_ERROR_EAGAIN) 
			{
				_do_log(LOGLEVEL_ERROR,"Unable to open file with SFTP");
				return false;
			}
			else 
			{
				_waitsocket();
			}
		}
	} while (!sftp_handle);

	KO_Handle<KO_Policy_sftp_handle> sftp_impl(sftp_handle);

	int rc;

    do 
	{

        LIBSSH2_SFTP_ATTRIBUTES attrs;


        char buffer[1024];
        rc = libssh2_sftp_readdir(sftp_handle,buffer, sizeof(buffer), &attrs);

		if(rc==LIBSSH2_ERROR_EAGAIN)
		{
			_waitsocket();
			continue;
		}

        if(rc > 0) 
		{

			FileItem item;
			item.filename=IConv::from_unknown(buffer);
			if(attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) 
			{
				item.filesize=attrs.filesize;		
			}

			if(attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS && attrs.permissions & 0x04000)
			{
				item.flags.add(FileItem::IS_FOLDER);
			}

			items.push_back(item);
        }
        else
		{
            return rc==0;
		}


    } while (1);

	return true;
}

bool SftpSession::Rename(const String& fp_old,const String& fp_new,int flag)
{
	int rc;
	while(rc=libssh2_sftp_rename_ex((LIBSSH2_SFTP*)impl.get(),fp_old.c_str(),fp_old.size(),fp_new.c_str(),fp_new.size(),flag)==LIBSSH2_ERROR_EAGAIN)
	{
		Thread::yield();
	}
	return rc==0;
}

bool SftpSession::Remove(const String& fp)
{
	int rc;
	while(rc=libssh2_sftp_unlink((LIBSSH2_SFTP*)impl.get(),fp.c_str())==LIBSSH2_ERROR_EAGAIN)
	{
		Thread::yield();
	}
	return rc==0;
}

class SerializerSftp : public SerializerDuplex
{
public:

	SftpSession sftp;
	KO_Handle<KO_Policy_sftp_handle> impl;

	void Close()
	{
		impl.reset();
	}

	virtual int64_t size()
	{
		if(!impl.ok()) return -1;

		LIBSSH2_SFTP_ATTRIBUTES attrs;
		int rc;

		while((rc=libssh2_sftp_fstat_ex((LIBSSH2_SFTP_HANDLE*)impl.get(), &attrs, 0)) ==LIBSSH2_ERROR_EAGAIN) 
		{
			Thread::yield();
		}

		if(rc<0) return -1;
		return attrs.filesize;
	}

	virtual int64_t seek(int64_t pos,int t)
	{
		if(!impl.ok()) return -1;

		if(t==SEEKTYPE_CUR)
		{
			pos+=tell();
		}
		else if(t==SEEKTYPE_END)
		{
			pos+=size();
		}

        libssh2_sftp_seek64((LIBSSH2_SFTP_HANDLE*)impl.get(), pos);
		return tell();
	}

	virtual int64_t tell()
	{
		if(!impl.ok()) return -1;
		return libssh2_sftp_tell64((LIBSSH2_SFTP_HANDLE*)impl.get());
	}



	SerializerSftp(SftpSession t,LIBSSH2_SFTP_HANDLE* p):sftp(t),impl(p){}

	void flush()
	{
		if(!impl.ok()) return;
		libssh2_sftp_fsync((LIBSSH2_SFTP_HANDLE*)impl.get());
	}

	virtual int recv(char* buf,int len)
	{
		if(!impl.ok()) return -1;

		int rc;
		do
		{
			rc = libssh2_sftp_read((LIBSSH2_SFTP_HANDLE*)impl.get(),buf,len);
			if(rc>=0)
			{
				return rc;
			}

			if(rc==LIBSSH2_ERROR_EAGAIN)
			{
				Thread::yield();
				continue;
			}

		}while(0);

		return -1;
	}

	virtual int32_t send(const char* buf,int len)
	{

		if(!impl.ok()) return -1;

		int rc;
		while(1)
		{
			rc = libssh2_sftp_write((LIBSSH2_SFTP_HANDLE*)impl.get(),buf,len);
			if(rc>=0)
			{
				return rc;
			}

			if(rc==LIBSSH2_ERROR_EAGAIN)
			{
				Thread::yield();
				continue;
			}
			break;
		};

		return -1;
	}
};

Stream SftpSession::Download(const String& fp)
{

	Stream stream;
	if(!impl.ok()) return stream;



	LIBSSH2_SFTP_HANDLE *sftp_handle=NULL;

	do 
	{
		sftp_handle = libssh2_sftp_open((LIBSSH2_SFTP*)impl.get(), fp.c_str(),
										LIBSSH2_FXF_READ, 0);

		if (!sftp_handle)
		{
			if (libssh2_session_last_errno((LIBSSH2_SESSION*)session.get()) != LIBSSH2_ERROR_EAGAIN) 
			{
				_do_log(LOGLEVEL_ERROR,"Unable to open file with SFTP");
				return stream;
			}
			else 
			{
				_waitsocket();
			}
		}
	} while (!sftp_handle);

	stream.assign(SharedPtrT<SerializerDuplex>(new SerializerSftp(*this,sftp_handle)));

	return stream;
}

Stream SftpSession::Upload(const String& fp,int flag)
{
	Stream stream;

	if(!impl.ok()) return stream;

	LIBSSH2_SFTP_HANDLE *sftp_handle=NULL;

	int sftp_flags=LIBSSH2_FXF_READ|LIBSSH2_FXF_WRITE;
	if(flag&FLAG_FILE_CR) sftp_flags|=LIBSSH2_FXF_CREAT;
	if(flag&FLAG_FILE_TRUNCATE) sftp_flags|=LIBSSH2_FXF_TRUNC;

	// APPEND doesn't have any effect on OpenSSH servers???
	//if(flag&FLAG_FILE_APPEND) sftp_flags|=LIBSSH2_FXF_APPEND;

	do 
	{
		sftp_handle =
			libssh2_sftp_open((LIBSSH2_SFTP*)impl.get(), fp.c_str(),
                          sftp_flags,
                          LIBSSH2_SFTP_S_IRUSR|LIBSSH2_SFTP_S_IWUSR|
                          LIBSSH2_SFTP_S_IRGRP|LIBSSH2_SFTP_S_IROTH);

		if (!sftp_handle)
		{
			if (libssh2_session_last_errno((LIBSSH2_SESSION*)session.get()) != LIBSSH2_ERROR_EAGAIN) 
			{
				_do_log(LOGLEVEL_ERROR,"Unable to open file with SFTP");
				return stream;
			}
			else 
			{
				_waitsocket();
			}
		}
	} while (!sftp_handle);

	stream.assign(SharedPtrT<SerializerSftp>(new SerializerSftp(*this,sftp_handle)));
	if(flag&FLAG_FILE_APPEND)
	{
		int64_t pos=stream.seekp(0,SEEKTYPE_END);
		pos=pos;
	}

	return stream;
}



bool Ssh2Object::_read_channel(KO_Handle<KO_Policy_ssh2_channel>& channel,StringBuffer<char>& sb)
{

	int bytecount=0;
	int spin=0;

	for( ;; )
	{
		int rc;

		char buffer[0x4000];
		rc = libssh2_channel_read( (LIBSSH2_CHANNEL*)channel.get(), buffer, sizeof(buffer) );
		if( rc > 0 )
		{
			bytecount += rc;
			sb.append(buffer,rc);
			continue;
		}
		
		if(rc != LIBSSH2_ERROR_EAGAIN)
		{
			break;
		}
		else if(libssh2_channel_eof( (LIBSSH2_CHANNEL*)channel.get()))
		{
			break;
		}
		else if(spin++>1)
		{
			break;
		}

		_waitsocket();
	}

	//if(flags.get(FLAG_ENCODING_GBK))
	//{
	//	IConv::gbk_to_utf8(sb,sb.data(),sb.size());
	//}

	return true;
}

KO_Handle<KO_Policy_ssh2_channel> Ssh2Object::_open_channel()
{

	if(!session.ok())
	{
		return KO_Handle<KO_Policy_ssh2_channel>();
	}

	LIBSSH2_CHANNEL* channel_handle=NULL;

	while( (channel_handle = libssh2_channel_open_session((LIBSSH2_SESSION*)session.get())) == NULL &&
			libssh2_session_last_error((LIBSSH2_SESSION*)session.get(),NULL,NULL,0) ==
			LIBSSH2_ERROR_EAGAIN )
	{
		_waitsocket();
	}

	if( channel_handle == NULL )
	{
		return KO_Handle<KO_Policy_ssh2_channel>();
	}

	libssh2_channel_set_blocking(channel_handle,0);
	
	return KO_Handle<KO_Policy_ssh2_channel>(channel_handle);
	
}

bool Ssh2Client::Execute(const String& commandline_,StringBuffer<char>& sb)
{
	KO_Handle<KO_Policy_ssh2_channel>  channel=_open_channel();
	if(!channel.ok())
	{
		return false;
	}

	String commandline(commandline_);

	if(flags.get(FLAG_ENCODING_GBK))
	{
		commandline=IConv::to_gbk(commandline);
	}

	int rc;
	while( (rc = libssh2_channel_exec((LIBSSH2_CHANNEL*)channel.get(), commandline.c_str())) ==
			LIBSSH2_ERROR_EAGAIN )
	{
		_waitsocket();
	}

	if( rc != 0 )
	{
		_do_log(LOGLEVEL_ERROR,String::Format("libssh2_channel_exec failed: ",rc));
		return false;
	}

	if(!_read_channel(channel,sb))
	{
		return false;
	}

	sb=IConv::from_unknown(sb.c_str());
	return true;
}

bool Ssh2Client::Connect(const String& ip,int port)
{
	Close();

	if(!socket.connect(ip,port))
	{
		_do_log(LOGLEVEL_ERROR,"CANNOT connect to ssh2 server");
		return false;
	}

	LIBSSH2_SESSION* session_handle = libssh2_session_init();
	if (!session_handle)
	{
		socket.close();
		_do_log(LOGLEVEL_ERROR,"CANNOT create ssh2_session");
		return false;
	}


	session.reset(session_handle);

	libssh2_session_set_blocking(session_handle, flags.get(FLAG_NON_BLOCKING)?0:1);

	int rc;
	while ((rc = libssh2_session_handshake(session_handle, socket.native_handle())) ==	LIBSSH2_ERROR_EAGAIN);

	if (rc) 
	{
		char* msg_str=NULL;
		int msg_len=0;

		libssh2_session_last_error(session_handle,&msg_str,&msg_len,0);

		Close();
		_do_log(LOGLEVEL_ERROR,String::Format("libssh2:handshake failed with %d",rc));
		return false;
	}

	return true;
}



bool Ssh2Client::Login(const String& username,const String& password,const String& fp_pub,const String& fp_priv)
{
	if(!session.ok())
	{
		return false;
	}

	//const char* fingerprint = libssh2_hostkey_hash((LIBSSH2_SESSION*)session.get(), LIBSSH2_HOSTKEY_HASH_SHA1);
	//EW_UNUSED(fingerprint);
	//char* userauthlist = libssh2_userauth_list((LIBSSH2_SESSION*)session.get(), username.c_str(), username.length());
	//EW_UNUSED(userauthlist);

	int rc;
	if ( fp_pub.empty() ) 
	{
		while ((rc = libssh2_userauth_password((LIBSSH2_SESSION*)session.get(), username.c_str(), password.c_str())) ==	LIBSSH2_ERROR_EAGAIN);
		if (rc) 
		{
			_do_log(LOGLEVEL_ERROR,"Authentication by password failed.");
			return false;
		}
	}
	else
	{
		while ((rc = libssh2_userauth_publickey_fromfile(
			(LIBSSH2_SESSION*)session.get(),
			username.c_str(),
			fp_pub.c_str(),
			fp_priv.c_str(),
			password.c_str())) ==
				LIBSSH2_ERROR_EAGAIN);

		if (rc) 
		{
			_do_log(LOGLEVEL_ERROR,"Authentication by public key failed.");
			return false;
		}
	}

	return true;
}


Ssh2Channel::Ssh2Channel(const Ssh2Channel& o)
:Ssh2Object(o),impl(o.impl)
{
	
}
Ssh2Channel::Ssh2Channel(const Ssh2Client& o)
:Ssh2Object(o)
{

	impl=_open_channel();
	if(!impl.ok())
	{
		return;
	}

	int rc;
	//vanilla
    while (rc=libssh2_channel_request_pty((LIBSSH2_CHANNEL*)impl.get(), "")) 
	{
		if(rc==LIBSSH2_ERROR_EAGAIN)
		{
			_waitsocket();
			continue;
		}
		impl.reset();
		_do_log(LOGLEVEL_ERROR,"Failed requesting pty.");
		return;
    }

    while (rc=libssh2_channel_shell((LIBSSH2_CHANNEL*)impl.get())) 
	{
		if(rc==LIBSSH2_ERROR_EAGAIN)
		{
			_waitsocket();
			continue;
		}

		impl.reset();
        _do_log(LOGLEVEL_ERROR,"Unable to request shell on allocated pty.");
		return;
    }

}

Ssh2Channel Ssh2Client::OpenChannel()
{
	return *this;
}



bool Ssh2Channel::Read(StringBuffer<char>& sb)
{
	return _read_channel(impl,sb);
}

void Ssh2Channel::Write(const String& cmdline)
{

	size_t n=cmdline.size();
	const char* p=cmdline.c_str();

	int rc;
	
	while(n>0)
	{
		rc= libssh2_channel_write( (LIBSSH2_CHANNEL*)impl.get(), p, n );
		if(rc>0)
		{
			n-=rc;
			p+=rc;
		}
		else if( rc == LIBSSH2_ERROR_EAGAIN )
		{
			_waitsocket();
		}
		else
		{
			break;
		}
	}

}

bool Ssh2Channel::Execute(const String& commandline_,StringBuffer<char>& sb)
{
	String commandline(commandline_);

	if(flags.get(FLAG_ENCODING_GBK))
	{
		commandline=IConv::to_gbk(commandline);
	}

	if(!impl.ok())
	{
		return false;
	}

	int rc;
	while( (rc = libssh2_channel_exec((LIBSSH2_CHANNEL*)impl.get(), commandline.c_str())) ==
			LIBSSH2_ERROR_EAGAIN )
	{
		_waitsocket();
	}

	if( rc != 0 )
	{
		_do_log(LOGLEVEL_ERROR,String::Format("Execute failed: %d",rc));
		return false;
	}
	
	return _read_channel(impl,sb);
}



void Ssh2Client::Close()
{
	session.reset();
	socket.close();
}


class CallableFunctionSsh2 : public CallableFunction
{
public:
	CallableSsh2Client* GetClient(Executor& ewsl)
	{
		CallableSsh2Client* pclient=dynamic_cast<CallableSsh2Client*>(ewsl.ci1.nbp[StackState1::SBASE_THIS].kptr());
		if(!pclient)
		{
			ewsl.kerror("invalid this pointer");
		}
		return pclient;
	}

	Ssh2Client& GetTarget(Executor& ewsl)
	{
		CallableSsh2Client* pclient=dynamic_cast<CallableSsh2Client*>(ewsl.ci1.nbp[StackState1::SBASE_THIS].kptr());
		if(!pclient)
		{
			ewsl.kerror("invalid this pointer");
		}
		return pclient->ssh2;
	}

};

class CallableFunctionSsh2Connect : public CallableFunctionSsh2
{
public:
	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.check_pmc(this,pm,1,2);
		Ssh2Client& ssh2(GetTarget(ewsl));	
		String ip=ewsl.ci0.nbx[1].ref<String>();
		int port=pm>1?variant_cast<int>(ewsl.ci0.nbx[2]):22;
		ewsl.ci0.nbx[1].reset(ssh2.Connect(ip,port));
		return 1;
	}
};

class CallableFunctionSsh2Login : public CallableFunctionSsh2
{
public:
	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.check_pmc(this,pm,2,4);

		Ssh2Client& ssh2(GetTarget(ewsl));

		String user=ewsl.ci0.nbx[1].ref<String>();
		String pass=ewsl.ci0.nbx[2].ref<String>();

		String fp_pub;
		String fp_priv;

		if(pm==4)
		{
			fp_pub=ewsl.ci0.nbx[3].ref<String>();
			fp_priv=ewsl.ci0.nbx[4].ref<String>();		
		}

		ewsl.ci0.nbx[1].reset(ssh2.Login(user,pass,fp_pub,fp_priv));
		return 1;
	}
};

class CallableFunctionSsh2Execute : public CallableFunctionSsh2
{
public:
	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.check_pmc(this,pm,1);
		Ssh2Client& ssh2(GetTarget(ewsl));

		String cmd=ewsl.ci0.nbx[1].ref<String>();
		StringBuffer<char> sb;

		if(ssh2.Execute(cmd,sb))
		{
			ewsl.ci0.nbx[1].reset(true);
			ewsl.ci0.nbx[2].reset(String(sb));
			return 2;
		}
		else
		{
			ewsl.ci0.nbx[1].reset(false);
			return 1;		
		}
	}
};


class CallableFunctionSsh2Upload : public CallableFunctionSsh2
{
public:
	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.check_pmc(this,pm,2);
		Ssh2Client& ssh2(GetTarget(ewsl));
		String localfile=ewsl.ci0.nbx[1].ref<String>();
		String remotefile=ewsl.ci0.nbx[2].ref<String>();
		SftpSession sftp(ssh2);
		bool flag=sftp.UploadFromFile(localfile,remotefile,FLAG_FILE_WC|FLAG_FILE_TRUNCATE);
		ewsl.ci0.nbx[1].reset(flag);
		return 1;		
	}
};

class CallableFunctionSsh2Download : public CallableFunctionSsh2
{
public:
	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.check_pmc(this,pm,2);
		Ssh2Client& ssh2(GetTarget(ewsl));
		String remotefile=ewsl.ci0.nbx[1].ref<String>();
		String localfile=ewsl.ci0.nbx[2].ref<String>();
		SftpSession sftp(ssh2);
		bool flag=sftp.DownloadToFile(remotefile,localfile,FLAG_FILE_WC|FLAG_FILE_TRUNCATE);
		ewsl.ci0.nbx[1].reset(flag);
		return 1;		
	}
};


class CallableFunctionSsh2Mkdir : public CallableFunctionSsh2
{
public:
	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.check_pmc(this,pm,1);
		Ssh2Client& ssh2(GetTarget(ewsl));

		String remotefile=ewsl.ci0.nbx[1].ref<String>();
		
		SftpSession sftp(ssh2);
		bool flag=sftp.Mkdir(remotefile);
		ewsl.ci0.nbx[1].reset(flag);
		return 1;		
	}
};


class CallableFunctionSsh2Remove : public CallableFunctionSsh2
{
public:
	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.check_pmc(this,pm,1);
		Ssh2Client& ssh2(GetTarget(ewsl));

		String remotefile=ewsl.ci0.nbx[1].ref<String>();
		
		SftpSession sftp(ssh2);
		bool flag=sftp.Remove(remotefile);
		ewsl.ci0.nbx[1].reset(flag);
		return 1;		
	}
};

class CallableFunctionSsh2Rmdir : public CallableFunctionSsh2
{
public:
	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.check_pmc(this,pm,1);
		Ssh2Client& ssh2(GetTarget(ewsl));

		String remotefile=ewsl.ci0.nbx[1].ref<String>();
		
		SftpSession sftp(ssh2);
		bool flag=sftp.Rmdir(remotefile,1);
		ewsl.ci0.nbx[1].reset(flag);
		return 1;		
	}
};

class CallableFunctionSsh2Dir : public CallableFunctionSsh2
{
public:

	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.check_pmc(this,pm,1);
		Ssh2Client& ssh2(GetTarget(ewsl));

		String remotefile=ewsl.ci0.nbx[1].ref<String>();
		
		SftpSession sftp(ssh2);
		arr_1t<FileItem> items=sftp.FindFilesEx(remotefile);
		arr_xt<Variant>  files;
		for(size_t i=0;i<items.size();i++)
		{
			if(items[i].filename.c_str()[0]=='.') continue;
			Variant item;
			VariantTable& tb(item.ref<VariantTable>());
			tb["filename"].reset(items[i].filename);
			tb["filesize"].reset(items[i].filesize);
			tb["flag"].reset(items[i].flags.val());
			files.push_back(item);
		}
		files.reshape(files.size());
		ewsl.ci0.nbx[1].ref<arr_xt<Variant> >().swap(files);

		return 1;		
	}
};


CallableSsh2Client::CallableSsh2Client()
{

}


class CallableFunctionSsh2Create : public CallableFunction
{
public:

	int __fun_call(Executor& ewsl,int pm)
	{
		ewsl.ci0.nbx[1].reset(new CallableSsh2Client);
		return 1;
	}
};


template<>
class CallableMetatableT<Ssh2Client> : public CallableMetatable
{
public:

	CallableMetatableT():CallableMetatable("ssh2client")
	{
		value["create"].reset(new CallableFunctionSsh2Create);

		value["dir"].reset(new CallableFunctionSsh2Dir);
		value["mkdir"].reset(new CallableFunctionSsh2Mkdir);
		value["rmdir"].reset(new CallableFunctionSsh2Rmdir);
		value["remove"].reset(new CallableFunctionSsh2Remove);
		value["upload"].reset(new CallableFunctionSsh2Upload);
		value["download"].reset(new CallableFunctionSsh2Download);
		value["execute"].reset(new CallableFunctionSsh2Execute);
		value["connect"].reset(new CallableFunctionSsh2Connect);
		value["login"].reset(new CallableFunctionSsh2Login);	
	}
	DECLARE_OBJECT_CACHED_INFO(CallableMetatableT<Ssh2Client>,ObjectInfo)
};


IMPLEMENT_OBJECT_INFO(CallableMetatableT<Ssh2Client>,ObjectInfo)

CallableMetatable* CallableSsh2Client::GetMetaTable()
{
	return CallableMetatableT<Ssh2Client>::sm_info.GetCachedInstance();
}



EW_LEAVE

#ifdef EWA_EXTERNAL_DLL
BOOL APIENTRY DllMain(HANDLE hModule,DWORD ul_reason_for_call,LPVOID lpReserved)
{
	EW_UNUSED(hModule);
	EW_UNUSED(ul_reason_for_call);
	EW_UNUSED(lpReserved);

	using namespace ew;	

	if(DLL_PROCESS_ATTACH==ul_reason_for_call)
	{
		CG_GGVar::current().import(CallableMetatableT<Ssh2Client>::sm_info.GetCachedInstance());
	}
	else if(DLL_PROCESS_DETACH==ul_reason_for_call)
	{
		CG_GGVar::current().unload("ssh2client");
	}

	return TRUE;
}
#endif
