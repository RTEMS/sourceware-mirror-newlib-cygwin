/*
	Copyright (C) 2026 Mikael Hildenborg
	SPDX-License-Identifier: BSD-2-Clause
*/

#include <_ansi.h>
#include <sys/reent.h>
#include <stdio.h>
#include <sys/fcntl.h>
#include <sys/lock.h>
#include <errno.h>
#include "../../stdio/local.h"


/*
	Thread safety in case support for MultiTOS or MagiC is added in the future.
*/
#ifndef __SINGLE_THREAD__
__LOCK_INIT(static, __setmode_mutex);
#endif

short setmode_fd;
FILE* setmode_fp;

int setmode_helper(struct _reent *ptr, FILE *f)
{
	/*
		We call __sfileno instead of fileno, as the latter tries to lock the filepointer.
		And the filepointer is already locked if we came from fopen etc.
	*/
	if (setmode_fd == __sfileno(f))
	{
		setmode_fp = f;
	}
	return 0;
}

FILE* GetFilePointer(short fd)
{
#ifndef __SINGLE_THREAD__
	__lock_acquire(__setmode_mutex);
#endif
	setmode_fd = fd;
	setmode_fp = 0;
	_fwalk_sglue (_GLOBAL_REENT, setmode_helper, &__sglue);
	FILE* fp = setmode_fp;
#ifndef __SINGLE_THREAD__
	__lock_release(__setmode_mutex);
#endif
	return fp;
}

extern int atari_setmode(short f, int mode);
extern int atari_istext_for_stdio(short fd);

int _setmode (short fd, int mode)
{
	if (fd < 0)
	{
		errno = EBADF;
		return -1;
	}

/*
	We need to make sure that stdio is set up, as this function most likely will be called before any file operations.
	The second parameter in the CHECK_INIT should be a file pointer, but paradoxically enough we cannot have one
	before setting up stdio.
	This is not a problem however as the file pointer never is used by the macro, so we set it to NULL.
*/
	CHECK_INIT(_REENT, NULL);

	FILE* fp = GetFilePointer(fd);
	if (fp == 0)
	{
		errno = EBADF;
		return -1;
	}

	int oldmode = atari_setmode(fd, mode);

	/*
		Set or clear __SCLE for correct CRLF handling.
	*/
	if (atari_istext_for_stdio(fd) != 0)
	{
		fp->_flags |= __SCLE;
	}
	else
	{
		fp->_flags &= ~__SCLE;
	}

	return oldmode;
}
