/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 * Reorganised mar-01 nzc
 * Some really low-level file thingies.
 */

/** \file blender/blenlib/intern/storage.c
 *  \ingroup bli
 */


#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef WIN32
#include <dirent.h>
#endif

#include <time.h>
#include <sys/stat.h>

#if defined(__sun__) || defined(__sun) || defined(__NetBSD__)
#include <sys/statvfs.h> /* Other modern unix os's should probably use this also */
#elif !defined(__FreeBSD__) && !defined(linux) && (defined(__sparc) || defined(__sparc__))
#include <sys/statfs.h>
#endif

#if defined(__FreeBSD__) || defined(__OpenBSD__)
#include <sys/param.h>
#include <sys/mount.h>
#endif

#if defined(linux) || defined(__CYGWIN32__) || defined(__hpux) || defined(__GNU__) || defined(__GLIBC__)
#include <sys/vfs.h>
#endif

#ifdef __APPLE__
/* For statfs */
#include <sys/param.h>
#include <sys/mount.h>
#endif /* __APPLE__ */


#include <fcntl.h>
#include <string.h>  /* strcpy etc.. */

#ifdef WIN32
#  include <io.h>
#  include <direct.h>
#  include "BLI_winstuff.h"
#  include "utfconv.h"
#else
#  include <sys/ioctl.h>
#  include <unistd.h>
#  include <pwd.h>
#endif

/* lib includes */
#include "MEM_guardedalloc.h"

#include "DNA_listBase.h"

#include "BLI_listbase.h"
#include "BLI_linklist.h"
#include "BLI_string.h"
#include "BLI_fileops.h"
#include "BLI_fileops_types.h"
#include "BLI_path_util.h"

/* vars: */
static int totnum, actnum;
static struct direntry *files;

static struct ListBase dirbase_ = {NULL, NULL};
static struct ListBase *dirbase = &dirbase_;

/* can return NULL when the size is not big enough */
char *BLI_current_working_dir(char *dir, const size_t maxncpy)
{
	const char *pwd = getenv("PWD");
	if (pwd) {
		BLI_strncpy(dir, pwd, maxncpy);
		return dir;
	}

	return getcwd(dir, maxncpy);
}


static int bli_compare(struct direntry *entry1, struct direntry *entry2)
{
	/* type is equal to stat.st_mode */

	if (S_ISDIR(entry1->type)) {
		if (S_ISDIR(entry2->type) == 0) return (-1);
	}
	else {
		if (S_ISDIR(entry2->type)) return (1);
	}
	if (S_ISREG(entry1->type)) {
		if (S_ISREG(entry2->type) == 0) return (-1);
	}
	else {
		if (S_ISREG(entry2->type)) return (1);
	}
	if ((entry1->type & S_IFMT) < (entry2->type & S_IFMT)) return (-1);
	if ((entry1->type & S_IFMT) > (entry2->type & S_IFMT)) return (1);
	
	/* make sure "." and ".." are always first */
	if (strcmp(entry1->relname, ".") == 0) return (-1);
	if (strcmp(entry2->relname, ".") == 0) return (1);
	if (strcmp(entry1->relname, "..") == 0) return (-1);
	if (strcmp(entry2->relname, "..") == 0) return (1);

	return (BLI_natstrcmp(entry1->relname, entry2->relname));
}


double BLI_dir_free_space(const char *dir)
{
#ifdef WIN32
	DWORD sectorspc, bytesps, freec, clusters;
	char tmp[4];
	
	tmp[0] = '\\'; tmp[1] = 0; /* Just a failsafe */
	if (dir[0] == '/' || dir[0] == '\\') {
		tmp[0] = '\\';
		tmp[1] = 0;
	}
	else if (dir[1] == ':') {
		tmp[0] = dir[0];
		tmp[1] = ':';
		tmp[2] = '\\';
		tmp[3] = 0;
	}

	GetDiskFreeSpace(tmp, &sectorspc, &bytesps, &freec, &clusters);

	return (double) (freec * bytesps * sectorspc);
#else

#if defined(__sun__) || defined(__sun) || defined(__NetBSD__)
	struct statvfs disk;
#else
	struct statfs disk;
#endif
	char name[FILE_MAXDIR], *slash;
	int len = strlen(dir);
	
	if (len >= FILE_MAXDIR) /* path too long */
		return -1;
	
	strcpy(name, dir);

	if (len) {
		slash = strrchr(name, '/');
		if (slash) slash[1] = 0;
	}
	else strcpy(name, "/");

#if defined(__FreeBSD__) || defined(linux) || defined(__OpenBSD__) || defined(__APPLE__) || defined(__GNU__) || defined(__GLIBC__)
	if (statfs(name, &disk)) return(-1);
#endif

#if defined(__sun__) || defined(__sun) || defined(__NetBSD__)
	if (statvfs(name, &disk)) return(-1);
#elif !defined(__FreeBSD__) && !defined(linux) && (defined(__sparc) || defined(__sparc__))
	/* WARNING - This may not be supported by geeneric unix os's - Campbell */
	if (statfs(name, &disk, sizeof(struct statfs), 0)) return(-1);
#endif

	return ( ((double) disk.f_bsize) * ((double) disk.f_bfree));
#endif
}

static void bli_builddir(const char *dirname, const char *relname)
{
	struct dirent *fname;
	struct dirlink *dlink;
	int rellen, newnum = 0;
	char buf[256];
	DIR *dir;

	BLI_strncpy(buf, relname, sizeof(buf));
	rellen = strlen(relname);

	if (rellen) {
		buf[rellen] = '/';
		rellen++;
	}
#ifndef WIN32
	if (chdir(dirname) == -1) {
		perror(dirname);
		return;
	}
#else
	UTF16_ENCODE(dirname);
	if (!SetCurrentDirectoryW(dirname_16)) {
		perror(dirname);
		free(dirname_16);
		return;
	}
	UTF16_UN_ENCODE(dirname);

#endif
	if ((dir = (DIR *)opendir("."))) {
		while ((fname = (struct dirent *) readdir(dir)) != NULL) {
			dlink = (struct dirlink *)malloc(sizeof(struct dirlink));
			if (dlink) {
				BLI_strncpy(buf + rellen, fname->d_name, sizeof(buf) - rellen);
				dlink->name = BLI_strdup(buf);
				BLI_addhead(dirbase, dlink);
				newnum++;
			}
		}
		
		if (newnum) {

			if (files) {
				void *tmp = realloc(files, (totnum + newnum) * sizeof(struct direntry));
				if (tmp) {
					files = (struct direntry *)tmp;
				}
				else { /* realloc fail */
					free(files);
					files = NULL;
				}
			}
			
			if (files == NULL)
				files = (struct direntry *)malloc(newnum * sizeof(struct direntry));

			if (files) {
				dlink = (struct dirlink *) dirbase->first;
				while (dlink) {
					memset(&files[actnum], 0, sizeof(struct direntry));
					files[actnum].relname = dlink->name;
					files[actnum].path = BLI_strdupcat(dirname, dlink->name);
// use 64 bit file size, only needed for WIN32 and WIN64. 
// Excluding other than current MSVC compiler until able to test
#ifdef WIN32
					{
						wchar_t *name_16 = alloc_utf16_from_8(dlink->name, 0);
#if (defined(WIN32) || defined(WIN64)) && (_MSC_VER >= 1500)
						_wstat64(name_16, &files[actnum].s);
#elif defined(__MINGW32__)
						_stati64(dlink->name, &files[actnum].s);
#endif
						free(name_16);
					}

#else
					stat(dlink->name, &files[actnum].s);
#endif
					files[actnum].type = files[actnum].s.st_mode;
					files[actnum].flags = 0;
					totnum++;
					actnum++;
					dlink = dlink->next;
				}
			}
			else {
				printf("Couldn't get memory for dir\n");
				exit(1);
			}

			BLI_freelist(dirbase);
			if (files) qsort(files, actnum, sizeof(struct direntry), (int (*)(const void *, const void *))bli_compare);
		}
		else {
			printf("%s empty directory\n", dirname);
		}

		closedir(dir);
	}
	else {
		printf("%s non-existant directory\n", dirname);
	}
}

static void bli_adddirstrings(void)
{
	char datum[100];
	char buf[512];
	char size[250];
	static const char *types[8] = {"---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx"};
	int num, mode;
#ifdef WIN32
	__int64 st_size;
#else
	off_t st_size;
#endif
	
	struct direntry *file;
	struct tm *tm;
	time_t zero = 0;
	
	for (num = 0, file = files; num < actnum; num++, file++) {
#ifdef WIN32
		mode = 0;
		BLI_strncpy(file->mode1, types[0], sizeof(file->mode1));
		BLI_strncpy(file->mode2, types[0], sizeof(file->mode2));
		BLI_strncpy(file->mode3, types[0], sizeof(file->mode3));
#else
		mode = file->s.st_mode;

		BLI_strncpy(file->mode1, types[(mode & 0700) >> 6], sizeof(file->mode1));
		BLI_strncpy(file->mode2, types[(mode & 0070) >> 3], sizeof(file->mode2));
		BLI_strncpy(file->mode3, types[(mode & 0007)], sizeof(file->mode3));
		
		if (((mode & S_ISGID) == S_ISGID) && (file->mode2[2] == '-')) file->mode2[2] = 'l';

		if (mode & (S_ISUID | S_ISGID)) {
			if (file->mode1[2] == 'x') file->mode1[2] = 's';
			else file->mode1[2] = 'S';

			if (file->mode2[2] == 'x') file->mode2[2] = 's';
		}

		if (mode & S_ISVTX) {
			if (file->mode3[2] == 'x') file->mode3[2] = 't';
			else file->mode3[2] = 'T';
		}
#endif

#ifdef WIN32
		strcpy(file->owner, "user");
#else
		{
			struct passwd *pwuser;
			pwuser = getpwuid(file->s.st_uid);
			if (pwuser) {
				BLI_strncpy(file->owner, pwuser->pw_name, sizeof(file->owner));
			}
			else {
				BLI_snprintf(file->owner, sizeof(file->owner), "%d", file->s.st_uid);
			}
		}
#endif

		tm = localtime(&file->s.st_mtime);
		// prevent impossible dates in windows
		if (tm == NULL) tm = localtime(&zero);
		strftime(file->time, sizeof(file->time), "%H:%M", tm);
		strftime(file->date, sizeof(file->date), "%d-%b-%y", tm);

		/*
		 * Seems st_size is signed 32-bit value in *nix and Windows.  This
		 * will buy us some time until files get bigger than 4GB or until
		 * everyone starts using __USE_FILE_OFFSET64 or equivalent.
		 */
		st_size = file->s.st_size;

		if (st_size > 1024 * 1024 * 1024) {
			BLI_snprintf(file->size, sizeof(file->size), "%.2f GB", ((double)st_size) / (1024 * 1024 * 1024));
		}
		else if (st_size > 1024 * 1024) {
			BLI_snprintf(file->size, sizeof(file->size), "%.1f MB", ((double)st_size) / (1024 * 1024));
		}
		else if (st_size > 1024) {
			BLI_snprintf(file->size, sizeof(file->size), "%d KB", (int)(st_size / 1024));
		}
		else {
			BLI_snprintf(file->size, sizeof(file->size), "%d B", (int)st_size);
		}

		strftime(datum, 32, "%d-%b-%y %H:%M", tm); /* XXX, is this used? - campbell */

		if (st_size < 1000) {
			BLI_snprintf(size, sizeof(size), "%10d",
			             (int) st_size);
		}
		else if (st_size < 1000 * 1000) {
			BLI_snprintf(size, sizeof(size), "%6d %03d",
			             (int) (st_size / 1000), (int) (st_size % 1000));
		}
		else if (st_size < 100 * 1000 * 1000) {
			BLI_snprintf(size, sizeof(size), "%2d %03d %03d",
			             (int) (st_size / (1000 * 1000)), (int) ((st_size / 1000) % 1000), (int) (st_size % 1000));
		}
		else {
			/* XXX, whats going on here?. 2x calls - campbell */
			BLI_snprintf(size, sizeof(size), "> %4.1f M", (double) (st_size / (1024.0 * 1024.0)));
			BLI_snprintf(size, sizeof(size), "%10d", (int) st_size);
		}

		BLI_snprintf(buf, sizeof(buf), "%s %s %s %7s %s %s %10s %s",
		             file->mode1, file->mode2, file->mode3, file->owner,
		             file->date, file->time, size, file->relname);

		file->string = BLI_strdup(buf);
	}
}

unsigned int BLI_dir_contents(const char *dirname,  struct direntry **filelist)
{
	/* reset global variables
	 * memory stored in files is free()'d in
	 * filesel.c:freefilelist() */

	actnum = totnum = 0;
	files = NULL;

	bli_builddir(dirname, "");
	bli_adddirstrings();

	if (files) {
		*(filelist) = files;
	}
	else {
		// keep blender happy. Blender stores this in a variable
		// where 0 has special meaning.....
		*(filelist) = files = malloc(sizeof(struct direntry));
	}

	return(actnum);
}


size_t BLI_file_descriptor_size(int file)
{
	if (file > 0) {
		struct stat buf;

		fstat(file, &buf);

		return (buf.st_size);
	}
	else {
		return -1;
	}
}

size_t BLI_file_size(const char *path)
{
	int size, file = BLI_open(path, O_BINARY | O_RDONLY, 0);

	if (file == -1) {
		return -1;
	}

	size = BLI_file_descriptor_size(file);

	close(file);

	return size;
}


int BLI_exists(const char *name)
{
#if defined(WIN32) 
#ifndef __MINGW32__
	struct _stat64i32 st;
#else
	struct _stati64 st;
#endif
	/* in Windows stat doesn't recognize dir ending on a slash
	 * To not break code where the ending slash is expected we
	 * don't mess with the argument name directly here - elubie */
	wchar_t *tmp_16 = alloc_utf16_from_8(name, 0);
	int len, res;
	len = wcslen(tmp_16);
	if (len > 3 && (tmp_16[len - 1] == L'\\' || tmp_16[len - 1] == L'/') ) tmp_16[len - 1] = '\0';
#ifndef __MINGW32__
	res = _wstat(tmp_16, &st);
#else
	res = _wstati64(tmp_16, &st);
#endif
	free(tmp_16);
	if (res == -1) return(0);
#else
	struct stat st;
	if (stat(name, &st)) return(0);
#endif
	return(st.st_mode);
}


#ifdef WIN32
int BLI_stat(const char *path, struct stat *buffer)
{
	int r;
	UTF16_ENCODE(path);
	r = _wstat(path_16, buffer);
	UTF16_UN_ENCODE(path);
	return r;
}
#else
int BLI_stat(const char *path, struct stat *buffer)
{
	return stat(path, buffer);
}
#endif

/* would be better in fileops.c except that it needs stat.h so add here */
int BLI_is_dir(const char *file)
{
	return S_ISDIR(BLI_exists(file));
}

int BLI_is_file(const char *path)
{
	int mode = BLI_exists(path);
	return (mode && !S_ISDIR(mode));
}

LinkNode *BLI_file_read_as_lines(const char *name)
{
	FILE *fp = BLI_fopen(name, "r");
	LinkNode *lines = NULL;
	char *buf;
	size_t size;

	if (!fp) return NULL;
		
	fseek(fp, 0, SEEK_END);
	size = (size_t)ftell(fp);
	fseek(fp, 0, SEEK_SET);

	buf = MEM_mallocN(size, "file_as_lines");
	if (buf) {
		size_t i, last = 0;
		
		/*
		 * size = because on win32 reading
		 * all the bytes in the file will return
		 * less bytes because of crnl changes.
		 */
		size = fread(buf, 1, size, fp);
		for (i = 0; i <= size; i++) {
			if (i == size || buf[i] == '\n') {
				char *line = BLI_strdupn(&buf[last], i - last);

				BLI_linklist_prepend(&lines, line);
				last = i + 1;
			}
		}
		
		MEM_freeN(buf);
	}
	
	fclose(fp);
	
	BLI_linklist_reverse(&lines);
	return lines;
}

void BLI_file_free_lines(LinkNode *lines)
{
	BLI_linklist_free(lines, (void (*)(void *))MEM_freeN);
}

/** is file1 older then file2 */
int BLI_file_older(const char *file1, const char *file2)
{
#ifdef WIN32
	struct _stat st1, st2;

	UTF16_ENCODE(file1);
	UTF16_ENCODE(file2);
	
	if (_wstat(file1_16, &st1)) return 0;
	if (_wstat(file2_16, &st2)) return 0;

	UTF16_UN_ENCODE(file2);
	UTF16_UN_ENCODE(file1);
#else
	struct stat st1, st2;

	if (stat(file1, &st1)) return 0;
	if (stat(file2, &st2)) return 0;
#endif
	return (st1.st_mtime < st2.st_mtime);
}

