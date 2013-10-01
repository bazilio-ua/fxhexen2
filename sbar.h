
//**************************************************************************
//**
//** sbar.h
//**
//** $Header: /H3/game/SBAR.H 12    8/04/97 3:34p Bgokey $
//**
//**************************************************************************

// HEADER FILES ------------------------------------------------------------

// MACROS ------------------------------------------------------------------

// TYPES -------------------------------------------------------------------

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

void Sbar_Init(void);
void Sbar_Changed(void);
void Sbar_Draw(void);
void Sbar_IntermissionOverlay(void);
void Sbar_FinaleOverlay(void);
void Sbar_InvChanged(void);
void Sbar_InvReset(void);
void Sbar_ViewSizeChanged(void);
void Sbar_DeathmatchOverlay(void);

// PUBLIC DATA DECLARATIONS ------------------------------------------------

extern int sb_lines; // scan lines to draw
extern int color_offsets[MAX_PLAYER_CLASS];
