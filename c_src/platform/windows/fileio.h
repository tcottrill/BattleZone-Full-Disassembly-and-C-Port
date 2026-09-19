#pragma once

#ifndef FILEIO_H
#define FILEIO_H

#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include "log.h"

#ifdef __cplusplus
extern "C" {
#endif

	// 64-bit: long is 32 bits on Windows in both the x64 and Win32 builds, so
	// the old long-based API silently capped at 2 GB.
	long long getLastFileSize(void);
	size_t getlastZsize(void);
	long long getFileSize(FILE *input);

	// Both loaders return a malloc'd buffer the caller must free(), or NULL on failure.
	unsigned char *load_file(const char *filename);
	unsigned char *loadGenericZip(const char *archname, const char *filename);

	int save_file(const char *filename, const unsigned char *buf, size_t size);
	bool saveGenericZip(const char *archname, const char *filename, const unsigned char *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
