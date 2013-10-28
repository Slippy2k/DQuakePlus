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
// cl_main.c  -- client main loop

#include "quakedef.h"
#include "cl_slist.h"

// we need to declare some mouse variables here, because the menu system
// references them even when on a unix system.

// these two are not intended to be set directly
cvar_t	cl_name = {"_cl_name", "player", true};
cvar_t	cl_color = {"_cl_color", "0", true};
cvar_t  cl_truelightning = {"cl_truelightning", "1", true};
cvar_t	cl_shownet = {"cl_shownet","0"};	// can be 0, 1, or 2
cvar_t	cl_nolerp = {"cl_nolerp","0"};
cvar_t	cl_lightning_zadjust = {"cl_lightning_zadjust", "0", true};

cvar_t	lookspring = {"lookspring","0", true};
cvar_t	lookstrafe = {"lookstrafe","0", true};
cvar_t	in_sensitivity = {"sensitivity","3", true};
cvar_t	in_tolerance = {"tolerance","0.25", true};
cvar_t	in_acceleration = {"acceleration","1.0", true};

cvar_t	cl_rocket2grenade = {"cl_r2g", "0"};
cvar_t	cl_deadbodyfilter = {"cl_deadbodyfilter", "0"};
cvar_t	cl_gibfilter = {"cl_gibfilter", "0"};

cvar_t	m_pitch = {"m_pitch","0.022", true};
cvar_t	m_yaw = {"m_yaw","0.022", true};
cvar_t	m_forward = {"m_forward","1", true};
cvar_t	m_side = {"m_side","0.8", true};

cvar_t	in_disable_analog = {"in_disable_analog", "0", true};
cvar_t	in_analog_strafe = {"in_analog_strafe", "0", true};
cvar_t  in_x_axis_adjust = {"in_x_axis_adjust", "0", true};
cvar_t  in_y_axis_adjust = {"in_y_axis_adjust", "0", true};

//=================================================//
modelindex_t		cl_modelindex[NUM_MODELINDEX]; //
char				*cl_modelnames[NUM_MODELINDEX];//
												   //
tagentity_t			q3player_body, q3player_head;  //
//=================================================//
client_static_t	cls;
client_state_t	cl;
// FIXME: put these on hunk?
efrag_t			cl_efrags[MAX_EFRAGS];
entity_t		cl_entities[MAX_EDICTS];
entity_t		cl_static_entities[MAX_STATIC_ENTITIES];
lightstyle_t	cl_lightstyle[MAX_LIGHTSTYLES];
dlight_t		cl_dlights[MAX_DLIGHTS];

modelindex_t		cl_modelindex[NUM_MODELINDEX];

int				cl_numvisedicts;
entity_t		*cl_visedicts[MAX_VISEDICTS];

void CL_ClearTEnts (void);

/*
=====================
CL_ClearState

=====================
*/
void CL_ClearState (void)
{
	int			i;

	if (!sv.active)
		Host_ClearMemory ();

	CL_ClearTEnts ();
// wipe the entire cl structure
	memset (&cl, 0, sizeof(cl));

	SZ_Clear (&cls.message);

// clear other arrays	
	memset (cl_efrags, 0, sizeof(cl_efrags));
	memset (cl_entities, 0, sizeof(cl_entities));
	memset (cl_dlights, 0, sizeof(cl_dlights));
	memset (cl_lightstyle, 0, sizeof(cl_lightstyle));
	memset (cl_temp_entities, 0, sizeof(cl_temp_entities));
	memset (cl_beams, 0, sizeof(cl_beams));

    memset (cl_static_entities, 0, sizeof(cl_static_entities));
//
// allocate the efrags and chain together into a free list
//
	cl.free_efrags = cl_efrags;
	for (i=0 ; i<MAX_EFRAGS-1 ; i++)
		cl.free_efrags[i].entnext = &cl.free_efrags[i+1];
	cl.free_efrags[i].entnext = NULL;
}

/*
=====================
CL_Disconnect

Sends a disconnect message to the server
This is also called on Host_Error, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect (void)
{
// stop sounds (especially looping!)
	S_StopAllSounds (true);
	
// bring the console down and fade the colors back to normal
//	SCR_BringDownConsole ();

// if running a local server, shut it down
	if (cls.demoplayback)
		CL_StopPlayback ();
	else if (cls.state == ca_connected)
	{
		if (cls.demorecording)
			CL_Stop_f ();

		Con_DPrintf ("Sending clc_disconnect\n");
		SZ_Clear (&cls.message);
		MSG_WriteByte (&cls.message, clc_disconnect);
		NET_SendUnreliableMessage (cls.netcon, &cls.message);
		SZ_Clear (&cls.message);
		NET_Close (cls.netcon);

		cls.state = ca_disconnected;
		if (sv.active)
			Host_ShutdownServer(false);
	}

	cls.demoplayback = cls.timedemo = false;
	cls.signon = 0;
}

void CL_Disconnect_f (void)
{
	CL_Disconnect ();
	if (sv.active)
		Host_ShutdownServer (false);
}




/*
=====================
CL_EstablishConnection

Host should be either "local" or a net address to be passed on
=====================
*/
void CL_EstablishConnection (char *host)
{
	if (cls.state == ca_dedicated)
		return;

	if (cls.demoplayback)
		return;

	CL_Disconnect ();

	cls.netcon = NET_Connect (host);
	if (!cls.netcon)
		Host_Error ("CL_Connect: connect failed\n");
	Con_DPrintf ("CL_EstablishConnection: connected to %s\n", host);
	
	cls.demonum = -1;			// not in the demo loop now
	cls.state = ca_connected;
	cls.signon = 0;				// need all the signon messages before playing
}

/*
=====================
CL_SignonReply

An svc_signonnum has been received, perform a client side setup
=====================
*/
void CL_SignonReply (void)
{
	char 	str[8192];

	Con_DPrintf ("CL_SignonReply: %i\n", cls.signon);

	switch (cls.signon)
	{
	case 1:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "prespawn");
		break;
		
	case 2:		
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va("name \"%s\"\n", cl_name.string));
	
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va("color %i %i\n", ((int)cl_color.value)>>4, ((int)cl_color.value)&15));
	
		MSG_WriteByte (&cls.message, clc_stringcmd);
		sprintf (str, "spawn %s", cls.spawnparms);
		MSG_WriteString (&cls.message, str);
		break;
		
	case 3:	
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "begin");
		Cache_Report ();		// print remaining memory
		break;
		
	case 4:
		SCR_EndLoadingPlaque ();		// allow normal screen updates
		break;
	}
}

/*
=====================
CL_NextDemo

Called to play the next demo in the demo loop
=====================
*/
void CL_NextDemo (void)
{
	char	str[1024];

	if (cls.demonum == -1)
		return;		// don't play demos

	SCR_BeginLoadingPlaque ();

	if (!cls.demos[cls.demonum][0] || cls.demonum == MAX_DEMOS)
	{
		cls.demonum = 0;
		if (!cls.demos[cls.demonum][0])
		{
			Con_Printf ("No demos listed with startdemos\n");
			cls.demonum = -1;
			return;
		}
	}

	sprintf (str,"playdemo %s\n", cls.demos[cls.demonum]);
	Cbuf_InsertText (str);
	cls.demonum++;
}

/*
==============
CL_PrintEntities_f
==============
*/
void CL_PrintEntities_f (void)
{
	entity_t	*ent;
	int			i;
	
	for (i=0,ent=cl_entities ; i<cl.num_entities ; i++,ent++)
	{
		Con_Printf ("%3i:",i);
		if (!ent->model)
		{
			Con_Printf ("EMPTY\n");
			continue;
		}
		Con_Printf ("%s:%2i  (%5.1f,%5.1f,%5.1f) [%5.1f %5.1f %5.1f]\n"
		,ent->model->name,ent->frame, ent->origin[0], ent->origin[1], ent->origin[2], ent->angles[0], ent->angles[1], ent->angles[2]);
	}
}


/*
===============
SetPal

Debugging tool, just flashes the screen
===============
*/
void SetPal (int i)
{
#if 0
	static int old;
	byte	pal[768];
	int		c;
	
	if (i == old)
		return;
	old = i;

	if (i==0)
		VID_SetPalette (host_basepal);
	else if (i==1)
	{
		for (c=0 ; c<768 ; c+=3)
		{
			pal[c] = 0;
			pal[c+1] = 255;
			pal[c+2] = 0;
		}
		VID_SetPalette (pal);
	}
	else
	{
		for (c=0 ; c<768 ; c+=3)
		{
			pal[c] = 0;
			pal[c+1] = 0;
			pal[c+2] = 255;
		}
		VID_SetPalette (pal);
	}
#endif
}

void CL_CopyPlayerInfo (entity_t *ent, entity_t *player)
{
	memcpy (&ent->baseline, &player->baseline, sizeof(entity_state_t));

	ent->msgtime = player->msgtime;
	memcpy (ent->msg_origins, player->msg_origins, sizeof(ent->msg_origins));
	VectorCopy (player->origin, ent->origin);
	memcpy (ent->msg_angles, player->msg_angles, sizeof(ent->msg_angles));
	VectorCopy (player->angles, ent->angles);

	ent->model = (ent == &q3player_body.ent) ? cl.model_precache[cl_modelindex[mi_q3torso]] : cl.model_precache[cl_modelindex[mi_q3head]];

	ent->efrag = player->efrag;

	ent->frame = player->frame;
	ent->syncbase = player->syncbase;
	ent->colormap = player->colormap;
	ent->effects = player->effects;
	ent->skinnum = player->skinnum;
	ent->visframe = player->visframe;
	ent->dlightframe = player->dlightframe;
	ent->dlightbits = player->dlightbits;

	ent->trivial_accept = player->trivial_accept;
	ent->topnode = player->topnode;

	ent->modelindex = (ent == &q3player_body.ent) ? cl_modelindex[mi_q3torso] : cl_modelindex[mi_q3head];

	ent->noshadow = player->noshadow;

	ent->rendermode = player->rendermode;
    ent->renderamt = player->renderamt;
    //ent->rendercolor = player->rendercolor;
}

/*
===============
CL_AllocDlight

===============
*/
dlight_t *CL_AllocDlight (int key)
{
	int		i;
	dlight_t	*dl;

// first look for an exact key match
	if (key)
	{
		dl = cl_dlights;
		for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
		{
			if (dl->key == key)
			{
				memset (dl, 0, sizeof(*dl));
				dl->key = key;
                dl->color[0] = dl->color[1] = dl->color[2] = 1; // LordHavoc: .lit support
				return dl;
			}
		}
	}

// then look for anything else
	dl = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
	{
		if (dl->die < cl.time)
		{
			memset (dl, 0, sizeof(*dl));
			dl->key = key;
            dl->color[0] = dl->color[1] = dl->color[2] = 1; // LordHavoc: .lit support
			return dl;
		}
	}

	dl = &cl_dlights[0];
	memset (dl, 0, sizeof(*dl));
	dl->key = key;
    dl->color[0] = dl->color[1] = dl->color[2] = 1; // LordHavoc: .lit support
	return dl;
}

void CL_NewDlight (int key, vec3_t origin, float radius, float time, int type)
{
	dlight_t	*dl;

	dl = CL_AllocDlight (key);
	VectorCopy (origin, dl->origin);
	dl->radius = radius;
	dl->die = cl.time + time;
	dl->type = type;

}

dlighttype_t SetDlightColor (float f, dlighttype_t def, qboolean random)
{
	dlighttype_t	colors[NUM_DLIGHTTYPES-4] = {lt_red, lt_blue, lt_redblue, lt_green};

	if ((int)f == 1)
		return lt_red;
	else if ((int)f == 2)
		return lt_blue;
	else if ((int)f == 3)
		return lt_redblue;
	else if ((int)f == 4)
		return lt_green;
	else if (((int)f == NUM_DLIGHTTYPES - 3) && random)
		return colors[rand()%(NUM_DLIGHTTYPES-4)];
	else
		return def;
}

/*
===============
CL_DecayLights

===============
*/
void CL_DecayLights (void)
{
	int			i;
	dlight_t	*dl;
	float		time;
	
	time = cl.time - cl.oldtime;

	dl = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
	{
		if (dl->die < cl.time || !dl->radius)
			continue;
		
		dl->radius -= time*dl->decay;
		if (dl->radius < 0)
			dl->radius = 0;
	}
}


/*
===============
CL_LerpPoint

Determines the fraction between the last two messages that the objects
should be put at.
===============
*/
float CL_LerpPoint (void)
{
	float	f, frac;

	f = cl.mtime[0] - cl.mtime[1];

	if (!f || cl_nolerp.value || cls.timedemo || sv.active)
	{
		cl.ctime = cl.time = cl.mtime[0];
		return 1;
	}

	if (f > 0.1)
	{	// dropped packet, or start of demo
		cl.mtime[1] = cl.mtime[0] - 0.1;
		f = 0.1;
	}

	frac = (cl.ctime - cl.mtime[1]) / f;
	if (frac < 0)
	{
		if (frac < -0.01)
		{
			cl.ctime = cl.time = cl.mtime[1];
		}
		frac = 0;
	}
	else if (frac > 1)
	{
		if (frac > 1.01)
		{
			cl.ctime = cl.time = cl.mtime[0];
		}
		frac = 1;
	}

	return frac;
}


qboolean Model_isDead (int modelindex, int frame)
{
	if (cl_deadbodyfilter.value == 2)
	{
		if ((modelindex == cl_modelindex[mi_fish] && frame >= 18 && frame <= 38) ||
			(modelindex == cl_modelindex[mi_dog] && frame >= 8 && frame <= 25) ||
			(modelindex == cl_modelindex[mi_soldier] && frame >= 8 && frame <= 28) ||
			(modelindex == cl_modelindex[mi_enforcer] && frame >= 41 && frame <= 65) ||
			(modelindex == cl_modelindex[mi_knight] && frame >= 76 && frame <= 96) ||
			(modelindex == cl_modelindex[mi_hknight] && frame >= 42 && frame <= 62) ||
			(modelindex == cl_modelindex[mi_scrag] && frame >= 46 && frame <= 53) ||
			(modelindex == cl_modelindex[mi_ogre] && frame >= 112 && frame <= 135) ||
			(modelindex == cl_modelindex[mi_fiend] && frame >= 45 && frame <= 53) ||
			(modelindex == cl_modelindex[mi_vore] && frame >= 16 && frame <= 22) ||
			(modelindex == cl_modelindex[mi_shambler] && frame >= 83 && frame <= 93) ||
			(modelindex == cl_modelindex[mi_player] && frame >= 41 && frame <= 102))
			return true;
	}
	else
	{
		if ((modelindex == cl_modelindex[mi_fish] && frame == 38) ||
			(modelindex == cl_modelindex[mi_dog] && (frame == 16 || frame == 25)) ||
			(modelindex == cl_modelindex[mi_soldier] && (frame == 17 || frame == 28)) ||
			(modelindex == cl_modelindex[mi_enforcer] && (frame == 54 || frame == 65)) ||
			(modelindex == cl_modelindex[mi_knight] && (frame == 85 || frame == 96)) ||
			(modelindex == cl_modelindex[mi_hknight] && (frame == 53 || frame == 62)) ||
			(modelindex == cl_modelindex[mi_scrag] && frame == 53) ||
			(modelindex == cl_modelindex[mi_ogre] && (frame == 125 || frame == 135)) ||
			(modelindex == cl_modelindex[mi_fiend] && frame == 53) ||
			(modelindex == cl_modelindex[mi_vore] && frame == 22) ||
			(modelindex == cl_modelindex[mi_shambler] && frame == 93) ||
			(modelindex == cl_modelindex[mi_player] && (frame == 49 || frame == 60 || frame == 69 ||
			 frame == 84 || frame == 93 || frame == 102)))
			return true;
	}

	return false;
}

qboolean Model_isHead (int modelindex)
{
	if (modelindex == cl_modelindex[mi_h_dog] || modelindex == cl_modelindex[mi_h_soldier] ||
		modelindex == cl_modelindex[mi_h_enforcer] || modelindex == cl_modelindex[mi_h_knight] ||
		modelindex == cl_modelindex[mi_h_hknight] || modelindex == cl_modelindex[mi_h_scrag] ||
		modelindex == cl_modelindex[mi_h_ogre] || modelindex == cl_modelindex[mi_h_fiend] ||
		modelindex == cl_modelindex[mi_h_vore] || modelindex == cl_modelindex[mi_h_shambler] ||
		modelindex == cl_modelindex[mi_h_zombie] || modelindex == cl_modelindex[mi_h_player])
		return true;

	return false;
}

extern cvar_t scr_fov;
/*
===============
CL_RelinkEntities
===============
*/
void CL_RelinkEntities (void)
{
	entity_t	*ent;
	int			i, j;
	float		frac, f, d;
	vec3_t		delta;
	float		bobjrotate;
	vec3_t		oldorg;
    model_t		*model;
	dlight_t	*dl;
    vec3_t		smokeorg, smokeorg2;
	float		scale;
// determine partial update time	
	frac = CL_LerpPoint ();

	
	extern int	particle_mode;

	cl_numvisedicts = 0;

//
// interpolate player info
//
	for (i=0 ; i<3 ; i++)
		cl.velocity[i] = cl.mvelocity[1][i] + frac * (cl.mvelocity[0][i] - cl.mvelocity[1][i]);

	if (cls.demoplayback)
	{
	// interpolate the angles	
		for (j=0 ; j<3 ; j++)
		{
			d = cl.mviewangles[0][j] - cl.mviewangles[1][j];
			if (d > 180)
				d -= 360;
			else if (d < -180)
				d += 360;
			cl.viewangles[j] = cl.mviewangles[1][j] + frac*d;
		}
	}
	
	bobjrotate = anglemod(100*cl.time);
	
// start on the entity after the world
	for (i=1,ent=cl_entities+1 ; i<cl.num_entities ; i++,ent++)
	{
		if (!ent->model)
		{	// empty slot
			if (ent->forcelink)
				R_RemoveEfrags (ent);	// just became empty
			continue;
		}

// if the object wasn't included in the last packet, remove it
		if (ent->msgtime != cl.mtime[0])
		{
			ent->model = NULL;
            // fenix@io.com: model transform interpolation
            ent->frame_start_time     = 0;
            ent->translate_start_time = 0;
            ent->rotate_start_time    = 0;
			continue;
		}

		VectorCopy (ent->origin, oldorg);

		if (ent->forcelink)
		{	// the entity was not updated in the last message
			// so move to the final spot
			VectorCopy (ent->msg_origins[0], ent->origin);
			VectorCopy (ent->msg_angles[0], ent->angles);
		}
		else
		{	// if the delta is large, assume a teleport and don't lerp
			f = frac;
			for (j=0 ; j<3 ; j++)
			{
				delta[j] = ent->msg_origins[0][j] - ent->msg_origins[1][j];
				if (delta[j] > 100 || delta[j] < -100)
					f = 1;		// assume a teleportation, not a motion
			}

		// fenix@io.com: model transform interpolation
        // interpolation should be reset in the event of a large delta
            if (f >= 1)
            {
              ent->frame_start_time     = 0;
              ent->translate_start_time = 0;
              ent->rotate_start_time    = 0;
            }

		// interpolate the origin and angles
			for (j=0 ; j<3 ; j++)
			{
				ent->origin[j] = ent->msg_origins[1][j] + f*delta[j];

				d = ent->msg_angles[0][j] - ent->msg_angles[1][j];
				if (d > 180)
					d -= 360;
				else if (d < -180)
					d += 360;
				ent->angles[j] = ent->msg_angles[1][j] + f*d;
			}
			
		}

        if (cl_deadbodyfilter.value && ent->model->type == mod_alias &&
			Model_isDead(ent->modelindex, ent->frame))
			continue;

		if (cl_gibfilter.value && ent->model->type == mod_alias &&
			(ent->modelindex == cl_modelindex[mi_gib1] || ent->modelindex == cl_modelindex[mi_gib2] ||
			 ent->modelindex == cl_modelindex[mi_gib3] || Model_isHead(ent->modelindex)))
			continue;
/*
		if (ent->modelindex == cl_modelindex[mi_explo1] || ent->modelindex == cl_modelindex[mi_explo2])
		{
			// software removal of sprites
			if (r_explosiontype.value == 2 || r_explosiontype.value == 3)
				continue;

			if (qmb_initialized && r_part_explosions.value)
				continue;
		}

		if (!(model = cl.model_precache[ent->modelindex]))
			Host_Error ("CL_RelinkEntities: bad modelindex");
*/
		if (ent->modelindex == cl_modelindex[mi_rocket] && cl_rocket2grenade.value && cl_modelindex[mi_grenade] != -1)
			ent->model = cl.model_precache[cl_modelindex[mi_grenade]];

// rotate binary objects locally

		if (ent->model->flags & EF_ROTATE)
		{
			ent->angles[1] = bobjrotate;
            ent->origin[2] += (( sin(bobjrotate/90*M_PI) * 5) + 5 );
		}

		if (ent->effects & EF_BRIGHTFIELD)
			R_EntityParticles (ent);

		if (ent->effects & EF_DARKFIELD)
            R_DarkFieldParticles (ent);


		if (ent->effects & EF_MUZZLEFLASH)
		{
			vec3_t		fv, rv, uv;

			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->origin[2] += 16;
			AngleVectors (ent->angles, fv, rv, uv);
			 
			VectorMA (dl->origin, 18, fv, dl->origin);
			dl->radius = 200 + (rand()&31);
			dl->minlight = 32;
			dl->die = cl.time + 0.1;
            dl->color[0] = 0.9;
            dl->color[1] = 0.8;
            dl->color[2] = 0.6;

			if ((ent->modelindex == cl_modelindex[mi_shambler] || (ent->model->modhint == MOD_PLAYER && cl.stats[STAT_ACTIVEWEAPON] == IT_LIGHTNING)) &&
				qmb_initialized && r_part_lightning.value)
				dl->type = lt_blue;
			else if (ent->modelindex == cl_modelindex[mi_scrag] && qmb_initialized)
				dl->type = lt_green;
			else
				dl->type = lt_muzzleflash;

			if (i == cl.viewentity && qmb_initialized && r_part_muzzleflash.value)
			{
				vec3_t	rv, uv, smokeorg, smokeorg2;

				VectorCopy (cl_entities[cl.viewentity].origin, smokeorg);
				smokeorg[2] += cl.crouch;

				smokeorg[2] += 14;	// move it up

				// adjust for current view angles
				AngleVectors (cl.viewangles, fv, rv, uv);

				// forward vector
				VectorMA (smokeorg, 18, fv, smokeorg);

				if (cl.stats[STAT_ACTIVEWEAPON] == IT_NAILGUN || cl.stats[STAT_ACTIVEWEAPON] == IT_SUPER_SHOTGUN)
				{
					VectorMA (smokeorg, -3, rv, smokeorg);
					VectorMA (smokeorg, 6, rv, smokeorg2);
					QMB_MuzzleFlash (smokeorg);
					QMB_MuzzleFlash (smokeorg2);
				}
				else if (cl.stats[STAT_ACTIVEWEAPON] == IT_SUPER_NAILGUN || cl.stats[STAT_ACTIVEWEAPON] == IT_SHOTGUN)
				{
					QMB_MuzzleFlash (smokeorg);
				}
			}

		}

		if (ent->modelindex != cl_modelindex[mi_eyes] &&
		    ((ent->modelindex != cl_modelindex[mi_player] && ent->model->modhint != MOD_PLAYER &&
		      ent->modelindex != cl_modelindex[mi_h_player])))
		{
			if (ent->effects & EF_BRIGHTLIGHT)
			{
				vec3_t	tmp;

				VectorCopy (ent->origin, tmp);
				tmp[2] += 16;
				dl->color[0] = 1;
                dl->color[1] = 0.8;
                dl->color[2] = 0.5;
				CL_NewDlight (i, tmp, 400 + (rand() & 31), 0.1, lt_default);
			}
			else if (ent->effects & EF_DIMLIGHT)
			{
			    dl->color[0] = 0.5;
                dl->color[1] = 0.5;
                dl->color[2] = 0.5;
				CL_NewDlight (i, ent->origin, 200 + (rand() & 31), 0.1, lt_default);
            }
		}

		if (ent->effects & EF_DARKLIGHT)
		{			
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->radius = 200.0 + (rand()&31);
			dl->die = cl.time + 0.001;
			dl->dark = true;
		}
		if (ent->effects & EF_LIGHT)
		{			
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->radius = 200;
			dl->die = cl.time + 0.001;
		}

		if (!strcmp(ent->model->name, "progs/flame2.mdl"))
		{
			if (qmb_initialized && r_part_flames.value)
			{
				//QMB_BigTorchFlame (ent->origin);
				if (qmb_initialized && r_part_trails.value)
					R_RocketTrail (oldorg, ent->origin, LAVA_TRAIL);
			}
		}

		if ((!strcmp(ent->model->name, "progs/s_spike.mdl"))||(!strcmp(ent->model->name, "progs/spike.mdl")))
		{
			if (qmb_initialized && r_part_trails.value)
			{
				R_RocketTrail (oldorg, ent->origin, NAIL_TRAIL);
			}
		}

        if (ent->model->flags)
		{
			if (ent->model->flags & EF_GIB)
				R_RocketTrail (oldorg, ent->origin, 2);
			else if (ent->model->flags & EF_ZOMGIB)
				R_RocketTrail (oldorg, ent->origin, 4);
			else if (ent->model->flags & EF_TRACER)
				R_RocketTrail (oldorg, ent->origin, 3);
			else if (ent->model->flags & EF_TRACER2)
				R_RocketTrail (oldorg, ent->origin, 5);
			else if (ent->model->flags & EF_ROCKET)
			{
				R_RocketTrail (oldorg, ent->origin, 0);
				dl = CL_AllocDlight (i);
				VectorCopy (ent->origin, dl->origin);
				dl->radius = 200;
				dl->die = cl.time + 0.01;
	            dl->color[0] = 0.2;
				dl->color[1] = 0.1;
				dl->color[2] = 0.5;
	            dl->type = SetDlightColor (2, lt_rocket, true);
			}
			else if (ent->model->flags & EF_GRENADE)
				R_RocketTrail (oldorg, ent->origin, 1);
			else if (ent->model->flags & EF_TRACER3)
				R_RocketTrail (oldorg, ent->origin, 6);
		}

		// Tomaz - QC Glow Begin
        if (ISLMPOINT(ent))
        {
	        dl = CL_AllocDlight (i);
	        VectorCopy (ent->origin, dl->origin);
	        dl->radius = 150;
	        dl->die = cl.time + 0.001;
	        dl->color[0] = ent->rendercolor[0];
	        dl->color[1] = ent->rendercolor[1];
	        dl->color[2] = ent->rendercolor[2];
        }
		// Tomaz - QC Glow End

		ent->forcelink = false;

		if (i == cl.viewentity && !chase_active.value)
			continue;


		if ( ent->effects & EF_NODRAW )
			continue;

        if (qmb_initialized)
		{
			if (ent->modelindex == cl_modelindex[mi_bubble])
			{
				QMB_StaticBubble (ent);
				continue;
			}
			else if (r_part_lightning.value && ent->modelindex == cl_modelindex[mi_shambler] &&  ent->frame >= 65 && ent->frame <= 68)
			{
				vec3_t	liteorg;

				VectorCopy (ent->origin, liteorg);
				liteorg[2] += 32;
				QMB_ShamblerCharge (liteorg);
			}
			else if (r_part_trails.value && ent->model->modhint == MOD_SPIKE)
			{
				if (!ent->traildrawn || !VectorL2Compare(ent->trail_origin, ent->origin, 140))
				{
					VectorCopy (ent->origin, oldorg);	//not present last frame or too far away
					ent->traildrawn = true;
				}
				else
				{
					VectorCopy (ent->trail_origin, oldorg);
				}
				QMB_RocketTrail (oldorg, ent->origin, BUBBLE_TRAIL);
			}
		}

		if (cl_numvisedicts < MAX_VISEDICTS)
		{
			cl_visedicts[cl_numvisedicts] = ent;
			cl_numvisedicts++;
		}
	}
}

/*
===============
CL_ReadFromServer

Read all incoming data from the server
===============
*/
int CL_ReadFromServer (void)
{
	int		ret;

	cl.oldtime = cl.time;
	cl.time += host_frametime;
	
	do
	{
		ret = CL_GetMessage ();
		if (ret == -1)
			Host_Error ("CL_ReadFromServer: lost server connection");
		if (!ret)
			break;
		
		cl.last_received_message = realtime;
		CL_ParseServerMessage ();
	} while (ret && cls.state == ca_connected);
	
	if (cl_shownet.value)
		Con_Printf ("\n");

	CL_RelinkEntities ();
	CL_UpdateTEnts ();

//
// bring the links up to date
//
	return 0;
}

/*
=================
CL_SendCmd
=================
*/
void CL_SendCmd (void)
{
	usercmd_t		cmd;

	if (cls.state != ca_connected)
		return;

	if (cls.signon == SIGNONS)
	{
	// get basic movement from keyboard
		CL_BaseMove (&cmd);
	
	// allow mice or other external controllers to add to the move
	if (!in_disable_analog.value)	
		IN_Move (&cmd);
	
	// send the unreliable message
		CL_SendMove (&cmd);
	
	}

	if (cls.demoplayback)
	{
		SZ_Clear (&cls.message);
		return;
	}
	
// send the reliable message
	if (!cls.message.cursize)
		return;		// no message at all
	
	if (!NET_CanSendMessage (cls.netcon))
	{
		Con_DPrintf ("CL_WriteToServer: can't send\n");
		return;
	}

	if (NET_SendMessage (cls.netcon, &cls.message) == -1)
		Host_Error ("CL_WriteToServer: lost server connection");

	SZ_Clear (&cls.message);
}

/*
=================
CL_Init
=================
*/
void CL_Init (void)
{	
	SZ_Alloc (&cls.message, 1024);

	SList_Init ();
	SList_Load ();

	CL_InitInput ();
	CL_InitTEnts ();
//
// register our commands
//
	Cvar_RegisterVariable (&cl_name);
	Cvar_RegisterVariable (&cl_color);
	Cvar_RegisterVariable (&cl_upspeed);
	Cvar_RegisterVariable (&cl_forwardspeed);
	Cvar_RegisterVariable (&cl_backspeed);
	Cvar_RegisterVariable (&cl_sidespeed);
	Cvar_RegisterVariable (&cl_movespeedkey);
	Cvar_RegisterVariable (&cl_yawspeed);
	Cvar_RegisterVariable (&cl_pitchspeed);
	Cvar_RegisterVariable (&cl_anglespeedkey);
	Cvar_RegisterVariable (&cl_shownet);
	Cvar_RegisterVariable (&cl_nolerp);
	Cvar_RegisterVariable (&lookspring);
	Cvar_RegisterVariable (&lookstrafe);
	Cvar_RegisterVariable (&cl_rocket2grenade);
	Cvar_RegisterVariable (&cl_deadbodyfilter);
	Cvar_RegisterVariable (&cl_gibfilter);
	Cvar_RegisterVariable (&cl_lightning_zadjust);
    Cvar_RegisterVariable (&cl_truelightning);
	Cvar_RegisterVariable (&in_sensitivity);
    Cvar_RegisterVariable (&in_mlook); //Heffo - mlook cvar
	Cvar_RegisterVariable (&in_tolerance);
	Cvar_RegisterVariable (&in_acceleration);
	Cvar_RegisterVariable (&in_disable_analog);
	Cvar_RegisterVariable (&in_analog_strafe);
	Cvar_RegisterVariable (&in_x_axis_adjust);
	Cvar_RegisterVariable (&in_y_axis_adjust);

	
	Cvar_RegisterVariable (&m_pitch);
	Cvar_RegisterVariable (&m_yaw);
	Cvar_RegisterVariable (&m_forward);
	Cvar_RegisterVariable (&m_side);

//	Cvar_RegisterVariable (&cl_autofire);
	
	Cmd_AddCommand ("entities", CL_PrintEntities_f);
	Cmd_AddCommand ("disconnect", CL_Disconnect_f);
	Cmd_AddCommand ("record", CL_Record_f);
	Cmd_AddCommand ("stop", CL_Stop_f);
	Cmd_AddCommand ("playdemo", CL_PlayDemo_f);
	Cmd_AddCommand ("timedemo", CL_TimeDemo_f);
}

