// gl_warp.c -- sky and water polygons

#include "quakedef.h"

extern	model_t	*loadmodel;

#define	MAX_CLIP_VERTS 64

float	skymins[2][6], skymaxs[2][6];
float	skyflatcolor[3];

int		sky_width[6], sky_height[6];

char	skybox_name[MAX_QPATH] = ""; // name of current skybox, or "" if no skybox
int		skytexorder[6] = {0,2,1,3,4,5}; // for skybox
char	*suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"}; // for skybox


gltexture_t		*solidskytexture;
gltexture_t		*alphaskytexture;
float	speedscale;		// for top sky and bottom sky

msurface_t	*warpface;

#define	SUBDIVIDE_SIZE	64

void BoundPoly (int numverts, float *verts, vec3_t mins, vec3_t maxs)
{
	int		i, j;
	float	*v;

	mins[0] = mins[1] = mins[2] = 9999;
	maxs[0] = maxs[1] = maxs[2] = -9999;
	v = verts;
	for (i=0 ; i<numverts ; i++)
		for (j=0 ; j<3 ; j++, v++)
		{
			if (*v < mins[j])
				mins[j] = *v;
			if (*v > maxs[j])
				maxs[j] = *v;
		}
}

void SubdividePolygon (int numverts, float *verts)
{
	int		i, j, k;
	vec3_t	mins, maxs;
	float	m;
	float	*v;
	vec3_t	front[64], back[64];
	int		f, b;
	float	dist[64];
	float	frac;
	glpoly_t	*poly;
	float	s, t;

	if (numverts > 60)
		Sys_Error ("numverts = %i", numverts);

	BoundPoly (numverts, verts, mins, maxs);

	for (i=0 ; i<3 ; i++)
	{
		m = (mins[i] + maxs[i]) * 0.5;
		m = SUBDIVIDE_SIZE * floor (m/SUBDIVIDE_SIZE + 0.5);
		if (maxs[i] - m < 8)
			continue;
		if (m - mins[i] < 8)
			continue;

		// cut it
		v = verts + i;
		for (j=0 ; j<numverts ; j++, v+= 3)
			dist[j] = *v - m;

		// wrap cases
		dist[j] = dist[0];
		v-=i;
		VectorCopy (verts, v);

		f = b = 0;
		v = verts;
		for (j=0 ; j<numverts ; j++, v+= 3)
		{
			if (dist[j] >= 0)
			{
				VectorCopy (v, front[f]);
				f++;
			}
			if (dist[j] <= 0)
			{
				VectorCopy (v, back[b]);
				b++;
			}
			if (dist[j] == 0 || dist[j+1] == 0)
				continue;
			if ( (dist[j] > 0) != (dist[j+1] > 0) )
			{
				// clip point
				frac = dist[j] / (dist[j] - dist[j+1]);
				for (k=0 ; k<3 ; k++)
					front[f][k] = back[b][k] = v[k] + frac*(v[3+k] - v[k]);
				f++;
				b++;
			}
		}

		SubdividePolygon (f, front[0]);
		SubdividePolygon (b, back[0]);
		return;
	}

	poly = Hunk_AllocName (sizeof(glpoly_t) + (numverts-4) * VERTEXSIZE * sizeof(float), "warpoly");
	poly->next = warpface->polys;
	warpface->polys = poly;
	poly->numverts = numverts;
	for (i=0 ; i<numverts ; i++, verts+= 3)
	{
		VectorCopy (verts, poly->verts[i]);
		s = DotProduct (verts, warpface->texinfo->vecs[0]);
		t = DotProduct (verts, warpface->texinfo->vecs[1]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;
	}
}

/*
================
GL_SubdivideSurface

Breaks a polygon up along axial 64 unit
boundaries so that turbulent and sky warps
can be done reasonably.
================
*/
void GL_SubdivideSurface (msurface_t *fa)
{
	vec3_t		verts[64];
	int			numverts;
	int			i;
	int			lindex;
	float		*vec;

	warpface = fa;

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
		VectorCopy (vec, verts[numverts]);
		numverts++;
	}

	SubdividePolygon (numverts, verts[0]);
}

//=========================================================



// speed up sin calculations - Ed
float	turbsin[] =
{
	#include "gl_warp_sin.h"
};
#define TURBSCALE (256.0 / (2 * M_PI))

/*
=============
EmitWaterPolys

Does a water warp on the pre-fragmented glpoly_t chain
=============
*/
void EmitWaterPolys (msurface_t *fa)
{
	glpoly_t	*p;
	float		*v;
	int			i;
	float		s, t, os, ot;


	for (p=fa->polys ; p ; p=p->next)
	{
		glBegin (GL_POLYGON);
		for (i=0,v=p->verts[0] ; i<p->numverts ; i++, v+=VERTEXSIZE)
		{
			os = v[3];
			ot = v[4];

			s = os + turbsin[(int)((ot*0.125+realtime) * TURBSCALE) & 255];
			s *= (1.0/64);

			t = ot + turbsin[(int)((os*0.125+realtime) * TURBSCALE) & 255];
			t *= (1.0/64);

			glTexCoord2f (s, t);
			glVertex3fv (v);
		}
		glEnd ();
	}
}



#define MAX_SAVE 10000

static float buffer_s[MAX_SAVE], buffer_t[MAX_SAVE];
static int buffer_pos;

/*
=============
EmitSkyPolys
=============
*/
void EmitSkyPolys (msurface_t *fa, qboolean save)
{
	glpoly_t	*p;
	float		*v;
	int			i;
	float	s, t;
	vec3_t	dir;
	float	length;

	for (p=fa->polys ; p ; p=p->next)
	{
		c_sky_polys++;

		glBegin (GL_POLYGON);
		for (i=0,v=p->verts[0] ; i<p->numverts ; i++, v+=VERTEXSIZE)
		{
			if (save || buffer_pos >= MAX_SAVE)
			{
				VectorSubtract (v, r_origin, dir);
				dir[2] *= 3;	// flatten the sphere

	/*			VectorCopy(dir,sort);
				if (sort[1] > sort[0])
				{
					t = sort[1];
					sort[1] = sort[0];
					sort[0] = t;
				}
				if (sort[2] > sort[0])
				{
					t = sort[2];
					sort[2] = sort[0];
					sort[0] = t;
				}
				length = sort[0] + (sort[1] * .25) + (sort[2] * .25);*/

				length = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];
				length = sqrt (length);

				length = 6*63/length;

				dir[0] *= length;
				dir[1] *= length;

				if (buffer_pos < MAX_SAVE)
				{
					buffer_s[buffer_pos] = dir[0];
					buffer_t[buffer_pos] = dir[1];
					buffer_pos++;
				}
			}
			else
			{
				dir[0] = buffer_s[buffer_pos];
				dir[1] = buffer_t[buffer_pos];
				buffer_pos++;
			}

			s = (speedscale + dir[0]) * (1.0/128);
			t = (speedscale + dir[1]) * (1.0/128);

			glTexCoord2f (s, t);
			glVertex3fv (v);
		}
		glEnd ();
	}
}

/*
===============
EmitBothSkyLayers

Does a sky warp on the pre-fragmented glpoly_t chain
This will be called for brushmodels, the world
will have them chained together.
===============
*/
void EmitBothSkyLayers (msurface_t *fa)
{
	GL_Bind (solidskytexture);
	speedscale = realtime*8;
	speedscale -= (int)speedscale & ~127 ;

	buffer_pos = 0;
	EmitSkyPolys (fa,true);

	glEnable (GL_BLEND);
	GL_Bind (alphaskytexture);
	speedscale = realtime*16;
	speedscale -= (int)speedscale & ~127 ;

	buffer_pos = 0;
	EmitSkyPolys (fa,false);

	glDisable (GL_BLEND);
}

/*
=================
R_DrawSkyChain
=================
*/
void R_DrawSkyChain (msurface_t *s)
{
	msurface_t	*fa;

	// used when gl_texsort is on
	GL_Bind(solidskytexture);
	speedscale = realtime*8;
	speedscale -= (int)speedscale & ~127 ;

	buffer_pos = 0;

	for (fa=s ; fa ; fa=fa->texturechain)
		EmitSkyPolys (fa,true);

	glEnable (GL_BLEND);
	GL_Bind (alphaskytexture);
	speedscale = realtime*16;
	speedscale -= (int)speedscale & ~127 ;

	buffer_pos = 0;

	for (fa=s ; fa ; fa=fa->texturechain)
		EmitSkyPolys (fa,false);

	glDisable (GL_BLEND);

	c_sky_polys >>= 1;
}


/*
=============
R_InitSky

A sky texture is 256*128, with the right side being a masked overlay
==============
*/
void R_InitSky (texture_t *mt)
{
	char		texturename[64];
	int			i, j, p, r, g, b, count;
	int			scaledx;
	byte		*src;
	byte		fixedsky[256*128];
	static byte	front_data[128*128]; //FIXME: Hunk_Alloc
	static byte	back_data[128*128]; //FIXME: Hunk_Alloc
	unsigned	*rgba;

	src = (byte *)mt + mt->offsets[0];

	if (mt->width * mt->height != sizeof(fixedsky))
	{
		Con_DPrintf ("R_InitSky: non-standard sky texture '%s' (%dx%d, should be 256x128)\n", mt->name, mt->width, mt->height);

		// Resize sky texture to correct size
		memset (fixedsky, 0, sizeof(fixedsky));

		for (i = 0; i < 256; ++i)
		{
			scaledx = i * mt->width / 256 * mt->height;

			for (j = 0; j < 128; ++j)
				fixedsky[i * 128 + j] = src[scaledx + j * mt->height / 128];
		}

		src = fixedsky;
	}

// extract back layer and upload
	for (i=0 ; i<128 ; i++)
	{
		for (j=0 ; j<128 ; j++)
		{
			back_data[(i*128) + j] = src[i*256 + j + 128];
		}
	}

	sprintf (texturename, "%s_s", mt->name);
//	solidskytexture = GL_LoadTexture (texturename, 128, 128, (byte *)back_data, false, false, 0);
	solidskytexture = GL_LoadTexture (loadmodel, texturename, 128, 128, SRC_INDEXED, back_data, "", (unsigned)back_data, TEXPREF_NONE);

// extract front layer and upload
	for (i=0 ; i<128 ; i++)
	{
		for (j=0 ; j<128 ; j++)
		{
			front_data[(i*128) + j] = src[i*256 + j];
			if (front_data[(i*128) + j] == 0)
				front_data[(i*128) + j] = 255;
		}
	}

	sprintf (texturename, "%s_a", mt->name);
//	alphaskytexture = GL_LoadTexture (texturename, 128, 128, (byte *)front_data, false, true, 0);
	alphaskytexture = GL_LoadTexture (loadmodel, texturename, 128, 128, SRC_INDEXED, front_data, "", (unsigned)front_data, TEXPREF_ALPHA);

// calculate r_fastsky color based on average of all opaque foreground colors
	r = g = b = count = 0;
	for (i=0 ; i<128 ; i++)
	{
		for (j=0 ; j<128 ; j++)
		{
			p = src[i*256 + j];
			if (p != 0)
			{
				rgba = &d_8to24table[p];

				r += ((byte *)rgba)[0];
				g += ((byte *)rgba)[1];
				b += ((byte *)rgba)[2];
				count++;
			}
		}
	}

	skyflatcolor[0] = (float)r/(count*255);
	skyflatcolor[1] = (float)g/(count*255);
	skyflatcolor[2] = (float)b/(count*255);
}

