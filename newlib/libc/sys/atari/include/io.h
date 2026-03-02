/*
	Copyright (C) 2026 Mikael Hildenborg
	SPDX-License-Identifier: BSD-2-Clause
*/

#ifndef _IO_H_
#define _IO_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
	fmode functions is in libgloss atari-setmode.c
*/
extern int atari_set_fmode(int mode);
extern int atari_get_fmode(int* mode);
extern int _setmode(short f, int mode);

#define _set_fmode(mode) atari_set_fmode((mode))
#define _get_fmode(mode) atari_get_fmode((mode))
#define set_fmode(mode) atari_set_fmode((mode))
#define get_fmode(mode) atari_get_fmode((mode))
#define setmode(f, mode) _setmode((f), (mode))

#ifdef __cplusplus
}
#endif

#endif
