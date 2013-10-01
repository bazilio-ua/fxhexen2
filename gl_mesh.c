// gl_mesh.c: triangle model functions

#include "quakedef.h"

/*
=================================================================

ALIAS MODEL DISPLAY LIST GENERATION

=================================================================
*/

model_t		*aliasmodel;
aliashdr_t	*paliashdr;

#define MAX_CMDS 16384 //8192

qboolean	used[MAX_CMDS];

// the command list holds counts and s/t values that are valid for
// every frame
int		commands[MAX_CMDS];
int		numcommands;

// all frames will have their vertexes rearranged and expanded
// so they are in the order expected by the command list
int		vertexorder[MAX_CMDS];
int		numorder;

#define MAX_STRIPS 256 //128

int		stripverts[MAX_STRIPS];
int		striptris[MAX_STRIPS];
int		stripstverts[MAX_STRIPS];
int		stripcount;

static void ChkStrip (char *Function)
{
	if (stripcount + 2 >= MAX_STRIPS)
		Sys_Error ("%s: too high stripcount (max = %d) in %s", Function, MAX_STRIPS - 2, aliasmodel->name);
}

static void ChkCmds (char *Function)
{
	if (numcommands >= MAX_CMDS)
		Sys_Error ("%s: too many commands (max = %d) in %s", Function, MAX_CMDS, aliasmodel->name);
}

/*
================
StripLength
================
*/
int	StripLength (int starttri, int startv)
{
	int			m1, m2;
	int			st1, st2;
	int			j;
	mtriangle_t	*last, *check;
	int			k;

	used[starttri] = 2;

	last = &triangles[starttri];

	stripverts[0] = last->vertindex[(startv)%3];
	stripstverts[0] = last->stindex[(startv)%3];

	stripverts[1] = last->vertindex[(startv+1)%3];
	stripstverts[1] = last->stindex[(startv+1)%3];

	stripverts[2] = last->vertindex[(startv+2)%3];
	stripstverts[2] = last->stindex[(startv+2)%3];

	striptris[0] = starttri;
	stripcount = 1;

	m1 = last->vertindex[(startv+2)%3];
	st1 = last->stindex[(startv+2)%3];
	m2 = last->vertindex[(startv+1)%3];
	st2 = last->stindex[(startv+1)%3];

	// look for a matching triangle
nexttri:
	for (j=starttri+1, check=&triangles[starttri+1] ; j<pheader->numtris ; j++, check++)
	{
		if (check->facesfront != last->facesfront)
			continue;
		for (k=0 ; k<3 ; k++)
		{
			if (check->vertindex[k] != m1)
				continue;
			if (check->stindex[k] != st1)
				continue;
			if (check->vertindex[ (k+1)%3 ] != m2)
				continue;
			if (check->stindex[ (k+1)%3 ] != st2)
				continue;

			// this is the next part of the fan

			// if we can't use this triangle, this tristrip is done
			if (used[j])
				goto done;

			// the new edge
			if (stripcount & 1)
			{
				m2 = check->vertindex[ (k+2)%3 ];
				st2 = check->stindex[ (k+2)%3 ];
			}
			else
			{
				m1 = check->vertindex[ (k+2)%3 ];
				st1 = check->stindex[ (k+2)%3 ];
			}

			ChkStrip ("StripLength");

			stripverts[stripcount+2] = check->vertindex[ (k+2)%3 ];
			stripstverts[stripcount+2] = check->stindex[ (k+2)%3 ];
			striptris[stripcount] = j;
			stripcount++;

			used[j] = 2;
			goto nexttri;
		}
	}
done:

	// clear the temp used flags
	for (j=starttri+1 ; j<pheader->numtris ; j++)
		if (used[j] == 2)
			used[j] = 0;

	return stripcount;
}

/*
===========
FanLength
===========
*/
int	FanLength (int starttri, int startv)
{
	int		m1, m2;
	int		st1, st2;
	int		j;
	mtriangle_t	*last, *check;
	int		k;

	used[starttri] = 2;

	last = &triangles[starttri];

	stripverts[0] = last->vertindex[(startv)%3];
	stripstverts[0] = last->stindex[(startv)%3];

	stripverts[1] = last->vertindex[(startv+1)%3];
	stripstverts[1] = last->stindex[(startv+1)%3];

	stripverts[2] = last->vertindex[(startv+2)%3];
	stripstverts[2] = last->stindex[(startv+2)%3];

	striptris[0] = starttri;
	stripcount = 1;

	m1 = last->vertindex[(startv+0)%3];
	st1 = last->stindex[(startv+2)%3];
	m2 = last->vertindex[(startv+2)%3];
	st2 = last->stindex[(startv+1)%3];


	// look for a matching triangle
nexttri:
	for (j=starttri+1, check=&triangles[starttri+1] ; j<pheader->numtris ; j++, check++)
	{
		if (check->facesfront != last->facesfront)
			continue;
		for (k=0 ; k<3 ; k++)
		{
			if (check->vertindex[k] != m1)
				continue;
			if (check->stindex[k] != st1)
				continue;
			if (check->vertindex[ (k+1)%3 ] != m2)
				continue;
			if (check->stindex[ (k+1)%3 ] != st2)
				continue;

			// this is the next part of the fan

			// if we can't use this triangle, this tristrip is done
			if (used[j])
				goto done;

			// the new edge
			m2 = check->vertindex[ (k+2)%3 ];
			st2 = check->stindex[ (k+2)%3 ];

			ChkStrip ("FanLength");

			stripverts[stripcount+2] = m2;
			stripstverts[stripcount+2] = st2;
			striptris[stripcount] = j;
			stripcount++;

			used[j] = 2;
			goto nexttri;
		}
	}
done:

	// clear the temp used flags
	for (j=starttri+1 ; j<pheader->numtris ; j++)
		if (used[j] == 2)
			used[j] = 0;

	return stripcount;
}


/*
================
BuildTris

Generate a list of trifans or strips
for the model, which holds for all frames
================
*/
void BuildTris (void)
{
	int		i, j, k;
	int		startv;
	float	s, t;
	int		len, bestlen, besttype;
	int		bestverts[1024];
	int		besttris[1024];
	int		beststverts[1024];
	int		type;
	int		stripmax = 0;

	//
	// build tristrips
	//
	numorder = 0;
	numcommands = 0;
	memset (used, 0, sizeof(used));
	for (i=0 ; i<pheader->numtris ; i++)
	{
		// pick an unused triangle and start the trifan
		if (used[i])
			continue;

		bestlen = 0;
		besttype = 0;
		for (type = 0 ; type < 2 ; type++)
//	type = 1;
		{
			for (startv =0 ; startv < 3 ; startv++)
			{
				if (type == 1)
					len = StripLength (i, startv);
				else
					len = FanLength (i, startv);

				// Save peak
				if (len > stripmax)
					stripmax = len;

				if (len > bestlen)
				{
					besttype = type;
					bestlen = len;
					for (j=0 ; j<bestlen+2 ; j++)
					{
						beststverts[j] = stripstverts[j];
						bestverts[j] = stripverts[j];
					}
					for (j=0 ; j<bestlen ; j++)
						besttris[j] = striptris[j];
				}
			}
		}

		// mark the tris on the best strip as used
		for (j=0 ; j<bestlen ; j++)
			used[besttris[j]] = 1;

		ChkCmds ("BuildTris");

		if (besttype == 1)
			commands[numcommands++] = (bestlen+2);
		else
			commands[numcommands++] = -(bestlen+2);
		
		for (j=0 ; j<bestlen+2 ; j++)
		{
			// emit a vertex into the reorder buffer
			k = bestverts[j];
			vertexorder[numorder++] = k;

			k = beststverts[j];

			// emit s/t coords into the commands stream
			s = stverts[k].s;
			t = stverts[k].t;
			if (!triangles[besttris[0]].facesfront && stverts[k].onseam)
				s += pheader->skinwidth / 2;	// on back side
			s = (s + 0.5) / pheader->skinwidth;
			t = (t + 0.5) / pheader->skinheight;

			ChkCmds ("BuildTris");

			*(float *)&commands[numcommands++] = s;
			*(float *)&commands[numcommands++] = t;
		}
	}

	ChkCmds ("BuildTris");

	commands[numcommands++] = 0;		// end of list marker

	// Check old limit
	if (stripmax + 2 >= 128)
	{
		Con_Warning ("BuildTris: excessive stripcount (%d, normal max = %d) in %s\n", stripmax, 128 - 2, aliasmodel->name);
	}

	// Check old limit
	if (numcommands >= 8192)
	{
		Con_Warning ("BuildTris: excessive commands (%d, normal max = %d) in %s\n", numcommands, 8192, aliasmodel->name);
	}
}


/*
================
R_MakeAliasModelDisplayLists
================
*/
void R_MakeAliasModelDisplayLists (model_t *m, aliashdr_t *hdr)
{
	int		i, j;
	int			*cmds;
	trivertx_t	*verts;

	aliasmodel = m;
	paliashdr = hdr;	// (aliashdr_t *)Mod_Extradata (m);

	//always regenerate meshes
	if (developer.value > 3)
		Con_DPrintf ("meshing %s...\n",m->name);
		BuildTris ();		// trifans or lists

	// save the data out

	paliashdr->poseverts = numorder;

	cmds = Hunk_AllocName (numcommands * 4, "cmds");
	paliashdr->commands = (byte *)cmds - (byte *)paliashdr;
	memcpy (cmds, commands, numcommands * 4);

	verts = Hunk_AllocName (paliashdr->numposes * paliashdr->poseverts * sizeof(trivertx_t), "verts");
	paliashdr->posedata = (byte *)verts - (byte *)paliashdr;
	for (i=0 ; i<paliashdr->numposes ; i++)
	{
		for (j=0 ; j<numorder ; j++)
		{
			if (vertexorder[j] < 0 || vertexorder[j] >= MAXALIASVERTS)
				Sys_Error ("R_MakeAliasModelDisplayLists: invalid vertexorder[%d] (%d, max = %d) in %s", j, vertexorder[j], MAXALIASVERTS, m->name);

			*verts++ = poseverts[i][vertexorder[j]];
		}
	}
}

