#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "fileio.h"
#include "miniz.h"

#pragma warning ( disable:4996 )

static long long filesz = 0;
static size_t uncomp_size = 0;

long long getLastFileSize(void)
{
	return filesz;
}

size_t getlastZsize(void)
{
	return uncomp_size;
}

long long getFileSize(FILE *input)
{
	long long fileSizeBytes;

	if (_fseeki64(input, 0, SEEK_END) != 0) return -1;
	fileSizeBytes = _ftelli64(input);
	if (_fseeki64(input, 0, SEEK_SET) != 0) return -1;
	return fileSizeBytes;
}

unsigned char *load_file(const char *filename)
{
	unsigned char *buf;

	FILE *fd = fopen(filename, "rb");
	if (!fd)
	{
		LOG_ERROR("Failed to find file! %s", filename);
		return NULL;
	}

	filesz = getFileSize(fd);
	if (filesz < 0)
	{
		LOG_ERROR("Failed to get size of file %s", filename);
		fclose(fd);
		return NULL;
	}

	if ((unsigned long long)filesz > (unsigned long long)SIZE_MAX)
	{
		LOG_ERROR("File %s is too large to load into memory", filename);
		fclose(fd);
		return NULL;
	}

	buf = malloc((size_t)filesz);
	if (!buf)
	{
		LOG_ERROR("Out of memory loading file %s", filename);
		fclose(fd);
		return NULL;
	}

	if (fread(buf, 1, (size_t)filesz, fd) != (size_t)filesz)
	{
		LOG_ERROR("Failed to read file %s", filename);
		free(buf);
		fclose(fd);
		return NULL;
	}

	fclose(fd);
	return buf;
}

int save_file(const char *filename, const unsigned char *buf, size_t size)
{
	FILE *fd = fopen(filename, "wb");
	if (!fd)
	{
		wrlog("Failed to save file %s.", filename);
		return 0;
	}

	if (fwrite(buf, 1, size, fd) != size)
	{
		wrlog("Failed to write file %s.", filename);
		fclose(fd);
		return 0;
	}

	fclose(fd);
	return 1;
}

// ToDo: Add a debug clause in front of the logging to disable it
unsigned char *loadGenericZip(const char *archname, const char *filename)
{
	mz_bool status;
	int file_index;
	mz_zip_archive zip_archive;
	mz_zip_archive_file_stat file_stat;

	unsigned char *buf = NULL;
	int ret = 1; // Zero means the file didn't load, 1 means everything is hunky dory.

	LOG_DEBUG("Opening Archive %s", archname);
	memset(&zip_archive, 0, sizeof(zip_archive));
	status = mz_zip_reader_init_file(&zip_archive, archname, 0);
	if (!status) { LOG_ERROR("Zip Archive %s not found. (Check your path?)", archname); ret = 0; goto end; }

	// Find the requested file
	file_index = mz_zip_reader_locate_file(&zip_archive, filename, NULL, 0);
	if (file_index < 0) { LOG_ERROR("File %s not found in Zip Archive %s", filename, archname); ret = 0; goto end; }

	// Get information on the file
	status = mz_zip_reader_file_stat(&zip_archive, (mz_uint)file_index, &file_stat);
	if (status != MZ_TRUE) { LOG_ERROR("Error reading Zip File Info, it's probably corrupt"); ret = 0; goto end; }

	// Fill in the size in case we need to get it later
	uncomp_size = (size_t)file_stat.m_uncomp_size;

	buf = (unsigned char *)malloc(uncomp_size);
	if (!buf) { LOG_ERROR("Failed to create char buffer, mem error?"); ret = 0; goto end; }

	// Read (decompress) the file
	status = mz_zip_reader_extract_to_mem(&zip_archive, (mz_uint)file_index, buf, uncomp_size, 0);
	if (status != MZ_TRUE) { LOG_ERROR("Failed to extract %s from %s", filename, archname); ret = 0; goto end; }

end:
	mz_zip_reader_end(&zip_archive);

	if (ret)
	{
		LOG_DEBUG("Zip file %s loaded successfully from archive %s", filename, archname);
		return buf;
	}

	LOG_ERROR("Zip file %s in archive %s failed to load!", filename, archname);
	free(buf);
	return NULL;
}

// ToDo: Add a debug clause in front of the logging to disable it
bool saveGenericZip(const char *archname, const char *filename, const unsigned char *data, size_t size)
{
	mz_bool status;

	status = mz_zip_add_mem_to_archive_file_in_place(archname, filename, data, size, NULL, 0, MZ_BEST_COMPRESSION);
	if (!status)
	{
		wrlog("mz_zip_add_mem_to_archive_file_in_place failed!");
		return false;
	}

	return true;
}
