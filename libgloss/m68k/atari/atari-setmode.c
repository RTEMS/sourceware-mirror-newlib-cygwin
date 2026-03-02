/*
	Copyright (C) 2026 Mikael Hildenborg
	SPDX-License-Identifier: BSD-2-Clause
*/
#include <unistd.h>
#include <_ansi.h>
#include <fcntl.h>
#include <io.h>
#include <errno.h>

/*
	EmuTOS allows for a maximum of 75 open files, and ordinary TOS even less.
	But MultiTOS and MagiC allows for much more, so to be safe we allow for even more.
*/
#define MAX_FILE_DESCRIPTORS	256
#define FILE_MODE_SHIFT			16

static int _fmode = O_TEXT;

unsigned char file_descriptor_modes[MAX_FILE_DESCRIPTORS] = {0};

int atari_set_fmode(int mode)
{
	_fmode = mode;
	return 0;
}

int atari_get_fmode(int* mode)
{
	*mode = _fmode;
	return 0;
}

int atari_getmode(short fd)
{
	if (fd < 0 || fd >= MAX_FILE_DESCRIPTORS)
	{
		errno = EINVAL;
		return -1;
	}
	int mode = ((int)file_descriptor_modes[fd]) << FILE_MODE_SHIFT;
	if (mode == 0 && fd < 2)
	{
		/*
			stdin, stdout and stderr isn't opened and assumed to exist.
			So the open function never gets the chance to set default mode for them.
			That's why we do this check and return O_TEXT if nothing else is set.
		*/
		mode = O_TEXT;
	}
	return mode;
}

int atari_setmode(short fd, int mode)
{
	int oldMode = atari_getmode(fd);
	if (oldMode != -1)
	{
		// We cannot get here without having enough entries in the array.
		file_descriptor_modes[fd] = (unsigned char)((mode & (O_TEXT | O_BINARY)) >> FILE_MODE_SHIFT);
	}
	return oldMode;
}

int atari_istext_for_stdio (short fd)
{
	int mode = atari_getmode(fd);
	if (mode != -1)
	{
		// Treat anything else than binary mode as text mode.
		return (mode & O_BINARY) == 0 ? 1 : 0;
	}
	return 1;
}
