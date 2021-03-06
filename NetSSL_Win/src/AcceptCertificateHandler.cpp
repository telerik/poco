//
// AcceptCertificateHandler.cpp
//
// $Id$
//
// Library: NetSSL_Win
// Package: SSLCore
// Module:  AcceptCertificateHandler
//
// Copyright (c) 2006-2014, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// SPDX-License-Identifier:	BSL-1.0
//


#include "Poco/Net/AcceptCertificateHandler.h"


namespace Poco {
namespace Net {


AcceptCertificateHandler::AcceptCertificateHandler(bool server): InvalidCertificateHandler(server)
{
}


AcceptCertificateHandler::~AcceptCertificateHandler()
{
}


void AcceptCertificateHandler::onInvalidCertificate(const void*, VerificationErrorArgs& errorCert)
{
	errorCert.setIgnoreError(true);
}


} } // namespace Poco::Net
