/* cygcheck.cc

   This file is part of Cygwin.

   This software is a copyrighted work licensed under the terms of the
   Cygwin license.  Please consult the file "CYGWIN_LICENSE" for
   details. */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <io.h>
#include <windows.h>
#include <wininet.h>
#include <shlwapi.h>
#include "path.h"
#include "wide_path.h"
#include "cygcheck.h"
#include <getopt.h>
#include <cygwin/version.h>
#define cygwin_internal cygwin_internal_dontuse
#include <sys/cygwin.h>
#undef cygwin_internal
#define _NOMNTENT_MACROS
#include <mntent.h>
#include "loadlib.h"

#ifndef PRODUCT_IOTENTERPRISES
#define PRODUCT_IOTENTERPRISES 0x000000bf
#endif

#ifndef max
#define max __max
#endif

#ifndef alloca
#define alloca __builtin_alloca
#endif

extern "C" {
uintptr_t (*cygwin_internal) (cygwin_getinfo_types, ...);
WCHAR cygwin_dll_path[32768];
};

int verbose = 0;
int registry = 0;
int sysinfo = 0;
int givehelp = 0;
int keycheck = 0;
int check_setup = 0;
int dump_only = 0;
int names_only = 0;
int find_package = 0;
int list_package = 0;
int grep_packages = 0;
int info_packages = 0;
int info_selector = 0;
int search_packages = 0;
int search_selector = 0;
int del_orphaned_reg = 0;

#define INFO_INST	0x01
#define INFO_CURR	0x02
#define INFO_PREV	0x04
#define INFO_TEST	0x08
#define INFO_ALL	0x0f
#define INFO_DEPS	0x10
#define INFO_BLDDEPS	0x20

#define SRCH_REQS	0x40
#define SRCH_BLDREQS	0x80

static char emptystr[] = "";

#ifdef __GNUC__
typedef long long longlong;
#else
typedef __int64 longlong;
#endif

/* In dump_setup.cc  */
void dump_setup (int, char **, bool, bool);
void package_find (int, char **);
void package_list (int, char **);
/* In bloda.cc  */
void dump_dodgy_apps (int verbose);

static const char *known_env_vars[] = {
  "c_include_path",
  "compiler_path",
  "cxx_include_path",
  "cygwin",
  "cygwin32",
  "dejagnu",
  "expect",
  "gcc_default_options",
  "gcc_exec_prefix",
  "home",
  "ld_library_path",
  "library_path",
  "login",
  "lpath",
  "make_mode",
  "makeflags",
  "path",
  "pwd",
  "strace",
  "tcl_library",
  "user",
  0
};

struct
{
  const char *name;
  int missing_is_good;
}
static common_apps[] = {
  {"awk", 0},
  {"bash", 0},
  {"cat", 0},
  {"certutil", 0},
  {"clinfo", 0},
  {"comp", 0},
  {"convert", 0},
  {"cp", 0},
  {"cpp", 1},
  {"crontab", 0},
  {"curl", 0},
  {"expand", 0},
  {"find", 0},
  {"ftp", 0},
  {"gcc", 0},
  {"gdb", 0},
  {"grep", 0},
  {"hostname", 0},
  {"kill", 0},
  {"klist", 0},
  {"ld", 0},
  {"ls", 0},
  {"make", 0},
  {"mv", 0},
  {"nslookup", 0},
  {"patch", 0},
  {"perl", 0},
  {"replace", 0},
  {"rm", 0},
  {"sed", 0},
  {"sh", 0},
  {"shutdown", 0},
  {"sort", 0},
  {"ssh", 0},
  {"tar", 0},
  {"test", 0},
  {"timeout", 0},
  {"vi", 0},
  {"vim", 0},
  {"whoami", 0},
  {0, 0}
};

/* Options without ASCII single char representation. */
enum
{
  CO_DELETE_KEYS = 0x100,
};

static int num_paths, max_paths;
struct pathlike
{
  char *dir;
  bool issys;
  void check_existence (const char *fn, int showall, int verbose,
			char* first, const char *ext1 = "",
			const char *ext2 = "");
};

pathlike *paths;
int first_nonsys_path;

void
eprintf (const char *format, ...)
{
  va_list ap;
  va_start (ap, format);
  vfprintf (stderr, format, ap);
  va_end (ap);
}

/*
 * display_error() is used to report failure modes
 */
static int
display_error (const char *name, bool show_error, bool print_failed)
{
  fprintf (stderr, "cygcheck: %s", name);
  if (show_error)
    fprintf (stderr, "%s: %lu\n",
	print_failed ? " failed" : "", GetLastError ());
  else
    fprintf (stderr, "%s\n",
	print_failed ? " failed" : "");
  return 1;
}

static int
display_error (const char *name)
{
  return display_error (name, true, true);
}

static int
display_error (const char *fmt, const char *x)
{
  char buf[4000];
  snprintf (buf, sizeof buf, fmt, x);
  return display_error (buf, false, false);
}

static int
display_error_fmt (const char *fmt, ...)
{
  char buf[4000];
  va_list va;

  va_start (va, fmt);
  vsnprintf (buf, sizeof buf, fmt, va);
  return display_error (buf, false, false);
}

/* Display a WinInet error message, and close a variable number of handles.
   (Passed a list of handles terminated by NULL.)  */
static int
display_internet_error (const char *message, ...)
{
  DWORD err = GetLastError ();
  TCHAR err_buf[256];
  va_list hptr;
  HINTERNET h;

  /* in the case of a successful connection but 404 response, there is no
     win32 error message, but we still get passed a message to display.  */
  if (err)
    {
      if (FormatMessage (FORMAT_MESSAGE_FROM_HMODULE,
	  GetModuleHandle ("wininet.dll"), err, 0, err_buf,
	  sizeof (err_buf), NULL) == 0)
	strcpy (err_buf, "(Unknown error)");

      fprintf (stderr, "cygcheck: %s: %s (win32 error %lu)\n", message,
	       err_buf, err);
    }
  else
    fprintf (stderr, "cygcheck: %s\n", message);

  va_start (hptr, message);
  while ((h = va_arg (hptr, HINTERNET)) != 0)
    InternetCloseHandle (h);
  va_end (hptr);

  return 1;
}

static inline char *
stpcpy (char *d, const char *s)
{
  while ((*d++ = *s++))
    ;
  return --d;
}

static void
add_path (char *s, int maxlen, bool issys)
{
  if (num_paths >= max_paths)
    {
      max_paths += 10;
      /* Extend path array */
      paths = (pathlike *) realloc (paths, (1 + max_paths) * sizeof (paths[0]));
    }

  pathlike *pth = paths + num_paths;

  /* Allocate space for directory in path list */
  char *dir = (char *) calloc (maxlen + 2, sizeof (char));
  if (dir == NULL)
    {
      display_error ("add_path: calloc() failed");
      return;
    }

  /* Copy input directory to path list */
  memcpy (dir, s, maxlen);

  /* Add a trailing slash by default */
  char *e = strchr (dir, '\0');
  if (e != dir && e[-1] != '\\')
    strcpy (e, "\\");

  /* Fill out this element */
  pth->dir = dir;
  pth->issys = issys;
  pth[1].dir = NULL;
  num_paths++;
}

static void
init_paths ()
{
  char tmp[4000], *sl;
  add_path ((char *) ".", 1, true);	/* to be replaced later */

  if (GetCurrentDirectory (4000, tmp))
    add_path (tmp, strlen (tmp), true);
  else
    display_error ("init_paths: GetCurrentDirectory()");

  if (GetSystemDirectory (tmp, 4000))
    add_path (tmp, strlen (tmp), true);
  else
    display_error ("init_paths: GetSystemDirectory()");
  sl = strrchr (tmp, '\\');
  if (sl)
    {
      strcpy (sl, "\\SYSTEM");
      add_path (tmp, strlen (tmp), true);
    }
  GetWindowsDirectory (tmp, 4000);
  add_path (tmp, strlen (tmp), true);

  char *wpath = getenv ("PATH");
  if (!wpath)
    display_error ("WARNING: PATH is not set\n", "");
  else
    {
      char *b, *e;
      b = wpath;
      while (1)
	{
	  for (e = b; *e && *e != ';'; e++)
	    continue;	/* loop terminates at first ';' or EOS */
	  if (strncmp(b, ".\\", 2) != 0)
	    add_path (b, e - b, false);
	  if (!*e)
	    break;
	  b = e + 1;
	}
    }
}

#define LINK_EXTENSION ".lnk"

void
pathlike::check_existence (const char *fn, int showall, int verbose,
			   char* first, const char *ext1, const char *ext2)
{
  char file[4000];
  snprintf (file, sizeof file, "%s%s%s%s", dir, fn, ext1, ext2);

  wide_path wpath (file);
  if (GetFileAttributesW (wpath) != (DWORD) - 1)
    {
      char *lastdot = strrchr (file, '.');
      bool is_link = lastdot && !strcmp (lastdot, LINK_EXTENSION);
      // If file is a link, fix up the extension before printing
      if (is_link)
	*lastdot = '\0';
      if (showall)
	printf ("Found: %s\n", file);
      if (verbose && *first != '\0' && strcasecmp (first, file) != 0)
	{
	  char *flastdot = strrchr (first, '.');
	  bool f_is_link = flastdot && !strcmp (flastdot, LINK_EXTENSION);
	  // if first is a link, fix up the extension before printing
	  if (f_is_link)
	    *flastdot = '\0';
	  printf ("Warning: %s hides %s\n", first, file);
	  if (f_is_link)
	    *flastdot = '.';
	}
      if (is_link)
	*lastdot = '.';
      if (!*first)
	strcpy (first, file);
    }
}

static const char *
find_on_path (const char *in_file, const char *ext, bool showall = false,
	      bool search_sys = false, bool checklinks = false)
{
  static char rv[4000];

  /* Sort of a kludge but we've already tested this once, so don't try it
     again */
  if (in_file == rv)
    return in_file;

  static pathlike abspath[2] =
  {
    {emptystr, 0},
    {NULL, 0}
  };

  *rv = '\0';
  if (!in_file)
    {
      display_error ("internal error find_on_path: NULL pointer for file",
		     false, false);
      return 0;
    }

  if (!ext)
    {
      display_error ("internal error find_on_path: "
		     "NULL pointer for default_extension", false, false);
      return 0;
    }

  const char *file;
  pathlike *search_paths;
  if (!strpbrk (in_file, ":/\\"))
    {
      file = in_file;
      search_paths = paths;
    }
  else
    {
      file = cygpath (in_file, NULL);
      search_paths = abspath;
      showall = false;
    }

  if (!file)
    {
      display_error ("internal error find_on_path: "
		     "cygpath conversion failed for %s\n", in_file);
      return 0;
    }

  char *hasext = strrchr (file, '.');
  if (hasext && !strpbrk (hasext, "/\\"))
    ext = "";

  for (pathlike *pth = search_paths; pth->dir; pth++)
    if (!pth->issys || search_sys)
      {
	pth->check_existence (file, showall, verbose, rv, ext);

	if (checklinks)
	  pth->check_existence (file, showall, verbose, rv, ext,
				LINK_EXTENSION);

	if (!*ext)
	  continue;

	pth->check_existence (file, showall, verbose, rv);
	if (checklinks)
	  pth->check_existence (file, showall, verbose, rv, LINK_EXTENSION);
      }

  return *rv ? rv : NULL;
}

#define DID_NEW		1
#define DID_ACTIVE	2
#define DID_INACTIVE	3

struct Did
{
  Did *next;
  char *file;
  int state;
};
static Did *did = 0;

static Did *
already_did (const char *file)
{
  Did *d;
  for (d = did; d; d = d->next)
    if (strcasecmp (d->file, file) == 0)
      return d;
  d = (Did *) malloc (sizeof (Did));
  d->file = strdup (file);
  d->next = did;
  d->state = DID_NEW;
  did = d;
  return d;
}

struct Section
{
  char name[8];
  int virtual_size;
  int virtual_address;
  int size_of_raw_data;
  int pointer_to_raw_data;
};

static int
rva_to_offset (int rva, char *sections, int nsections, int *sz)
{
  int i;

  if (sections == NULL)
    {
      display_error ("rva_to_offset: NULL passed for sections", true, false);
      return 0;
    }

  for (i = 0; i < nsections; i++)
    {
      Section *s = (Section *) (sections + i * 40);
#if 0
      printf ("%08x < %08x < %08x ? %08x\n",
	      s->virtual_address, rva,
	      s->virtual_address + s->virtual_size, s->pointer_to_raw_data);
#endif
      if (rva >= s->virtual_address
	  && rva < s->virtual_address + s->virtual_size)
	{
	  if (sz)
	    *sz = s->virtual_address + s->virtual_size - rva;
	  return rva - s->virtual_address + s->pointer_to_raw_data;
	}
    }
  return 0;			/* punt */
}

struct ExpDirectory
{
  int flags;
  int timestamp;
  short major_ver;
  short minor_ver;
  int name_rva;
};

struct ImpDirectory
{
  unsigned characteristics;
  unsigned timestamp;
  unsigned forwarder_chain;
  unsigned name_rva;
  unsigned iat_rva;
};

static bool track_down (const char *file, const char *suffix, int lvl);

#define CYGPREFIX (sizeof ("%%% Cygwin ") - 1)
static void
cygwin_info (HANDLE h)
{
  char *buf, *bufend, *buf_start = NULL;
  const char *hello = "    Cygwin DLL version info:\n";
  DWORD size = GetFileSize (h, NULL);
  DWORD n;

  if (size == 0xffffffff)
    return;

  buf_start = buf = (char *) calloc (1, size + 1);
  if (buf == NULL)
    {
      display_error ("cygwin_info: calloc()");
      return;
    }

  (void) SetFilePointer (h, 0, NULL, FILE_BEGIN);
  if (!ReadFile (h, buf, size, &n, NULL))
    {
      free (buf_start);
      return;
    }

  static char dummy[] = "\0\0\0\0\0\0\0";
  char *dll_major = dummy;
  bufend = buf + size;
  while (buf < bufend)
    if ((buf = (char *) memchr (buf, '%', bufend - buf)) == NULL)
      break;
    else if (strncmp ("%%% Cygwin ", buf, CYGPREFIX) != 0)
      buf++;
    else
      {
	char *p = strchr (buf += CYGPREFIX, '\n');
	if (!p)
	  break;
	if (strncasecmp (buf, "dll major:", 10) == 0)
	  {
	    dll_major = buf + 11;
	    continue;
	  }
	char *s, pbuf[80];
	int len;
	len = 1 + p - buf;
	if (strncasecmp (buf, "dll minor:", 10) != 0)
	  s = buf;
	else
	  {
	    char c = dll_major[1];
	    dll_major[1] = '\0';
	    int maj = atoi (dll_major);
	    dll_major[1] = c;
	    int min = atoi (dll_major + 1);
	    sprintf (pbuf, "DLL version: %d.%d.%.*s", maj, min, len - 11,
		     buf + 11);
	    len = strlen (s = pbuf);
	  }
	if (strncmp (s, "dll", 3) == 0)
	  memcpy (s, "DLL", 3);
	else if (strncmp (s, "api", 3) == 0)
	  memcpy (s, "API", 3);
	else if (islower (*s))
	  *s = toupper (*s);
	fprintf (stdout, "%s        %.*s", hello, len, s);
	hello = "";
      }

  if (!*hello)
    puts ("");

  free (buf_start);
  return;
}

static void
dll_info (const char *path, HANDLE fh, int lvl, int recurse)
{
  DWORD junk;
  int i;
  if (is_symlink (fh))
    {
      if (!verbose)
	puts ("");
      else
	{
	  char buf[PATH_MAX + 1] = "";
	  readlink (fh, buf, sizeof(buf) - 1);
	  printf (" (symlink to %s)\n", buf);
	}
      return;
    }
  int pe_header_offset = get_dword (fh, 0x3c);
  if (GetLastError () != NO_ERROR)
    display_error ("get_dword");
  WORD arch = get_word (fh, pe_header_offset + 4);
  if (GetLastError () != NO_ERROR)
    display_error ("get_word");
#if defined(__x86_64__)
  if (arch != IMAGE_FILE_MACHINE_AMD64)
    {
      puts (verbose ? " (not x86_64 dll)" : "\n");
      return;
    }
#elif defined (__aarch64__)
  if (arch != IMAGE_FILE_MACHINE_ARM64)
    {
      puts (verbose ? " (not aarch64 dll)" : "\n");
      return;
    }
#else
#error unimplemented for this target
#endif
  int base_off = 108;
  int opthdr_ofs = pe_header_offset + 4 + 20;
  unsigned short v[6];

  if (path == NULL)
    {
      display_error ("dll_info: NULL passed for path", true, false);
      return;
    }

  if (SetFilePointer (fh, opthdr_ofs + 40, 0, FILE_BEGIN) ==
      INVALID_SET_FILE_POINTER && GetLastError () != NO_ERROR)
    display_error ("dll_info: SetFilePointer()");

  if (!ReadFile (fh, &v, sizeof (v), &junk, 0))
    display_error ("dll_info: Readfile()");

  if (verbose)
    printf (" - os=%d.%d img=%d.%d sys=%d.%d\n",
	    v[0], v[1], v[2], v[3], v[4], v[5]);
  else
    printf ("\n");

  int num_entries = get_dword (fh, opthdr_ofs + base_off + 0);
  if (GetLastError () != NO_ERROR)
    display_error ("get_dword");
  int export_rva = get_dword (fh, opthdr_ofs + base_off + 4);
  if (GetLastError () != NO_ERROR)
    display_error ("get_dword");
  int export_size = get_dword (fh, opthdr_ofs + base_off + 8);
  if (GetLastError () != NO_ERROR)
    display_error ("get_dword");
  int import_rva = get_dword (fh, opthdr_ofs + base_off + 12);
  if (GetLastError () != NO_ERROR)
    display_error ("get_dword");
  int import_size = get_dword (fh, opthdr_ofs + base_off + 16);
  if (GetLastError () != NO_ERROR)
    display_error ("get_dword");

  int nsections = get_word (fh, pe_header_offset + 4 + 2);
  if (nsections == -1)
    display_error ("get_word");
  char *sections = (char *) malloc (nsections * 40);

  if (SetFilePointer (fh, pe_header_offset + 4 + 20 +
		      get_word (fh, pe_header_offset + 4 + 16), 0,
		      FILE_BEGIN) == INVALID_SET_FILE_POINTER
      && GetLastError () != NO_ERROR)
    display_error ("dll_info: SetFilePointer()");

  if (!ReadFile (fh, sections, nsections * 40, &junk, 0))
    display_error ("dll_info: Readfile()");

  if (verbose && num_entries >= 1 && export_size > 0)
    {
      int expsz;
      int expbase = rva_to_offset (export_rva, sections, nsections, &expsz);

      if (expbase)
	{
	  if (SetFilePointer (fh, expbase, 0, FILE_BEGIN) ==
	      INVALID_SET_FILE_POINTER && GetLastError () != NO_ERROR)
	    display_error ("dll_info: SetFilePointer()");

	  unsigned char *exp = (unsigned char *) malloc (expsz);

	  if (!ReadFile (fh, exp, expsz, &junk, 0))
	    display_error ("dll_info: Readfile()");

	  ExpDirectory *ed = (ExpDirectory *) exp;
	  int ofs = ed->name_rva - export_rva;
	  time_t ts = ed->timestamp;	/* timestamp is only 4 bytes! */
	  struct tm *tm = localtime (&ts);
	  if (tm && tm->tm_year < 60)
	    tm->tm_year += 2000;
	  if (tm && tm->tm_year < 200)
	    tm->tm_year += 1900;
	  printf ("%*c", lvl + 2, ' ');
	  printf ("\"%s\" v%d.%d", exp + ofs,
		  ed->major_ver, ed->minor_ver);
	  if (tm)
	    printf (" ts=%04d-%02d-%02d %02d:%02d",
		    tm->tm_year, tm->tm_mon + 1, tm->tm_mday,
		    tm->tm_hour, tm->tm_min);
	  putchar ('\n');
	}
    }

  if (num_entries >= 2 && import_size > 0 && recurse)
    {
      int impsz;
      int impbase = rva_to_offset (import_rva, sections, nsections, &impsz);
      if (impbase)
	{
	  if (SetFilePointer (fh, impbase, 0, FILE_BEGIN) ==
	      INVALID_SET_FILE_POINTER && GetLastError () != NO_ERROR)
	    display_error ("dll_info: SetFilePointer()");

	  unsigned char *imp = (unsigned char *) malloc (impsz);
	  if (imp == NULL)
	    {
	      display_error ("dll_info: malloc()");
	      return;
	    }

	  if (!ReadFile (fh, imp, impsz, &junk, 0))
	    display_error ("dll_info: Readfile()");

	  ImpDirectory *id = (ImpDirectory *) imp;
	  for (i = 0; id[i].name_rva; i++)
	    {
	      /* int ofs = id[i].name_rva - import_rva; */
	      track_down ((char *) imp + id[i].name_rva - import_rva,
			  (char *) ".dll", lvl + 2);
	    }
	}
    }
  if (strstr (path, "\\cygwin1.dll"))
    cygwin_info (fh);
}

// Return true on success, false if error printed
static bool
track_down (const char *file, const char *suffix, int lvl)
{
  if (file == NULL)
    {
      display_error ("track_down: NULL passed for file", true, false);
      return false;
    }

  if (suffix == NULL)
    {
      display_error ("track_down: NULL passed for suffix", false, false);
      return false;
    }

  const char *path = find_on_path (file, suffix, false, true);
  if (!path)
    {
      /* The api-ms-win-*.dll files are in system32/downlevel and not in the
	 DLL search path, so find_on_path doesn't find them.  Since they are
	 never actually linked against by the executables, they are of no
	 interest to us.  Skip any error message in not finding them. */
      if (strncasecmp (file, "api-ms-win-", 11) || strcasecmp (suffix, ".dll"))
	display_error ("track_down: could not find %s\n", file);
      return false;
    }

  Did *d = already_did (file);
  switch (d->state)
    {
    case DID_NEW:
      break;
    case DID_ACTIVE:
      if (verbose)
	{
	  if (lvl)
	    printf ("%*c", lvl, ' ');
	  printf ("%s", path);
	  printf (" (recursive)\n");
	}
      return true;
    case DID_INACTIVE:
      if (verbose)
	{
	  if (lvl)
	    printf ("%*c", lvl, ' ');
	  printf ("%s", path);
	  printf (" (already done)\n");
	}
      return true;
    default:
      break;
    }

  if (lvl)
    printf ("%*c", lvl, ' ');

  printf ("%s", path);

  wide_path wpath (path);
  HANDLE fh =
    CreateFileW (wpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		 NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (fh == INVALID_HANDLE_VALUE)
    {
      display_error ("cannot open - '%s'\n", path);
      return false;
    }

  d->state = DID_ACTIVE;

  if (is_exe (fh))
    dll_info (path, fh, lvl, 1);
  else if (is_symlink (fh))
    display_error ("%s is a symlink instead of a DLL\n", path);
  else
    {
      int magic = get_word (fh, 0x0);
      if (magic == -1)
	display_error ("get_word");
      magic &= 0x00FFFFFF;
      display_error_fmt ("%s is not a DLL: magic number %x (%d) '%s'\n",
			 path, magic, magic, (char *)&magic);
    }

  d->state = DID_INACTIVE;
  if (!CloseHandle (fh))
    display_error ("track_down: CloseHandle()");
  return true;
}

static void
ls (char *f)
{
  wide_path wpath (f);
  HANDLE h = CreateFileW (wpath, GENERIC_READ,
			  FILE_SHARE_READ | FILE_SHARE_WRITE,
			  0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
  BY_HANDLE_FILE_INFORMATION info;

  if (!GetFileInformationByHandle (h, &info))
    display_error ("ls: GetFileInformationByHandle()");

  SYSTEMTIME systime;

  if (!FileTimeToSystemTime (&info.ftLastWriteTime, &systime))
    display_error ("ls: FileTimeToSystemTime()");
  printf ("%5dk %04d/%02d/%02d %s",
	  (((int) info.nFileSizeLow) + 512) / 1024,
	  systime.wYear, systime.wMonth, systime.wDay, f);
  dll_info (f, h, 16, 0);
  if (!CloseHandle (h))
    display_error ("ls: CloseHandle()");
}

/* Remove filename from 's' and return directory name without trailing
   backslash, or NULL if 's' doesn't seem to have a dirname.  */
static char *
dirname (const char *s)
{
  static char buf[PATH_MAX];

  if (!s)
    return NULL;

  strncpy (buf, s, PATH_MAX);
  buf[PATH_MAX - 1] = '\0';   // in case strlen(s) > PATH_MAX
  char *lastsep = strrchr (buf, '\\');
  if (!lastsep)
    return NULL;          // no backslash -> no dirname
  else if (lastsep - buf <= 2 && buf[1] == ':')
    lastsep[1] = '\0';    // can't remove backslash of "x:\"
  else
    *lastsep = '\0';
  return buf;
}

// Find a real application on the path (possibly following symlinks)
static const char *
find_app_on_path (const char *app, bool showall = false)
{
  const char *papp = find_on_path (app, ".exe", showall, false, true);

  if (!papp)
    return NULL;

  wide_path wpath (papp);
  HANDLE fh =
    CreateFileW (wpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		 NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (fh == INVALID_HANDLE_VALUE)
    return NULL;

  if (is_symlink (fh))
    {
      static char tmp[SYMLINK_MAX + 1];
      if (!readlink (fh, tmp, SYMLINK_MAX))
	display_error("readlink failed");

      /* Resolve the linkname relative to the directory of the link.  */
      char *ptr = cygpath_rel (dirname (papp), tmp, NULL);
      printf (" -> %s\n", ptr);
      if (!strchr (ptr, '\\'))
	{
	  char *lastsep;
	  strncpy (tmp, cygpath (papp, NULL), SYMLINK_MAX);
	  lastsep = strrchr (tmp, '\\');
	  strncpy (lastsep+1, ptr, SYMLINK_MAX - (lastsep-tmp));
	  ptr = tmp;
	}
      if (!CloseHandle (fh))
	display_error ("find_app_on_path: CloseHandle()");
      /* FIXME: We leak the ptr returned by cygpath() here which is a
	 malloc()d string.  */
      return find_app_on_path (ptr, showall);
    }

  if (!CloseHandle (fh))
    display_error ("find_app_on_path: CloseHandle()");
  return papp;
}

// Return true on success, false if error printed
static bool
cygcheck (const char *app)
{
  const char *papp = find_app_on_path (app, 1);
  if (!papp)
    {
      display_error ("could not find '%s'\n", app);
      return false;
    }

  char *s;
  char *sep = strpbrk (papp, ":/\\");
  if (!sep)
    {
      static char dot[] = ".";
      s = dot;
    }
  else
    {
      int n = sep - papp;
      s = (char *) malloc (n + 2);
      memcpy ((char *) s, papp, n);
      strcpy (s + n, "\\");
    }

  paths[0].dir = s;
  did = NULL;
  return track_down (papp, ".exe", 0);
}

struct RegInfo
{
  RegInfo *prev;
  char *name;
  HKEY key;
};

static void
show_reg (RegInfo * ri, int nest)
{
  if (!ri)
    return;
  show_reg (ri->prev, 1);
  if (nest)
    printf ("%s\\", ri->name);
  else
    printf ("%s\n", ri->name);
}

static void
scan_registry (RegInfo * prev, HKEY hKey, char *name, int cygwin, bool wow64)
{
  RegInfo ri;
  ri.prev = prev;
  ri.name = name;
  ri.key = hKey;

  char *cp;
  for (cp = name; *cp; cp++)
    if (strncasecmp (cp, "Cygwin", 6) == 0)
      cygwin = 1;

  DWORD num_subkeys, max_subkey_len, num_values;
  DWORD max_value_len, max_valdata_len, i;
  if (RegQueryInfoKey (hKey, 0, 0, 0, &num_subkeys, &max_subkey_len, 0,
		       &num_values, &max_value_len, &max_valdata_len, 0, 0)
      != ERROR_SUCCESS)
    {
#if 0
      char tmp[400];
      FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM, 0, GetLastError (),
		     MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT), tmp, 400, 0);
      printf ("RegQueryInfoKey: %s\n", tmp);
#endif
      return;
    }

  if (cygwin)
    {
      show_reg (&ri, 0);

      char *value_name = (char *) malloc (max_value_len + 1);
      if (value_name == NULL)
	{
	  display_error ("scan_registry: malloc()");
	  return;
	}

      char *value_data = (char *) malloc (max_valdata_len + 1);
      if (value_data == NULL)
	{
	  display_error ("scan_registry: malloc()");
	  return;
	}

      for (i = 0; i < num_values; i++)
	{
	  DWORD dlen = max_valdata_len + 1;
	  DWORD nlen = max_value_len + 1;
	  DWORD type;
	  RegEnumValue (hKey, i, value_name, &nlen, 0,
			&type, (BYTE *) value_data, &dlen);
	  {
	    printf ("  %s = ", i ? value_name : "(default)");
	    switch (type)
	      {
	      case REG_DWORD:
		printf ("0x%08x\n", *(unsigned *) value_data);
		break;
	      case REG_EXPAND_SZ:
	      case REG_SZ:
		printf ("'%s'\n", value_data);
		break;
	      default:
		printf ("(unsupported type)\n");
		break;
	      }
	  }
	}
      free (value_name);
      free (value_data);
    }

  char *subkey_name = (char *) malloc (max_subkey_len + 1);
  for (i = 0; i < num_subkeys; i++)
    {
      if (RegEnumKey (hKey, i, subkey_name, max_subkey_len + 1) ==
	  ERROR_SUCCESS)
	{
	  HKEY sKey;
	  /* Don't recurse more than one level into the WOW64 subkey since
	     that would lead to an endless recursion. */
	  if (!strcasecmp (subkey_name, "Wow6432Node"))
	    {
	      if (wow64)
		continue;
	      wow64 = true;
	    }
	  if (RegOpenKeyEx (hKey, subkey_name, 0, KEY_READ, &sKey)
	      == ERROR_SUCCESS)
	    {
	      scan_registry (&ri, sKey, subkey_name, cygwin, wow64);
	      if (RegCloseKey (sKey) != ERROR_SUCCESS)
		display_error ("scan_registry: RegCloseKey()");
	    }
	}
    }
  free (subkey_name);
}

void
pretty_id ()
{
  char *groups[16384];

  char *id = cygpath ("/bin/id.exe", NULL);
  for (char *p = id; (p = strchr (p, '/')); p++)
    *p = '\\';

  if (access (id, X_OK))
    {
      fprintf (stderr, "'id' program not found\n");
      return;
    }

  char buf[16384];
  snprintf (buf, sizeof (buf), "\"%s\"", id);
  FILE *f = popen (buf, "rt");

  buf[0] = '\0';
  fgets (buf, sizeof (buf), f);
  pclose (f);
  char *uid = strtok (buf, ")");
  if (uid)
    uid += strlen ("uid=");
  else
    {
      fprintf (stderr, "garbled output from 'id' command - no uid= found\n");
      return;
    }
  char *gid = strtok (NULL, ")");
  if (gid)
    gid += strlen ("gid=") + 1;
  else
    {
      fprintf (stderr, "garbled output from 'id' command - no gid= found\n");
      return;
    }

  char **ng = groups - 1;
  size_t len_uid = strlen ("UID: )") + strlen (uid);
  size_t len_gid = strlen ("GID: )") + strlen (gid);
  *++ng = groups[0] = (char *) alloca (len_uid + 1);
  *++ng = groups[1] = (char *) alloca (len_gid + 1);
  sprintf (groups[0], "UID: %s)", uid);
  sprintf (groups[1], "GID: %s)", gid);
  size_t sz = max (len_uid, len_gid);
  while ((*++ng = strtok (NULL, ",")))
    {
      char *p = strchr (*ng, '\n');
      if (p)
	*p = '\0';
      if (ng == groups + 2)
	*ng += strlen (" groups=");
      size_t len = strlen (*ng);
      if (sz < len)
	sz = len;
    }
  ng--;

  printf ("\nOutput from %s\n", id);
  int n = 80 / (int) ++sz;
  int i = n > 2 ? n - 2 : 0;
  sz = -sz;
  for (char **g = groups; g <= ng; g++)
    if ((g != ng) && (++i < n))
      printf ("%*s", (int) sz, *g);
    else
      {
	puts (*g);
	i = 0;
      }
}

/* This dumps information about each installed cygwin service, if cygrunsrv
   is available.  */
void
dump_sysinfo_services ()
{
  char buf[1024];
  char buf2[1024];
  FILE *f;
  bool no_services = false;

  if (givehelp)
    printf ("\nChecking for any Cygwin services... %s\n\n",
		  verbose ? "" : "(use -v for more detail)");
  else
    fputc ('\n', stdout);

  /* find the location of cygrunsrv.exe */
  char *cygrunsrv = cygpath ("/bin/cygrunsrv.exe", NULL);
  for (char *p = cygrunsrv; (p = strchr (p, '/')); p++)
    *p = '\\';

  if (access (cygrunsrv, X_OK))
    {
      puts ("Can't find the cygrunsrv utility, skipping services check.\n");
      return;
    }

  /* check for a recent cygrunsrv */
  snprintf (buf, sizeof (buf), "\"%s\" --version", cygrunsrv);
  if ((f = popen (buf, "rt")) == NULL)
    {
      printf ("Failed to execute '%s', skipping services check.\n", buf);
      return;
    }
  int maj, min;
  int ret = fscanf (f, "cygrunsrv V%u.%u", &maj, &min);
  if (ferror (f) || feof (f) || ret == EOF || maj < 1 || min < 10)
    {
      puts ("The version of cygrunsrv installed is too old to dump "
	    "service info.\n");
      return;
    }
  pclose (f);

  /* For verbose mode, just run cygrunsrv --list --verbose and copy output
     verbatim; otherwise run cygrunsrv --list and then cygrunsrv --query for
     each service.  */
  snprintf (buf, sizeof (buf),
	    (verbose ? "\"%s\" --list --verbose" : "\"%s\" --list"),
	    cygrunsrv);
  if ((f = popen (buf, "rt")) == NULL)
    {
      printf ("Failed to execute '%s', skipping services check.\n", buf);
      return;
    }

  if (verbose)
    {
      /* copy output to stdout */
      size_t nchars = 0;
      while (!feof (f) && !ferror (f))
	  nchars += fwrite ((void *) buf, 1,
			    fread ((void *) buf, 1, sizeof (buf), f), stdout);

      /* cygrunsrv outputs nothing if there are no cygwin services found */
      if (nchars < 1)
	no_services = true;
      pclose (f);
    }
  else
    {
      /* read the output of --list, and then run --query for each service */
      size_t nchars = fread ((void *) buf, 1, sizeof (buf) - 1, f);
      buf[nchars] = 0;
      pclose (f);

      if (nchars > 0)
	for (char *srv = strtok (buf, "\n"); srv; srv = strtok (NULL, "\n"))
	  {
	    snprintf (buf2, sizeof (buf2), "\"%s\" --query %s", cygrunsrv, srv);
	    if ((f = popen (buf2, "rt")) == NULL)
	      {
		printf ("Failed to execute '%s', skipping services check.\n",
			buf2);
		return;
	      }

	    /* copy output to stdout */
	    while (!feof (f) && !ferror (f))
	      fwrite ((void *) buf2, 1,
		      fread ((void *) buf2, 1, sizeof (buf2), f), stdout);
	    pclose (f);
	  }
      else
	no_services = true;
    }

  /* inform the user if nothing found */
  if (no_services)
    puts ("No Cygwin services found.\n");
}

enum handle_reg_t
{
  PRINT_KEY,
  DELETE_KEY
};

void
handle_reg_installation (handle_reg_t what)
{
  HKEY key;

  if (what == PRINT_KEY)
    printf ("Cygwin installations found in the registry:\n");
  for (int i = 0; i < 2; ++i)
    if (RegOpenKeyEx (i ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE,
		      "SOFTWARE\\Cygwin\\Installations", 0,
		      what == DELETE_KEY ? KEY_READ | KEY_WRITE : KEY_READ,
		      &key)
	== ERROR_SUCCESS)
      {
	char name[32], data[PATH_MAX];
	DWORD nsize, dsize, type;
	LONG ret;

	for (DWORD index = 0;
	     (ret = RegEnumValue (key, index, name, (nsize = 32, &nsize), 0,
				  &type, (PBYTE) data,
				  (dsize = PATH_MAX, &dsize)))
	     != ERROR_NO_MORE_ITEMS; ++index)
	  if (ret == ERROR_SUCCESS && dsize > 5)
	    {
	      char *path = data + 4;
	      if (path[1] != ':')
		*(path += 2) = '\\';
	      if (what == PRINT_KEY)
		printf ("  %s Key: %s Path: %s", i ? "User:  " : "System:",
			name, path);
	      strcat (path, "\\bin\\cygwin1.dll");
	      if (what == PRINT_KEY)
		printf ("%s\n", access (path, F_OK) ? " (ORPHANED)" : "");
	      else if (access (path, F_OK))
		{
		  RegDeleteValue (key, name);
		  /* Start over since index is not reliable anymore. */
		  --i;
		  break;
		}
	    }
	RegCloseKey (key);
      }
  if (what == PRINT_KEY)
    printf ("\n");
}

void
print_reg_installations ()
{
  handle_reg_installation (PRINT_KEY);
}

void
del_orphaned_reg_installations ()
{
  handle_reg_installation (DELETE_KEY);
}

/* Unfortunately neither mingw nor Windows know this function. */
char *
memmem (char *haystack, size_t haystacklen,
	const char *needle, size_t needlelen)
{
  if (needlelen == 0)
    return haystack;
  while (needlelen <= haystacklen)
    {
      if (!memcmp (haystack, needle, needlelen))
	return haystack;
      haystack++;
      haystacklen--;
    }
  return NULL;
}

extern "C" NTSTATUS NTAPI RtlGetVersion (PRTL_OSVERSIONINFOEXW);

static void
dump_sysinfo ()
{
  int i, j;
  char tmp[4000];
  time_t now;
  char *found_cygwin_dll;
  bool is_nt = false;
  char osname[128];
  DWORD obcaseinsensitive = 1;
  HKEY key;

  /* MSVCRT popen (called by pretty_id and dump_sysinfo_services) SEGVs if
     COMSPEC isn't set correctly.  Simply enforce it here.  Using
     Get/SetEnvironmentVariable to set the dir does *not* help, btw.
     Apparently MSVCRT keeps its own copy of the environment and changing
     that requires to use _wputenv. */
  if (!_wgetenv (L"COMSPEC"))
    {
      WCHAR comspec[MAX_PATH + 17];
      wcscpy (comspec, L"COMSPEC=");
      GetSystemDirectoryW (comspec + 8, MAX_PATH);
      wcsncat (comspec, L"\\cmd.exe", sizeof comspec);
      _wputenv (comspec);
    }

  printf ("\nCygwin Configuration Diagnostics\n");
  time (&now);
  printf ("Current System Time: %s\n", ctime (&now));

  RTL_OSVERSIONINFOEXW osversion;
  osversion.dwOSVersionInfoSize = sizeof (RTL_OSVERSIONINFOEXW);
  RtlGetVersion (&osversion);

  switch (osversion.dwPlatformId)
    {
    case VER_PLATFORM_WIN32_NT:
      is_nt = true;
      if (osversion.dwMajorVersion >= 6)
	{
	  HMODULE k32 = GetModuleHandleW (L"kernel32.dll");
	  BOOL (WINAPI *GetProductInfo) (DWORD, DWORD, DWORD, DWORD, PDWORD) =
		  (BOOL (WINAPI *)(DWORD, DWORD, DWORD, DWORD, PDWORD))
		  GetProcAddress (k32, "GetProductInfo");
	  if (osversion.dwMajorVersion == 6)
	    switch (osversion.dwMinorVersion)
	      {
	      case 0:
		strcpy (osname, osversion.wProductType == VER_NT_WORKSTATION
				? "Vista" : "Server 2008");
		break;
	      case 1:
		strcpy (osname, osversion.wProductType == VER_NT_WORKSTATION
				? "7" : "Server 2008 R2");
		break;
	      case 2:
		strcpy (osname, osversion.wProductType == VER_NT_WORKSTATION
				? "8" : "Server 2012");
		break;
	      case 3:
		strcpy (osname, osversion.wProductType == VER_NT_WORKSTATION
				? "8.1" : "Server 2012 R2");
		break;
	      case 4:
	      default:
		strcpy (osname, osversion.wProductType == VER_NT_WORKSTATION
				? "10 Preview" : "Server 2016 Preview");
		break;
	      }
	  else if (osversion.dwMajorVersion == 10)
	    {
	      if (osversion.wProductType == VER_NT_WORKSTATION)
		strcpy (osname, osversion.dwBuildNumber >= 22000 ? "11" : "10");
	      else
		{
		  if (osversion.dwBuildNumber <= 14393)
		    strcpy (osname, "Server 2016");
		  else if (osversion.dwBuildNumber <= 17763)
		    strcpy (osname, "Server 2019");
		  else if (osversion.dwBuildNumber <= 20348)
		    strcpy (osname, "Server 2022");
		  else if (osversion.dwBuildNumber <= 26100)
		    strcpy (osname, "Server 2025");
		  else
		    strcpy (osname, "Server 20??");
		}
	    }
	  DWORD prod;
	  if (GetProductInfo (osversion.dwMajorVersion,
			      osversion.dwMinorVersion,
			      osversion.wServicePackMajor,
			      osversion.wServicePackMinor,
			      &prod))
	    {
	      const char *products[] =
		{
 /* 0x00000000 */ "",
 /* 0x00000001 */ " Ultimate",
 /* 0x00000002 */ " Home Basic",
 /* 0x00000003 */ " Home Premium",
 /* 0x00000004 */ " Enterprise",
 /* 0x00000005 */ " Home Basic N",
 /* 0x00000006 */ " Business",
 /* 0x00000007 */ " Server Standard",
 /* 0x00000008 */ " Server Datacenter",
 /* 0x00000009 */ " Small Business Server",
 /* 0x0000000a */ " Server Enterprise",
 /* 0x0000000b */ " Starter",
 /* 0x0000000c */ " Server Datacenter Core",
 /* 0x0000000d */ " Server Standard Core",
 /* 0x0000000e */ " Server Enterprise Core",
 /* 0x0000000f */ " Server Enterprise for Itanium-based Systems",
 /* 0x00000010 */ " Business N",
 /* 0x00000011 */ " Web Server",
 /* 0x00000012 */ " HPC Edition",
 /* 0x00000013 */ " Home Server",
 /* 0x00000014 */ " Storage Server Express",
 /* 0x00000015 */ " Storage Server Standard",
 /* 0x00000016 */ " Storage Server Workgroup",
 /* 0x00000017 */ " Storage Server Enterprise",
 /* 0x00000018 */ " for Windows Essential Server Solutions",
 /* 0x00000019 */ " Small Business Server Premium",
 /* 0x0000001a */ " Home Premium N",
 /* 0x0000001b */ " Enterprise N",
 /* 0x0000001c */ " Ultimate N",
 /* 0x0000001d */ " Web Server Core",
 /* 0x0000001e */ " Essential Business Server Management Server",
 /* 0x0000001f */ " Essential Business Server Security Server",
 /* 0x00000020 */ " Essential Business Server Messaging Server",
 /* 0x00000021 */ " Server Foundation",
 /* 0x00000022 */ " Home Server 2011",
 /* 0x00000023 */ " without Hyper-V for Windows Essential Server Solutions",
 /* 0x00000024 */ " Server Standard without Hyper-V",
 /* 0x00000025 */ " Server Datacenter without Hyper-V",
 /* 0x00000026 */ " Server Enterprise without Hyper-V",
 /* 0x00000027 */ " Server Datacenter Core without Hyper-V",
 /* 0x00000028 */ " Server Standard Core without Hyper-V",
 /* 0x00000029 */ " Server Enterprise Core without Hyper-V",
 /* 0x0000002a */ " Hyper-V Server",
 /* 0x0000002b */ " Storage Server Express Core",
 /* 0x0000002c */ " Storage Server Standard Core",
 /* 0x0000002d */ " Storage Server Workgroup Core",
 /* 0x0000002e */ " Storage Server Enterprise Core",
 /* 0x0000002f */ " Starter N",
 /* 0x00000030 */ " Professional",
 /* 0x00000031 */ " Professional N",
 /* 0x00000032 */ " Small Business Server 2011 Essentials",
 /* 0x00000033 */ " Server For SB Solutions",
 /* 0x00000034 */ " Server Solutions Premium",
 /* 0x00000035 */ " Server Solutions Premium Core",
 /* 0x00000036 */ " Server For SB Solutions EM", /* per MSDN, 2012-09-01 */
 /* 0x00000037 */ " Server For SB Solutions EM", /* per MSDN, 2012-09-01 */
 /* 0x00000038 */ " Multipoint Server",
 /* 0x00000039 */ "",
 /* 0x0000003a */ "",
 /* 0x0000003b */ " Essential Server Solution Management",
 /* 0x0000003c */ " Essential Server Solution Additional",
 /* 0x0000003d */ " Essential Server Solution Management SVC",
 /* 0x0000003e */ " Essential Server Solution Additional SVC",
 /* 0x0000003f */ " Small Business Server Premium Core",
 /* 0x00000040 */ " Server Hyper Core V",
 /* 0x00000041 */ "",
 /* 0x00000042 */ " Starter E",
 /* 0x00000043 */ " Home Basic E",
 /* 0x00000044 */ " Home Premium E",
 /* 0x00000045 */ " Professional E",
 /* 0x00000046 */ " Enterprise E",
 /* 0x00000047 */ " Ultimate E",
 /* 0x00000048 */ " Server Enterprise (Evaluation inst.)",
 /* 0x00000049 */ "",
 /* 0x0000004a */ "",
 /* 0x0000004b */ "",
 /* 0x0000004c */ " MultiPoint Server Standard",
 /* 0x0000004d */ " MultiPoint Server Premium",
 /* 0x0000004e */ "",
 /* 0x0000004f */ " Server Standard (Evaluation inst.)",
 /* 0x00000050 */ " Server Datacenter (Evaluation inst.)",
 /* 0x00000051 */ "",
 /* 0x00000052 */ "",
 /* 0x00000053 */ "",
 /* 0x00000054 */ " Enterprise N (Evaluation inst.)",
 /* 0x00000055 */ "",
 /* 0x00000056 */ "",
 /* 0x00000057 */ "",
 /* 0x00000058 */ "",
 /* 0x00000059 */ "",
 /* 0x0000005a */ "",
 /* 0x0000005b */ "",
 /* 0x0000005c */ "",
 /* 0x0000005d */ "",
 /* 0x0000005e */ "",
 /* 0x0000005f */ " Storage Server Workgroup (Evaluation inst.)",
 /* 0x00000060 */ " Storage Server Standard (Evaluation inst.)",
 /* 0x00000061 */ "",
 /* 0x00000062 */ " N",
 /* 0x00000063 */ " China",
 /* 0x00000064 */ " Single Language",
 /* 0x00000065 */ " Home",
 /* 0x00000066 */ "",
 /* 0x00000067 */ " Professional with Media Center",
 /* 0x00000068 */ " Mobile",
 /* 0x00000069 */ "",
 /* 0x0000006a */ "",
 /* 0x0000006b */ "",
 /* 0x0000006c */ "",
 /* 0x0000006d */ "",
 /* 0x0000006e */ "",
 /* 0x0000006f */ "",
 /* 0x00000070 */ "",
 /* 0x00000071 */ "",
 /* 0x00000072 */ "",
 /* 0x00000073 */ "",
 /* 0x00000074 */ "",
 /* 0x00000075 */ "",
 /* 0x00000076 */ "",
 /* 0x00000077 */ " Team",
 /* 0x00000078 */ "",
 /* 0x00000079 */ " Education",
 /* 0x0000007a */ " Education N",
 /* 0x0000007b */ "",
 /* 0x0000007c */ "",
 /* 0x0000007d */ " Enterprise 2015 LTSB",
 /* 0x0000007e */ " Enterprise 2015 LTSB N",
 /* 0x0000007f */ "",
 /* 0x00000080 */ "",
 /* 0x00000081 */ " Enterprise 2015 LTSB Evaluation",
 /* 0x00000082 */ " Enterprise 2015 LTSB N Evaluation",
 /* 0x00000083 */ " IoT Core Commercial",
 /* 0x00000084 */ "",
 /* 0x00000085 */ " Mobile Enterprise",
 /* 0x00000086 */ "",
 /* 0x00000087 */ "",
 /* 0x00000088 */ "",
 /* 0x00000089 */ "",
 /* 0x0000008a */ "",
 /* 0x0000008b */ "",
 /* 0x0000008c */ "",
 /* 0x0000008d */ "",
 /* 0x0000008e */ "",
 /* 0x0000008f */ "",
 /* 0x00000090 */ "",
 /* 0x00000091 */ " Server Datacenter, Semi-Annual Channel (core installation)",
 /* 0x00000092 */ " Server Standard, Semi-Annual Channel (core installation)",
 /* 0x00000093 */ "",
 /* 0x00000094 */ "",
 /* 0x00000095 */ "",
 /* 0x00000096 */ "",
 /* 0x00000097 */ "",
 /* 0x00000098 */ "",
 /* 0x00000099 */ "",
 /* 0x0000009a */ "",
 /* 0x0000009b */ "",
 /* 0x0000009c */ "",
 /* 0x0000009d */ "",
 /* 0x0000009e */ "",
 /* 0x0000009f */ "",
 /* 0x000000a0 */ "",
 /* 0x000000a1 */ " Pro for Workstations",
 /* 0x000000a2 */ " Pro for Workstations N",
 /* 0x000000a3 */ "",
 /* 0x000000a4 */ " Pro Education",
 /* 0x000000a5 */ "",
 /* 0x000000a6 */ "",
 /* 0x000000a7 */ "",
 /* 0x000000a8 */ "",
 /* 0x000000a9 */ "",
 /* 0x000000aa */ "",
 /* 0x000000ab */ "",
 /* 0x000000ac */ "",
 /* 0x000000ad */ "",
 /* 0x000000ae */ "",
 /* 0x000000af */ " Enterprise for Virtual Desktops",
 /* 0x000000b0 */ "",
 /* 0x000000b1 */ "",
 /* 0x000000b2 */ "",
 /* 0x000000b3 */ "",
 /* 0x000000b4 */ "",
 /* 0x000000b5 */ "",
 /* 0x000000b6 */ "",
 /* 0x000000b7 */ "",
 /* 0x000000b8 */ "",
 /* 0x000000b9 */ "",
 /* 0x000000ba */ "",
 /* 0x000000bb */ "",
 /* 0x000000bc */ " IoT Enterprise",
 /* 0x000000bd */ "",
 /* 0x000000be */ "",
 /* 0x000000bf */ " IoT Enterprise LTSC",
		};
	      if (prod == PRODUCT_UNLICENSED)
		strcat (osname, "Unlicensed");
	      else if (prod > PRODUCT_IOTENTERPRISES)
		strcat (osname, "");
	      else
		strcat (osname, products[prod]);
	    }
	  else
	    {
	    }
	}
      else
	strcpy (osname, "NT");
      break;
    default:
      strcpy (osname, "??");
      break;
    }
  printf ("Windows %s Ver %lu.%lu Build %lu %ls\n", osname,
	  osversion.dwMajorVersion, osversion.dwMinorVersion,
	  osversion.dwPlatformId == VER_PLATFORM_WIN32_NT ?
	  osversion.dwBuildNumber : (osversion.dwBuildNumber & 0xffff),
	  osversion.dwPlatformId == VER_PLATFORM_WIN32_NT ?
	  osversion.szCSDVersion : L"");

  if (osversion.dwPlatformId == VER_PLATFORM_WIN32s
      || osversion.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS)
    exit (EXIT_FAILURE);

  if (GetSystemMetrics (SM_REMOTESESSION))
    printf ("\nRunning in Terminal Service session\n");

  printf ("\nPath:");
  char *s = getenv ("PATH"), *e;
  if (!s)
    puts ("");
  else
    {
      char sep = strchr (s, ';') ? ';' : ':';
      int count_path_items = 0;
      while (1)
	{
	  for (e = s; *e && *e != sep; e++);
	  if (e-s)
	    printf ("\t%.*s\n", (int) (e - s), s);
	  else
	    puts ("\t.");
	  count_path_items++;
	  if (!*e)
	    break;
	  s = e + 1;
	}
    }

  fflush (stdout);

  pretty_id ();

  if (!GetSystemDirectory (tmp, 4000))
    display_error ("dump_sysinfo: GetSystemDirectory()");
  printf ("\nSysDir: %s\n", tmp);

  GetWindowsDirectory (tmp, 4000);
  printf ("WinDir: %s\n\n", tmp);


  if (givehelp)
    printf ("Here's some environment variables that may affect cygwin:\n");
  for (i = 0; environ[i]; i++)
    {
      char *eq = strchr (environ[i], '=');
      if (!eq)
	continue;
      /* int len = eq - environ[i]; */
      for (j = 0; known_env_vars[j]; j++)
	{
	  *eq = 0;
	  if (strcmp (environ[i], "PATH") == 0)
	    continue;		/* we handle this one specially */
	  if (strcasecmp (environ[i], known_env_vars[j]) == 0)
	    printf ("%s = '%s'\n", environ[i], eq + 1);
	  *eq = '=';
	}
    }
  printf ("\n");

  if (verbose)
    {
      if (givehelp)
	printf ("Here's the rest of your environment variables:\n");
      for (i = 0; environ[i]; i++)
	{
	  int found = 0;
	  char *eq = strchr (environ[i], '=');
	  if (!eq)
	    continue;
	  /* int len = eq - environ[i]; */
	  for (j = 0; known_env_vars[j]; j++)
	    {
	      *eq = 0;
	      if (strcasecmp (environ[i], known_env_vars[j]) == 0)
		found = 1;
	      *eq = '=';
	    }
	  if (!found)
	    {
	      *eq = 0;
	      printf ("%s = '%s'\n", environ[i], eq + 1);
	      *eq = '=';
	    }
	}
      printf ("\n");
    }

  if (registry)
    {
      if (givehelp)
	printf ("Scanning registry for keys with 'Cygwin' in them...\n");
      scan_registry (0, HKEY_CURRENT_USER,
		     (char *) "HKEY_CURRENT_USER", 0, false);
      scan_registry (0, HKEY_LOCAL_MACHINE,
		     (char *) "HKEY_LOCAL_MACHINE", 0, false);
      printf ("\n");
    }
  else
    printf ("Use '-r' to scan registry\n\n");

  if (RegOpenKeyEx (HKEY_LOCAL_MACHINE,
		  "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel",
		  0, KEY_READ, &key) == ERROR_SUCCESS)
    {
      DWORD size;
      RegQueryValueEx (key, "obcaseinsensitive", NULL, NULL,
		       (LPBYTE) &obcaseinsensitive, &size);
      RegCloseKey (key);
    }
  printf ("obcaseinsensitive set to %lu\n\n", obcaseinsensitive);

  print_reg_installations ();

  if (givehelp)
    {
      printf ("Listing available drives...\n");
      printf ("Drv Type          Size   Used Flags              Name\n");
    }
  int prev_mode =
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
  int drivemask = GetLogicalDrives ();

  for (i = 0; i < 26; i++)
    {
      if (!(drivemask & (1 << i)))
	continue;
      char drive[4], name[200], fsname[200];
      DWORD serno = 0, maxnamelen = 0, flags = 0;
      name[0] = fsname[0] = 0;
      sprintf (drive, "%c:\\", i + 'a');
      /* Report all errors, except if the Volume is ERROR_NOT_READY.
	 ERROR_NOT_READY is returned when removeable media drives are empty
	 (CD, floppy, etc.) */
      if (!GetVolumeInformation (drive, name, sizeof (name), &serno,
				 &maxnamelen, &flags, fsname,
				 sizeof (fsname))
	  && GetLastError () != ERROR_NOT_READY)
	{
#	  define FMT "dump_sysinfo: GetVolumeInformation() for drive %c:"
	  char buf[sizeof (FMT)];
	  sprintf (buf, FMT, 'A' + i);
	  display_error (buf);
#	  undef FMT
	}

      int dtype = GetDriveType (drive);
      char drive_type[4] = "unk";
      switch (dtype)
	{
	case DRIVE_REMOVABLE:
	  strcpy (drive_type, "fd ");
	  break;
	case DRIVE_FIXED:
	  strcpy (drive_type, "hd ");
	  break;
	case DRIVE_REMOTE:
	  strcpy (drive_type, "net");
	  break;
	case DRIVE_CDROM:
	  strcpy (drive_type, "cd ");
	  break;
	case DRIVE_RAMDISK:
	  strcpy (drive_type, "ram");
	  break;
	default:
	  strcpy (drive_type, "unk");
	}

      long capacity_mb = -1;
      int percent_full = -1;

      ULARGE_INTEGER free_me, free_bytes, total_bytes;
      free_me.QuadPart = free_bytes.QuadPart = 0ULL;
      total_bytes.QuadPart = 1ULL;
      if (GetDiskFreeSpaceEx (drive, &free_me, &total_bytes, &free_bytes))
	{
	  capacity_mb = total_bytes.QuadPart / (1024L * 1024L);
	  percent_full = 100 - (int) ((100.0 * free_me.QuadPart)
				      / total_bytes.QuadPart);
	}
      else
	{
	  DWORD spc = 0, bps = 0, fc = 0, tc = 1;
	  if (GetDiskFreeSpace (drive, &spc, &bps, &fc, &tc))
	    {
	      capacity_mb = (spc * bps * tc) / (1024 * 1024);
	      percent_full = 100 - (int) ((100.0 * fc) / tc);
	    }
	}

      printf ("%.2s  %s %-6s ", drive, drive_type, fsname);
      if (capacity_mb >= 0)
	printf ("%7dMb %3d%% ", (int) capacity_mb, (int) percent_full);
      else
	printf ("    N/A    N/A ");
      printf ("%s %s %s %s %s %s %s  %s\n",
	      flags & FS_CASE_IS_PRESERVED ? "CP" : "  ",
	      flags & FS_CASE_SENSITIVE ? "CS" : "  ",
	      flags & FS_UNICODE_STORED_ON_DISK ? "UN" : "  ",
	      flags & FS_PERSISTENT_ACLS ? "PA" : "  ",
	      flags & FS_FILE_COMPRESSION ? "FC" : "  ",
	      flags & FS_VOL_IS_COMPRESSED ? "VC" : "  ",
	      flags & FILE_VOLUME_QUOTAS ? "QU" : "  ",
	      name);
    }

  SetErrorMode (prev_mode);
  if (givehelp)
    {
      puts ("\n"
	  "fd = floppy,          hd = hard drive,       cd = CD-ROM\n"
	  "net= Network Share,   ram= RAM drive,        unk= Unknown\n"
	  "CP = Case Preserving, CS = Case Sensitive,   UN = Unicode\n"
	  "PA = Persistent ACLS, FC = File Compression, VC = Volume Compression");
    }
  printf ("\n");

  unsigned ml_fsname = 4, ml_dir = 7, ml_type = 6;
  bool ml_trailing = false;

  struct mntent *mnt;
  setmntent (0, 0);
  while ((mnt = getmntent (0)))
    {
      unsigned n = (int) strlen (mnt->mnt_fsname);
      ml_trailing |= (n > 1 && strchr ("\\/", mnt->mnt_fsname[n - 1]));
      if (ml_fsname < n)
	ml_fsname = n;
      n = (int) strlen (mnt->mnt_dir);
      ml_trailing |= (n > 1 && strchr ("\\/", mnt->mnt_dir[n - 1]));
      if (ml_dir < n)
	ml_dir = n;
    }

  if (ml_trailing)
    puts ("Warning: Mount entries should not have a trailing (back)slash\n");

  if (givehelp)
    {
      printf
	("Mount entries: these map POSIX directories to your NT drives.\n");
      printf ("%-*s  %-*s  %-*s  %s\n", ml_fsname, "-NT-", ml_dir, "-POSIX-",
	      ml_type, "-Type-", "-Flags-");
    }

  setmntent (0, 0);
  while ((mnt = getmntent (0)))
    {
      printf ("%-*s  %-*s  %-*s  %s\n",
	      ml_fsname, mnt->mnt_fsname,
	      ml_dir, mnt->mnt_dir, ml_type, mnt->mnt_type, mnt->mnt_opts);
    }
  printf ("\n");

  if (givehelp)
    printf
      ("Looking to see where common programs can be found, if at all...\n");
  for (i = 0; common_apps[i].name; i++)
    if (!find_app_on_path ((char *) common_apps[i].name, 1))
      {
	if (common_apps[i].missing_is_good)
	  printf ("Not Found: %s (good!)\n", common_apps[i].name);
	else
	  printf ("Not Found: %s\n", common_apps[i].name);
      }
  printf ("\n");

  if (givehelp)
    printf ("Looking for various Cygwin DLLs...  (-v gives version info)\n");
  int cygwin_dll_count = 0;
  char cygdll_path[32768];
  for (pathlike *pth = paths; pth->dir; pth++)
    {
      WIN32_FIND_DATAW ffinfo;
      sprintf (tmp, "%s*.*", pth->dir);
      wide_path wpath (tmp);
      HANDLE ff = FindFirstFileW (wpath, &ffinfo);
      int found = (ff != INVALID_HANDLE_VALUE);
      found_cygwin_dll = NULL;
      while (found)
	{
	  char f[FILENAME_MAX + 1];
	  wcstombs (f, ffinfo.cFileName, sizeof f);
	  if (strcasecmp (f + strlen (f) - 4, ".dll") == 0)
	    {
	      if (strncasecmp (f, "cyg", 3) == 0)
		{
		  sprintf (tmp, "%s%s", pth->dir, f);
		  if (strcasecmp (f, "cygwin1.dll") == 0)
		    {
		      if (!cygwin_dll_count)
			strcpy (cygdll_path, pth->dir);
		      if (!cygwin_dll_count
			  || strcasecmp (cygdll_path, pth->dir) != 0)
			cygwin_dll_count++;
		      found_cygwin_dll = strdup (tmp);
		    }
		  else
		    ls (tmp);
		}
	    }
	  found = FindNextFileW (ff, &ffinfo);
	}
      if (found_cygwin_dll)
	{
	  ls (found_cygwin_dll);
	  free (found_cygwin_dll);
	}

      FindClose (ff);
    }
  if (cygwin_dll_count > 1)
    puts ("Warning: There are multiple cygwin1.dlls on your path");
  if (!cygwin_dll_count)
    puts ("Warning: cygwin1.dll not found on your path");

  dump_dodgy_apps (verbose);

  if (is_nt)
    dump_sysinfo_services ();
}

static int
check_keys ()
{
  HANDLE h = CreateFileW (L"CONIN$", GENERIC_READ | GENERIC_WRITE,
			  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
			  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

  if (h == INVALID_HANDLE_VALUE || h == NULL)
    return (display_error ("check_keys: Opening CONIN$"));

  DWORD mode;

  if (!GetConsoleMode (h, &mode))
    display_error ("check_keys: GetConsoleMode()");
  else
    {
      mode &= ~ENABLE_PROCESSED_INPUT;
      if (!SetConsoleMode (h, mode))
	display_error ("check_keys: SetConsoleMode()");
    }

  fputs ("\nThis key check works only in a console window,", stderr);
  fputs (" _NOT_ in a terminal session!\n", stderr);
  fputs ("Abort with Ctrl+C if in a terminal session.\n\n", stderr);
  fputs ("Press 'q' to exit.\n", stderr);

  INPUT_RECORD in, prev_in;

  // Drop first <RETURN> key
  ReadConsoleInputW (h, &in, 1, &mode);

  memset (&in, 0, sizeof in);

  do
    {
      prev_in = in;
      if (!ReadConsoleInputW (h, &in, 1, &mode))
	display_error ("check_keys: ReadConsoleInput()");

      if (!memcmp (&in, &prev_in, sizeof in))
	continue;

      switch (in.EventType)
	{
	case KEY_EVENT:
	  printf ("%s %ux VK: 0x%04x VS: 0x%04x C: 0x%04x CTRL: ",
		  in.Event.KeyEvent.bKeyDown ? "Pressed " : "Released",
		  in.Event.KeyEvent.wRepeatCount,
		  in.Event.KeyEvent.wVirtualKeyCode,
		  in.Event.KeyEvent.wVirtualScanCode,
		  (unsigned char) in.Event.KeyEvent.uChar.UnicodeChar);
	  fputs (in.Event.KeyEvent.dwControlKeyState & CAPSLOCK_ON ?
		 "CL " : "-- ", stdout);
	  fputs (in.Event.KeyEvent.dwControlKeyState & ENHANCED_KEY ?
		 "EK " : "-- ", stdout);
	  fputs (in.Event.KeyEvent.dwControlKeyState & LEFT_ALT_PRESSED ?
		 "LA " : "-- ", stdout);
	  fputs (in.Event.KeyEvent.dwControlKeyState & LEFT_CTRL_PRESSED ?
		 "LC " : "-- ", stdout);
	  fputs (in.Event.KeyEvent.dwControlKeyState & NUMLOCK_ON ?
		 "NL " : "-- ", stdout);
	  fputs (in.Event.KeyEvent.dwControlKeyState & RIGHT_ALT_PRESSED ?
		 "RA " : "-- ", stdout);
	  fputs (in.Event.KeyEvent.dwControlKeyState & RIGHT_CTRL_PRESSED ?
		 "RC " : "-- ", stdout);
	  fputs (in.Event.KeyEvent.dwControlKeyState & SCROLLLOCK_ON ?
		 "SL " : "-- ", stdout);
	  fputs (in.Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED ?
		 "SH " : "-- ", stdout);
	  fputc ('\n', stdout);
	  break;

	default:
	  break;
	}
    }
  while (in.EventType != KEY_EVENT ||
	 in.Event.KeyEvent.bKeyDown != FALSE ||
	 in.Event.KeyEvent.uChar.UnicodeChar != L'q');

  CloseHandle (h);
  return 0;
}

/* These do not need to be escaped in application/x-www-form-urlencoded */
static const char safe_chars[] = "$-_.!*'(),";

/* the URL to query.  */
static const char grep_base_url[] =
	"http://cygwin.com/cgi-bin2/package-grep.cgi?text=1&grep=";

#if defined(__x86_64__)
#define ARCH_STR  "&arch=x86_64"
#elif defined(__aarch64__)
#define ARCH_STR  "&arch=aarch64"
#else
#error unimplemented for this target
#endif
static const char *ARCH_str = ARCH_STR;

static int
fetch_url (const char *url, FILE *outstream)
{
  DWORD rc = 0, rc_s = sizeof (DWORD);
  HINTERNET hi = NULL, hurl = NULL;
  char buf[4096];
  DWORD numread;
  int ret;

  /* Connect to the net and open the URL.  */
  if (InternetAttemptConnect (0) != ERROR_SUCCESS)
    {
      fputs ("An internet connection is required for this function.\n", stderr);
      return 1;
    }

  /* Initialize WinInet and attempt to fetch our URL.  */
  if (!(hi = InternetOpenA ("cygcheck", INTERNET_OPEN_TYPE_PRECONFIG,
			    NULL, NULL, 0)))
    return display_internet_error ("InternetOpen() failed", NULL);

  if (!(hurl = InternetOpenUrlA (hi, url, NULL, 0, 0, 0)))
    {
      ret = display_internet_error ("unable to contact cygwin.com site, "
				    "InternetOpenUrl() failed", hi, NULL);
      goto out_open;
    }

  /* Check the HTTP response code.  */
  if (!HttpQueryInfoA (hurl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
		      (void *) &rc, &rc_s, NULL))
    {
      ret = display_internet_error ("HttpQueryInfo() failed", hurl, hi, NULL);
      goto out_open_url;
    }

  if (rc != HTTP_STATUS_OK)
    {
      sprintf (buf, "error retrieving results from cygwin.com site, "
		    "HTTP status code %lu", rc);
      ret = display_internet_error (buf, hurl, hi, NULL);
      goto out_open_url;
    }

  /* Fetch result and print to outstream.  */
  do
    {
      if (!InternetReadFile (hurl, (void *) buf, sizeof (buf), &numread))
	{
	  ret = display_internet_error ("InternetReadFile failed", hurl, hi,
					NULL);
	  goto out_open_url;
	}
      if (numread)
	fwrite ((void *) buf, (size_t) numread, 1, outstream);
    }
  while (numread);

  ret = 0;

out_open_url:
  InternetCloseHandle (hurl);
out_open:
  InternetCloseHandle (hi);
  return ret;
}

struct passwd {
  char    *pw_name;    /* user name */
  char    *pw_passwd;  /* encrypted password */
  uint32_t pw_uid;     /* user uid */
  uint32_t pw_gid;     /* user gid */
  char    *pw_comment; /* comment */
  char    *pw_gecos;   /* Honeywell login info */
  char    *pw_dir;     /* home directory */
  char    *pw_shell;   /* default shell */
};

/* Downloads setup.ini from cygwin.com, if it hasn't been downloaded
   already or is older than 24h. */
static FILE *
maybe_download_setup_ini ()
{
  time_t t3h_before;
  char *path;
  struct stat st;
  FILE *fp;

  t3h_before = time (NULL) - 3 * 60 * 60;
  for (int i = 0; i < 2; ++i)
    {
      /* Check for the system-wide setup.ini file first.  If that's too
	 old and not writable, check for ~/.setup.ini. */
      if (i == 0)
	path = cygpath ("/etc/setup/setup.ini", NULL);
      else
	{
	  char *localappdata = getenv ("LOCALAPPDATA");
	  char *cp;

	  if (!localappdata)
	    return NULL;
	  path = (char *) malloc (strlen (localappdata)
				  + strlen ("\\.setup.ini") + 1);
	  cp = stpcpy (path, localappdata);
	  stpcpy (cp, "\\.setup.ini");
	}
      /* If file exists, and has been downloaded less than 3h ago,
         and if we can open it for reading, just use it. */
      if (stat (path, &st) == 0
	  && st.st_mtime > t3h_before
	  && (fp = fopen (path, "rt")) != NULL)
	return fp;
      /* Otherwise, try to open it for writing and fetch from cygwin.com. */
      if ((fp = fopen (path, "w+")) != NULL)
	{
	  fputs ("Fetching setup.ini from cygwin.com...\n", stderr);
	  if (!fetch_url ("https://cygwin.com/ftp/cygwin/x86_64/setup.ini", fp))
	    {
	      fclose (fp);
	      fp = fopen (path, "rt");
	      fputs ("\n", stderr);
	      return fp;
	    }
	  fclose (fp);
	}
    }
  return NULL;
}

struct vers_info
{
  char *version;
  char *install;
  char *source;
  char *depends2;
  char *build_depends;
  time_t install_date;
  bool matches;
  bool installed;
};

struct ini_package_info
{
  char *name;
  char *sdesc;
  char *ldesc;
  char *category;
  char *url;
  char *license;
  vers_info curr;
  size_t prev_count;
  vers_info *prev;
  size_t test_count;
  vers_info *test;
};

static void
free_pkg_info (ini_package_info *pi)
{
  free (pi->name);
  free (pi->sdesc);
  free (pi->ldesc);
  free (pi->category);
  free (pi->url);
  free (pi->license);
  free (pi->curr.version);
  free (pi->curr.install);
  free (pi->curr.source);
  free (pi->curr.depends2);
  free (pi->curr.build_depends);
  if (pi->prev)
    {
      for (size_t i = 0; i < pi->prev_count; ++i)
	{
	  free (pi->prev[i].version);
	  free (pi->prev[i].install);
	  free (pi->prev[i].source);
	  free (pi->prev[i].depends2);
	  free (pi->prev[i].build_depends);
	}
      free (pi->prev);
    }
  if (pi->test)
    {
      for (size_t i = 0; i < pi->test_count; ++i)
	{
	  free (pi->test[i].version);
	  free (pi->test[i].install);
	  free (pi->test[i].source);
	  free (pi->test[i].depends2);
	  free (pi->test[i].build_depends);
	}
      free (pi->test);
    }
}

static void
collect_quoted_string (char *&tgt, FILE *fp, char *buf, size_t size, size_t offset)
{
  bool found = false;
  char *cp, *s;

  cp = buf + offset;
  if (*cp != '"')	/* just 'til end of line */
    {
      if ((s = strchr (cp, '\n')))
	*s = '\0';
      tgt = strdup (cp);
      return;
    }
  /* text starts with a quote, collect 'til the closing quote */
  ++cp;
  do
    {
      if ((s = strrchr (cp, '"')) && (s == cp || s[-1] != '\\'))
	{
	  *s = '\0';
	  found = true;
	}
      if (!tgt)
	s = (char *) calloc (strlen (cp) + 1, sizeof *s);
      else
	s = (char *) realloc (tgt, strlen (tgt) + strlen (cp) + 1);
      if (s)
	{
	  tgt = s;
	  strcat (tgt, cp);
	}
    }
  while (!found && (cp = fgets (buf, size, fp)));
}

static ini_package_info *
collect_pkg_info (FILE *fp, ini_package_info *pi, bool search)
{
  vers_info *vinfo = &pi->curr;
  char buf[4096];
  char *s;

  memset (pi, 0, sizeof *pi);
  /* Search next line starting with "@ ". */
  while ((s = fgets (buf, sizeof buf, fp)))
    {
      if (s[0] == '@' && s[1] == ' ')
	break;
    }
  if (!s)
    goto error;
  /* Extract package name */
  if ((s = strchr (buf, '\n')))
    *s = '\0';
  pi->name = strdup (buf + 2);
  /* collect all of this package block. */
  while ((s = fgets (buf, sizeof buf, fp)))
    {
      /* empty line? EOR. */
      if (s[0] == '\n')
	break;
      /* prev or test version? */
      if (buf[0] == '[')
	{
	  vers_info **vers_p = NULL;
	  size_t *vers_cnt_p = NULL;

	  if (!strncmp (buf, "[prev]", strlen ("[prev]")))
	    {
	      vers_p = &pi->prev;
	      vers_cnt_p = &pi->prev_count;
	    }
	  else if (!strncmp (buf, "[test]", strlen ("[test]")))
	    {
	      vers_p = &pi->test;
	      vers_cnt_p = &pi->test_count;
	    }
	  if (vers_p)
	    {
	      vers_info *v;

	      v = (vers_info *) realloc (*vers_p, (*vers_cnt_p + 1)
						  * sizeof (vers_info));
	      if (!v)
		goto error;
	      *vers_p = v;
	      vinfo = *vers_p + *vers_cnt_p;
	      memset (vinfo, 0, sizeof *vinfo);
	      ++(*vers_cnt_p);
	    }
	}
      else if (!strncmp (buf, "sdesc: ", strlen ("sdesc: ")))
	collect_quoted_string (pi->sdesc, fp, buf, sizeof buf,
			       strlen ("sdesc: "));
      else if (!strncmp (buf, "ldesc: ", strlen ("ldesc: ")))
	collect_quoted_string (pi->ldesc, fp, buf, sizeof buf,
			       strlen ("ldesc: "));
      else
        {
	  if ((s = strchr (buf, '\n')))
	    *s = '\0';
	  if (!strncmp (buf, "category: ", strlen ("category: ")))
	    pi->category = strdup (buf + strlen ("category: "));
	  else if (!strncmp (buf, "url: ", strlen ("url: ")))
	    pi->url = strdup (buf + strlen ("url: "));
	  else if (!strncmp (buf, "license: ", strlen ("license: ")))
	    pi->license = strdup (buf + strlen ("license: "));
	  else if (!strncmp (buf, "version: ", strlen ("version: ")))
	    vinfo->version = strdup (buf + strlen ("version: "));
	  else if (!strncmp (buf, "install: ", strlen ("install: ")))
	    vinfo->install = strdup (buf + strlen ("install: "));
	  else if (!strncmp (buf, "source: ", strlen ("source: ")))
	    vinfo->source = strdup (buf + strlen ("source: "));
	  else if (!strncmp (buf, "depends2: ", strlen ("depends2: ")))
	    {
	      if (!search)
		vinfo->depends2 = strdup (buf + strlen ("depends2: "));
	      else
		{
		  /* For pattern matching we need a standarized format.
		     Make sure all deps are prepended and trailed by a comma.
		     Note the missing space after the colon, that's deliberate
		     to keep it in the stored string.  Originally we kept the
		     spaces in, but spaces are filtered out by PathMatchSpecA,
		     so we now replace all space by comma here. */
		  char *start = buf + strlen ("depends2:");
		  size_t len = strlen (start);

		  vinfo->depends2 = (char *) calloc (len + 2, 1);
		  if (vinfo->depends2)
		    {
		      *stpcpy (vinfo->depends2, start) = ',';
		      char *cp = vinfo->depends2;
		      while ((cp = strchr (cp, ' ')))
			*cp = ',';
		    }
		}
	    }
	  else if (!strncmp (buf, "build-depends: ",
			     strlen ("build-depends: ")))
	    {
	      if (!search)
		vinfo->build_depends = strdup (buf
					       + strlen ("build-depends: "));
	      else
		{
		  /* Ditto */
		  char *start = buf + strlen ("build-depends:");
		  size_t len = strlen (start);

		  vinfo->build_depends = (char *) calloc (len + 2, 1);
		  if (vinfo->build_depends)
		    {
		      *stpcpy (vinfo->build_depends, start) = ',';
		      char *cp = vinfo->build_depends;
		      while ((cp = strchr (cp, ' ')))
			*cp++ = ',';
		    }
		}
	    }
	}
    }
  return pi;
error:
  free_pkg_info (pi);
  return NULL;
}

static const char *
human_readable (char *buf, size_t bytes)
{
  const char *siz[] = { "B", "K", "M", "G", NULL };
  double db = bytes;
  int idx = 0;
  int prec;

  while (bytes > 1023 && siz[idx + 1])
    {
      bytes >>= 10;
      db /= 1024.0;
      ++idx;
    }
  prec = log10 (db) + 1;
  if (prec < 2)
    prec = 2;
  sprintf (buf, "%.*g %s", prec, db, siz[idx]);
  return buf;
}

static void
package_info_print (ini_package_info *pi, vers_info *vers, int selector)
{
  char buf[4096];

  printf ("Name        : %s\n", pi->name);
  if (vers->version)
    {
      char *version = strcpy (buf, vers->version);
      char *release = NULL;

      release = strrchr (version, '-');
      if (release)
	*release++ = '\0';
      printf ("Version     : %s\n", version);
      if (release)
	printf ("Release     : %s\n", release);
    }
  if (vers->install)
    {
      char *arch = strcpy (buf, vers->install);
      char *sizep;
      size_t size = 0;

      char *cp;

      cp = strchr (arch, '/');
      if (cp)
	{
	  *cp++ = '\0';
	  cp = strchr (cp, ' ');
	  if (cp)
	    {
	      sizep = ++cp;
	      cp = strchr (cp, ' ');
	      if (cp)
		{
		  *cp = '\0';
		  size = strtoull (sizep, NULL, 10);
		}
	    }
	}
      if (cp)
	{
	  printf ("Architecture: %s\n", arch);
	  if (vers->install_date)
	    printf ("Install Date: %s", ctime (&vers->install_date));

	  printf ("Size        : %llu (%s)\n", size,
					       human_readable (buf, size));
	}
    }
  if (pi->category)
    printf ("Categories  : %s\n", pi->category);
  if (vers->source)
    {
      char *source = strcpy (buf, vers->source);
      char *cp = strchr (source, ' ');
      if (cp)
	{
	  *cp = '\0';
	  cp = strrchr (source, '/');
	  if (cp)
	    printf ("Source      : %s\n", cp + 1);
	}
    }
  if ((selector & INFO_DEPS) && vers->depends2)
    printf ("Dependencies: %s\n", vers->depends2);
  if ((selector & INFO_BLDDEPS) && vers->build_depends)
    printf ("Build Deps  : %s\n", vers->build_depends);
  if (pi->sdesc)
    printf ("Summary     : %s\n", pi->sdesc);
  if (pi->url)
    printf ("Url         : %s\n", pi->url);
  if (pi->license)
    printf ("License     : %s\n", pi->license);
  if (pi->ldesc)
    printf ("Description :\n%s\n", pi->ldesc);
  puts ("");
}

static void
package_info_check (ini_package_info *pi, vers_info *vi, pkgver *pv,
		    bool &am, bool &ai)
{
  vi->matches = true;
  if (pv && !strcmp (vi->version, pv->ver))
    vi->installed = true;
  am |= vi->matches;
  ai |= vi->installed;
}

static inline bool
check_name_version (char *pkg_name, char *pkg_version, char *search)
{
  char nv_buf[4096];
  char *nvp, *cp;

  nvp = stpcpy (nv_buf, pkg_name);
  *nvp++ = '-';
  stpcpy (nvp, pkg_version);
  if (PathMatchSpecA (nv_buf, search))
    return true;
  if ((cp = strrchr (nvp, '-'))) /* try w/o release */
    {
      *cp = '\0';
      if (PathMatchSpecA (nv_buf, search))
	return true;
    }
  return false;
}

static void
package_info_vers_check (ini_package_info *pi, vers_info *vi, char *search,
			 pkgver *pv, bool &am, bool &ai)
{
  vi->matches = check_name_version (pi->name, vi->version, search);
  if (pv && !strcmp (vi->version, pv->ver))
    vi->installed = true;
  am |= vi->matches;
  ai |= vi->installed;
}

int
pkg_comp (const void *a, const void *b)
{
  pkgver *pa = (pkgver *) a;
  pkgver *pb = (pkgver *) b;

  return strcmp (pa->name, pb->name);
}

/* Print full info for the package matching the search string in terms of
   name/version. */
static int
package_info (char **search, int selector)
{
  FILE *fp = maybe_download_setup_ini ();
  ini_package_info pi_buf, *pi;
  size_t inst_pkg_count;
  pkgver *inst_pkgs;

  if (!fp)
    return 1;

  if ((selector & INFO_ALL) == 0)
    selector |= INFO_ALL;

  inst_pkgs = get_installed_packages (NULL, &inst_pkg_count);

  while (search && *search)
    {
      rewind (fp);
      while ((pi = collect_pkg_info (fp, &pi_buf, false)))
	{
	  pkgver pv = { pi->name, NULL }, *inst_pkg = NULL;
	  bool avail_installed = false;
	  bool avail_matches = false;
	  bool inst_matches = false;

	  if (selector & INFO_INST)
	    {
	      inst_pkg = (pkgver *) bsearch (&pv, inst_pkgs, inst_pkg_count,
					     sizeof *inst_pkgs, pkg_comp);
	      if (inst_pkg)
		{
		  if (PathMatchSpecA (inst_pkg->name, *search))
		    inst_matches = true;
		  else
		    inst_matches = check_name_version (inst_pkg->name,
						       inst_pkg->ver,
						       *search);
		}
	    }

	  /* Name matches?  Print all versions */
	  if (PathMatchSpecA (pi->name, *search))
	    {
	      if (pi->curr.version)
		package_info_check (pi, &pi->curr, inst_pkg,
				    avail_matches, avail_installed);
	      for (size_t i = 0; i < pi->prev_count; ++i)
		package_info_check (pi, pi->prev + i, inst_pkg,
				    avail_matches, avail_installed);
	      for (size_t i = 0; i < pi->test_count; ++i)
		package_info_check (pi, pi->test + i, inst_pkg,
				    avail_matches, avail_installed);
	    }
	  else
	    {
	      /* Check if search matches name-version string */
	      if (pi->curr.version)
		package_info_vers_check (pi, &pi->curr, *search, inst_pkg,
					 avail_matches, avail_installed);
	      for (size_t i = 0; i < pi->prev_count; ++i)
		package_info_vers_check (pi, pi->prev + i, *search, inst_pkg,
					 avail_matches, avail_installed);
	      for (size_t i = 0; i < pi->test_count; ++i)
		package_info_vers_check (pi, pi->test + i, *search, inst_pkg,
					 avail_matches, avail_installed);
	    }

	  /* First print installed package(s) */
	  if (inst_pkg && inst_matches)
	    {
	      time_t install_ts = 0;
	      struct stat st;
	      char *path;

	      printf ("Installed package:\n"
	              "------------------\n\n");
	      /* fetch timestamp of last install. */

	      path = cygpath ("/etc/setup/", inst_pkg->name, ".lst.gz", NULL);
	      if (path)
		{
		  if (stat (path, &st) == 0)
		    install_ts = st.st_mtime;
		  free (path);
		}

	      /* Fake min info if installed package is not available anymore */
	      if (!avail_installed)
		{
		  ini_package_info inst_pi = { 0 };

		  inst_pi.name = inst_pkg->name;
		  inst_pi.sdesc = pi->sdesc;
		  inst_pi.ldesc = pi->ldesc;
		  inst_pi.url = pi->url;
		  inst_pi.license = pi->license;
		  inst_pi.curr.version = inst_pkg->ver;
		  inst_pi.curr.install_date = install_ts;
		  package_info_print (&inst_pi, &pi->curr, selector);
		}
	      else
		{
		  if (pi->curr.installed)
		    {
		      pi->curr.install_date = install_ts;
		      package_info_print (pi, &pi->curr, selector);
		    }
		  for (size_t i = 0; i < pi->prev_count; ++i)
		    if (pi->prev[i].installed)
		      {
			pi->prev[i].install_date = install_ts;
			package_info_print (pi, pi->prev + i, selector);
		      }
		  for (size_t i = 0; i < pi->test_count; ++i)
		    if (pi->test[i].installed)
		      {
			pi->test[i].install_date = install_ts;
			package_info_print (pi, pi->test + i, selector);
		      }
		}
	    }

	  /* Next print available, matching packages */
	  if (avail_matches)
	    {
	      if ((selector & INFO_CURR) && pi->curr.matches)
		{
		  puts ("Latest available package:\n"
			"-------------------------\n");
		  package_info_print (pi, &pi->curr, selector);
		}
	      if (selector & INFO_PREV)
		{
		  uint32_t header_printed = 0;

		  for (size_t i = 0; i < pi->prev_count; ++i)
		    if (pi->prev[i].matches)
		      {
			printf ("%s", header_printed++
				      ? ""
				      : "Older available packages:\n"
					"-------------------------\n\n");
			package_info_print (pi, pi->prev + i, selector);
		      }
		}
	      if (selector & INFO_TEST)
		{
		  uint32_t header_printed = 0;

		  for (size_t i = 0; i < pi->test_count; ++i)
		    if (pi->test[i].matches)
		      {
			printf ("%s", header_printed++
				      ? ""
				      : "Available test packages:\n"
					"------------------------\n\n");
			package_info_print (pi, pi->test + i, selector);
		      }
		}
	    }

	  free_pkg_info (&pi_buf);
	}
      ++search;
    }
  return 0;
}

/* Search for the search string in name and sdesc of available packages.
   The selector is used to search for dependencies. */
static int
package_search (char **search, int selector)
{
  FILE *fp = maybe_download_setup_ini ();
  ini_package_info pi_buf, *pi;
  char *ext_search, *ep;

  if (!fp)
    return 1;

  while (search && *search)
    {
      rewind (fp);

      ext_search = (char *) malloc (strlen (*search) + 5);
      ep = ext_search;
      if (selector)
	{
	  ep = stpcpy (ep, "*,");
	  ep = stpcpy (ep, *search);
	  ep = stpcpy (ep, ",*");
	}
      else
	{
	  if (*(search)[0] != '*')
	    *ep++ = '*';
	  ep = stpcpy (ep, *search);
	  if (ep[-1] != '*')
	    stpcpy (ep, "*");
	}

      while ((pi = collect_pkg_info (fp, &pi_buf, true)))
	{
	  /* Skip debuginfo packages */
	  if (PathMatchSpecA (pi->name, "*-debuginfo"))
	    continue;
	  if (selector)
	    {
	      /* search only curr version info for the dependency */
	      if (((selector & SRCH_REQS) && pi->curr.depends2
		   && PathMatchSpecA (pi->curr.depends2, ext_search))
		  || ((selector & SRCH_BLDREQS) && pi->curr.build_depends
		      && PathMatchSpecA (pi->curr.build_depends, ext_search)))
	      printf ("%s : %s\n", pi->name, pi->sdesc);
	    }
	  else if (PathMatchSpecA (pi->name, ext_search)
	      || (pi->sdesc && PathMatchSpecA (pi->sdesc, ext_search)))
	    printf ("%s : %s\n", pi->name, pi->sdesc);
	  free_pkg_info (&pi_buf);
	}
      free (ext_search);
      ++search;
    }

  return 0;
}

/* Queries Cygwin web site for packages containing files matching a regexp.
   Return value is 1 if there was a problem, otherwise 0.  */
static int
package_grep (const char *search)
{
  /* construct the actual URL by escaping  */
  char *url = (char *) alloca (sizeof (grep_base_url) + strlen (ARCH_str)
			       + strlen (search) * 3);
  strcpy (url, grep_base_url);

  char *dest;
  for (dest = &url[sizeof (grep_base_url) - 1]; *search; search++)
    {
      if (isalnum (*search)
	  || memchr (safe_chars, *search, sizeof (safe_chars) - 1))
	{
	  *dest++ = *search;
	}
      else
	{
	  *dest++ = '%';
	  sprintf (dest, "%02x", (unsigned char) *search);
	  dest += 2;
	}
    }
  strcpy (dest, ARCH_str);

  return fetch_url (url, stdout);
}

static void __attribute__ ((__noreturn__))
usage (FILE * stream, int status)
{
  fprintf (stream, "\
Usage: cygcheck [-v] [-h] PROGRAM\n\
       cygcheck -c [-d] [PACKAGE]\n\
       cygcheck -s [-r] [-v] [-h]\n\
       cygcheck -k\n\
       cygcheck -f FILE [FILE]...\n\
       cygcheck -l [PACKAGE]...\n\
       cygcheck -i [--inst] [--curr] [--prev] [--test] [PATTERN]...\n\
       cygcheck -e [PATTERN]...\n\
       cygcheck -p REGEXP\n\
       cygcheck --delete-orphaned-installation-keys\n\
       cygcheck -h\n\n\
List system information, check installed packages, or query package database.\n\
\n\
At least one command option or a PROGRAM is required, as shown above.\n\
\n\
  PROGRAM              list library (DLL) dependencies of PROGRAM\n\
  -c, --check-setup    show installed version of PACKAGE and verify integrity\n\
                       (or for all installed packages if none specified)\n\
  -d, --dump-only      do not verify packages (with -c)\n\
  -n, --names-only     just list package names (implies -c -d)\n\
  -s, --sysinfo        produce diagnostic system information (implies -c)\n\
  -r, --registry       also scan registry for Cygwin settings (with -s)\n\
  -k, --keycheck       perform a keyboard check session (must be run from a\n\
                       plain console only, not from a pty/rxvt/xterm)\n\
  -e, --search-package list all available packages matching PATTERN\n\
                       PATTERN is a glob pattern with * and ? as wildcard chars\n\
      search selection specifiers (multiple allowed):\n\
      --requires       list packages depending on packages matching PATTERN\n\
      --build-reqs     list packages depending on packages matching PATTERN\n\
                       when building these packages\n\
                       only the most recent available releases are checked\n\
                       to collect requirements info\n\
  -i, --info-package   print full info on packages matching PATTERN, installed\n\
                       and available releases\n\
                       PATTERN is a glob pattern with * and ? as wildcard chars\n\
      info selection specifiers (multiple allowed):\n\
      --inst           only print info on installed package release\n\
      --curr           only print info on most recent available release\n\
      --prev           only print info on older, still available releases\n\
      --test           only print info on test releases\n\
      --deps           additionally print package dependencies\n\
      --build-deps     additionally print package build dependencies\n\
  -f, --find-package   find the installed package to which FILE belongs\n\
  -l, --list-package   list contents of the installed PACKAGE (or all\n\
                       installed packages if none given)\n\
  -p, --package-query  search for REGEXP in the entire cygwin.com package\n\
                       repository (requires internet connectivity)\n\
  --delete-orphaned-installation-keys\n\
                       Delete installation keys of old, now unused\n\
                       installations from the registry.  Requires the right\n\
                       to change the registry.\n\
  -v, --verbose        produce more verbose output\n\
  -h, --help           annotate output with explanatory comments when given\n\
                       with another command, otherwise print this help\n\
  -V, --version        print the version of cygcheck and exit\n\
\n\
Notes:\n\
  -c, -f, and -l only report on packages that are currently installed.\n\
  -i and -e report on available packages, too.  To search for files within\n\
  uninstalled Cygwin packages, use -p.  The -p REGEXP matches package names,\n\
  descriptions, and names of files/paths within all packages.\n\
\n");
  exit (status);
}

struct option longopts[] = {
  {"check-setup", no_argument, NULL, 'c'},
  {"dump-only", no_argument, NULL, 'd'},
  {"names-only", no_argument, NULL, 'n'},
  {"sysinfo", no_argument, NULL, 's'},
  {"registry", no_argument, NULL, 'r'},
  {"verbose", no_argument, NULL, 'v'},
  {"keycheck", no_argument, NULL, 'k'},
  {"find-package", no_argument, NULL, 'f'},
  {"list-package", no_argument, NULL, 'l'},
  {"info-packages", no_argument, NULL, 'i'},
  {"inst", no_argument, NULL, 0x1001},
  {"curr", no_argument, NULL, 0x1002},
  {"prev", no_argument, NULL, 0x1004},
  {"test", no_argument, NULL, 0x1008},
  {"deps", no_argument, NULL, 0x1010},
  {"build-deps", no_argument, NULL, 0x1020},
  {"requires", no_argument, NULL, 0x1040},
  {"build-reqs", no_argument, NULL, 0x1080},
  {"search-packages", no_argument, NULL, 'e'},
  {"package-query", no_argument, NULL, 'p'},
  {"delete-orphaned-installation-keys", no_argument, NULL, CO_DELETE_KEYS},
  {"help", no_argument, NULL, 'h'},
  {"version", no_argument, 0, 'V'},
  {0, no_argument, NULL, 0}
};

static char opts[] = "cdnsrvkfliephV";

static void
print_version ()
{
  printf ("cygcheck (cygwin) %d.%d.%d\n"
	  "System Checker for Cygwin\n"
	  "Copyright (C) 1998 - %s Cygwin Authors\n"
	  "This is free software; see the source for copying conditions.  "
	  "There is NO\n"
	  "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR "
	  "PURPOSE.\n",
	  CYGWIN_VERSION_DLL_MAJOR / 1000,
	  CYGWIN_VERSION_DLL_MAJOR % 1000,
	  CYGWIN_VERSION_DLL_MINOR,
	  strrchr (__DATE__, ' ') + 1);
}

void
nuke (char *ev)
{
  int n = 1 + strchr (ev, '=') - ev;
  char *s = (char *) malloc (n + 1);
  memcpy (s, ev, n);
  s[n] = '\0';
  putenv (s);
}

static void
load_cygwin (int& argc, char **&argv)
{
  HMODULE h;

  if (!(h = LoadLibrary ("cygwin1.dll")))
    return;
  GetModuleFileNameW (h, cygwin_dll_path, 32768);
  if ((cygwin_internal = (uintptr_t (*) (cygwin_getinfo_types, ...))
			 GetProcAddress (h, "cygwin_internal")))
    {
      char **av = (char **) cygwin_internal (CW_ARGV);
      if (av && ((uintptr_t) av != (uintptr_t) -1))
	{
	  /* Copy cygwin's idea of the argument list into this Window
	     application. */
	  for (argc = 0; av[argc]; argc++)
	    continue;
	  argv = (char **) calloc (argc + 1, sizeof (char *));
	  for (char **argvp = argv; *av; av++)
	    *argvp++ = strdup (*av);
	}


      char **envp = (char **) cygwin_internal (CW_ENVP);
      if (envp && ((uintptr_t) envp != (uintptr_t) -1))
	{
	  /* Store path and revert to this value, otherwise path gets
	     overwritten by the POSIXy Cygwin variation, which breaks cygcheck.
	     Another approach would be to use the Cygwin PATH and convert it to
	     Win32 again. */
	  char *path = NULL;
	  char **env;
	  while (*(env = _environ))
	    {
	      if (strncmp (*env, "PATH=", 5) == 0)
		path = strdup (*env);
	      nuke (*env);
	    }
	  for (char **ev = envp; *ev; ev++)
	    if (strncmp (*ev, "PATH=", 5) != 0)
	      putenv (strdup (*ev));
	  if (path)
	    putenv (path);
	}
    }

  /* GDB chokes when the DLL got unloaded and, for some reason, fails to set
     any breakpoint after the fact. */
  if (!IsDebuggerPresent ())
    FreeLibrary (h);
}

int
main (int argc, char **argv)
{
  int i;
  bool ok = true;
  load_cygwin (argc, argv);

  _setmode (1, _O_BINARY);
  _setmode (2, _O_BINARY);

  /* Need POSIX sorting while parsing args, but don't forget the
     user's original environment.  */
  char *posixly = getenv ("POSIXLY_CORRECT");
  if (posixly == NULL)
    (void) putenv ("POSIXLY_CORRECT=1");
  while ((i = getopt_long (argc, argv, opts, longopts, NULL)) != EOF)
    switch (i)
      {
      case 's':
	sysinfo = 1;
	break;
      case 'c':
	check_setup = 1;
	break;
      case 'd':
	dump_only = 1;
	break;
      case 'n':
	check_setup = 1;
	dump_only = 1;
	names_only = 1;
	break;
      case 'r':
	registry = 1;
	break;
      case 'v':
	verbose = 1;
	break;
      case 'k':
	keycheck = 1;
	break;
      case 'f':
	find_package = 1;
	break;
      case 'l':
	list_package = 1;
	break;
      case 'i':
	info_packages = 1;
	break;
      case 0x1001:
      case 0x1002:
      case 0x1004:
      case 0x1008:
      case 0x1010:
      case 0x1020:
	info_selector |= (i & 0x3f);
	break;
      case 'e':
	search_packages = 1;
	break;
      case 0x1040:
      case 0x1080:
	search_selector |= (i & 0xc0);
	break;
      case 'p':
	grep_packages = 1;
	break;
      case 'h':
	givehelp = 1;
	break;
      case CO_DELETE_KEYS:
	del_orphaned_reg = 1;
	break;
      case 'V':
	print_version ();
	exit (0);
      default:
	fprintf (stderr, "Try `cygcheck --help' for more information.\n");
	exit (1);
       /*NOTREACHED*/
    }
  argc -= optind;
  argv += optind;
  if (posixly == NULL)
    putenv ("POSIXLY_CORRECT=");

  if ((argc == 0) && !sysinfo && !keycheck && !check_setup && !list_package
      && !del_orphaned_reg && !info_packages && !search_packages)
    {
      if (givehelp)
	usage (stdout, 0);
      else
	usage (stderr, 1);
    }

  if ((check_setup || sysinfo || find_package || list_package || grep_packages
       || del_orphaned_reg || info_packages || search_packages)
      && keycheck)
    usage (stderr, 1);

  if ((find_package || list_package || grep_packages
       || info_packages || search_packages)
      && (check_setup || del_orphaned_reg))
    usage (stderr, 1);

  if (dump_only && !check_setup && !sysinfo)
    usage (stderr, 1);

  if (find_package + list_package + grep_packages
      + info_packages + search_packages > 1)
    usage (stderr, 1);

  if (!info_packages && info_selector)
    usage (stderr, 1);

  if (!search_packages && search_selector)
    usage (stderr, 1);

  if (keycheck)
    return check_keys ();
  if (del_orphaned_reg)
    del_orphaned_reg_installations ();
  if (grep_packages)
    return package_grep (*argv);
  if (info_packages)
    return package_info (argv, info_selector);
  if (search_packages)
    return package_search (argv, search_selector);

  init_paths ();

  /* FIXME: Add help for check_setup and {list,find}_package */
  if (argc >= 1 && givehelp && !check_setup && !find_package && !list_package)
    {
      printf("Here is where the OS will find your program%s, and which dlls\n",
	     argc > 1 ? "s" : "");
      printf ("will be used for it.  Use -v to see DLL version info\n");

      if (!sysinfo)
	printf ("\n");
    }

  if (check_setup)
    dump_setup (verbose, argv, !dump_only, names_only);
  else if (find_package)
    package_find (verbose, argv);
  else if (list_package)
    package_list (verbose, argv);
  else
    for (i = 0; i < argc; i++)
      {
	if (i)
	  puts ("");
       ok &= cygcheck (argv[i]);
      }

  if (sysinfo)
    {
      dump_sysinfo ();
      if (!check_setup)
	{
	  puts ("");
	  dump_setup (verbose, NULL, !dump_only, FALSE);
	}

      if (!givehelp)
	puts ("Use -h to see help about each section");
    }

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
