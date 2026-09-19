#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ini.h"
#include "log.h"

// Longest value this reads or writes.
#define MAX_INI 255

#pragma warning (disable : 4996 )

// MAX_PATH, not MAX_INI: a 255-byte buffer silently truncated legitimate
// 256-259 character paths, after which every read and write silently targeted
// a different file.
static char m_szFileName[MAX_PATH];


void set_config_file(const char *szFileName)
{
	// GetPrivateProfile* resolves a filename with no path component against
	// the Windows directory rather than the working directory, so a relative
	// name reads C:\Windows\<name> and writes there get blocked by UAC.
	// Expanding to a full path up front makes relative names behave the way
	// callers expect.
	DWORD len = GetFullPathNameA(szFileName, (DWORD)sizeof(m_szFileName), m_szFileName, NULL);

	if (len == 0 || len >= sizeof(m_szFileName))
	{
		LOG_ERROR("set_config_file: could not resolve '%s' to a full path", szFileName);
		m_szFileName[0] = '\0';
		return;
	}

	LOG_INFO("Config file set to %s", m_szFileName);
}


static void write_config_value(const char *szSection, const char *szKey, const char *szValue)
{
	if (m_szFileName[0] == '\0')
	{
		LOG_ERROR("No config file set; cannot write [%s] %s", szSection, szKey);
		return;
	}

	if (!WritePrivateProfileStringA(szSection, szKey, szValue, m_szFileName))
		LOG_ERROR("Failed writing [%s] %s to %s (error %lu)",
			szSection, szKey, m_szFileName, GetLastError());
}


int get_config_int(const char *szSection, const char *szKey, int iDefaultValue)
{
	return GetPrivateProfileIntA(szSection, szKey, iDefaultValue, m_szFileName);
}


float get_config_float(const char *szSection, const char *szKey, float fltDefaultValue)
{
	char szResult[MAX_INI];
	char szDefault[MAX_INI];
	snprintf(szDefault, sizeof(szDefault), "%f", fltDefaultValue);
	GetPrivateProfileStringA(szSection, szKey, szDefault, szResult, MAX_INI, m_szFileName);
	return (float)atof(szResult);
}


bool get_config_bool(const char *szSection, const char *szKey, bool bolDefaultValue)
{
	char szResult[MAX_INI];
	const char *szDefault = bolDefaultValue ? "True" : "False";
	GetPrivateProfileStringA(szSection, szKey, szDefault, szResult, MAX_INI, m_szFileName);
	return (_stricmp(szResult, "True") == 0);
}


char *get_config_string(const char *szSection, const char *szKey, const char *szDefaultValue)
{
	char *szResult = (char *)calloc(MAX_INI, 1);
	if (!szResult)
	{
		LOG_ERROR("Out of memory in get_config_string");
		return NULL;
	}
	GetPrivateProfileStringA(szSection, szKey, szDefaultValue, szResult, MAX_INI, m_szFileName);
	return szResult;
}


void set_config_int(const char *szSection, const char *szKey, int iValue)
{
	char szValue[MAX_INI];
	snprintf(szValue, sizeof(szValue), "%d", iValue);
	write_config_value(szSection, szKey, szValue);
}


void set_config_float(const char *szSection, const char *szKey, float fltValue)
{
	char szValue[MAX_INI];
	snprintf(szValue, sizeof(szValue), "%f", fltValue);
	write_config_value(szSection, szKey, szValue);
}


void set_config_bool(const char *szSection, const char *szKey, bool bolValue)
{
	write_config_value(szSection, szKey, bolValue ? "True" : "False");
}


void set_config_string(const char *szSection, const char *szKey, const char *szValue)
{
	write_config_value(szSection, szKey, szValue);
}
