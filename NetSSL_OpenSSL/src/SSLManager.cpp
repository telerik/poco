//
// SSLManager.cpp
//
// $Id: //poco/1.4/NetSSL_OpenSSL/src/SSLManager.cpp#3 $
//
// Library: NetSSL_OpenSSL
// Package: SSLCore
// Module:  SSLManager
//
// Copyright (c) 2006-2010, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// SPDX-License-Identifier:	BSL-1.0
//


#include "Poco/Net/SSLManager.h"
#include "Poco/Net/Context.h"
#include "Poco/Net/Utility.h"
#include "Poco/Net/PrivateKeyPassphraseHandler.h"
#include "Poco/Crypto/OpenSSLInitializer.h"
#include "Poco/Net/SSLException.h"
#include "Poco/SingletonHolder.h"
#include "Poco/Delegate.h"
#include "Poco/Util/Application.h"
#include "Poco/Util/OptionException.h"


namespace Poco {
namespace Net {


const std::string SSLManager::CFG_PRIV_KEY_FILE("privateKeyFile");
const std::string SSLManager::CFG_CERTIFICATE_FILE("certificateFile");
const std::string SSLManager::CFG_CA_LOCATION("caConfig");
const std::string SSLManager::CFG_VER_MODE("verificationMode");
const Context::VerificationMode SSLManager::VAL_VER_MODE(Context::VERIFY_STRICT);
const std::string SSLManager::CFG_VER_DEPTH("verificationDepth");
const int         SSLManager::VAL_VER_DEPTH(9);
const std::string SSLManager::CFG_ENABLE_DEFAULT_CA("loadDefaultCAFile");
const bool        SSLManager::VAL_ENABLE_DEFAULT_CA(false);
const std::string SSLManager::CFG_CIPHER_LIST("cipherList");
const std::string SSLManager::CFG_CYPHER_LIST("cypherList");
const std::string SSLManager::VAL_CIPHER_LIST("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
const std::string SSLManager::CFG_DELEGATE_HANDLER("privateKeyPassphraseHandler.name");
const std::string SSLManager::VAL_DELEGATE_HANDLER("KeyConsoleHandler");
const std::string SSLManager::CFG_CERTIFICATE_HANDLER("invalidCertificateHandler.name");
const std::string SSLManager::VAL_CERTIFICATE_HANDLER("ConsoleCertificateHandler");
const std::string SSLManager::CFG_SERVER_PREFIX("openSSL.server.");
const std::string SSLManager::CFG_CLIENT_PREFIX("openSSL.client.");
const std::string SSLManager::CFG_CACHE_SESSIONS("cacheSessions");
const std::string SSLManager::CFG_SESSION_ID_CONTEXT("sessionIdContext");
const std::string SSLManager::CFG_SESSION_CACHE_SIZE("sessionCacheSize");
const std::string SSLManager::CFG_SESSION_TIMEOUT("sessionTimeout");
const std::string SSLManager::CFG_EXTENDED_VERIFICATION("extendedVerification");
const std::string SSLManager::CFG_REQUIRE_TLSV1("requireTLSv1");
#ifdef OPENSSL_FIPS
const std::string SSLManager::CFG_FIPS_MODE("openSSL.fips");
const bool        SSLManager::VAL_FIPS_MODE(false);
#endif


SSLManager::SSLManager()
{
}


SSLManager::~SSLManager()
{
	shutdown();
}


void SSLManager::shutdown()
{
	PrivateKeyPassphraseRequired.clear();
	ClientVerificationError.clear();
	ServerVerificationError.clear();
	_ptrDefaultServerContext = 0;
	_ptrDefaultClientContext = 0;
}


namespace
{
	static Poco::SingletonHolder<SSLManager> singleton;
}


SSLManager& SSLManager::instance()
{
	return *singleton.get();
}


void SSLManager::initializeServer(PrivateKeyPassphraseHandlerPtr ptrPassphraseHandler, InvalidCertificateHandlerPtr ptrHandler, Context::Ptr ptrContext)
{
	_ptrServerPassphraseHandler  = ptrPassphraseHandler;
	_ptrServerCertificateHandler = ptrHandler;
	_ptrDefaultServerContext     = ptrContext;
}


void SSLManager::initializeClient(PrivateKeyPassphraseHandlerPtr ptrPassphraseHandler, InvalidCertificateHandlerPtr ptrHandler, Context::Ptr ptrContext)
{
	_ptrClientPassphraseHandler  = ptrPassphraseHandler;
	_ptrClientCertificateHandler = ptrHandler;
	_ptrDefaultClientContext     = ptrContext;
}


Context::Ptr SSLManager::defaultServerContext()
{
	Poco::FastMutex::ScopedLock lock(_mutex);
	
	if (!_ptrDefaultServerContext)
		initDefaultContext(true);

	return _ptrDefaultServerContext;
}


Context::Ptr SSLManager::defaultClientContext()
{
	Poco::FastMutex::ScopedLock lock(_mutex);

	if (!_ptrDefaultClientContext)
		initDefaultContext(false);

	return _ptrDefaultClientContext;
}


SSLManager::PrivateKeyPassphraseHandlerPtr SSLManager::serverPassphraseHandler()
{
	Poco::FastMutex::ScopedLock lock(_mutex);

	if (!_ptrServerPassphraseHandler)
		initPassphraseHandler(true);

	return _ptrServerPassphraseHandler;
}


SSLManager::PrivateKeyPassphraseHandlerPtr SSLManager::clientPassphraseHandler()
{
	Poco::FastMutex::ScopedLock lock(_mutex);

	if (!_ptrClientPassphraseHandler)
		initPassphraseHandler(false);

	return _ptrClientPassphraseHandler;
}


SSLManager::InvalidCertificateHandlerPtr SSLManager::serverCertificateHandler()
{
	Poco::FastMutex::ScopedLock lock(_mutex);

	if (!_ptrServerCertificateHandler)
		initCertificateHandler(true);

	return _ptrServerCertificateHandler;
}


SSLManager::InvalidCertificateHandlerPtr SSLManager::clientCertificateHandler()
{
	Poco::FastMutex::ScopedLock lock(_mutex);

	if (!_ptrClientCertificateHandler)
		initCertificateHandler(false);

	return _ptrClientCertificateHandler;
}


int SSLManager::verifyCallback(bool server, int ok, X509_STORE_CTX* pStore)
{
	if (!ok)
	{
		X509* pCert = X509_STORE_CTX_get_current_cert(pStore);
		X509Certificate x509(pCert, true);
		int depth = X509_STORE_CTX_get_error_depth(pStore);
		int err = X509_STORE_CTX_get_error(pStore);
		std::string error(X509_verify_cert_error_string(err));
		VerificationErrorArgs args(x509, depth, err, error);
		if (server)
			SSLManager::instance().ServerVerificationError.notify(&SSLManager::instance(), args);
		else
			SSLManager::instance().ClientVerificationError.notify(&SSLManager::instance(), args);
		ok = args.getIgnoreError() ? 1 : 0;
	}

	return ok;
}


int SSLManager::privateKeyPassphraseCallback(char* pBuf, int size, int flag, void* userData)
{
	std::string pwd;
	SSLManager::instance().PrivateKeyPassphraseRequired.notify(&SSLManager::instance(), pwd);

	strncpy(pBuf, (char *)(pwd.c_str()), size);
	pBuf[size - 1] = '\0';
	if (size > pwd.length())
		size = (int) pwd.length();

	return size;
}


void SSLManager::initDefaultContext(bool server)
{
	if (server && _ptrDefaultServerContext) return;
	if (!server && _ptrDefaultClientContext) return;

	Poco::Crypto::OpenSSLInitializer openSSLInitializer;
	initEvents(server);
	Poco::Util::AbstractConfiguration& config = appConfig();

#ifdef OPENSSL_FIPS
	bool fipsEnabled = config.getBool(CFG_FIPS_MODE, VAL_FIPS_MODE);
	if (fipsEnabled && !Poco::Crypto::OpenSSLInitializer::isFIPSEnabled())
	{
		Poco::Crypto::OpenSSLInitializer::enableFIPSMode(true);
	}
#endif

	std::string prefix = server ? CFG_SERVER_PREFIX : CFG_CLIENT_PREFIX;

	// mandatory options
	std::string privKeyFile = config.getString(prefix + CFG_PRIV_KEY_FILE, "");
	std::string certFile = config.getString(prefix + CFG_CERTIFICATE_FILE, privKeyFile);	
	std::string caLocation = config.getString(prefix + CFG_CA_LOCATION, "");

	if (server && certFile.empty() && privKeyFile.empty())
		throw SSLException("Configuration error: no certificate file has been specified");

	// optional options for which we have defaults defined
	Context::VerificationMode verMode = VAL_VER_MODE;
	if (config.hasProperty(prefix + CFG_VER_MODE))
	{
		// either: none, relaxed, strict, once
		std::string mode = config.getString(prefix + CFG_VER_MODE);
		verMode = Utility::convertVerificationMode(mode);
	}

	int verDepth = config.getInt(prefix + CFG_VER_DEPTH, VAL_VER_DEPTH);
	bool loadDefCA = config.getBool(prefix + CFG_ENABLE_DEFAULT_CA, VAL_ENABLE_DEFAULT_CA);
	std::string cipherList = config.getString(prefix + CFG_CIPHER_LIST, VAL_CIPHER_LIST);
	cipherList = config.getString(prefix + CFG_CYPHER_LIST, cipherList); // for backwards compatibility
	bool requireTLSv1 = config.getBool(prefix + CFG_REQUIRE_TLSV1, false);
	if (server)
		_ptrDefaultServerContext = new Context(requireTLSv1 ? Context::TLSV1_SERVER_USE : Context::SERVER_USE, privKeyFile, certFile, caLocation, verMode, verDepth, loadDefCA, cipherList);
	else
		_ptrDefaultClientContext = new Context(requireTLSv1 ? Context::TLSV1_CLIENT_USE : Context::CLIENT_USE, privKeyFile, certFile, caLocation, verMode, verDepth, loadDefCA, cipherList);
		
	bool cacheSessions = config.getBool(prefix + CFG_CACHE_SESSIONS, false);
	if (server)
	{
		std::string sessionIdContext = config.getString(prefix + CFG_SESSION_ID_CONTEXT, config.getString("application.name", ""));
		_ptrDefaultServerContext->enableSessionCache(cacheSessions, sessionIdContext);
		if (config.hasProperty(prefix + CFG_SESSION_CACHE_SIZE))
		{
			int cacheSize = config.getInt(prefix + CFG_SESSION_CACHE_SIZE);
			_ptrDefaultServerContext->setSessionCacheSize(cacheSize);
		}
		if (config.hasProperty(prefix + CFG_SESSION_TIMEOUT))
		{
			int timeout = config.getInt(prefix + CFG_SESSION_TIMEOUT);
			_ptrDefaultServerContext->setSessionTimeout(timeout);
		}
	}
	else
	{
		_ptrDefaultClientContext->enableSessionCache(cacheSessions);
	}
	bool extendedVerification = config.getBool(prefix + CFG_EXTENDED_VERIFICATION, false);
	if (server)
		_ptrDefaultServerContext->enableExtendedCertificateVerification(extendedVerification);
	else
		_ptrDefaultClientContext->enableExtendedCertificateVerification(extendedVerification);
}


void SSLManager::initEvents(bool server)
{
	initPassphraseHandler(server);
	initCertificateHandler(server);
}


void SSLManager::initPassphraseHandler(bool server)
{
	if (server && _ptrServerPassphraseHandler) return;
	if (!server && _ptrClientPassphraseHandler) return;
	
	std::string prefix = server ? CFG_SERVER_PREFIX : CFG_CLIENT_PREFIX;
	Poco::Util::AbstractConfiguration& config = appConfig();

	std::string className(config.getString(prefix + CFG_DELEGATE_HANDLER, VAL_DELEGATE_HANDLER));

	const PrivateKeyFactory* pFactory = 0;
	if (privateKeyFactoryMgr().hasFactory(className))
	{
		pFactory = privateKeyFactoryMgr().getFactory(className);
	}

	if (pFactory)
	{
		if (server)
			_ptrServerPassphraseHandler = pFactory->create(server);
		else
			_ptrClientPassphraseHandler = pFactory->create(server);
	}
	else throw Poco::Util::UnknownOptionException(std::string("No passphrase handler known with the name ") + className);
}
	

void SSLManager::initCertificateHandler(bool server)
{
	if (server && _ptrServerCertificateHandler) return;
	if (!server && _ptrClientCertificateHandler) return;

	std::string prefix = server ? CFG_SERVER_PREFIX : CFG_CLIENT_PREFIX;
	Poco::Util::AbstractConfiguration& config = appConfig();

	std::string className(config.getString(prefix+CFG_CERTIFICATE_HANDLER, VAL_CERTIFICATE_HANDLER));

	const CertificateHandlerFactory* pFactory = 0;
	if (certificateHandlerFactoryMgr().hasFactory(className))
	{
		pFactory = certificateHandlerFactoryMgr().getFactory(className);
	}

	if (pFactory)
	{
		if (server)
			_ptrServerCertificateHandler = pFactory->create(true);
		else
			_ptrClientCertificateHandler = pFactory->create(false);
	}
	else throw Poco::Util::UnknownOptionException(std::string("No InvalidCertificate handler known with the name ") + className);
}


Poco::Util::AbstractConfiguration& SSLManager::appConfig()
{
	try
	{
		return Poco::Util::Application::instance().config();
	}
	catch (Poco::NullPointerException&)
	{
		throw Poco::IllegalStateException(
			"An application configuration is required to initialize the Poco::Net::SSLManager, "
			"but no Poco::Util::Application instance is available."
		);
	}
}


void initializeSSL()
{
	Poco::Crypto::initializeCrypto();
}


void uninitializeSSL()
{
	SSLManager::instance().shutdown();
	Poco::Crypto::uninitializeCrypto();
}


} } // namespace Poco::Net
