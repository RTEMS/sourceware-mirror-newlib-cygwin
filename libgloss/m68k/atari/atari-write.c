/*
	Copyright (C) 2025 Mikael Hildenborg
	SPDX-License-Identifier: BSD-2-Clause
*/

#include <unistd.h>
#include <_ansi.h>
#include "atari-gem_errno.h"
#include "atari-traps.h"

_READ_WRITE_RETURN_TYPE write(int fd, const void *buf, size_t nbytes)
{
	int numWritten = GEM_EIHNDL;
	if (fd >= 0)
	{
		if (fd == 2)
		{
			fd = GSH_CONOUT; // Use console out for stderr.
		}
		numWritten = trap1_40((unsigned short)fd, nbytes, buf);
	}
	if (numWritten < 0)
	{
		gem_error_to_errno(numWritten);
		return -1;
	}
	return numWritten;
}
