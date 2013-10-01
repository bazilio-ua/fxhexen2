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
// gl_misc.c

#include "quakedef.h"

byte *playerTranslation;

/*
==================
R_InitTextures
==================
*/
void	R_InitTextures (void)
{
	int		x,y, m;
	byte	*dest;

// create a simple checkerboard texture for the default
	r_notexture_mip = Hunk_AllocName (sizeof(texture_t) + 16*16+8*8+4*4+2*2, "notexture");
	
	r_notexture_mip->width = r_notexture_mip->height = 16;
	r_notexture_mip->offsets[0] = sizeof(texture_t);
	r_notexture_mip->offsets[1] = r_notexture_mip->offsets[0] + 16*16;
	r_notexture_mip->offsets[2] = r_notexture_mip->offsets[1] + 8*8;
	r_notexture_mip->offsets[3] = r_notexture_mip->offsets[2] + 4*4;
	
	for (m=0 ; m<4 ; m++)
	{
		dest = (byte *)r_notexture_mip + r_notexture_mip->offsets[m];
		for (y=0 ; y< (16>>m) ; y++)
			for (x=0 ; x< (16>>m) ; x++)
			{
				if (  (y< (8>>m) ) ^ (x< (8>>m) ) )
					*dest++ = 0;
				else
					*dest++ = 0xff;
			}
	}	
}



float RTint[256],GTint[256],BTint[256];

int ColorIndex[16] =
{
	0, 31, 47, 63, 79, 95, 111, 127, 143, 159, 175, 191, 199, 207, 223, 231
};

unsigned ColorPercent[16] =
{
	25, 51, 76, 102, 114, 127, 140, 153, 165, 178, 191, 204, 216, 229, 237, 247
};


void R_LoadPalette (void)
{
//	byte	*pal;
	int		r,g,b,v;
	int		i,c,p;
	unsigned	*table;

	byte mask[] = {255,255,255,0};
	byte black[] = {0,0,0,255};
	byte *pal, *src, *dst;
//	int i;

	host_basepal = (byte *)COM_LoadHunkFile ("gfx/palette.lmp", NULL);
	if (!host_basepal)
		Sys_Error ("Couldn't load gfx/palette.lmp");


	pal = host_basepal;

	//standard palette, 255 is transparent
	dst = (byte *)d_8to24table;
	src = pal;
	for (i=0; i<256; i++)
	{
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = 255;
	}
	d_8to24table[255] &= *mask; // fix gcc warnings

	//fullbright palette, 0-223 are black (for additive blending)
	src = pal + 224*3;
	dst = (byte *)(d_8to24table_fbright) + 224*4;
	for (i=224; i<256; i++)
	{
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = 255;
	}
	for (i=0; i<224; i++)
		d_8to24table_fbright[i] = *black; // fix gcc warnings

	//nobright palette, 224-255 are black (for additive blending)
	dst = (byte *)d_8to24table_nobright;
	src = pal;
	for (i=0; i<256; i++)
	{
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = 255;
	}
	for (i=224; i<256; i++)
		d_8to24table_nobright[i] = *black; // fix gcc warnings

	//conchars palette, 0 and 255 are transparent
	memcpy(d_8to24table_conchars, d_8to24table, 256*4);
	d_8to24table_conchars[0] &= *mask; // fix gcc warnings




//
// 8 8 8 encoding
//
/*	pal = host_basepal;
	table = d_8to24table;
	
	for (i=0 ; i<256 ; i++)
	{
		r = pal[0];
		g = pal[1];
		b = pal[2];
		pal += 3;
		
		v = (255<<24) + (r<<0) + (g<<8) + (b<<16);
		*table++ = v;
	}

	d_8to24table[255] &= 0xffffff;	// 255 is transparent
*/

	//h2 part d_8to24TranslucentTable, used for rain effect in castle4 and eidolon map 
	//fixme: tint(ottenok) is not used yet
	pal = host_basepal;
	table = d_8to24TranslucentTable;
//
	for (i=0; i<16;i++)
	{
		c = ColorIndex[i]*3;

		r = pal[c];
		g = pal[c+1];
		b = pal[c+2];

		for(p=0;p<16;p++)
		{
			v = (ColorPercent[15-p]<<24) + (r<<0) + (g<<8) + (b<<16);
			*table++ = v;

			RTint[i*16+p] = ((float)r) / ((float)ColorPercent[15-p]) ;
			GTint[i*16+p] = ((float)g) / ((float)ColorPercent[15-p]);
			BTint[i*16+p] = ((float)b) / ((float)ColorPercent[15-p]);
		}
	}
//
}




/*
===============
R_Init
===============
*/
void R_Init (void)
{	
	extern byte *hunk_base;
	int counter;

	Cmd_AddCommand ("timerefresh", R_TimeRefresh_f);	
	Cmd_AddCommand ("pointfile", R_ReadPointFile_f);	

	Cvar_RegisterVariable (&r_norefresh, NULL);
	Cvar_RegisterVariable (&r_lightmap, NULL);
	Cvar_RegisterVariable (&r_fullbright, NULL);
	Cvar_RegisterVariable (&r_drawentities, NULL);
	Cvar_RegisterVariable (&r_drawviewmodel, NULL);
	Cvar_RegisterVariable (&r_shadows, NULL);
	Cvar_RegisterVariable (&r_mirroralpha, NULL);
	Cvar_RegisterVariable (&r_wateralpha, NULL);
	Cvar_RegisterVariable (&r_dynamic, NULL);
	Cvar_RegisterVariable (&r_novis, NULL);
	Cvar_RegisterVariable (&r_speeds, NULL);
	Cvar_RegisterVariable (&r_wholeframe, NULL);

	Cvar_RegisterVariable (&gl_clear, NULL);
	Cvar_RegisterVariable (&gl_texsort, NULL);
	Cvar_RegisterVariable (&gl_cull, NULL);
	Cvar_RegisterVariable (&gl_smoothmodels, NULL);
	Cvar_RegisterVariable (&gl_affinemodels, NULL);
	Cvar_RegisterVariable (&gl_polyblend, NULL);
	Cvar_RegisterVariable (&gl_flashblend, NULL);
	Cvar_RegisterVariable (&gl_playermip, NULL);
	Cvar_RegisterVariable (&gl_nocolors, NULL);

	Cvar_RegisterVariable (&gl_keeptjunctions, NULL);
	Cvar_RegisterVariable (&gl_reporttjunctions, NULL);

	R_InitParticles ();
	R_InitParticleTexture ();

	R_InitTranslatePlayerTextures ();

	playertextures = texture_extension_number;
	texture_extension_number += MAX_SCOREBOARD; //16

	for(counter=0;counter<MAX_EXTRA_TEXTURES;counter++)
		gl_extra_textures[counter] = -1;

	playerTranslation = (byte *)COM_LoadHunkFile ("gfx/player.lmp", NULL);
	if (!playerTranslation)
		Sys_Error ("Couldn't load gfx/player.lmp");
}


/*
===============
R_InitTranslatePlayerTextures
===============
*/
static int oldtop[MAX_SCOREBOARD]; 
static int oldbottom[MAX_SCOREBOARD];
static int oldskinnum[MAX_SCOREBOARD];
static int oldplayerclass[MAX_SCOREBOARD];
void R_InitTranslatePlayerTextures (void)
{
	int i;

	for (i = 0; i < MAX_SCOREBOARD; i++)
	{
		oldtop[i] = -1;
		oldbottom[i] = -1;
		oldskinnum[i] = -1;
		oldplayerclass[i] = -1;
	}
}


/*
===============
R_TranslatePlayerSkin

Translates a skin texture by the per-player color lookup
===============
*/
void R_TranslatePlayerSkin (int playernum)
{

	int		top, bottom;
	byte	translate[256];
	int		i, size;
	model_t	*model;
	aliashdr_t *paliashdr;
	byte	*original;
	byte	*src, *dst; 
	byte	*pixels = NULL;
	byte	*sourceA, *sourceB, *colorA, *colorB;
	int		playerclass;
	char		name[64];

	//
	// locate the original skin pixels
	//
	currententity = &cl_entities[1+playernum];
	model = currententity->model;

	if (!model)
		return;		// player doesn't have a model yet
	if (model->type != mod_alias)
		return; // only translate skins on alias models

	paliashdr = (aliashdr_t *)Mod_Extradata (model);

	top = (cl.scores[playernum].colors & 0xf0) >> 4;
	bottom = (cl.scores[playernum].colors & 15);
	playerclass = (int)cl.scores[playernum].playerclass;

/*	if (!strcmp (currententity->model->name, "models/paladin.mdl") ||
		!strcmp (currententity->model->name, "models/crusader.mdl") ||
		!strcmp (currententity->model->name, "models/necro.mdl") ||
		!strcmp (currententity->model->name, "models/assassin.mdl") ||
		!strcmp (currententity->model->name, "models/succubus.mdl"))	*/
	if (currententity->model == player_models[0] ||
	    currententity->model == player_models[1] ||
	    currententity->model == player_models[2] ||
	    currententity->model == player_models[3] ||
	    currententity->model == player_models[4])
	{
		if (top == oldtop[playernum] && bottom == oldbottom[playernum] && 
			currententity->skinnum == oldskinnum[playernum] && playerclass == oldplayerclass[playernum])
			return; // translate if only player change his color
	}
	else
	{
		oldtop[playernum] = -1;
		oldbottom[playernum] = -1;
		oldskinnum[playernum] = -1;
		oldplayerclass[playernum] = -1;
		goto skip;
	}

	oldtop[playernum] = top;
	oldbottom[playernum] = bottom;
	oldskinnum[playernum] = currententity->skinnum;
	oldplayerclass[playernum] = playerclass;

skip:
	for (i=0 ; i<256 ; i++)
		translate[i] = i;

	top -= 1;
	bottom -= 1;

	colorA = playerTranslation + 256 + color_offsets[playerclass-1];
	colorB = colorA + 256;
	sourceA = colorB + 256 + (top * 256);
	sourceB = colorB + 256 + (bottom * 256);
	for(i=0;i<256;i++,colorA++,colorB++,sourceA++,sourceB++)
	{
		if (top >= 0 && (*colorA != 255)) 
			translate[i] = *sourceA;
		if (bottom >= 0 && (*colorB != 255)) 
			translate[i] = *sourceB;
	}

	// class limit is mission pack dependant
	i = portals ? MAX_PLAYER_CLASS : MAX_PLAYER_CLASS - PORTALS_EXTRA_CLASSES;
	if (playerclass >= 1 && playerclass <= i)
		original = player_texels[playerclass-1];
	else
		original = player_texels[0];

	//
	// translate texture
	//
	sprintf (name, "%s_%i_%i", currententity->model->name, currententity->skinnum, playernum);
	size = paliashdr->skinwidth * paliashdr->skinheight;

	// allocate dynamic memory
	pixels = malloc (size);

	dst = pixels;
	src = original;

	for (i=0; i<size; i++)
		*dst++ = translate[*src++];

	original = pixels;

	// do it fast and correct.
	GL_Bind(playertextures + playernum);

	GL_Upload8 (name, original, paliashdr->skinwidth, paliashdr->skinheight, false, false, 0); 

	// free allocated memory
	free (pixels);

}


/*
===============
R_NewMap
===============
*/
void R_NewMap (void)
{
	int		i;
	
	for (i=0 ; i<256 ; i++)
		d_lightstylevalue[i] = 264;		// normal light value

	memset (&r_worldentity, 0, sizeof(r_worldentity));
	r_worldentity.model = cl.worldmodel;

// clear out efrags in case the level hasn't been reloaded
// FIXME: is this one short?
	for (i=0 ; i<cl.worldmodel->numleafs ; i++)
		cl.worldmodel->leafs[i].efrags = NULL;
		 	
	r_viewleaf = NULL;
	R_ClearParticles ();

	R_BuildLightmaps ();

// identify sky texture
}


/*
====================
R_TimeRefresh_f

For program optimization
====================
*/
void R_TimeRefresh_f (void)
{
	int			i;
	float		start, stop, time;

	if (cls.state != ca_connected)
	{
		Con_Printf ("Not connected to a server\n");
		return;
	}

	start = Sys_DoubleTime ();
	for (i=0 ; i<128 ; i++)
	{
		GL_BeginRendering (&glx, &gly, &glwidth, &glheight);

		r_refdef.viewangles[1] = i/128.0*360.0;
		R_RenderView ();
//		glFinish ();

// workaround to avoid flickering uncovered by 3d refresh 2d areas when bloom enabled
		GL_Set2D ();  
		if (/*scr_sbar.value || */scr_viewsize.value < 120)
		{
//			SCR_TileClear ();
			Sbar_Changed ();
			Sbar_Draw ();
		}

		GL_EndRendering ();
	}

	stop = Sys_DoubleTime ();
	time = stop-start;
	Con_Printf ("%f seconds (%f fps)\n", time, 128/time);

}



