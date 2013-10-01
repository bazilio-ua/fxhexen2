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

// gl_draw.c -- this is the only file outside the refresh that touches the vid buffer

#include "quakedef.h"

extern int ColorIndex[16];
extern unsigned ColorPercent[16];
//extern qboolean	vid_initialized;

#define MAX_DISC 18

cvar_t		scr_conalpha = {"scr_conalpha", "1", true};
cvar_t		gl_nobind = {"gl_nobind", "0"};
cvar_t		gl_max_size = {"gl_max_size", "2048"};
cvar_t		gl_round_down = {"gl_round_down", "0"};
cvar_t		gl_picmip = {"gl_picmip", "0"};
cvar_t		gl_swapinterval = {"gl_swapinterval", "0", true};
cvar_t		gl_warp_image_size = {"gl_warp_image_size", "256", true}; // was 512, for water warp

byte		*draw_chars;				// 8*8 graphic characters
byte		*draw_smallchars;			// Small characters for status bar
byte		*draw_menufont; 			// Big Menu Font
qpic_t		*draw_disc[MAX_DISC] = { NULL }; // make the first one null for sure
qpic_t		*draw_backtile;

int			translate_texture[MAX_PLAYER_CLASS];
int			char_texture;
int			char_smalltexture;
int			char_menufonttexture;

byte		conback_buffer[sizeof(qpic_t) + sizeof(glpic_t)];
qpic_t		*conback = (qpic_t *)&conback_buffer;

int		indexed_bytes = 1;
int		rgba_bytes = 4;
int		bgra_bytes = 4;

int		gl_lightmap_format = 4;
int		gl_solid_format = GL_RGB; // was 3
int		gl_alpha_format = GL_RGBA; // was 4

#define MAXGLMODES 6

typedef struct
{
	int magfilter;
	int minfilter;
	char *name;
} glmode_t;

glmode_t modes[MAXGLMODES] = {
	{GL_NEAREST, GL_NEAREST,				"GL_NEAREST"},
	{GL_NEAREST, GL_NEAREST_MIPMAP_NEAREST,	"GL_NEAREST_MIPMAP_NEAREST"},
	{GL_NEAREST, GL_NEAREST_MIPMAP_LINEAR,	"GL_NEAREST_MIPMAP_LINEAR"},
	{GL_LINEAR,  GL_LINEAR,					"GL_LINEAR"},
	{GL_LINEAR,  GL_LINEAR_MIPMAP_NEAREST,	"GL_LINEAR_MIPMAP_NEAREST"},
	{GL_LINEAR,  GL_LINEAR_MIPMAP_LINEAR,	"GL_LINEAR_MIPMAP_LINEAR"},
};

int		gl_texturemode = 3; // linear
int		gl_filter_min = GL_LINEAR; // was GL_NEAREST
int		gl_filter_mag = GL_LINEAR;

float	gl_hardware_max_anisotropy = 1; // just in case
float 	gl_texture_anisotropy = 1;

int		gl_hardware_max_size = 1024; // just in case
int		gl_texture_max_size = 1024;

int		gl_warpimage_size = 256; // fitzquake has 512, for water warp

#define MAX_GLTEXTURES	2048
gltexture_t	gltextures[MAX_GLTEXTURES];
int			numgltextures;


//GLuint currenttexture = (GLuint)-1; // to avoid unnecessary texture sets
GLenum TEXTURE0, TEXTURE1;
qboolean mtexenabled = false;

unsigned int d_8to24table[256]; // for GL renderer
unsigned int d_8to24table_fbright[256];
unsigned int d_8to24table_nobright[256];
unsigned int d_8to24table_conchars[256];
unsigned int d_8to24TranslucentTable[256];

const char *gl_vendor;
const char *gl_renderer;
const char *gl_version;
const char *gl_extensions;

qboolean fullsbardraw = false;
qboolean isIntel = false; // intel video workaround
qboolean gl_mtexable = false;
qboolean gl_texture_env_combine = false;
qboolean gl_texture_env_add = false;
qboolean gl_swap_control = false;

/*
================================================

			OpenGL Stuff

================================================
*/
 

/*
===============
GL_CheckExtensions
===============
*/
/*void CheckTextureExtensions (void)
{
	HINSTANCE	hInstGL;

	if (  COM_CheckParm ("-gl11") )
	{
		hInstGL = LoadLibrary("opengl32.dll");
		
		if (hInstGL == NULL)
			Sys_Error ("Couldn't load opengl32.dll\n");

		bindTexFunc = (void *)GetProcAddress(hInstGL,"glBindTexture");

		if (!bindTexFunc)
			Sys_Error ("No texture objects!");
		return;
	}

// load library and get procedure adresses for texture extension API /
	if ((bindTexFunc = (BINDTEXFUNCPTR)
		wglGetProcAddress((LPCSTR) "glBindTextureEXT")) == NULL)
	{
		Sys_Error ("GetProcAddress for BindTextureEXT failed");
		return;
	}
}*/

//int		texture_mode = GL_NEAREST;
//int		texture_mode = GL_NEAREST_MIPMAP_NEAREST;
//int		texture_mode = GL_NEAREST_MIPMAP_LINEAR;
int		texture_mode = GL_LINEAR;
//int		texture_mode = GL_LINEAR_MIPMAP_NEAREST;
//int		texture_mode = GL_LINEAR_MIPMAP_LINEAR;

int		texture_extension_number = 1;
/*
===============
GL_SetupState

does all the stuff from GL_Init that needs to be done every time a new GL render context is created
GL_Init will still do the stuff that only needs to be done once
===============
*/
void GL_SetupState (void)
{
	glClearColor (0.15,0.15,0.15,0); // originally was 1,0,0,0

	glCullFace (GL_BACK); // glquake used CCW with backwards culling -- let's do it right
	glFrontFace (GL_CW); // glquake used CCW with backwards culling -- let's do it right

	glEnable (GL_TEXTURE_2D);

	glEnable (GL_ALPHA_TEST);
	glAlphaFunc (GL_GREATER, 0.666);

	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel (GL_FLAT);

	glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE); 

	glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); // was GL_NEAREST
	glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // was GL_NEAREST
	glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glDepthRange (0, 1); // moved here because gl_ztrick is gone.
	glDepthFunc (GL_LEQUAL); // moved here because gl_ztrick is gone.
}

/*
===============
GL_Init
===============
*/
void GL_Init (void)
{
	gl_vendor = glGetString (GL_VENDOR);
	Con_Printf ("GL_VENDOR: %s\n", gl_vendor);
	gl_renderer = glGetString (GL_RENDERER);
	Con_Printf ("GL_RENDERER: %s\n", gl_renderer);

	gl_version = glGetString (GL_VERSION);
	Con_Printf ("GL_VERSION: %s\n", gl_version);
	gl_extensions = glGetString (GL_EXTENSIONS);
	Con_Printf ("GL_EXTENSIONS: %s\n", gl_extensions);

//	CheckTextureExtensions ();

	GL_SetupState ();
}


/*
================
GL_Bind
================
*/
void GL_Bind (int texnum)
{
	if (texnum != currenttexture)
	{
		currenttexture = texnum;
		glBindTexture (GL_TEXTURE_2D, currenttexture);
	}
}

/*
=============================================================================

  scrap allocation

  Allocate all the little status bar objects into a single texture
  to crutch up stupid hardware / drivers

=============================================================================
*/

#define MAX_SCRAPS		1
#define BLOCK_WIDTH		256
#define BLOCK_HEIGHT	256

int			scrap_allocated[MAX_SCRAPS][BLOCK_WIDTH];
byte		scrap_texels[MAX_SCRAPS][BLOCK_WIDTH*BLOCK_HEIGHT];
qboolean	scrap_dirty;
int			scrap_textures;


// returns a texture number and the position inside it
int Scrap_AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;
	int		texnum;

	for (texnum=0 ; texnum<MAX_SCRAPS ; texnum++)
	{
		best = BLOCK_HEIGHT;

		for (i=0 ; i<BLOCK_WIDTH-w ; i++)
		{
			best2 = 0;

			for (j=0 ; j<w ; j++)
			{
				if (scrap_allocated[texnum][i+j] >= best)
					break;
				if (scrap_allocated[texnum][i+j] > best2)
					best2 = scrap_allocated[texnum][i+j];
			}
			if (j == w)
			{	// this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > BLOCK_HEIGHT)
			continue;

		for (i=0 ; i<w ; i++)
			scrap_allocated[texnum][*x + i] = best + h;

		return texnum;
	}

	return -1;
}


void Scrap_Upload (void)
{
	char name[64];
	GL_Bind(scrap_textures);
	sprintf (name, "scrap");
	GL_Upload8 (name, scrap_texels[0], BLOCK_WIDTH, BLOCK_HEIGHT, false, true, 0);

	scrap_dirty = false;
}

//=============================================================================
/* Support Routines */

typedef struct cachepic_s
{
	char		name[MAX_QPATH];
	qpic_t		pic;
	byte		padding[32];	// for appended glpic
} cachepic_t;

#define MAX_CACHED_PICS 	256
cachepic_t	menu_cachepics[MAX_CACHED_PICS];
int			menu_numcachepics;

/*
 * Geometry for the player/skin selection screen image.
 */
#define PLAYER_PIC_WIDTH 68
#define PLAYER_PIC_HEIGHT 114
#define PLAYER_DEST_WIDTH 128
#define PLAYER_DEST_HEIGHT 128

byte		menuplyr_pixels[MAX_PLAYER_CLASS][PLAYER_PIC_WIDTH*PLAYER_PIC_HEIGHT];


qpic_t *Draw_PicFromWad (char *name)
{
	qpic_t	*p;
	glpic_t *gl;

	p = W_GetLumpName (name);

	// Sanity ...
	if (p->width & 0xC0000000 || p->height & 0xC0000000)
		Sys_Error ("Draw_PicFromWad: invalid dimensions (%dx%d) for '%s'", p->width, p->height, name);

	gl = (glpic_t *)p->data;

	// load little ones into the scrap
	if (p->width < 64 && p->height < 64)
	{
		int		x, y;
		int		i, j, k;
		int		texnum;

		texnum = Scrap_AllocBlock (p->width, p->height, &x, &y);
		if (texnum == -1)
			goto nonscrap;

		scrap_dirty = true;
		k = 0;
		for (i=0 ; i<p->height ; i++)
			for (j=0 ; j<p->width ; j++, k++)
				scrap_texels[texnum][(y+i)*BLOCK_WIDTH + x + j] = p->data[k];

		texnum += scrap_textures;
		gl->gltexture = texnum;
		// no longer go from 0.01 to 0.99
		gl->sl = x/(float)BLOCK_WIDTH;
		gl->sh = (x+p->width)/(float)BLOCK_WIDTH;
		gl->tl = y/(float)BLOCK_WIDTH;
		gl->th = (y+p->height)/(float)BLOCK_WIDTH;

	}
	else
	{
nonscrap:
		gl->gltexture = GL_LoadTexture (name, p->width, p->height, p->data, false, true, 0);
		gl->sl = 0;
		gl->sh = 1;
		gl->tl = 0;
		gl->th = 1;
	}
	return p;
}


qpic_t *Draw_PicFromFile (char *name)
{
	qpic_t	*p;
	glpic_t *gl;

	p = (qpic_t *)COM_LoadHunkFile (name, NULL);
	if (!p)
	{
		return NULL;
	}

	gl = (glpic_t *)p->data;

	{
		gl->gltexture = GL_LoadTexture (name, p->width, p->height, p->data, false, true, 0);
		gl->sl = 0;
		gl->sh = 1;
		gl->tl = 0;
		gl->th = 1;
	}
	return p;
}

/*
================
Draw_CachePic
================
*/
qpic_t	*Draw_CachePic (char *path)
{
	cachepic_t	*pic;
	int			i;
	qpic_t		*dat;
	glpic_t 	*gl;

	for (pic=menu_cachepics, i=0 ; i<menu_numcachepics ; pic++, i++)
		if (!strcmp (path, pic->name))
			return &pic->pic;

	if (menu_numcachepics == MAX_CACHED_PICS)
		Sys_Error ("menu_numcachepics == MAX_CACHED_PICS");
	menu_numcachepics++;
	strcpy (pic->name, path);

//
// load the pic from disk
//
	dat = (qpic_t *)COM_LoadTempFile (path, NULL);
	if (!dat)
		Sys_Error ("Draw_CachePic: failed to load %s", path);
	SwapPic (dat);

	// HACK HACK HACK --- we need to keep the bytes for
	// the translatable player picture just for the menu
	// configuration dialog
	/* garymct */
	if (!strcmp (path, "gfx/menu/netp1.lmp"))
		memcpy (menuplyr_pixels[0], dat->data, dat->width*dat->height);
	else if (!strcmp (path, "gfx/menu/netp2.lmp"))
		memcpy (menuplyr_pixels[1], dat->data, dat->width*dat->height);
	else if (!strcmp (path, "gfx/menu/netp3.lmp"))
		memcpy (menuplyr_pixels[2], dat->data, dat->width*dat->height);
	else if (!strcmp (path, "gfx/menu/netp4.lmp"))
		memcpy (menuplyr_pixels[3], dat->data, dat->width*dat->height);
	else if (!strcmp (path, "gfx/menu/netp5.lmp"))
		memcpy (menuplyr_pixels[4], dat->data, dat->width*dat->height);

	pic->pic.width = dat->width;
	pic->pic.height = dat->height;

	gl = (glpic_t *)pic->pic.data;
	gl->gltexture = GL_LoadTexture (path, dat->width, dat->height, dat->data, false, true, 0);
	gl->sl = 0;
	gl->sh = 1;
	gl->tl = 0;
	gl->th = 1;

	// point sample status bar
//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	return &pic->pic;
}

/*
================
Draw_CachePicNoTrans

Pa3PyX: Function added to cache pics ignoring transparent colors
(e.g. in intermission screens)
================
*/
qpic_t *Draw_CachePicNoTrans(char *path)
{
	cachepic_t      *pic;
	int                     i;
	qpic_t          *dat;
	glpic_t         *gl;

	for (pic=menu_cachepics, i=0 ; i<menu_numcachepics ; pic++, i++)
	{
		if (!strcmp (path, pic->name))
			return &pic->pic;
	}

	if (menu_numcachepics == MAX_CACHED_PICS)
		Sys_Error ("menu_numcachepics == MAX_CACHED_PICS");

	menu_numcachepics++;
	strcpy (pic->name, path);

//
// load the pic from disk
//
	dat = (qpic_t *)COM_LoadTempFile (path, NULL);
	if (!dat)
		Sys_Error ("Draw_CachePicNoTrans: failed to load %s", path);

	SwapPic (dat);

	pic->pic.width = dat->width;
	pic->pic.height = dat->height;

	gl = (glpic_t *)pic->pic.data;

	// Get rid of transparencies
	for (i = 0; i < dat->width * dat->height; i++)
	{
		if (dat->data[i] == 255)
			dat->data[i] = 31; // pal(31) == pal(255) == FCFCFC (white)
	}

	gl->gltexture = GL_LoadTexture (path, dat->width, dat->height, dat->data, false, true, 0);
	gl->sl = 0;
	gl->sh = 1;
	gl->tl = 0;
	gl->th = 1;

//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	return &pic->pic;
}

void Draw_CharToConback (int num, byte *dest)
{
	int		row, col;
	byte	*source;
	int		drawline;
	int		x;

	row = num>>5;
	col = num&31;
	source = draw_chars + (row<<11) + (col<<3);

	drawline = 8;

	while (drawline--)
	{
		for (x=0 ; x<8 ; x++)
			if (source[x] != 255)
				dest[x] = 0x60 + source[x];
		source += 256;
		dest += 320;
	}

}


/*
===============
Draw_TextureMode_f
===============
*/
void Draw_TextureMode_f (void)
{
	int		i;
	char *arg;
	gltexture_t	*glt;

	if (Cmd_Argc() == 1)
	{
		for (i=0 ; i< MAXGLMODES ; i++)
		{
			if (gl_filter_min == modes[i].minfilter)
			{
				Con_Printf ("gl_texturemode is %s (%d)\n", modes[i].name, i + 1);
				return;
			}
		}
	}

	for (i=0 ; i< MAXGLMODES ; i++)
	{
		arg = Cmd_Argv(1);
		if (!strcasecmp (modes[i].name, arg ) || (isdigit(*arg) && atoi(arg) - 1 == i))
			break;
	}
	if (i == MAXGLMODES)
	{
		Con_Printf ("bad filter name, available are:\n");
		for (i=0 ; i< MAXGLMODES ; i++)
			Con_Printf ("%s (%d)\n", modes[i].name, i + 1);
		return;
	}

	gl_texturemode = i;
	gl_filter_min = modes[gl_texturemode].minfilter;
	gl_filter_mag = modes[gl_texturemode].magfilter;

	// change all the existing texture objects
	for (i=0, glt=gltextures ; i<numgltextures ; i++, glt++)
	{
		if (glt->mipmap)
		{
			GL_Bind (glt->texnum);
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_mag);
		}
		else
		{
			GL_Bind (glt->texnum);
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_mag);
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_mag);
		}
	}
}

/*
===============
Pics_Upload
===============
*/
void Pics_Upload (void)
{
	int		i;
	qpic_t	*mf;
	char temp[MAX_QPATH];


	// load the console background and the charset
	// by hand, because we need to write the version
	// string into the background before turning
	// it into a texture
	//draw_chars = W_GetLumpName ("conchars");
	draw_chars = COM_LoadHunkFile ("gfx/menu/conchars.lmp", NULL);
	for (i=0 ; i<256*128 ; i++)
		if (draw_chars[i] == 0)
			draw_chars[i] = 255;	// proper transparent color

	char_texture = GL_LoadTexture ("charset", 256, 128, draw_chars, false, true, 0);
//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	//
	
	draw_smallchars = W_GetLumpName("tinyfont");
	for (i=0 ; i<128*32 ; i++)
		if (draw_smallchars[i] == 0)
			draw_smallchars[i] = 255;	// proper transparent color

	// now turn them into textures
	char_smalltexture = GL_LoadTexture ("smallcharset", 128, 32, draw_smallchars, false, true, 0);
//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	//
	
	mf = (qpic_t *)COM_LoadTempFile("gfx/menu/bigfont2.lmp", NULL);
//	mf = (qpic_t *)COM_LoadTempFile("gfx/menu/bigfont.lmp", NULL);
	for (i=0 ; i<160*80 ; i++)
		if (mf->data[i] == 0)
			mf->data[i] = 255;	// proper transparent color

	char_menufonttexture = GL_LoadTexture ("menufont", 160, 80, mf->data, false, true, 0);
//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_mag);
//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_mag);

	//
	
	//
	// get the other pics we need
	//
	for(i=MAX_DISC-1 ; i>=0 ; i--)
	{
		sprintf(temp, "gfx/menu/skull%d.lmp", i);
		draw_disc[i] = Draw_PicFromFile (temp);
	}

//	draw_disc = Draw_PicFromWad ("disc");
//	draw_backtile = Draw_PicFromWad ("backtile");
	draw_backtile = Draw_PicFromFile ("gfx/menu/backtile.lmp");

}

/*
===============
Draw_Init
===============
*/
void Draw_Init (void)
{

	Cvar_RegisterVariable (&gl_nobind, NULL);
	Cvar_RegisterVariable (&gl_max_size, NULL);
	Cvar_RegisterVariable (&gl_round_down, NULL);
	Cvar_RegisterVariable (&gl_picmip, NULL);

	Cmd_AddCommand ("gl_texturemode", &Draw_TextureMode_f);


	// save a texture slot for translated picture
	translate_texture[0] = texture_extension_number++;
	translate_texture[1] = texture_extension_number++;
	translate_texture[2] = texture_extension_number++;
	translate_texture[3] = texture_extension_number++;
	translate_texture[4] = texture_extension_number++;

	// save slots for scraps
	scrap_textures = texture_extension_number;
	texture_extension_number += MAX_SCRAPS;


	Pics_Upload ();
}

/*
===============
Draw_Crosshair
===============
*/
void Draw_Crosshair (void)
{
	if (!crosshair.value) 
		return;

//	Draw_Character (scr_vrect.x + scr_vrect.width/2 + cl_crossx.value, scr_vrect.y + scr_vrect.height/2 + cl_crossy.value, '+');
}

/*
================
Draw_CharacterQuad

seperate function to spit out verts
================
*/
void Draw_CharacterQuad (int x, int y, short num)
{
	int				row, col;
	float			frow, fcol, xsize, ysize;

	row = num>>5;
	col = num&31;

	xsize = 0.03125;
	ysize = 0.0625;
	fcol = col*xsize;
	frow = row*ysize;

	glTexCoord2f (fcol, frow);
	glVertex2f (x, y);
	glTexCoord2f (fcol + xsize, frow);
	glVertex2f (x+8, y);
	glTexCoord2f (fcol + xsize, frow + ysize);
	glVertex2f (x+8, y+8);
	glTexCoord2f (fcol, frow + ysize);
	glVertex2f (x, y+8);

}

/*
================
Draw_Character

Draws one 8*8 graphics character with 0 being transparent.
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.

modified to call Draw_CharacterQuad
================
*/
void Draw_Character (int x, int y, unsigned int num)
{
	if (y <= -8)
		return; 		// totally off screen

	num &= 511;

	if (num == 32)
		return; 	// don't waste verts on spaces

	GL_Bind (char_texture);
	glBegin (GL_QUADS);

	Draw_CharacterQuad (x, y, (short) num);

	glEnd ();
}

/*
================
Draw_String

modified to call Draw_CharacterQuad
================
*/
void Draw_String (int x, int y, char *str)
{
	if (y <= -8)
		return;			// totally off screen

	GL_Bind (char_texture);
	glBegin (GL_QUADS);

	while (*str)
	{
		if (*str != 32) // don't waste verts on spaces
			Draw_CharacterQuad (x, y, *str);
		str++;
		x += 8;
	}

	glEnd ();
}




/*
================
Draw_SmallCharacterQuad

seperate function to spit out verts
================
*/
void Draw_SmallCharacterQuad (int x, int y, char num)
{
	int				row, col;
	float			frow, fcol, xsize, ysize;

	row = num>>4;
	col = num&15;

	xsize = 0.0625;
	ysize = 0.25;
	fcol = col*xsize;
	frow = row*ysize;

	glTexCoord2f (fcol, frow);
	glVertex2f (x, y);
	glTexCoord2f (fcol + xsize, frow);
	glVertex2f (x+8, y);
	glTexCoord2f (fcol + xsize, frow + ysize);
	glVertex2f (x+8, y+8);
	glTexCoord2f (fcol, frow + ysize);
	glVertex2f (x, y+8);
}

/*
================
Draw_SmallCharacter

Draws a small character that is clipped at the bottom edge of the
screen.

modified to call Draw_SmallCharacterQuad
================
*/
void Draw_SmallCharacter (int x, int y, int num)
{
	if (y <= -8)
		return; 		// totally off screen

	if (y >= vid.height)
		return;			// totally off screen

	if (num < 32)
		num = 0;
	else if (num >= 'a' && num <= 'z')
		num -= 64;
	else if (num > '_')
		num = 0;
	else
		num -= 32;

	if (num == 0)
		return;

	GL_Bind (char_smalltexture);
	glBegin (GL_QUADS);

	Draw_SmallCharacterQuad (x, y, (char) num);

	glEnd ();
}

/*
================
Draw_SmallString

modified to call Draw_SmallCharacterQuad
================
*/
void Draw_SmallString (int x, int y, char *str)
{
	int num;

	if (y <= -8)
		return; 		// totally off screen

	if (y >= vid.height)
		return;			// totally off screen

	GL_Bind (char_smalltexture);
	glBegin (GL_QUADS);

	while (*str)
	{
		num = *str;

		if (num < 32)
			num = 0;
		else if (num >= 'a' && num <= 'z')
			num -= 64;
		else if (num > '_')
			num = 0;
		else
			num -= 32;

		if (num != 0)
			Draw_SmallCharacterQuad (x, y, (char) num);

		str++;
		x += 6;
	}

	glEnd ();
}


/*
=============
Draw_Pic
=============
*/
void Draw_Pic (int x, int y, qpic_t *pic)
{
	glpic_t 		*gl;

	if (scrap_dirty)
		Scrap_Upload ();

	gl = (glpic_t *)pic->data;

	glDisable (GL_ALPHA_TEST); //FX new
	glEnable (GL_BLEND); //FX
	glColor4f (1,1,1,1);
	GL_Bind (gl->gltexture);

//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

	glBegin (GL_QUADS);
	glTexCoord2f (gl->sl, gl->tl);
	glVertex2f (x, y);
	glTexCoord2f (gl->sh, gl->tl);
	glVertex2f (x+pic->width, y);
	glTexCoord2f (gl->sh, gl->th);
	glVertex2f (x+pic->width, y+pic->height);
	glTexCoord2f (gl->sl, gl->th);
	glVertex2f (x, y+pic->height);
	glEnd ();

	glEnable (GL_ALPHA_TEST); //FX new
	glDisable (GL_BLEND); //FX

//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

/*
=============
Draw_IntermissionPic

Pa3PyX: this new function introduced to draw the intermission screen only
=============
*/
void Draw_IntermissionPic (qpic_t *pic)
{
	glpic_t                 *gl;
	
	gl = (glpic_t *)pic->data;

	glDisable (GL_ALPHA_TEST); //FX new
	glEnable (GL_BLEND); //FX

	glColor4f (1,1,1,1);
	GL_Bind (gl->gltexture);
	
//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f);
	glVertex2f(0.0f, 0.0f);
	glTexCoord2f(1.0f, 0.0f);
	glVertex2f(vid.width, 0.0f);
	glTexCoord2f(1.0f, 1.0f);
	glVertex2f(vid.width, vid.height);
	glTexCoord2f(0.0f, 1.0f);
	glVertex2f(0.0f, vid.height);
	glEnd();

	glEnable (GL_ALPHA_TEST); //FX new
	glDisable (GL_BLEND); //FX

//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

void Draw_PicCropped(int x, int y, qpic_t *pic)
{
	int height;
	glpic_t 		*gl;
	float th,tl;

	if((x < 0) || (x+pic->width > (int)vid.width))
	{
		Sys_Error("Draw_PicCropped: bad coordinates");
	}

	if (y >= (int)vid.height || y+pic->height < 0)
	{ // Totally off screen
		return;
	}

	if (scrap_dirty)
		Scrap_Upload ();

	gl = (glpic_t *)pic->data;

	// rjr tl/th need to be computed based upon pic->tl and pic->th
	//     cuz the piece may come from the scrap
	if(y+pic->height > (int)vid.height)
	{
		height = vid.height-y;
		tl = 0;
		th = (height-0.01)/pic->height;
	}
	else if (y < 0)
	{
		height = pic->height+y;
		y = -y;
		tl = (y-0.01)/pic->height;
		th = (pic->height-0.01)/pic->height;
		y = 0;
	}
	else
	{
		height = pic->height;
		tl = gl->tl;
		th = gl->th;//(height-0.01)/pic->height;
	}

	glDisable (GL_ALPHA_TEST); //FX new
	glEnable (GL_BLEND); //FX

	glColor4f (1,1,1,1);
	GL_Bind (gl->gltexture);
	glBegin (GL_QUADS);
	glTexCoord2f (gl->sl, tl);
	glVertex2f (x, y);
	glTexCoord2f (gl->sh, tl);
	glVertex2f (x+pic->width, y);
	glTexCoord2f (gl->sh, th);
	glVertex2f (x+pic->width, y+height);
	glTexCoord2f (gl->sl, th);
	glVertex2f (x, y+height);
	glEnd ();

	glEnable (GL_ALPHA_TEST); //FX new
	glDisable (GL_BLEND); //FX
}

/*
=============
Draw_TransPic
=============
*/
void Draw_TransPic (int x, int y, qpic_t *pic)
{

	if (x < 0 || (x + pic->width) > vid.width || y < 0 || (y + pic->height) > vid.height)
	{
		Sys_Error ("Draw_TransPic: bad coordinates (%d, %d)", x, y);
	}

	Draw_Pic (x, y, pic);
}

void Draw_TransPicCropped(int x, int y, qpic_t *pic)
{
	Draw_PicCropped (x, y, pic);
}

/*
=============
Draw_TransPicTranslate

Only used for the player color selection menu
=============
*/
void Draw_TransPicTranslate (int x, int y, qpic_t *pic, byte *translation)
{
	byte		trans[PLAYER_DEST_WIDTH * PLAYER_DEST_HEIGHT];
	byte			*src, *dst;
	byte	*data;
	char	name[64];
	int i, j;


	data = menuplyr_pixels[setup_class-1];
	sprintf (name, "gfx/menu/netp%d.lmp", setup_class);

	dst = trans;
	src = data;

	for( i = 0; i < PLAYER_PIC_WIDTH; i++ )
	{
		for( j = 0; j < PLAYER_PIC_HEIGHT; j++ )
		{
			dst[j * PLAYER_DEST_WIDTH + i] = translation[src[j * PLAYER_PIC_WIDTH + i]];
		}
	}

	data = trans;

	GL_Bind (translate_texture[setup_class-1]);
	GL_Upload8 (name, data, PLAYER_DEST_WIDTH, PLAYER_DEST_HEIGHT, false, true, 0); 


	glColor3f (1,1,1);
	glBegin (GL_QUADS);
	glTexCoord2f (0, 0);
	glVertex2f (x, y);
	glTexCoord2f (( float )PLAYER_PIC_WIDTH / PLAYER_DEST_WIDTH, 0);
	glVertex2f (x+pic->width, y);
	glTexCoord2f (( float )PLAYER_PIC_WIDTH / PLAYER_DEST_WIDTH, ( float )PLAYER_PIC_HEIGHT / PLAYER_DEST_HEIGHT);
	glVertex2f (x+pic->width, y+pic->height);
	glTexCoord2f (0, ( float )PLAYER_PIC_HEIGHT / PLAYER_DEST_HEIGHT);
	glVertex2f (x, y+pic->height);
	glEnd ();
}



/*
================
Draw_BigCharacterQuad

seperate function to spit out verts
================
*/
void Draw_BigCharacterQuad (int x, int y, char num)
{
	int				row, col;
	float			frow, fcol, xsize, ysize;

	row = num/8;
	col = num%8;

	xsize = 0.125;
	ysize = 0.25;
	fcol = col*xsize;
	frow = row*ysize;

	glTexCoord2f (fcol, frow);
	glVertex2f (x, y);
	glTexCoord2f (fcol + xsize, frow);
	glVertex2f (x+20, y);
	glTexCoord2f (fcol + xsize, frow + ysize);
	glVertex2f (x+20, y+20);
	glTexCoord2f (fcol, frow + ysize);
	glVertex2f (x, y+20);
}

int M_DrawBigCharacter (int x, int y, int num, int numnext)
{
	int				add;

	if (num == ' ')
		return 32;

	if (num == '/')
		num = 26;
	else
		num -= 65;

	if (num < 0 || num >= 27)  // only a-z and /
		return 0;

	if (numnext == '/')
		numnext = 26;
	else
		numnext -= 65;

	GL_Bind (char_menufonttexture);
	glBegin (GL_QUADS);

	Draw_BigCharacterQuad (x, y, (char) num);

	glEnd ();

	if (numnext < 0 || numnext >= 27)
		return 0;

	add = 0;
	if (num == (int)'C'-65 && numnext == (int)'P'-65)
		add = 3;

	return BigCharWidth[num][numnext] + add;
}

/*
================
Draw_ConsoleBackground

================
*/
void Draw_ConsoleBackground (int lines)
{
	qpic_t	*cb;
	byte	*dest;
	int		x, y;
	char	ver[40];
	glpic_t *gl;


	cb = (qpic_t *)COM_LoadTempFile ("gfx/menu/conback.lmp", NULL);
	if (!cb)
		Sys_Error ("Couldn't load gfx/menu/conback.lmp");
	SwapPic (cb);

	// hack the version number directly into the pic
	dest = cb->data + 320 - 43 + 320*186;
	sprintf (ver, "%4.2f", VERSION);


	y = strlen(ver);
	for (x=0 ; x<y ; x++)
		Draw_CharToConback (ver[x], dest+(x<<3));

	gl = (glpic_t *)conback->data;
	gl->gltexture = GL_LoadTexture ("conback", cb->width, cb->height, cb->data, false, false, 0);
	gl->sl = 0;
	gl->sh = 1;
	gl->tl = 0;
	gl->th = 1;
	conback->width = vid.conwidth;
	conback->height = vid.conheight;


	Draw_Pic (0, lines-vid.height, conback);
}


/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear (int x, int y, int w, int h)
{
	glpic_t	*gl;

	gl = (glpic_t *)draw_backtile->data;

	glDisable (GL_ALPHA_TEST); //FX new
	glEnable (GL_BLEND); //FX
//	glColor3f (1,1,1);
	glColor4f (1,1,1,1); //FX new

	GL_Bind (gl->gltexture);
	glBegin (GL_QUADS);
	glTexCoord2f (x/64.0, y/64.0);
	glVertex2f (x, y);
	glTexCoord2f ( (x+w)/64.0, y/64.0);
	glVertex2f (x+w, y);
	glTexCoord2f ( (x+w)/64.0, (y+h)/64.0);
	glVertex2f (x+w, y+h);
	glTexCoord2f ( x/64.0, (y+h)/64.0 );
	glVertex2f (x, y+h);
	glEnd ();
	glEnable (GL_ALPHA_TEST); //FX new
	glDisable (GL_BLEND); //FX
}


/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c)
{
	byte *pal = (byte *)d_8to24table; // use d_8to24table instead of host_basepal
	float alpha = 1.0;

	glDisable (GL_TEXTURE_2D);
	glEnable (GL_BLEND); // for alpha
	glDisable (GL_ALPHA_TEST); // for alpha
	glColor4f (pal[c*4]/255.0, pal[c*4+1]/255.0, pal[c*4+2]/255.0, alpha); // added alpha

	glBegin (GL_QUADS);
	glVertex2f (x, y);
	glVertex2f (x+w, y);
	glVertex2f (x+w, y+h);
	glVertex2f (x, y+h);
	glEnd ();

	glColor3f (1,1,1);
	glDisable (GL_BLEND); // for alpha
	glEnable (GL_ALPHA_TEST); // for alpha
	glEnable (GL_TEXTURE_2D);
}
//=============================================================================

/*
================
Draw_FadeScreen

================
*/
void Draw_FadeScreen (void)
{
	int bx,by,ex,ey;
	int c;

	glAlphaFunc(GL_ALWAYS, 0);

	glEnable (GL_BLEND);
	glDisable (GL_TEXTURE_2D);

//	glColor4f (248.0/255.0, 220.0/255.0, 120.0/255.0, 0.2);
	glColor4f (208.0/255.0, 180.0/255.0, 80.0/255.0, 0.2);
	glBegin (GL_QUADS);
	glVertex2f (0,0);
	glVertex2f (vid.width, 0);
	glVertex2f (vid.width, vid.height);
	glVertex2f (0, vid.height);
	glEnd ();

	glColor4f (208.0/255.0, 180.0/255.0, 80.0/255.0, 0.035);
	for(c=0;c<40;c++)
	{
		bx = rand() % vid.width-20;
		by = rand() % vid.height-20;
		ex = bx + (rand() % 40) + 20;
		ey = by + (rand() % 40) + 20;
		if (bx < 0) bx = 0;
		if (by < 0) by = 0;
		if (ex > (int)vid.width) ex = (int)vid.width;
		if (ey > (int)vid.height) ey = (int)vid.height;

		glBegin (GL_QUADS);
		glVertex2f (bx,by);
		glVertex2f (ex, by);
		glVertex2f (ex, ey);
		glVertex2f (bx, ey);
		glEnd ();
	}

	glColor4f (1,1,1,1);
	glEnable (GL_TEXTURE_2D);
	glDisable (GL_BLEND);

	glAlphaFunc(GL_GREATER, 0.666);

	Sbar_Changed();
}

//=============================================================================

/*
================
Draw_BeginDisc

Draws the little blue disc in the corner of the screen.
Call before beginning any disc IO.
================
*/
void Draw_BeginDisc (void)
{
	static int index = 0;

	if (!draw_disc[index] || loading_stage)
		return;

	index++;
	if (index >= MAX_DISC)
		index = 0;

//	glDrawBuffer  (GL_FRONT);

	Draw_Pic (vid.width - 28, 0, draw_disc[index]);

//	glDrawBuffer  (GL_BACK);
}

/*
================
Draw_EndDisc

Erases the disc icon.
Call after completing any disc IO
================
*/
void Draw_EndDisc (void)
{
}

/*
================
GL_Set2D

Setup as if the screen was 320*200
================
*/
void GL_Set2D (void)
{
	//
	// set up viewpoint
	//
	glViewport (glx, gly, glwidth, glheight);

	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();

	glOrtho  (0, glwidth, glheight, 0, -99999, 99999);

	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();

	//
	// set drawing parms
	//
	glDisable (GL_DEPTH_TEST);
	glDisable (GL_CULL_FACE);
	glDisable (GL_BLEND);
	glEnable (GL_ALPHA_TEST);

	glColor4f (1,1,1,1);
}

//====================================================================

/*
================
GL_FindTexture
================
*/
int GL_FindTexture (char *identifier)
{
	int		i;
	gltexture_t	*glt;

	for (i=0, glt=gltextures ; i<numgltextures ; i++, glt++)
	{
		if (!strcmp (identifier, glt->identifier))
			return gltextures[i].texnum;
	}

	return -1;
}

/*
================
GL_ResampleTexture
================
*/
static void ResampleTextureQuality (unsigned *in, int inwidth, int inheight, unsigned *out,  int outwidth, int outheight, qboolean alpha)
{
	byte	 *nwpx, *nepx, *swpx, *sepx, *dest, *inlimit = (byte *)(in + inwidth * inheight);
	unsigned xfrac, yfrac, x, y, modx, mody, imodx, imody, injump, outjump;
	int	 i, j;

	// Make sure "out" size is at least 2x2!
	xfrac = ((inwidth-1) << 16) / (outwidth-1);
	yfrac = ((inheight-1) << 16) / (outheight-1);
	y = outjump = 0;

	for (i=0; i<outheight; i++)
	{
		mody = (y>>8) & 0xFF;
		imody = 256 - mody;
		injump = (y>>16) * inwidth;
		x = 0;

		for (j=0; j<outwidth; j++)
		{
			modx = (x>>8) & 0xFF;
			imodx = 256 - modx;

			nwpx = (byte *)(in + (x>>16) + injump);
			nepx = nwpx + sizeof(int);
			swpx = nwpx + inwidth * sizeof(int); // Next line

			// Don't exceed "in" size
			if (swpx + sizeof(int) >= inlimit)
			{
//				Con_SafePrintf ("\002ResampleTextureQuality: ");
//				Con_SafePrintf ("error: %4d\n", swpx + sizeof(int) - inlimit);
				swpx = nwpx; // There's no next line
			}

			sepx = swpx + sizeof(int);

			dest = (byte *)(out + outjump + j);

			dest[0] = (nwpx[0]*imodx*imody + nepx[0]*modx*imody + swpx[0]*imodx*mody + sepx[0]*modx*mody)>>16;
			dest[1] = (nwpx[1]*imodx*imody + nepx[1]*modx*imody + swpx[1]*imodx*mody + sepx[1]*modx*mody)>>16;
			dest[2] = (nwpx[2]*imodx*imody + nepx[2]*modx*imody + swpx[2]*imodx*mody + sepx[2]*modx*mody)>>16;
			if (alpha)
				dest[3] = (nwpx[3]*imodx*imody + nepx[3]*modx*imody + swpx[3]*imodx*mody + sepx[3]*modx*mody)>>16;
			else
				dest[3] = 255;

			x += xfrac;
		}
		outjump += outwidth;
		y += yfrac;
	}
}

void GL_ResampleTexture (unsigned *in, int inwidth, int inheight, unsigned *out,  int outwidth, int outheight, qboolean alpha)
{

	// Sanity ...
	if (inwidth <= 0 || inheight <= 0 || outwidth <= 0 || outheight <= 0 ||
	    inwidth * 0x10000 & 0xC0000000 || inheight * outheight & 0xC0000000 ||
	    inwidth * inheight & 0xC0000000)
		Sys_Error ("GL_ResampleTexture: invalid parameters (in:%dx%d, out:%dx%d)", inwidth, inheight, outwidth, outheight);

	ResampleTextureQuality (in, inwidth, inheight, out, outwidth, outheight, alpha);

}

/*
================
GL_MipMap

Operates in place, quartering the size of the texture
================
*/
void GL_MipMap (byte *in, int width, int height)
{
	int		i, j;
	byte	*out;

	width <<=2;
	height >>= 1;
	out = in;
	for (i=0 ; i<height ; i++, in+=width)
	{
		for (j=0 ; j<width ; j+=8, out+=4, in+=8)
		{
			out[0] = (in[0] + in[4] + in[width+0] + in[width+4])>>2;
			out[1] = (in[1] + in[5] + in[width+1] + in[width+5])>>2;
			out[2] = (in[2] + in[6] + in[width+2] + in[width+6])>>2;
			out[3] = (in[3] + in[7] + in[width+3] + in[width+7])>>2;
		}
	}
}


/*
================
ScaleSize
================
*/
static int ScaleSize (int oldsize, qboolean force)
{
	int newsize, nextsize;

	// gl_texquality == 0 : GLQuake compatibility
	// gl_texquality == 1 : default, better resampling, less blurring of non-mipmapped texes
	// gl_texquality == 2 : better resampling, less blurring of all texes, requires a lot of memory
	if (/* gl_texquality.value < 1 || */ force)
		nextsize = oldsize;
	else
		nextsize = 3 * oldsize / 2; // Avoid unfortunate resampling

	for (newsize = 1; newsize < nextsize && newsize != oldsize; newsize <<= 1)
		;

	return newsize;
}


/*
===============
GL_Upload32
===============
*/
void GL_Upload32 (char *name, unsigned *data, int width, int height, qboolean mipmap, qboolean alpha)
{
	int			samples;
	int			scaled_width, scaled_height;
	static unsigned	*scaled = NULL;

	scaled_width = ScaleSize (width, false);
	scaled_height = ScaleSize (height, false);

	if (width && height)
	{
		// Preserve proportions
		while (width > height && scaled_width < scaled_height)
			scaled_width *= 2;

		while (width < height && scaled_width > scaled_height)
			scaled_height *= 2;
	}

	// Note: Can't use Con_Printf here!
	if (developer.value > 1 && (scaled_width != ScaleSize (width, true) || scaled_height != ScaleSize (height, true)))
		Con_SafePrintf ("GL_Upload32: in:%dx%d, out:%dx%d, '%s'\n", width, height, scaled_width, scaled_height, name);

	// Prevent too large or too small images (might otherwise crash resampling)
	scaled_width = CLAMP(2, scaled_width, gl_max_size.value);
	scaled_height = CLAMP(2, scaled_height, gl_max_size.value);

	scaled = (unsigned *)malloc (scaled_width * scaled_height * sizeof(unsigned));

	samples = alpha ? gl_alpha_format : gl_solid_format;

	if (scaled_width == width && scaled_height == height)
	{
		if (!mipmap)
		{
			glTexImage2D (GL_TEXTURE_2D, 0, samples, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
			goto done;
		}
		memcpy (scaled, data, width*height*4);
	}
	else if (width && height) // Don't resample 0-sized images
		GL_ResampleTexture (data, width, height, scaled, scaled_width, scaled_height, alpha);

	glTexImage2D (GL_TEXTURE_2D, 0, samples, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);

	if (mipmap)
	{
		int		miplevel;

		miplevel = 0;
		while (scaled_width > 1 || scaled_height > 1)
		{
			GL_MipMap ((byte *)scaled, scaled_width, scaled_height);
			scaled_width >>= 1;
			scaled_height >>= 1;
			if (scaled_width < 1)
				scaled_width = 1;
			if (scaled_height < 1)
				scaled_height = 1;
			miplevel++;

			glTexImage2D (GL_TEXTURE_2D, miplevel, samples, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);
		}
	}
done: ;

	  if (mipmap)
	  {
		  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
		  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_mag);
	  }
	  else
	  {
		  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_mag);
		  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_mag);
	  }

	  free (scaled);
}


/*
===============
GL_AlphaEdgeFix

eliminate pink edges on sprites, etc.
operates in place on 32bit data
===============
*/
void GL_AlphaEdgeFix (byte *data, int width, int height)
{
	int i,j,n=0,b,c[3]={0,0,0},lastrow,thisrow,nextrow,lastpix,thispix,nextpix;
	byte *dest = data;

	for (i=0; i<height; i++)
	{
		lastrow = width * 4 * ((i == 0) ? height-1 : i-1);
		thisrow = width * 4 * i;
		nextrow = width * 4 * ((i == height-1) ? 0 : i+1);

		for (j=0; j<width; j++, dest+=4)
		{
			if (dest[3]) // not transparent
				continue;

			lastpix = 4 * ((j == 0) ? width-1 : j-1);
			thispix = 4 * j;
			nextpix = 4 * ((j == width-1) ? 0 : j+1);

			b = lastrow + lastpix; if (data[b+3]) {c[0] += data[b]; c[1] += data[b+1]; c[2] += data[b+2]; n++;}
			b = thisrow + lastpix; if (data[b+3]) {c[0] += data[b]; c[1] += data[b+1]; c[2] += data[b+2]; n++;}
			b = nextrow + lastpix; if (data[b+3]) {c[0] += data[b]; c[1] += data[b+1]; c[2] += data[b+2]; n++;}
			b = lastrow + thispix; if (data[b+3]) {c[0] += data[b]; c[1] += data[b+1]; c[2] += data[b+2]; n++;}
			b = nextrow + thispix; if (data[b+3]) {c[0] += data[b]; c[1] += data[b+1]; c[2] += data[b+2]; n++;}
			b = lastrow + nextpix; if (data[b+3]) {c[0] += data[b]; c[1] += data[b+1]; c[2] += data[b+2]; n++;}
			b = thisrow + nextpix; if (data[b+3]) {c[0] += data[b]; c[1] += data[b+1]; c[2] += data[b+2]; n++;}
			b = nextrow + nextpix; if (data[b+3]) {c[0] += data[b]; c[1] += data[b+1]; c[2] += data[b+2]; n++;}

			// average all non-transparent neighbors
			if (n)
			{
				dest[0] = (byte)(c[0]/n);
				dest[1] = (byte)(c[1]/n);
				dest[2] = (byte)(c[2]/n);

				n = c[0] = c[1] = c[2] = 0;
			}
		}
	}
}


/*
===============
GL_Upload8
===============
*/
/*
* mode:
* 0 - standard
* 1 - color 0 transparent, odd - translucent, even - full value
* 2 - color 0 transparent
* 3 - special (particle translucency table)
*/
void GL_Upload8 (char *name, byte *data, int width, int height, qboolean mipmap, qboolean alpha, int mode)
{
	int			i, s;
	qboolean	noalpha;
	int			p;
	static unsigned	    *trans = NULL;

	s = width*height;

	trans = (unsigned *)malloc (s * sizeof(unsigned));
	if (s&3)
	{
		Con_SafePrintf ("\002GL_Upload8: ");
		Con_SafePrintf ("size %d is not a multiple of 4 in '%s'\n", s, name);
	}

	// if there are no transparent pixels, make it a 3 component
	// texture even if it was specified as otherwise
	if ((alpha || mode != 0) && mode != 10 )
	{
		noalpha = true;
		for (i=0 ; i<s ; i++)
		{
			p = data[i];
			if (p == 255)
				noalpha = false;
			trans[i] = d_8to24table[p];
		}

		if (alpha && noalpha)
			alpha = false;

		switch( mode )
		{
			case 1:
				alpha = true;
				for (i=0 ; i<s ; i++)
				{
					p = data[i];
					if (p == 0)
						trans[i] &= 0x00ffffff;
					else if( p & 1 )
					{
						trans[i] &= 0x00ffffff;
						trans[i] |= ( ( int )( 255 * r_wateralpha.value ) ) << 24; //todo r_wateralpha -> r_modelalpha with clamping
					}
					else
					{
						trans[i] |= 0xff000000;
					}
				}
				break;
			case 2:
				alpha = true;
				for (i=0 ; i<s ; i++)
				{
					p = data[i];
					if (p == 0)
						trans[i] &= 0x00ffffff;
				}
				break;
			case 3:
				alpha = true;
				for (i=0 ; i<s ; i++)
				{
					p = data[i];
					trans[i] = d_8to24table[ColorIndex[p>>4]] & 0x00ffffff;
					trans[i] |= ( int )ColorPercent[p&15] << 24;
				}
				break;
		}
	}
	else
	{
		for (i=0 ; i<s ; ++i)
			trans[i] = d_8to24table[data[i]];
	}

	if (alpha)
		GL_AlphaEdgeFix ((byte *)trans, width, height);

	GL_Upload32 (name, trans, width, height, mipmap, alpha);

	free (trans);
}

void GL_UploadLightmap (char *name, unsigned *data, int width, int height)
{
	glTexImage2D (GL_TEXTURE_2D, 0, lightmap_bytes, width, height, 0, gl_lightmap_format, GL_UNSIGNED_BYTE, data);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

/*
================
GL_LoadTexture
================
*/
static int lhcsumtable[256]; // used to verify textures are identical
int GL_LoadTexture (char *identifier, int width, int height, byte *data, qboolean mipmap, qboolean alpha, int mode)
{
	int			i, s, lhcsum;
	gltexture_t	*glt;
	float		size;

	if (cls.state == ca_dedicated)
		return 0; // No textures in dedicated mode

	for (i=0 ; i<256 ; i++)
		lhcsumtable[i] = i + 1;

	s = width * height;
	size = (float)width * (float)height;

	if (size == 0)
		Con_DPrintf ("GL_LoadTexture: texture '%s' has size 0\n", identifier);

	// Sanity check, max = 32kx32k
	if (width < 0 || height < 0 || size > 0x40000000)
		Sys_Error ("GL_LoadTexture: texture '%s' has invalid size (%.0fM, max = %dM)", identifier, size / (1024 * 1024), 0x40000000 / (1024 * 1024));

	lhcsum = 0;

	for (i=0; i<s ; i++)
		lhcsum += (lhcsumtable[data[i] & 255]++);

	// see if the texture is already present
	if (identifier[0])
	{
		for (i=0, glt=gltextures ; i<numgltextures ; i++, glt++)
		{
			if (lhcsum == glt->lhcsum && width == glt->width && height == glt->height && !strcmp (identifier, glt->identifier))
				return glt->texnum;
		}
	}

	if (numgltextures >= MAX_GLTEXTURES)
		Sys_Error ("GL_LoadTexture: cache full, max is %i textures", MAX_GLTEXTURES);

	glt = &gltextures[numgltextures];
	numgltextures++;

	strcpy(glt->identifier, identifier);
	glt->texnum = texture_extension_number;
	texture_extension_number++;
	glt->lhcsum = lhcsum;
	glt->width = width;
	glt->height = height;
	glt->mipmap = mipmap;

	GL_Bind(glt->texnum);

	GL_Upload8 (identifier, data, width, height, mipmap, alpha, mode);

	return glt->texnum;
}




void D_ShowLoadingSize (void)
{
#ifdef RELEASE
	int cur_perc;
	static int prev_perc; 
	
//	if (!vid_initialized)
//		return;
	if (cls.state == ca_dedicated)
		return;				// stdout only

	//HoT speedup loading progress
	cur_perc = loading_stage * 100;
	if (total_loading_size)
		cur_perc += current_loading_size * 100 / total_loading_size;
	if (cur_perc == prev_perc)
		return;
	prev_perc = cur_perc; 

	glDrawBuffer  (GL_FRONT);

	SCR_DrawLoading();

	glFlush(); //EER1 this is cause very slow map loading, but show loading plaque (updating server/client progress) correctly
//	glFinish();

	glDrawBuffer  (GL_BACK);
#endif
}


