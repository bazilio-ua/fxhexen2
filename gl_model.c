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
// gl_model.c -- model loading and caching

// models are the only shared resource between a client and server running
// on the same machine.

/*
 * $Header: /H2 Mission Pack/gl_model.c 11    3/16/98 4:38p Jmonroe $
 */

#include "quakedef.h"


model_t	*loadmodel;
char	loadname[32];	// for hunk tags

model_t *player_models[MAX_PLAYER_CLASS];

void Mod_LoadSpriteModel (model_t *mod, void *buffer);
void Mod_LoadBrushModel (model_t *mod, void *buffer);
void Mod_LoadAliasModel (model_t *mod, void *buffer);
void Mod_LoadAliasModelNew (model_t *mod, void *buffer);

model_t *Mod_LoadModel (model_t *mod, qboolean crash);

byte	mod_novis[MAX_MAP_LEAFS/8];

#define	MAX_MOD_KNOWN	2048 // was 1500
model_t	mod_known[MAX_MOD_KNOWN];
int		mod_numknown;

int entity_file_size;

cvar_t	external_lit = {"external_lit","0"};
cvar_t	external_vis = {"external_vis","0"};
cvar_t	external_ent = {"external_ent","0"};

/*
===============
Mod_Init
===============
*/
void Mod_Init (void)
{
	Cvar_RegisterVariable (&external_lit, NULL);
	Cvar_RegisterVariable (&external_vis, NULL);
	Cvar_RegisterVariable (&external_ent, NULL);

	memset (mod_novis, 0xff, sizeof(mod_novis));
}

/*
===============
Mod_Extradata

Caches the data if needed
===============
*/
void *Mod_Extradata (model_t *mod)
{
	void	*r;
	
	r = Cache_Check (&mod->cache);
	if (r)
		return r;

	Mod_LoadModel (mod, true);
	
	if (!mod->cache.data)
		Sys_Error ("Mod_Extradata: caching failed");
	return mod->cache.data;
}

/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *Mod_PointInLeaf (vec3_t p, model_t *model)
{
	mnode_t		*node;
	float		d;
	mplane_t	*plane;
	
	if (!model || !model->nodes)
		Sys_Error ("Mod_PointInLeaf: bad model");

	node = model->nodes;
	while (1)
	{
		if (node->contents < 0)
			return (mleaf_t *)node;
		plane = node->plane;
		d = DotProduct (p,plane->normal) - plane->dist;
		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}
	
	return NULL;	// never reached
}


/*
===================
Mod_DecompressVis
===================
*/
byte *Mod_DecompressVis (byte *in, model_t *model)
{
	static byte	decompressed[MAX_MAP_LEAFS/8];
	int		c;
	byte	*out;
	int		row;

	row = (model->numleafs+7)>>3;
	out = decompressed;

	if (!in)
	{	// no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}
		return decompressed;
	}

	do
	{
		if (*in)
		{
			*out++ = *in++;
			continue;
		}
	
		c = in[1];
		in += 2;
		while (c)
		{
			*out++ = 0;
			c--;
		}
	} while (out - decompressed < row);

	return decompressed;
}

/*
=================
Mod_LeafPVS
=================
*/
byte *Mod_LeafPVS (mleaf_t *leaf, model_t *model)
{
	if (leaf == model->leafs)
		return mod_novis;
	return Mod_DecompressVis (leaf->compressed_vis, model);
}

/*
===================
Mod_ClearAll
===================
*/
void Mod_ClearAll (void)
{
	int		i;
	model_t	*mod;
	
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
		if (mod->type != mod_alias)
			mod->needload = true;
}

/*
==================
Mod_FindName

==================
*/
model_t *Mod_FindName (char *name)
{
	int		i;
	model_t	*mod;
	
	if (!name[0])
		Sys_Error ("Mod_ForName: NULL name");
		
//
// search the currently loaded models
//
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
		if (!strcmp (mod->name, name) )
			break;
			
	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
			Sys_Error ("mod_numknown == MAX_MOD_KNOWN");
		strcpy (mod->name, name);
		mod->needload = true;
		mod_numknown++;
	}

	return mod;
}

/*
==================
Mod_TouchModel

==================
*/
void Mod_TouchModel (char *name)
{
	model_t	*mod;
	
	mod = Mod_FindName (name);
	
	if (!mod->needload)
	{
		if (mod->type == mod_alias)
			Cache_Check (&mod->cache);
	}
}

/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/
model_t *Mod_LoadModel (model_t *mod, qboolean crash)
{
	void	*d;
	unsigned *buf;
	byte	stackbuf[1024];		// avoid dirtying the cache heap

	if (!mod->needload)
	{
		if (mod->type == mod_alias)
		{
			d = Cache_Check (&mod->cache);
			if (d)
				return mod;
		}
		else
			return mod;		// not cached at all
	}

//
// because the world is so huge, load it one piece at a time
//
	if (!crash)
	{

	}
	
//
// load the file
//
	buf = (unsigned *)COM_LoadStackFile (mod->name, stackbuf, sizeof(stackbuf), NULL);
	if (!buf)
	{
		if (crash)
			Sys_Error ("Mod_NumForName: %s not found", mod->name);
		return NULL;
	}
	
//
// allocate a new model
//
	COM_FileBase (mod->name, loadname);
	
	loadmodel = mod;

//
// fill it in
//

// call the apropriate loader
	mod->needload = false;
	
	switch (LittleLong(*(unsigned *)buf))
	{
		case RAPOLYHEADER:
			Mod_LoadAliasModelNew (mod, buf);
			break;
	
		case IDPOLYHEADER:
			Mod_LoadAliasModel (mod, buf);
			break;
	
		case IDSPRITEHEADER:
			Mod_LoadSpriteModel (mod, buf);
			break;
		
		default:
			Mod_LoadBrushModel (mod, buf);
			break;
	}

	return mod;
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
model_t *Mod_ForName (char *name, qboolean crash)
{
	model_t	*mod;
	
	mod = Mod_FindName (name);
	
	return Mod_LoadModel (mod, crash);
}


/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/

static int missingtex;
byte	*mod_base;

/*
==================
IsFullbright
==================
*/
qboolean IsFullbright (byte *pixels, int size)
{
	int	i;

	for (i=0 ; i<size ; i++)
		if (pixels[i] > 237) //hexen2 palette specific
			return true;

	return false;
} 

/*
=================
Mod_LoadTextures
=================
*/
void Mod_LoadTextures (lump_t *l)
{
	int		i, j, pixels, num, max, altmax;
	miptex_t	*mt;
	texture_t	*tx, *tx2;
	texture_t	*anims[10];
	texture_t	*altanims[10];
	dmiptexlump_t *m;
	char		texturename[64];
	int			nummiptex;
	unsigned	offset;
	int			mark;
	char		texname[16 + 1];

	// don't return early if no textures; still need to create dummy texture
	if (!l->filelen)
	{
		loadmodel->textures = NULL;
		return;
	}
	m = (dmiptexlump_t *)(mod_base + l->fileofs);
	
	m->nummiptex = LittleLong (m->nummiptex);
	
	loadmodel->numtextures = m->nummiptex;
	loadmodel->textures = Hunk_AllocName (m->nummiptex * sizeof(*loadmodel->textures) , loadname);

	for (i=0 ; i<m->nummiptex ; i++)
	{
		m->dataofs[i] = LittleLong(m->dataofs[i]);
		if (m->dataofs[i] == -1)
			continue;
		mt = (miptex_t *)((byte *)m + m->dataofs[i]);
		mt->width = LittleLong (mt->width);
		mt->height = LittleLong (mt->height);
		for (j=0 ; j<MIPLEVELS ; j++)
			mt->offsets[j] = LittleLong (mt->offsets[j]);
		
		if ( (mt->width & 15) || (mt->height & 15) )
			Sys_Error ("Texture %s is not 16 aligned", mt->name);
		pixels = mt->width*mt->height/64*85;
		tx = Hunk_AllocName (sizeof(texture_t) +pixels, loadname );
		loadmodel->textures[i] = tx;

		memcpy (tx->name, mt->name, sizeof(tx->name));
		tx->width = mt->width;
		tx->height = mt->height;
		for (j=0 ; j<MIPLEVELS ; j++)
			tx->offsets[j] = mt->offsets[j] + sizeof(texture_t) - sizeof(miptex_t);
		// the pixels immediately follow the structures
		memcpy ( tx+1, mt+1, pixels);

		tx->update_warp = false;
		tx->warpimage = NULL;
		tx->fullbright = NULL;

		if (cls.state != ca_dedicated) // no texture uploading for dedicated server
		{
			if (!strncasecmp(tx->name,"sky",3)) // sky texture (also note: was strncmp, changed to match qbsp)
			{
				R_InitSky (tx);
			}
			else if (tx->name[0] == '*') // warping texture
			{
//				byte	*dummy;
				mark = Hunk_LowMark();

				sprintf (texturename, "%s:%s", loadmodel->name, tx->name);
				offset = (unsigned)(mt+1) - (unsigned)mod_base;
				tx->gltexture = GL_LoadTexture (loadmodel, texturename, tx->width, tx->height, SRC_INDEXED, (byte *)(tx+1), loadmodel->name, offset, TEXPREF_NONE);

				// now create the warpimage, using dummy data to create the initial image
//				dummy = malloc (gl_warpimage_size*gl_warpimage_size*4);
				//now create the warpimage, using dummy data from the hunk to create the initial image
				Hunk_Alloc (gl_warpimage_size*gl_warpimage_size*4); //make sure hunk is big enough so we don't reach an illegal address
				Hunk_FreeToLowMark (mark);
				sprintf (texturename, "%s_warp", texturename);
//				tx->warpimage = GL_LoadTexture (loadmodel, texturename, gl_warpimage_size, gl_warpimage_size, SRC_RGBA /* SRC_INDEXED */, dummy, "", (unsigned)dummy, TEXPREF_NOPICMIP | TEXPREF_WARPIMAGE);
				tx->warpimage = GL_LoadTexture (loadmodel, texturename, gl_warpimage_size, gl_warpimage_size, SRC_RGBA /* SRC_INDEXED */, hunk_base, "", (unsigned)hunk_base, TEXPREF_NOPICMIP | TEXPREF_WARPIMAGE);
				tx->update_warp = true;
//				free (dummy);
			}
			else // regular texture
			{
				offset = (unsigned)(mt+1) - (unsigned)mod_base;
				if (IsFullbright ((byte *)(tx+1), pixels))
				{
					sprintf (texturename, "%s:%s", loadmodel->name, tx->name);
					tx->gltexture = GL_LoadTexture (loadmodel, texturename, tx->width, tx->height, SRC_INDEXED, (byte *)(tx+1), loadmodel->name, offset, TEXPREF_MIPMAP | TEXPREF_NOBRIGHT);
					sprintf (texturename, "%s:%s_glow", loadmodel->name, tx->name);
					tx->fullbright = GL_LoadTexture (loadmodel, texturename, tx->width, tx->height, SRC_INDEXED, (byte *)(tx+1), loadmodel->name, offset, TEXPREF_MIPMAP | TEXPREF_FULLBRIGHT);
				}
				else
				{
					sprintf (texturename, "%s:%s", loadmodel->name, tx->name);
					tx->gltexture = GL_LoadTexture (loadmodel, texturename, tx->width, tx->height, SRC_INDEXED, (byte *)(tx+1), loadmodel->name, offset, TEXPREF_MIPMAP);
				}
			}
		}
	}

	// last 2 slots in array should be filled with dummy textures
	loadmodel->textures[loadmodel->numtextures-2] = notexture_mip; // for lightmapped surfs
	loadmodel->textures[loadmodel->numtextures-1] = notexture_mip2; // for SURF_DRAWTILED surfs

//
// sequence the animations
//
	for (i=0 ; i<m->nummiptex ; i++)
	{
		tx = loadmodel->textures[i];
		if (!tx || tx->name[0] != '+')
			continue;
		if (tx->anim_next)
			continue;	// already sequenced

	// find the number of frames in the animation
		memset (anims, 0, sizeof(anims));
		memset (altanims, 0, sizeof(altanims));

		max = tx->name[1];
		altmax = 0;
		if (max >= 'a' && max <= 'z')
			max -= 'a' - 'A';
		if (max >= '0' && max <= '9')
		{
			max -= '0';
			altmax = 0;
			anims[max] = tx;
			max++;
		}
		else if (max >= 'A' && max <= 'J')
		{
			altmax = max - 'A';
			max = 0;
			altanims[altmax] = tx;
			altmax++;
		}
		else
			Sys_Error ("Bad animating texture %s", tx->name);

		for (j=i+1 ; j<m->nummiptex ; j++)
		{
			tx2 = loadmodel->textures[j];
			if (!tx2 || tx2->name[0] != '+')
				continue;
			if (strcmp (tx2->name+2, tx->name+2))
				continue;

			num = tx2->name[1];
			if (num >= 'a' && num <= 'z')
				num -= 'a' - 'A';
			if (num >= '0' && num <= '9')
			{
				num -= '0';
				anims[num] = tx2;
				if (num+1 > max)
					max = num + 1;
			}
			else if (num >= 'A' && num <= 'J')
			{
				num = num - 'A';
				altanims[num] = tx2;
				if (num+1 > altmax)
					altmax = num+1;
			}
			else
				Sys_Error ("Bad animating texture %s", tx->name);
		}
		
#define	ANIM_CYCLE	2
	// link them all together
		for (j=0 ; j<max ; j++)
		{
			tx2 = anims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s",j, tx->name);
			tx2->anim_total = max * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = anims[ (j+1)%max ];
			if (altmax)
				tx2->alternate_anims = altanims[0];
		}
		for (j=0 ; j<altmax ; j++)
		{
			tx2 = altanims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s",j, tx->name);
			tx2->anim_total = altmax * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = altanims[ (j+1)%altmax ];
			if (max)
				tx2->alternate_anims = anims[0];
		}
	}
}

/*
=================
Mod_LoadLighting

replaced with lit support code via lordhavoc
=================
*/
void Mod_LoadLighting (lump_t *l)
{
	// LordHavoc: .lit support
	int i;
	byte *in, *out, *data;
	byte d;
	char litfilename[MAX_QPATH];
	unsigned int path_id;

	loadmodel->lightdata = NULL;

	if (loadmodel->isworldmodel && external_lit.value)
	{
		// LordHavoc: check for a .lit file
		strcpy(litfilename, loadmodel->name);
		COM_StripExtension(litfilename, litfilename);
		strcat(litfilename, ".lit");
		Con_DPrintf("trying to load %s\n", litfilename);

		data = (byte *) COM_LoadHunkFile (litfilename, &path_id);
		if (data)
		{
			// use .lit file only from the same gamedir as the map
			// itself or from a searchpath with higher priority.
			if (path_id < loadmodel->path_id)
			{
				Con_DPrintf("Ignored %s from a gamedir with lower priority\n", litfilename);
			}
			else if (data[0] == 'Q' && data[1] == 'L' && data[2] == 'I' && data[3] == 'T')
			{
				i = LittleLong(((int *)data)[1]);
				if (i == 1)
				{
					Con_DPrintf("%s loaded\n", litfilename);
					loadmodel->lightdata = data + 8;
					return;
				}
				else
					Con_Printf("Unknown .lit file version (%d)\n", i);
			}
			else
				Con_Printf("Corrupt .lit file (old version?), ignoring\n");
		}
	}

	// LordHavoc: no .lit found, expand the white lighting data to color
	if (!l->filelen)
	{
		return;
	}
	loadmodel->lightdata = Hunk_AllocName ( l->filelen*3, litfilename);	
	in = loadmodel->lightdata + l->filelen*2; // place the file at the end, so it will not be overwritten until the very last write
	out = loadmodel->lightdata;
	memcpy (in, mod_base + l->fileofs, l->filelen);
	for (i = 0;i < l->filelen;i++)
	{
		d = *in++;
		*out++ = d;
		*out++ = d;
		*out++ = d;
	}
}


/*
=================
Mod_LoadVisibility
=================
*/
void Mod_LoadVisibility (lump_t *l)
{
	if (!l->filelen)
	{
		loadmodel->visdata = NULL;
		return;
	}
	loadmodel->visdata = Hunk_AllocName ( l->filelen, loadname);	
	memcpy (loadmodel->visdata, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadEntities
=================
*/
void Mod_LoadEntities (lump_t *l)
{
	if (!l->filelen)
	{
		loadmodel->entities = NULL;
		return;
	}
	loadmodel->entities = Hunk_AllocName ( l->filelen, loadname);	
	memcpy (loadmodel->entities, mod_base + l->fileofs, l->filelen);
	entity_file_size = l->filelen;
}


/*
=================
Mod_LoadVertexes
=================
*/
void Mod_LoadVertexes (lump_t *l)
{
	dvertex_t	*in;
	mvertex_t	*out;
	int			i, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->vertexes = out;
	loadmodel->numvertexes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->position[0] = LittleFloat (in->point[0]);
		out->position[1] = LittleFloat (in->point[1]);
		out->position[2] = LittleFloat (in->point[2]);
	}
}

/*
=================
Mod_LoadSubmodels
=================
*/
void Mod_LoadSubmodels (lump_t *l)
{
	dmodel_t	*in;
	dmodel_t	*out;
	int			i, j, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{	// spread the mins / maxs by a pixel
			out->mins[j] = LittleFloat (in->mins[j]) - 1;
			out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
			out->origin[j] = LittleFloat (in->origin[j]);
		}
		for (j=0 ; j<MAX_MAP_HULLS ; j++)
			out->headnode[j] = LittleLong (in->headnode[j]);
		out->visleafs = LittleLong (in->visleafs);
		out->firstface = LittleLong (in->firstface);
		out->numfaces = LittleLong (in->numfaces);
	}

	// check world visleafs
	out = loadmodel->submodels;

	if (out->visleafs > MAX_MAP_LEAFS)
		Sys_Error ("Mod_LoadSubmodels: too many visleafs (%d, max = %d) in %s", out->visleafs, MAX_MAP_LEAFS, loadmodel->name);

	if (out->visleafs > 8192) // old limit warning
		Con_Warning ("Mod_LoadSubmodels: visleafs exceeds standard limit (%d, normal max = %d) in %s\n", out->visleafs, 8192, loadmodel->name);
}

/*
=================
Mod_LoadEdges
=================
*/
void Mod_LoadEdges (lump_t *l)
{
	dedge_t *in;
	medge_t *out;
	int 	i, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( (count + 1) * sizeof(*out), loadname);	

	loadmodel->edges = out;
	loadmodel->numedges = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->v[0] = (unsigned short)LittleShort(in->v[0]);
		out->v[1] = (unsigned short)LittleShort(in->v[1]);
	}
}

/*
=================
Mod_LoadTexinfo
=================
*/
void Mod_LoadTexinfo (lump_t *l)
{
	texinfo_t *in;
	mtexinfo_t *out;
	int 	i, j, count;
	int		miptex;
	float	len1, len2;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->texinfo = out;
	loadmodel->numtexinfo = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<8 ; j++)
			out->vecs[0][j] = LittleFloat (in->vecs[0][j]);
		len1 = VectorLength (out->vecs[0]);
		len2 = VectorLength (out->vecs[1]);
		len1 = (len1 + len2)/2;
		if (len1 < 0.32)
			out->mipadjust = 4;
		else if (len1 < 0.49)
			out->mipadjust = 3;
		else if (len1 < 0.99)
			out->mipadjust = 2;
		else
			out->mipadjust = 1;

		miptex = LittleLong (in->miptex);
		out->flags = LittleLong (in->flags);
	
		if (miptex >= loadmodel->numtextures-1 || !loadmodel->textures[miptex])
		{
			if (out->flags & TEX_SPECIAL)
				out->texture = loadmodel->textures[loadmodel->numtextures-1]; // checkerboard texture (texture not found)
			else
				out->texture = loadmodel->textures[loadmodel->numtextures-2]; // checkerboard texture (texture not found)
			out->flags |= TEX_MISSING;
		}
		else
		{
			out->texture = loadmodel->textures[miptex];
		}
	}

	if (missingtex > 0)
		Con_Warning ("Mod_LoadTexinfo: %d texture%s missing in %s\n", missingtex, missingtex == 1 ? " is" : "s are", loadmodel->name);
}


/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
#define MAX_SURF_EXTENTS 2048 // was 512 in glquake, 256 in winquake, fitzquake has 2000
void CalcSurfaceExtents (msurface_t *s)
{
	float	mins[2], maxs[2], val;
	int		i,j, e;
	mvertex_t	*v;
	mtexinfo_t	*tex;
	int		bmins[2], bmaxs[2];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -999999;

	tex = s->texinfo;
	
	for (i=0 ; i<s->numedges ; i++)
	{
		e = loadmodel->surfedges[s->firstedge+i];
		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];
		
		for (j=0 ; j<2 ; j++)
		{
			val = v->position[0] * tex->vecs[j][0] + 
				v->position[1] * tex->vecs[j][1] +
				v->position[2] * tex->vecs[j][2] +
				tex->vecs[j][3];
			if (val < mins[j])
				mins[j] = val;
			if (val > maxs[j])
				maxs[j] = val;
		}
	}

	for (i=0 ; i<2 ; i++)
	{	
		bmins[i] = floor(mins[i]/16);
		bmaxs[i] = ceil(maxs[i]/16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;

		if ( !(tex->flags & TEX_SPECIAL) && s->extents[i] > 512 /* 256 */ )
			Sys_Error ("Bad surface extents");
	}
}

/*
================
Mod_PolyForUnlitSurface
creates polys for unlightmapped surfaces (sky and water)

TODO: merge this into R_BuildSurfaceDisplayList?
================
*/
void Mod_PolyForUnlitSurface (msurface_t *fa)
{
	vec3_t		verts[64];
	int			numverts, i, lindex;
	float		*vec;
	glpoly_t	*poly;
	float		texscale;

	if (fa->flags & (SURF_DRAWTURB | SURF_DRAWSKY))
		texscale = (1.0/128.0); // warp animation repeats every 128
	else
		texscale = (1.0/16.0); // to match notexture_mip

	//
	// convert edges back to a normal polygon
	//
	numverts = 0;
	for (i=0 ; i<fa->numedges ; i++)
	{
		lindex = loadmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
			vec = loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position;
		else
			vec = loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position;

		if (numverts >= 64)
			Host_Error ("Mod_PolyForUnlitSurface: excessive numverts %i", numverts);

		VectorCopy (vec, verts[numverts]);
		numverts++;
	}

	//create the poly
	poly = Hunk_AllocName (sizeof(glpoly_t) + (numverts-4) * VERTEXSIZE * sizeof(float), "unlitpoly");
	poly->next = NULL;
	fa->polys = poly;
	poly->numverts = numverts;
	for (i=0, vec=(float *)verts; i<numverts; i++, vec+= 3)
	{
		VectorCopy (vec, poly->verts[i]);
		poly->verts[i][3] = DotProduct(vec, fa->texinfo->vecs[0]) * texscale;
		poly->verts[i][4] = DotProduct(vec, fa->texinfo->vecs[1]) * texscale;
	}
}

/*
=================
Mod_CalcSurfaceBounds

calculate bounding box for per-surface frustum culling
=================
*/
void Mod_CalcSurfaceBounds (msurface_t *s)
{
	int			i, e;
	mvertex_t	*v;

	s->mins[0] = s->mins[1] = s->mins[2] = 9999;
	s->maxs[0] = s->maxs[1] = s->maxs[2] = -9999;

	for (i=0 ; i<s->numedges ; i++)
	{
		e = loadmodel->surfedges[s->firstedge+i];
		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];

		if (s->mins[0] > v->position[0])
			s->mins[0] = v->position[0];
		if (s->mins[1] > v->position[1])
			s->mins[1] = v->position[1];
		if (s->mins[2] > v->position[2])
			s->mins[2] = v->position[2];

		if (s->maxs[0] < v->position[0])
			s->maxs[0] = v->position[0];
		if (s->maxs[1] < v->position[1])
			s->maxs[1] = v->position[1];
		if (s->maxs[2] < v->position[2])
			s->maxs[2] = v->position[2];
	}
}

/*
=================
Mod_LoadFaces
=================
*/
void Mod_LoadFaces (lump_t *l)
{
	dface_t		*in;
	msurface_t 	*out;
	int			i, count, surfnum;
	int			planenum, side;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	for ( surfnum=0 ; surfnum<count ; surfnum++, in++, out++)
	{
		out->firstedge = LittleLong (in->firstedge);
		out->numedges = LittleShort (in->numedges);		
		out->flags = 0;

		planenum = LittleShort (in->planenum);
		side = LittleShort (in->side);
		if (side)
			out->flags |= SURF_PLANEBACK;			

		out->plane = loadmodel->planes + planenum;

		out->texinfo = loadmodel->texinfo + LittleShort (in->texinfo);

		CalcSurfaceExtents (out);
				
		Mod_CalcSurfaceBounds (out); // for per-surface frustum culling

		// lighting info
		for (i=0 ; i<MAXLIGHTMAPS ; i++)
			out->styles[i] = in->styles[i];
		i = LittleLong(in->lightofs);
		if (i == -1)
			out->samples = NULL;
		else
			out->samples = loadmodel->lightdata + (i * 3); // lit support via lordhavoc (was "+ i") 
		
		// set the drawing flags flag
		if (!strncasecmp(out->texinfo->texture->name, "sky", 3)) // sky surface (also note: was strncmp, changed to match qbsp)
		{
			out->flags |= (SURF_DRAWSKY | SURF_DRAWTILED);
			Mod_PolyForUnlitSurface (out);
			// no more subdivision 
		}
		else if (out->texinfo->texture->name[0] == '*')		// warp surface
		{
			out->flags |= (SURF_DRAWTURB | SURF_DRAWTILED);

			// detect special liquid types
			if (!strncasecmp(out->texinfo->texture->name, "*lava", 5)
			|| !strncasecmp(out->texinfo->texture->name, "*brim", 5))
				out->flags |= SURF_DRAWLAVA;
			else if (!strncasecmp(out->texinfo->texture->name, "*slime", 6))
				out->flags |= SURF_DRAWSLIME;
			else if (!strncasecmp(out->texinfo->texture->name, "*tele", 5)
			|| !strncasecmp(out->texinfo->texture->name, "*rift", 5)
			|| !strncasecmp(out->texinfo->texture->name, "*gate", 5))
				out->flags |= SURF_DRAWTELE;
			else
				out->flags |= SURF_DRAWWATER; 

			if (!strncasecmp(out->texinfo->texture->name, "*rtex078", 8) 
			|| !strncasecmp(out->texinfo->texture->name, "*lowlight", 9))
				out->flags |= SURF_TRANSLUCENT;

			Mod_PolyForUnlitSurface (out);
			// no more subdivision 
		}
		else if (out->texinfo->flags & TEX_MISSING) // texture is missing from bsp
		{
			if (out->samples) // lightmapped
			{
				out->flags |= SURF_NOTEXTURE;
			}
			else // not lightmapped
			{
				out->flags |= (SURF_NOTEXTURE | SURF_DRAWTILED);
				Mod_PolyForUnlitSurface (out);
			}
		}
	}
}


/*
=================
Mod_SetParent
=================
*/
void Mod_SetParent (mnode_t *node, mnode_t *parent)
{
	node->parent = parent;
	if (node->contents < 0)
		return;
	Mod_SetParent (node->children[0], node);
	Mod_SetParent (node->children[1], node);
}

/*
=================
Mod_LoadNodes
=================
*/
void Mod_LoadNodes (lump_t *l)
{
	int			i, j, count, p;
	dnode_t		*in;
	mnode_t 	*out;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}
	
		p = LittleLong(in->planenum);
		out->plane = loadmodel->planes + p;

		out->firstsurface = (unsigned short)LittleShort (in->firstface);
		out->numsurfaces = (unsigned short)LittleShort (in->numfaces);
		
		for (j=0 ; j<2 ; j++)
		{
			//johnfitz -- hack to handle nodes > 32k, adapted from darkplaces
			p = (unsigned short)LittleShort (in->children[j]);

			if (p < count)
				out->children[j] = loadmodel->nodes + p;
			else
			{
				p = 65535 - p; //note this uses 65535 intentionally, -1 is leaf 0
				if (p < loadmodel->numleafs)
					out->children[j] = (mnode_t *)(loadmodel->leafs + p);
				else
				{
					Con_Warning ("Mod_LoadNodes: invalid leaf index %i (file has only %i leafs)\n", p, loadmodel->numleafs);
					out->children[j] = (mnode_t *)(loadmodel->leafs); //map it to the solid leaf
				}
			}
		}
	}
	
	Mod_SetParent (loadmodel->nodes, NULL);	// sets nodes and leafs
}

/*
=================
Mod_LoadLeafs
=================
*/
void Mod_LoadLeafs (lump_t *l)
{
	dleaf_t 	*in;
	mleaf_t 	*out;
	int			i, j, count, p;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->contents);
		out->contents = p;

		out->firstmarksurface = loadmodel->marksurfaces + (unsigned short)LittleShort (in->firstmarksurface);
		out->nummarksurfaces = (unsigned short)LittleShort (in->nummarksurfaces);
		
		p = LittleLong(in->visofs);
		if (p == -1)
			out->compressed_vis = NULL;
		else
			out->compressed_vis = loadmodel->visdata + p;
		out->efrags = NULL;
		
		for (j=0 ; j<4 ; j++)
			out->ambient_sound_level[j] = in->ambient_level[j];
	}	
}

/*
=================
Mod_LoadClipnodes
=================
*/
void Mod_LoadClipnodes (lump_t *l)
{
	dclipnode_t *in;
	mclipnode_t *out; //johnfitz -- was dclipnode_t
	int			i, count;
	hull_t		*hull;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->clipnodes = out;
	loadmodel->numclipnodes = count;

	hull = &loadmodel->hulls[1];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -16;
	hull->clip_mins[1] = -16;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 16;
	hull->clip_maxs[1] = 16;
	hull->clip_maxs[2] = 32;

	hull = &loadmodel->hulls[2];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -24;
	hull->clip_mins[1] = -24;
	hull->clip_mins[2] = -20;
	hull->clip_maxs[0] = 24;
	hull->clip_maxs[1] = 24;
	hull->clip_maxs[2] = 20;

	hull = &loadmodel->hulls[3];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -16;
	hull->clip_mins[1] = -16;
	hull->clip_mins[2] = -12;
	hull->clip_maxs[0] = 16;
	hull->clip_maxs[1] = 16;
	hull->clip_maxs[2] = 16;

	hull = &loadmodel->hulls[4];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;

	hull->clip_mins[0] = -8;
	hull->clip_mins[1] = -8;
	hull->clip_mins[2] = -8;
	hull->clip_maxs[0] = 8;
	hull->clip_maxs[1] = 8;
	hull->clip_maxs[2] = 8;

	hull = &loadmodel->hulls[5];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -48;
	hull->clip_mins[1] = -48;
	hull->clip_mins[2] = -50;
	hull->clip_maxs[0] = 48;
	hull->clip_maxs[1] = 48;
	hull->clip_maxs[2] = 50;

	for (i=0 ; i<count ; i++, out++, in++)
	{
		out->planenum = LittleLong(in->planenum);

		// bounds check
		if (out->planenum < 0 || out->planenum >= loadmodel->numplanes)
			Host_Error ("Mod_LoadClipnodes: planenum in clipnode %d out of bounds (%d, max = %d) in %s\n", i, out->planenum, loadmodel->numplanes, loadmodel->name);

		//johnfitz -- support clipnodes > 32k
		out->children[0] = (unsigned short)LittleShort(in->children[0]);
		out->children[1] = (unsigned short)LittleShort(in->children[1]);
		if (out->children[0] >= count)
			out->children[0] -= 65536;
		if (out->children[1] >= count)
			out->children[1] -= 65536;
		//johnfitz
	}
}

/*
=================
Mod_MakeHull0

Duplicate the drawing hull structure as a clipping hull
=================
*/
void Mod_MakeHull0 (void)
{
	mnode_t		*in, *child;
	mclipnode_t *out; //johnfitz -- was dclipnode_t
	int			i, j, count;
	hull_t		*hull;
	
	hull = &loadmodel->hulls[0];	
	
	in = loadmodel->nodes;
	count = loadmodel->numnodes;
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;

	for (i=0 ; i<count ; i++, out++, in++)
	{
		out->planenum = in->plane - loadmodel->planes;
		for (j=0 ; j<2 ; j++)
		{
			child = in->children[j];
			if (child->contents < 0)
				out->children[j] = child->contents;
			else
				out->children[j] = child - loadmodel->nodes;
		}
	}
}

/*
=================
Mod_LoadMarksurfaces
=================
*/
void Mod_LoadMarksurfaces (lump_t *l)
{	
	int		i, j, count;
	short		*in;
	msurface_t **out;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->marksurfaces = out;
	loadmodel->nummarksurfaces = count;

	for ( i=0 ; i<count ; i++)
	{
		j = (unsigned short)LittleShort(in[i]); //johnfitz -- explicit cast as unsigned short
		if (j >= loadmodel->numsurfaces)
			Sys_Error ("Mod_ParseMarksurfaces: bad surface number");
		out[i] = loadmodel->surfaces + j;
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/
void Mod_LoadSurfedges (lump_t *l)
{	
	int		i, count;
	int		*in, *out;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->surfedges = out;
	loadmodel->numsurfedges = count;

	for ( i=0 ; i<count ; i++)
		out[i] = LittleLong (in[i]);
}


/*
=================
Mod_LoadPlanes
=================
*/
void Mod_LoadPlanes (lump_t *l)
{
	int			i, j;
	mplane_t	*out;
	dplane_t 	*in;
	int			count;
	int			bits;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*2*sizeof(*out), loadname);	
	
	loadmodel->planes = out;
	loadmodel->numplanes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		bits = 0;
		for (j=0 ; j<3 ; j++)
		{
			out->normal[j] = LittleFloat (in->normal[j]);
			if (out->normal[j] < 0)
				bits |= 1<<j;
		}

		out->dist = LittleFloat (in->dist);
		out->type = LittleLong (in->type);
		out->signbits = bits;
	}
}

/*
=================
RadiusFromBounds
=================
*/
float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	int		i;
	vec3_t	corner;

	for (i=0 ; i<3 ; i++)
	{
		corner[i] = fabs(mins[i]) > fabs(maxs[i]) ? fabs(mins[i]) : fabs(maxs[i]);
	}

	return VectorLength (corner);
}

/*
=================
Mod_LoadBrushModel
=================
*/
void Mod_LoadBrushModel (model_t *mod, void *buffer)
{
	int			i, j;
	dheader_t	*header;
	dmodel_t 	*bm;
	float		radius;
	
	loadmodel->type = mod_brush;
	
	header = (dheader_t *)buffer;

	mod->bspversion = LittleLong (header->version);
	if (mod->bspversion != BSPVERSION)
		Host_Error ("Mod_LoadBrushModel: %s has wrong version number (%i should be %i (Hexen II))", mod->name, mod->bspversion, BSPVERSION); // was Sys_Error

	{
		// isworldmodel
		qboolean servermatch = sv.modelname[0] && !strcasecmp (loadname, sv.name);
		qboolean clientmatch = cl.worldname[0] && !strcasecmp (loadname, cl.worldname);
		Con_DPrintf ("loadname: %s\n", loadname);
		if (servermatch)
			Con_DPrintf ("sv.modelname: %s\n", sv.modelname);
		if (clientmatch)
			Con_DPrintf ("cl.modelname: %s\n", cl.worldname);
		loadmodel->isworldmodel = servermatch || clientmatch;
	}

// swap all the lumps
	mod_base = (byte *)header;

	for (i=0 ; i<sizeof(dheader_t)/4 ; i++)
		((int *)header)[i] = LittleLong ( ((int *)header)[i]);

// load into heap
	
	Mod_LoadVertexes (&header->lumps[LUMP_VERTEXES]);
	Mod_LoadEdges (&header->lumps[LUMP_EDGES]);
	Mod_LoadSurfedges (&header->lumps[LUMP_SURFEDGES]);
	Mod_LoadTextures (&header->lumps[LUMP_TEXTURES]);
	Mod_LoadLighting (&header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes (&header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo (&header->lumps[LUMP_TEXINFO]);
	Mod_LoadFaces (&header->lumps[LUMP_FACES]);
	Mod_LoadMarksurfaces (&header->lumps[LUMP_MARKSURFACES]);
	Mod_LoadVisibility (&header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs (&header->lumps[LUMP_LEAFS]);
	Mod_LoadNodes (&header->lumps[LUMP_NODES]);
	Mod_LoadClipnodes (&header->lumps[LUMP_CLIPNODES]);
	Mod_LoadEntities (&header->lumps[LUMP_ENTITIES]);
	Mod_LoadSubmodels (&header->lumps[LUMP_MODELS]);

	Mod_MakeHull0 ();
	
	mod->numframes = 2;		// regular and alternate animation
	
//
// set up the submodels (FIXME: this is confusing)
//

	// okay, so that i stop getting confused every time i look at this loop, here's how it works:
	// we're looping through the submodels starting at 0.  Submodel 0 is the main model, so we don't have to
	// worry about clobbering data the first time through, since it's the same data.  At the end of the loop,
	// we create a new copy of the data to use the next time through.
	for (i=0 ; i<mod->numsubmodels ; i++)
	{
		bm = &mod->submodels[i];

		mod->hulls[0].firstclipnode = bm->headnode[0];
		for (j=1 ; j<MAX_MAP_HULLS ; j++)
		{
			mod->hulls[j].firstclipnode = bm->headnode[j];
			mod->hulls[j].lastclipnode = mod->numclipnodes-1;
		}
		
		mod->firstmodelsurface = bm->firstface;
		mod->nummodelsurfaces = bm->numfaces;
		
		VectorCopy (bm->maxs, mod->maxs);
		VectorCopy (bm->mins, mod->mins);

		// calculate rotate bounds and yaw bounds
		radius = RadiusFromBounds (mod->mins, mod->maxs);
		mod->rmaxs[0] = mod->rmaxs[1] = mod->rmaxs[2] = mod->ymaxs[0] = mod->ymaxs[1] = mod->ymaxs[2] = radius;
		mod->rmins[0] = mod->rmins[1] = mod->rmins[2] = mod->ymins[0] = mod->ymins[1] = mod->ymins[2] = -radius;

		mod->numleafs = bm->visleafs;

		if (i < mod->numsubmodels-1)
		{	// duplicate the basic information
			char	name[10];

			sprintf (name, "*%i", i+1);
			loadmodel = Mod_FindName (name);
			*loadmodel = *mod;
			strcpy (loadmodel->name, name);
			mod = loadmodel;
		}
	}
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

aliashdr_t	*pheader;

stvert_t	stverts[MAXALIASVERTS];
mtriangle_t	triangles[MAXALIASTRIS];

// a pose is a single set of vertexes.  a frame may be
// an animating sequence of poses
trivertx_t	*poseverts[MAXALIASFRAMES];
int			posenum;

byte		player_texels[MAX_PLAYER_CLASS][620*245];

/*
=================
Mod_LoadAliasFrame
=================
*/
void * Mod_LoadAliasFrame (void * pin, maliasframedesc_t *frame)
{
	trivertx_t		*pinframe;
	int				i;
	daliasframe_t	*pdaliasframe;

	pdaliasframe = (daliasframe_t *)pin;

	strcpy (frame->name, pdaliasframe->name);
	frame->firstpose = posenum;
	frame->numposes = 1;

	for (i=0 ; i<3 ; i++)
	{
	// these are byte values, so we don't have to worry about
	// endianness
		frame->bboxmin.v[i] = pdaliasframe->bboxmin.v[i];
		frame->bboxmax.v[i] = pdaliasframe->bboxmax.v[i];
	}

	pinframe = (trivertx_t *)(pdaliasframe + 1);

	if (posenum >= MAXALIASFRAMES)
		Sys_Error ("Mod_LoadAliasFrame: invalid # of frames (%d, max = %d) in %s", posenum, MAXALIASFRAMES, loadmodel->name);

	poseverts[posenum] = pinframe;
	posenum++;

	pinframe += pheader->numverts;

	return (void *)pinframe;
}


/*
=================
Mod_LoadAliasGroup
=================
*/
void *Mod_LoadAliasGroup (void *pin,  maliasframedesc_t *frame)
{
	daliasgroup_t		*pingroup;
	int					i, numframes;
	daliasinterval_t	*pin_intervals;
	void				*ptemp;
	
	pingroup = (daliasgroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	frame->firstpose = posenum;
	frame->numposes = numframes;

	for (i=0 ; i<3 ; i++)
	{
	// these are byte values, so we don't have to worry about endianness
		frame->bboxmin.v[i] = pingroup->bboxmin.v[i];
		frame->bboxmax.v[i] = pingroup->bboxmax.v[i];
	}

	pin_intervals = (daliasinterval_t *)(pingroup + 1);

	frame->interval = LittleFloat (pin_intervals->interval);

	pin_intervals += numframes;

	ptemp = (void *)pin_intervals;

	for (i=0 ; i<numframes ; i++)
	{
		if (posenum >= MAXALIASFRAMES)
			Sys_Error ("Mod_LoadAliasGroup: invalid # of frames (%d, max = %d) in %s", posenum, MAXALIASFRAMES, loadmodel->name);

		poseverts[posenum] = (trivertx_t *)((daliasframe_t *)ptemp + 1);
		posenum++;

		ptemp = (trivertx_t *)((daliasframe_t *)ptemp + 1) + pheader->numverts;
	}

	return ptemp;
}

//=========================================================


/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes - Ed
=================
*/

typedef struct
{
	short		x, y;
} floodfill_t;

// must be a power of 2
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP( off, dx, dy ) \
do { \
	if (pos[off] == fillcolor) \
	{ \
		pos[off] = 255; \
		fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
		inpt = (inpt + 1) & FLOODFILL_FIFO_MASK; \
	} \
	else if (pos[off] != 255) fdc = pos[off]; \
} while (0)

void Mod_FloodFillSkin( byte *skin, int skinwidth, int skinheight )
{
	byte				fillcolor = *skin; // assume this is the pixel to fill
	floodfill_t			fifo[FLOODFILL_FIFO_SIZE];
	int					inpt = 0, outpt = 0;
	int					filledcolor = -1;
	int					i, size = skinwidth * skinheight, notfill;

	if (filledcolor == -1)
	{
		filledcolor = 0;
		// attempt to find opaque black
		for (i = 0; i < 256; ++i)
			if (d_8to24table[i] == (255 << 0)) // alpha 1.0
			{
				filledcolor = i;
				break;
			}
	}

	// can't fill to filled color or to transparent color (used as visited marker)
	if ((fillcolor == filledcolor) || (fillcolor == 255))
	{
//		Con_Warning ("Mod_FloodFillSkin: not filling skin from %d to %d\n", fillcolor, filledcolor);
		return;
	}

	for (i = notfill = 0; i < size && notfill < 2; ++i)
	{
		if (skin[i] != fillcolor)
			++notfill;
	}

	// don't fill almost mono-coloured texes
	if (notfill < 2)
	{
//		Con_Warning ("Mod_FloodFillSkin: not filling skin in %s\n", loadmodel->name);
		return;
	}

	fifo[inpt].x = 0, fifo[inpt].y = 0;
	inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

	while (outpt != inpt)
	{
		int			x = fifo[outpt].x, y = fifo[outpt].y;
		int			fdc = filledcolor;
		byte		*pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

		if (x > 0)				FLOODFILL_STEP( -1, -1, 0 );
		if (x < skinwidth - 1)	FLOODFILL_STEP( 1, 1, 0 );
		if (y > 0)				FLOODFILL_STEP( -skinwidth, 0, -1 );
		if (y < skinheight - 1)	FLOODFILL_STEP( skinwidth, 0, 1 );
		skin[x + skinwidth * y] = fdc;
	}
}

/*
===============
Mod_LoadAllSkins
===============
*/
void *Mod_LoadAllSkins (int numskins, daliasskintype_t *pskintype, int mdl_flags)
{
	int		i, size;
//	char	name[32];
	char	skinname[64];
	byte	*skin;
	unsigned		texture_mode;
	unsigned				offset; //johnfitz
	
	skin = (byte *)(pskintype + 1);

	if (numskins < 1 || numskins > MAX_SKINS)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of skins: %d\n", numskins);

	size = pheader->skinwidth * pheader->skinheight;
	for (i=0 ; i<numskins ; i++)
	{
		Mod_FloodFillSkin( skin, pheader->skinwidth, pheader->skinheight );

		// save 8 bit texels for the player model to remap
		if (!strcmp(loadmodel->name, "models/paladin.mdl"))
		{
			if (size > sizeof(player_texels[0]))
				Sys_Error ("Player skin too large");
			memcpy (player_texels[0], (byte *)(pskintype + 1), size);
		}
		else if (!strcmp(loadmodel->name, "models/crusader.mdl"))
		{
			if (size > sizeof(player_texels[1]))
				Sys_Error ("Player skin too large");
			memcpy (player_texels[1], (byte *)(pskintype + 1), size);
		}
		else if (!strcmp(loadmodel->name, "models/necro.mdl"))
		{
			if (size > sizeof(player_texels[2]))
				Sys_Error ("Player skin too large");
			memcpy (player_texels[2], (byte *)(pskintype + 1), size);
		}
		else if (!strcmp(loadmodel->name, "models/assassin.mdl"))
		{
			if (size > sizeof(player_texels[3]))
				Sys_Error ("Player skin too large");
			memcpy (player_texels[3], (byte *)(pskintype + 1), size);
		}
		else if (!strcmp(loadmodel->name, "models/succubus.mdl"))
		{
			if (size > sizeof(player_texels[4]))
				Sys_Error ("Player skin too large");
			memcpy (player_texels[4], (byte *)(pskintype + 1), size);
		}


		if( mdl_flags & EF_HOLEY )
			texture_mode = TEXPREF_HOLEY; // was 2
		else if( mdl_flags & EF_TRANSPARENT )
			texture_mode = TEXPREF_TRANSPARENT; // was 1
		else if( mdl_flags & EF_SPECIAL_TRANS )
			texture_mode = TEXPREF_SPECIAL_TRANS; // was 3
		else
			texture_mode = TEXPREF_NONE; // 0

		offset = (unsigned)(pskintype+1) - (unsigned)mod_base;
		if (IsFullbright ((byte *)(pskintype+1), size))
		{
			sprintf (skinname, "%s:frame%i", loadmodel->name, i);
			pheader->gltexture[i] = GL_LoadTexture (loadmodel, skinname, pheader->skinwidth, pheader->skinheight, SRC_INDEXED, (byte *)(pskintype+1), loadmodel->name, offset, TEXPREF_PAD | TEXPREF_NOBRIGHT | texture_mode);

			sprintf (skinname, "%s:frame%i_glow", loadmodel->name, i);
			pheader->fullbright[i] = GL_LoadTexture (loadmodel, skinname, pheader->skinwidth, pheader->skinheight, SRC_INDEXED, (byte *)(pskintype+1), loadmodel->name, offset, TEXPREF_PAD | TEXPREF_FULLBRIGHT | texture_mode);
		}
		else
		{
			sprintf (skinname, "%s:frame%i", loadmodel->name, i);
			pheader->gltexture[i] = GL_LoadTexture (loadmodel, skinname, pheader->skinwidth, pheader->skinheight, SRC_INDEXED, (byte *)(pskintype+1), loadmodel->name, offset, TEXPREF_PAD | texture_mode);
			pheader->fullbright[i] = NULL;
		}

		pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + size);
	}

	return (void *)pskintype;
}

//=========================================================================

/*
=================
Mod_CalcAliasBounds

calculate bounds of alias model for nonrotated, yawrotated, and fullrotated cases
=================
*/
void Mod_CalcAliasBounds (aliashdr_t *a)
{
	int			i,j,k;
	float		dist, yawradius, radius;
	vec3_t		v;

	//clear out all data
	for (i=0; i<3;i++)
	{
		loadmodel->mins[i] = loadmodel->ymins[i] = loadmodel->rmins[i] = 999999;
		loadmodel->maxs[i] = loadmodel->ymaxs[i] = loadmodel->rmaxs[i] = -999999;
		radius = yawradius = 0;
	}

	//process verts
	for (i=0 ; i<a->numposes; i++)
		for (j=0; j<a->numverts; j++)
		{
			for (k=0; k<3;k++)
				v[k] = poseverts[i][j].v[k] * pheader->scale[k] + pheader->scale_origin[k];

			for (k=0; k<3;k++)
			{
				loadmodel->mins[k] = min (loadmodel->mins[k], v[k]);
				loadmodel->maxs[k] = max (loadmodel->maxs[k], v[k]);
			}
			dist = v[0] * v[0] + v[1] * v[1];
			if (yawradius < dist)
				yawradius = dist;
			dist += v[2] * v[2];
			if (radius < dist)
				radius = dist;
		}

	//rbounds will be used when entity has nonzero pitch or roll
	radius = sqrt(radius);
	loadmodel->rmins[0] = loadmodel->rmins[1] = loadmodel->rmins[2] = -radius;
	loadmodel->rmaxs[0] = loadmodel->rmaxs[1] = loadmodel->rmaxs[2] = radius;

	//ybounds will be used when entity has nonzero yaw
	yawradius = sqrt(yawradius);
	loadmodel->ymins[0] = loadmodel->ymins[1] = -yawradius;
	loadmodel->ymaxs[0] = loadmodel->ymaxs[1] = yawradius;
	loadmodel->ymins[2] = loadmodel->mins[2];
	loadmodel->ymaxs[2] = loadmodel->maxs[2];
}


/*
=================
Mod_LoadAliasModel
=================
*/
void Mod_LoadAliasModel (model_t *mod, void *buffer)
{
	int					i, j;
	mdl_t				*pinmodel;
	stvert_t			*pinstverts;
	dtriangle_t			*pintriangles;
	int					version, numframes;
	int					size;
	daliasframetype_t	*pframetype;
	daliasskintype_t	*pskintype;
	int					start, end, total;
	
	start = Hunk_LowMark ();

	pinmodel = (mdl_t *)buffer;
	mod_base = (byte *)buffer;

	version = LittleLong (pinmodel->version);
	if (version != ALIAS_VERSION)
		Sys_Error ("%s has wrong version number (%i should be %i)",
				 mod->name, version, ALIAS_VERSION);

//	Con_Printf("Loading OLD id model %s\n",mod->name);
//
// allocate space for a working header, plus all the data except the frames,
// skin and group info
//
	size = 	sizeof (aliashdr_t) 
			+ (LittleLong (pinmodel->numframes) - 1) *
			sizeof (pheader->frames[0]);
	pheader = Hunk_AllocName (size, loadname);
	
	mod->flags = LittleLong (pinmodel->flags);
	
	if (mod->flags & EF_FACE_VIEW)
		Con_Printf ("model %s has EF_FACE_VIEW flag\n", mod->name);

//
// endian-adjust and copy the data, starting with the alias model header
//
	pheader->boundingradius = LittleFloat (pinmodel->boundingradius);
	pheader->numskins = LittleLong (pinmodel->numskins);
	pheader->skinwidth = LittleLong (pinmodel->skinwidth);
	pheader->skinheight = LittleLong (pinmodel->skinheight);

	if (pheader->skinheight > MAX_SKIN_HEIGHT)
		Sys_Error ("model %s has a skin taller than %d", mod->name,
				   MAX_SKIN_HEIGHT);

	pheader->numverts = LittleLong (pinmodel->numverts);
	pheader->version = pheader->numverts;	//hide num_st in version

	if (pheader->numverts <= 0)
		Sys_Error ("model %s has no vertices", mod->name);

	if (pheader->numverts > MAXALIASVERTS)
		Sys_Error ("model %s has too many vertices", mod->name);

	pheader->numtris = LittleLong (pinmodel->numtris);

	if (pheader->numtris <= 0)
		Sys_Error ("model %s has no triangles", mod->name);

	pheader->numframes = LittleLong (pinmodel->numframes);
	numframes = pheader->numframes;
	if (numframes < 1)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of frames: %d\n", numframes);

	pheader->size = LittleFloat (pinmodel->size) * ALIAS_BASE_SIZE_RATIO;
	mod->synctype = LittleLong (pinmodel->synctype);
	mod->numframes = pheader->numframes;

	for (i=0 ; i<3 ; i++)
	{
		pheader->scale[i] = LittleFloat (pinmodel->scale[i]);
		pheader->scale_origin[i] = LittleFloat (pinmodel->scale_origin[i]);
		pheader->eyeposition[i] = LittleFloat (pinmodel->eyeposition[i]);
	}


//
// load the skins
//
	pskintype = (daliasskintype_t *)&pinmodel[1];
	pskintype = Mod_LoadAllSkins (pheader->numskins, pskintype, mod->flags);

//
// load base s and t vertices
//
	pinstverts = (stvert_t *)pskintype;

	for (i=0 ; i<pheader->version ; i++)	//version holds num_st_verts
	{
		stverts[i].onseam = LittleLong (pinstverts[i].onseam);
		stverts[i].s = LittleLong (pinstverts[i].s);
		stverts[i].t = LittleLong (pinstverts[i].t);
	}

//
// load triangle lists
//
	pintriangles = (dtriangle_t *)&pinstverts[pheader->numverts];

	for (i=0 ; i<pheader->numtris ; i++)
	{
		triangles[i].facesfront = LittleLong (pintriangles[i].facesfront);

		for (j=0 ; j<3 ; j++)
		{
			triangles[i].vertindex[j] =	(unsigned short)LittleLong (pintriangles[i].vertindex[j]);
			triangles[i].stindex[j]	  = triangles[i].vertindex[j];
		}
	}

//
// load the frames
//
	posenum = 0;
	pframetype = (daliasframetype_t *)&pintriangles[pheader->numtris];

	for (i=0 ; i<numframes ; i++)
	{
		aliasframetype_t	frametype;
		frametype = LittleLong (pframetype->type);
		if (frametype == ALIAS_SINGLE)
			pframetype = (daliasframetype_t *) Mod_LoadAliasFrame (pframetype + 1, &pheader->frames[i]);
		else
			pframetype = (daliasframetype_t *) Mod_LoadAliasGroup (pframetype + 1, &pheader->frames[i]);
	}

	pheader->numposes = posenum;

	mod->type = mod_alias;

	
	Mod_CalcAliasBounds (pheader); // calc correct bounds

	//
	// build the draw lists
	//
	R_MakeAliasModelDisplayLists (mod, pheader);

//
// move the complete, relocatable alias model to the cache
//	
	end = Hunk_LowMark ();
	total = end - start;
	
	Cache_Alloc (&mod->cache, total, loadname);
	if (!mod->cache.data)
		return;
	memcpy (mod->cache.data, pheader, total);

	Hunk_FreeToLowMark (start);
}


/*
=================
Mod_LoadAliasModelNew

reads extra field for num ST verts, and extra index list of them
=================
*/
void Mod_LoadAliasModelNew (model_t *mod, void *buffer)
{
	int					i, j;
	newmdl_t			*pinmodel;
	stvert_t			*pinstverts;
	dnewtriangle_t		*pintriangles;
	int					version, numframes;
	int					size;
	daliasframetype_t	*pframetype;
	daliasskintype_t	*pskintype;
	int					start, end, total;
	
	start = Hunk_LowMark ();

	pinmodel = (newmdl_t *)buffer;

	version = LittleLong (pinmodel->version);
	if (version != ALIAS_NEWVERSION)
		Sys_Error ("%s has wrong version number (%i should be %i)",
				 mod->name, version, ALIAS_NEWVERSION);

//	Con_Printf("Loading NEW raven model %s\n",mod->name);
//
// allocate space for a working header, plus all the data except the frames,
// skin and group info
//
	size = 	sizeof (aliashdr_t) 
			+ (LittleLong (pinmodel->numframes) - 1) *
			sizeof (pheader->frames[0]);
	pheader = Hunk_AllocName (size, loadname);
	
	mod->flags = LittleLong (pinmodel->flags);

	if (mod->flags & EF_FACE_VIEW)
		Con_Printf ("model %s has EF_FACE_VIEW flag\n", mod->name);

//
// endian-adjust and copy the data, starting with the alias model header
//
	pheader->boundingradius = LittleFloat (pinmodel->boundingradius);
	pheader->numskins = LittleLong (pinmodel->numskins);
	pheader->skinwidth = LittleLong (pinmodel->skinwidth);
	pheader->skinheight = LittleLong (pinmodel->skinheight);

	if (pheader->skinheight > MAX_SKIN_HEIGHT)
		Sys_Error ("model %s has a skin taller than %d", mod->name,
				   MAX_SKIN_HEIGHT);

	pheader->numverts = LittleLong (pinmodel->numverts);
	pheader->version = LittleLong (pinmodel->num_st_verts);	//hide num_st in version
	
	if (pheader->numverts <= 0)
		Sys_Error ("model %s has no vertices", mod->name);

	if (pheader->numverts > MAXALIASVERTS)
		Sys_Error ("model %s has too many vertices", mod->name);

	pheader->numtris = LittleLong (pinmodel->numtris);

	if (pheader->numtris <= 0)
		Sys_Error ("model %s has no triangles", mod->name);

	pheader->numframes = LittleLong (pinmodel->numframes);
	numframes = pheader->numframes;
	if (numframes < 1)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of frames: %d\n", numframes);

	pheader->size = LittleFloat (pinmodel->size) * ALIAS_BASE_SIZE_RATIO;
	mod->synctype = LittleLong (pinmodel->synctype);
	mod->numframes = pheader->numframes;

	for (i=0 ; i<3 ; i++)
	{
		pheader->scale[i] = LittleFloat (pinmodel->scale[i]);
		pheader->scale_origin[i] = LittleFloat (pinmodel->scale_origin[i]);
		pheader->eyeposition[i] = LittleFloat (pinmodel->eyeposition[i]);
	}

//
// load the skins
//
	pskintype = (daliasskintype_t *)&pinmodel[1];
	pskintype = Mod_LoadAllSkins (pheader->numskins, pskintype, mod->flags);

//
// load base s and t vertices
//
	pinstverts = (stvert_t *)pskintype;

	for (i=0 ; i<pheader->version ; i++)	//version holds num_st_verts
	{
		stverts[i].onseam = LittleLong (pinstverts[i].onseam);
		stverts[i].s = LittleLong (pinstverts[i].s);
		stverts[i].t = LittleLong (pinstverts[i].t);
	}

//
// load triangle lists
//
	pintriangles = (dnewtriangle_t *)&pinstverts[pheader->version];

	for (i=0 ; i<pheader->numtris ; i++)
	{
		triangles[i].facesfront = LittleLong (pintriangles[i].facesfront);

		for (j=0 ; j<3 ; j++)
		{
			triangles[i].vertindex[j] = LittleShort (pintriangles[i].vertindex[j]);
			triangles[i].stindex[j]	  = LittleShort (pintriangles[i].stindex[j]);
		}
	}

//
// load the frames
//
	posenum = 0;
	pframetype = (daliasframetype_t *)&pintriangles[pheader->numtris];

	for (i=0 ; i<numframes ; i++)
	{
		aliasframetype_t	frametype;
		frametype = LittleLong (pframetype->type);
		if (frametype == ALIAS_SINGLE)
			pframetype = (daliasframetype_t *) Mod_LoadAliasFrame (pframetype + 1, &pheader->frames[i]);
		else
			pframetype = (daliasframetype_t *) Mod_LoadAliasGroup (pframetype + 1, &pheader->frames[i]);
	}

	pheader->numposes = posenum;

	mod->type = mod_alias;

	
	Mod_CalcAliasBounds (pheader); // calc correct bounds

	//
	// build the draw lists
	//
	R_MakeAliasModelDisplayLists (mod, pheader);

//
// move the complete, relocatable alias model to the cache
//	
	end = Hunk_LowMark ();
	total = end - start;
	
	Cache_Alloc (&mod->cache, total, loadname);
	if (!mod->cache.data)
		return;
	memcpy (mod->cache.data, pheader, total);

	Hunk_FreeToLowMark (start);
}


//=============================================================================


/*
=================
Mod_LoadSpriteFrame
=================
*/
void *Mod_LoadSpriteFrame (void *pin, mspriteframe_t **ppframe, int framenum)
{
	dspriteframe_t		*pinframe;
	mspriteframe_t		*pspriteframe;
	int					width, height, size, origin[2];
	char				name[64];
	unsigned			offset; //johnfitz

	pinframe = (dspriteframe_t *)pin;

	width = LittleLong (pinframe->width);
	height = LittleLong (pinframe->height);
	size = width * height;

	pspriteframe = Hunk_AllocName (sizeof (mspriteframe_t),loadname);

	memset (pspriteframe, 0, sizeof (mspriteframe_t));

	*ppframe = pspriteframe;

	pspriteframe->width = width;
	pspriteframe->height = height;
	origin[0] = LittleLong (pinframe->origin[0]);
	origin[1] = LittleLong (pinframe->origin[1]);

	pspriteframe->up = origin[1];
	pspriteframe->down = origin[1] - height;
	pspriteframe->left = origin[0];
	pspriteframe->right = width + origin[0];



	sprintf (name, "%s:frame%i", loadmodel->name, framenum);
	offset = (unsigned)(pinframe+1) - (unsigned)mod_base; //johnfitz
	pspriteframe->gltexture = GL_LoadTexture (loadmodel, name, width, height, SRC_INDEXED, (byte *)(pinframe+1), loadmodel->name, offset, TEXPREF_PAD | TEXPREF_ALPHA | TEXPREF_NOPICMIP);


	return (void *)((byte *)pinframe + sizeof (dspriteframe_t) + size);
}


/*
=================
Mod_LoadSpriteGroup
=================
*/
void *Mod_LoadSpriteGroup (void *pin, mspriteframe_t **ppframe, int framenum)
{
	dspritegroup_t		*pingroup;
	mspritegroup_t		*pspritegroup;
	int					i, numframes;
	dspriteinterval_t	*pin_intervals;
	float				*poutintervals;
	void				*ptemp;

	pingroup = (dspritegroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	pspritegroup = Hunk_AllocName (sizeof (mspritegroup_t) +
				(numframes - 1) * sizeof (pspritegroup->frames[0]), loadname);

	pspritegroup->numframes = numframes;

	*ppframe = (mspriteframe_t *)pspritegroup;

	pin_intervals = (dspriteinterval_t *)(pingroup + 1);

	poutintervals = Hunk_AllocName (numframes * sizeof (float), loadname);

	pspritegroup->intervals = poutintervals;

	for (i=0 ; i<numframes ; i++)
	{
		*poutintervals = LittleFloat (pin_intervals->interval);
		if (*poutintervals <= 0.0)
			Sys_Error ("Mod_LoadSpriteGroup: interval<=0");

		poutintervals++;
		pin_intervals++;
	}

	ptemp = (void *)pin_intervals;

	for (i=0 ; i<numframes ; i++)
	{
		ptemp = Mod_LoadSpriteFrame ( ptemp, &pspritegroup->frames[i], framenum * 100 + i);
	}

	return ptemp;
}


/*
=================
Mod_LoadSpriteModel
=================
*/
void Mod_LoadSpriteModel (model_t *mod, void *buffer)
{
	int					i;
	int					version;
	dsprite_t			*pin;
	msprite_t			*psprite;
	int					numframes;
	int					size;
	dspriteframetype_t	*pframetype;
	
	pin = (dsprite_t *)buffer;

	version = LittleLong (pin->version);
	if (version != SPRITE_VERSION)
		Sys_Error ("%s has wrong version number "
				 "(%i should be %i)", mod->name, version, SPRITE_VERSION);

	numframes = LittleLong (pin->numframes);

	size = sizeof (msprite_t) +	(numframes - 1) * sizeof (psprite->frames);

	psprite = Hunk_AllocName (size, loadname);

	mod->cache.data = psprite;

	psprite->type = LittleLong (pin->type);
	psprite->maxwidth = LittleLong (pin->width);
	psprite->maxheight = LittleLong (pin->height);
	psprite->beamlength = LittleFloat (pin->beamlength);
	mod->synctype = LittleLong (pin->synctype);
	psprite->numframes = numframes;

	mod->mins[0] = mod->mins[1] = -psprite->maxwidth/2;
	mod->maxs[0] = mod->maxs[1] = psprite->maxwidth/2;
	mod->mins[2] = -psprite->maxheight/2;
	mod->maxs[2] = psprite->maxheight/2;
	
//
// load the frames
//
	if (numframes < 1)
		Sys_Error ("Mod_LoadSpriteModel: Invalid # of frames: %d\n", numframes);

	mod->numframes = numframes;

	pframetype = (dspriteframetype_t *)(pin + 1);

	for (i=0 ; i<numframes ; i++)
	{
		spriteframetype_t	frametype;

		frametype = LittleLong (pframetype->type);
		psprite->frames[i].type = frametype;

		if (frametype == SPR_SINGLE)
		{
			pframetype = (dspriteframetype_t *)
					Mod_LoadSpriteFrame ( pframetype + 1,
										 &psprite->frames[i].frameptr, i);
		}
		else
		{
			pframetype = (dspriteframetype_t *)
					Mod_LoadSpriteGroup ( pframetype + 1,
										 &psprite->frames[i].frameptr, i);
		}
	}

	mod->type = mod_sprite;
}

//=============================================================================

/*
================
Mod_Print
================
*/
void Mod_Print (void)
{
	int		i;
	model_t	*mod;

	Con_Printf ("Cached models:\n");
	for (i=0, mod=mod_known ; i < mod_numknown ; i++, mod++)
	{
		Con_Printf ("%8p : %s\n",mod->cache.data, mod->name);
	}
}

