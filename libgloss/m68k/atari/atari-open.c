/*
	Copyright (C) 2025 Mikael Hildenborg
	SPDX-License-Identifier: BSD-2-Clause
*/

#include <unistd.h>
#include <_ansi.h>
#include <fcntl.h>
#include <io.h>
#include "atari-gem_errno.h"
#include "atari-traps.h"

extern int atari_get_fmode(int* mode);
extern int atari_setmode(short f, int mode);

int open(const char *buf, int flags, ...)
{
	int bios_handle = -1;
	unsigned short bios_mode = (unsigned short)(flags & 0x3); // bits 0-1 the same for st and linux.
	int create = flags & O_CREAT;
	int append = flags & O_APPEND; // open doc says that seek end should be done before each write call. We assume that newlib handles that.
	int excl = flags & O_EXCL;	   // File must be created by this call.
	int trunc = flags & O_TRUNC;   // File is forced to be created and thus truncated.

	const char *bios_path = buf;
	if (!trunc)
	{
		bios_handle = trap1_3d(bios_path, bios_mode);
	}
	if (bios_handle < 0 && (create || trunc))
	{
		unsigned short bios_attrib = 0;
		bios_handle = trap1_3c(bios_path, bios_attrib);
	}
	else if (create && excl)
	{
		// We explicitly specified that file must be created, and it already existed, so error!
		gem_error_to_errno(GEM_EACCDN);
		// Close file.
		trap1_3e((unsigned short)bios_handle);
		return -1;
	}

	if (bios_handle >= 0 && append)
	{
		// Seek to end.
		int new_file_pos = trap1_42(0, (unsigned short)bios_handle, 2);
		if (new_file_pos < 0)
		{
			gem_error_to_errno(new_file_pos);
			// Close file.
			trap1_3e((unsigned short)bios_handle);
			return -1;
		}
	}

	if (bios_handle < 0)
	{
		gem_error_to_errno(bios_handle);
		bios_handle = -1;
	}
	else
	{
		int fmode = flags & (O_TEXT | O_BINARY);
		if (fmode == 0)
		{
			// Initializes the correct default mode.
			atari_get_fmode(&fmode);
		}
		if (atari_setmode((short)bios_handle, fmode) == -1)
		{
			// Close file. errno is alredy set.
			trap1_3e((unsigned short)bios_handle);
			return -1;
		}
	}
	/*
		If bios_handle is positive, then the low word is the gemdos handle, and the high word is zero.
	*/
	return bios_handle;
}
