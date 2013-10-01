/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// common.h  -- general definitions

#ifndef TYPES_DEFINED
typedef unsigned char 		byte;
typedef unsigned short		word;
typedef unsigned long		dword;
#define TYPES_DEFINED 1
#endif

#undef true
#undef false

typedef enum {false, true}	qboolean;

#define stringify__(x) #x
#define stringify(x) stringify__(x)

//============================================================================

typedef struct sizebuf_s
{
	qboolean	allowoverflow;	// if false, do a Sys_Error
	qboolean	overflowed;		// set to true if the buffer size failed
	byte		*data;
	int			maxsize;
	int			cursize;
} sizebuf_t;

void SZ_Alloc (sizebuf_t *buf, int startsize);
void SZ_Free (sizebuf_t *buf);
void SZ_Clear (sizebuf_t *buf);
void *SZ_GetSpace (sizebuf_t *buf, int length);
void SZ_Write (sizebuf_t *buf, void *data, int length);
void SZ_Print (sizebuf_t *buf, char *data);	// strcats onto the sizebuf

//============================================================================

typedef struct link_s
{
	struct link_s	*prev, *next;
} link_t;

qboolean IsTimeout (float *prevtime, float waittime);

void ClearLink (link_t *l);
void RemoveLink (link_t *l);
void InsertLinkBefore (link_t *l, link_t *before);
void InsertLinkAfter (link_t *l, link_t *after);

// (type *)STRUCT_FROM_LINK(link_t *link, type, member)
// ent = STRUCT_FROM_LINK(link,entity_t,order)
// FIXME: remove this mess!
#define	STRUCT_FROM_LINK(l,t,m) ((t *)((byte *)l - (int)&(((t *)0)->m)))

//============================================================================

#ifndef NULL
#define NULL ((void *)0)
#endif

#define Q_MAXCHAR ((char)0x7f)
#define Q_MAXSHORT ((short)0x7fff)
#define Q_MAXINT	((int)0x7fffffff)
#define Q_MAXLONG ((int)0x7fffffff)
#define Q_MAXFLOAT ((int)0x7fffffff)

#define Q_MINCHAR ((char)0x80)
#define Q_MINSHORT ((short)0x8000)
#define Q_MININT 	((int)0x80000000)
#define Q_MINLONG ((int)0x80000000)
#define Q_MINFLOAT ((int)0x7fffffff)

//============================================================================

size_t Q_strnlen (const char *s, size_t maxlen);
#ifndef strnlen
#define strnlen Q_strnlen
#endif

//============================================================================

extern	qboolean		bigendian;

extern	short	(*BigShort) (short l);
extern	short	(*LittleShort) (short l);
extern	int	(*BigLong) (int l);
extern	int	(*LittleLong) (int l);
extern	float	(*BigFloat) (float l);
extern	float	(*LittleFloat) (float l);

//============================================================================

void MSG_WriteChar (sizebuf_t *sb, int c);
void MSG_WriteByte (sizebuf_t *sb, int c);
void MSG_WriteShort (sizebuf_t *sb, int c);
void MSG_WriteLong (sizebuf_t *sb, int c);
void MSG_WriteFloat (sizebuf_t *sb, float f);
void MSG_WriteString (sizebuf_t *sb, char *s);
void MSG_WriteCoord16 (sizebuf_t *sb, float f); //johnfitz -- original behavior, 13.3 fixed point coords, max range +-4096
void MSG_WriteCoord24 (sizebuf_t *sb, float f); //johnfitz -- 16.8 fixed point coords, max range +-32768
void MSG_WriteCoord32f (sizebuf_t *sb, float f); //johnfitz -- 32-bit float coords
void MSG_WriteCoord (sizebuf_t *sb, float f);
void MSG_WriteAngle (sizebuf_t *sb, float f);
void MSG_WritePreciseAngle (sizebuf_t *sb, float f); // precise aim for ProQuake
void MSG_WriteAngle16 (sizebuf_t *sb, float f); //johnfitz -- PROTOCOL_FITZQUAKE

typedef struct msg_s {
	int readcount;
	qboolean badread;		// set if a read goes beyond end of message
	sizebuf_t *message;
	size_t badread_string_size;
	char *badread_string;
} qmsg_t;

void MSG_BeginReading (qmsg_t *msg);
int MSG_ReadChar (qmsg_t *msg);
int MSG_ReadByte (qmsg_t *msg);
int MSG_ReadShort (qmsg_t *msg);
int MSG_ReadLong (qmsg_t *msg);
float MSG_ReadFloat (qmsg_t *msg);
char *MSG_ReadString (qmsg_t *msg);
float MSG_ReadCoord16 (qmsg_t *msg); //johnfitz -- original behavior, 13.3 fixed point coords, max range +-4096
float MSG_ReadCoord24 (qmsg_t *msg); //johnfitz -- 16.8 fixed point coords, max range +-32768
float MSG_ReadCoord32f (qmsg_t *msg); //johnfitz -- 32-bit float coords
float MSG_ReadCoord (qmsg_t *msg);
float MSG_ReadAngle (qmsg_t *msg);
float MSG_ReadPreciseAngle (qmsg_t *msg); // precise aim for ProQuake
float MSG_ReadAngle16 (qmsg_t *msg); //johnfitz -- PROTOCOL_FITZQUAKE

//============================================================================

extern	char		com_token[1024];
extern	qboolean	com_eof;

char *COM_Parse (char *data);


extern	int		com_argc;
extern	char	**com_argv;

int COM_CheckParm (char *parm);
void COM_Init (void);
void COM_InitArgv (int argc, char **argv);

char *COM_SkipPath (char *pathname);
void COM_StripExtension (char *in, char *out);
void COM_FileBase (char *in, char *out);
void COM_DefaultExtension (char *path, char *extension);

char	*va(char *format, ...);
// does a varargs printf into a temp buffer

//============================================================================

//
// file IO
//

// returns the file size
// return -1 if file is not present
// the file should be in BINARY mode for stupid OSs that care
int Sys_FileOpenRead (char *path, int *handle);

int Sys_FileOpenWrite (char *path);
void Sys_FileClose (int handle);
void Sys_FileSeek (int handle, int position);
int Sys_FileRead (int handle, void *dst, int count);
int Sys_FileWrite (int handle, void *src, int count);
int Sys_FileTime (char *path);

//tmp
int Sys_FileLength (FILE *f);

//============================================================================

extern int com_filesize;
struct cache_user_s;

extern	char	com_gamedir[MAX_OSPATH];
extern	char	com_basedir[MAX_OSPATH];
extern	char	com_savedir[MAX_OSPATH];

void COM_WriteFile (char *filename, void *data, int len);
int COM_OpenFile (char *filename, int *handle, unsigned int *path_id);
int COM_FOpenFile (char *filename, FILE **file, unsigned int *path_id);
void COM_CloseFile (int h);

byte *COM_LoadMallocFile (char *path, void *buffer, unsigned int *path_id);
byte *COM_LoadStackFile (char *path, void *buffer, int bufsize, unsigned int *path_id);
byte *COM_LoadTempFile (char *path, unsigned int *path_id);
byte *COM_LoadZoneFile (char *path, unsigned int *path_id);
byte *COM_LoadHunkFile (char *path, unsigned int *path_id);
void COM_LoadCacheFile (char *path, struct cache_user_s *cu, unsigned int *path_id);

void COM_CreatePath (char *path);

extern	struct cvar_s	registered;
extern	struct cvar_s	oem;

// This variable is set during file system init and determines Portals or non-Portals behavior of the game.
extern qboolean portals;
