//
// NamedEvent_WIN32.cpp
//
// $Id: //poco/1.4/Foundation/src/NamedEvent_WIN32U.cpp#1 $
//
// Library: Foundation
// Package: Processes
// Module:  NamedEvent
//
// Copyright (c) 2004-2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// SPDX-License-Identifier:	BSL-1.0
//


#include "Poco/NamedEvent_WIN32U.h"
#include "Poco/Exception.h"
#include "Poco/UnicodeConverter.h"


namespace Poco {


NamedEventImpl::NamedEventImpl(const std::string& name):
	_name(name)
{
	UnicodeConverter::toUTF16(_name, _uname);
	_event = CreateEventW(NULL, FALSE, FALSE, _uname.c_str());
	if (!_event)
		throw SystemException("cannot create named event", _name);
}


NamedEventImpl::~NamedEventImpl()
{
	try
	{
		CloseHandle(_event);
	}
	catch (...)
	{
	}
}


void NamedEventImpl::setImpl()
{
	if (!SetEvent(_event))
		throw SystemException("cannot signal named event", _name);
}


void NamedEventImpl::waitImpl()
{
	switch (WaitForSingleObject(_event, INFINITE))
	{
	case WAIT_OBJECT_0:
		return;
	default:
		throw SystemException("wait for named event failed", _name);
	}
}


} // namespace Poco
