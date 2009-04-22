/****************************************************************************
 * Snes9x 1.51 Nintendo Wii/Gamecube Port
 *
 * softdev July 2006
 * crunchy2 May 2007
 * Michniewski 2008
 * Tantric 2008-2009
 *
 * video.cpp
 *
 * Video routines
 ***************************************************************************/

#include <gccore.h>
#include <ogcsys.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wiiuse/wpad.h>
#include <ogc/texconv.h>

#include "snes9x.h"
#include "memmap.h"

#include "aram.h"
#include "snes9xGX.h"
#include "menu.h"
#include "filter.h"
#include "filelist.h"
#include "audio.h"
#include "gui/gui.h"
#include "input.h"

/*** Snes9x GFX Buffer ***/
#define SNES9XGFX_SIZE 		EXT_PITCH*EXT_HEIGHT
#define FILTERMEM_SIZE 		512*MAX_SNES_HEIGHT*4

static unsigned char * snes9xgfx = NULL;
unsigned char * filtermem = NULL; // only want ((512*2) X (239*2))

/*** 2D Video ***/
static unsigned int *xfb[2] = { NULL, NULL }; // Double buffered
static int whichfb = 0; // Switch
static GXRModeObj *vmode; // Menu video mode
int screenheight;
int screenwidth;
static int currentVideoMode = -1; // -1 - not set, 0 - automatic, 1 - NTSC (480i), 2 - Progressive (480p), 3 - PAL (50Hz), 4 - PAL (60Hz)

/*** GX ***/
#define TEX_WIDTH 512
#define TEX_HEIGHT 512
#define TEXTUREMEM_SIZE 	TEX_WIDTH*(TEX_HEIGHT+8)*2
static unsigned char texturemem[TEXTUREMEM_SIZE] ATTRIBUTE_ALIGN (32);

#define DEFAULT_FIFO_SIZE 256 * 1024
static unsigned int copynow = GX_FALSE;
static unsigned char gp_fifo[DEFAULT_FIFO_SIZE] ATTRIBUTE_ALIGN (32);
static GXTexObj texobj;
static Mtx view;
static Mtx GXmodelView2D;
static int vwidth, vheight, oldvwidth, oldvheight;

u8 * gameScreenTex = NULL; // a GX texture screen capture of the game
u8 * gameScreenTex2 = NULL; // a GX texture screen capture of the game (copy)

u32 FrameTimer = 0;

u8 vmode_60hz = 0;
bool progressive = 0;

#define HASPECT 320
#define VASPECT 240

/* New texture based scaler */
typedef struct tagcamera
{
	Vector pos;
	Vector up;
	Vector view;
}
camera;

/*** Square Matrix
     This structure controls the size of the image on the screen.
	 Think of the output as a -80 x 80 by -60 x 60 graph.
***/
s16 square[] ATTRIBUTE_ALIGN (32) =
{
  /*
   * X,   Y,  Z
   * Values set are for roughly 4:3 aspect
   */
	-HASPECT,  VASPECT, 0,		// 0
	 HASPECT,  VASPECT, 0,	// 1
	 HASPECT, -VASPECT, 0,	// 2
	-HASPECT, -VASPECT, 0	// 3
};


static camera cam = {
	{0.0F, 0.0F, 0.0F},
	{0.0F, 0.5F, 0.0F},
	{0.0F, 0.0F, -0.5F}
};


/***
*** Custom Video modes (used to emulate original console video modes)
***/

/** Original SNES PAL Resolutions: **/

/* 239 lines progressive (PAL 50Hz) */
static GXRModeObj TV_239p =
{
	VI_TVMODE_PAL_DS,       // viDisplayMode
	512,             // fbWidth
	239,             // efbHeight
	239,             // xfbHeight
	(VI_MAX_WIDTH_PAL - 640)/2,         // viXOrigin
	(VI_MAX_HEIGHT_PAL/2 - 478/2)/2,        // viYOrigin
	640,             // viWidth
	478,             // viHeight
	VI_XFBMODE_SF,   // xFBmode
	GX_FALSE,        // field_rendering
	GX_FALSE,        // aa

	// sample points arranged in increasing Y order
	{
		{6,6},{6,6},{6,6},  // pix 0, 3 sample points, 1/12 units, 4 bits each
		{6,6},{6,6},{6,6},  // pix 1
		{6,6},{6,6},{6,6},  // pix 2
		{6,6},{6,6},{6,6}   // pix 3
	},

	// vertical filter[7], 1/64 units, 6 bits each
	{
		0,         // line n-1
		0,         // line n-1
		21,         // line n
		22,         // line n
		21,         // line n
		0,         // line n+1
		0          // line n+1
	}
};

/* 478 lines interlaced (PAL 50Hz, Deflicker) */
static GXRModeObj TV_478i =
{
	VI_TVMODE_PAL_INT,      // viDisplayMode
	512,             // fbWidth
	478,             // efbHeight
	478,             // xfbHeight
	(VI_MAX_WIDTH_PAL - 640)/2,         // viXOrigin
	(VI_MAX_HEIGHT_PAL - 478)/2,        // viYOrigin
	640,             // viWidth
	478,             // viHeight
	VI_XFBMODE_DF,   // xFBmode
	GX_FALSE,         // field_rendering
	GX_FALSE,        // aa

	// sample points arranged in increasing Y order
	{
		{6,6},{6,6},{6,6},  // pix 0, 3 sample points, 1/12 units, 4 bits each
		{6,6},{6,6},{6,6},  // pix 1
		{6,6},{6,6},{6,6},  // pix 2
		{6,6},{6,6},{6,6}   // pix 3
	},

	// vertical filter[7], 1/64 units, 6 bits each
	{
		8,         // line n-1
		8,         // line n-1
		10,         // line n
		12,         // line n
		10,         // line n
		8,         // line n+1
		8          // line n+1
	}
};

/** Original SNES NTSC Resolutions: **/

/* 224 lines progressive (NTSC or PAL 60Hz) */
static GXRModeObj TV_224p =
{
	VI_TVMODE_EURGB60_DS,      // viDisplayMode
	512,             // fbWidth
	224,             // efbHeight
	224,             // xfbHeight
	(VI_MAX_WIDTH_NTSC - 640)/2,	// viXOrigin
	(VI_MAX_HEIGHT_NTSC/2 - 448/2)/2,	// viYOrigin
	640,             // viWidth
	448,             // viHeight
	VI_XFBMODE_SF,   // xFBmode
	GX_FALSE,        // field_rendering
	GX_FALSE,        // aa

	// sample points arranged in increasing Y order
	{
		{6,6},{6,6},{6,6},  // pix 0, 3 sample points, 1/12 units, 4 bits each
		{6,6},{6,6},{6,6},  // pix 1
		{6,6},{6,6},{6,6},  // pix 2
		{6,6},{6,6},{6,6}   // pix 3
	},

	// vertical filter[7], 1/64 units, 6 bits each
	{
		0,         // line n-1
		0,         // line n-1
		21,         // line n
		22,         // line n
		21,         // line n
		0,         // line n+1
		0          // line n+1
	}
};

/* 448 lines interlaced (NTSC or PAL 60Hz, Deflicker) */
static GXRModeObj TV_448i =
{
	VI_TVMODE_EURGB60_INT,     // viDisplayMode
	512,             // fbWidth
	448,             // efbHeight
	448,             // xfbHeight
	(VI_MAX_WIDTH_NTSC - 640)/2,        // viXOrigin
	(VI_MAX_HEIGHT_NTSC - 448)/2,       // viYOrigin
	640,             // viWidth
	448,             // viHeight
	VI_XFBMODE_DF,   // xFBmode
	GX_FALSE,         // field_rendering
	GX_FALSE,        // aa


	// sample points arranged in increasing Y order
	{
		{6,6},{6,6},{6,6},  // pix 0, 3 sample points, 1/12 units, 4 bits each
		{6,6},{6,6},{6,6},  // pix 1
		{6,6},{6,6},{6,6},  // pix 2
		{6,6},{6,6},{6,6}   // pix 3
	},

	// vertical filter[7], 1/64 units, 6 bits each
	{
		8,         // line n-1
		8,         // line n-1
		10,         // line n
		12,         // line n
		10,         // line n
		8,         // line n+1
		8          // line n+1
	}
};

static GXRModeObj TV_Custom;

/* TV Modes table */
static GXRModeObj *tvmodes[4] = {
	&TV_239p, &TV_478i,			/* Snes PAL video modes */
	&TV_224p, &TV_448i,			/* Snes NTSC video modes */
};

/****************************************************************************
 * VideoThreading
 ***************************************************************************/
#define TSTACK 16384
static lwpq_t videoblankqueue;
static lwp_t vbthread = LWP_THREAD_NULL;
static unsigned char vbstack[TSTACK];

/****************************************************************************
 * vbgetback
 *
 * This callback enables the emulator to keep running while waiting for a
 * vertical blank.
 *
 * Putting LWP to good use :)
 ***************************************************************************/
static void *
vbgetback (void *arg)
{
	while (1)
	{
		VIDEO_WaitVSync ();	 /**< Wait for video vertical blank */
		LWP_SuspendThread (vbthread);
	}

	return NULL;

}

/****************************************************************************
 * InitVideoThread
 *
 * libOGC provides a nice wrapper for LWP access.
 * This function sets up a new local queue and attaches the thread to it.
 ***************************************************************************/
void
InitVideoThread ()
{
	/*** Initialise a new queue ***/
	LWP_InitQueue (&videoblankqueue);

	/*** Create the thread on this queue ***/
	LWP_CreateThread (&vbthread, vbgetback, NULL, vbstack, TSTACK, 100);
}

/****************************************************************************
 * copy_to_xfb
 *
 * Stock code to copy the GX buffer to the current display mode.
 * Also increments the frameticker, as it's called for each vb.
 ***************************************************************************/
static void
copy_to_xfb (u32 arg)
{
	if (copynow == GX_TRUE)
	{
		GX_CopyDisp (xfb[whichfb], GX_TRUE);
		GX_Flush ();
		copynow = GX_FALSE;
	}

	FrameTimer++;
}

/****************************************************************************
 * Scaler Support Functions
 ***************************************************************************/
static void
draw_init ()
{
	GX_ClearVtxDesc ();
	GX_SetVtxDesc (GX_VA_POS, GX_INDEX8);
	GX_SetVtxDesc (GX_VA_CLR0, GX_INDEX8);
	GX_SetVtxDesc (GX_VA_TEX0, GX_DIRECT);

	GX_SetVtxAttrFmt (GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_S16, 0);
	GX_SetVtxAttrFmt (GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt (GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	GX_SetArray (GX_VA_POS, square, 3 * sizeof (s16));

	GX_SetNumTexGens (1);
	GX_SetNumChans (0);

	GX_SetTexCoordGen (GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

	GX_SetTevOp (GX_TEVSTAGE0, GX_REPLACE);
	GX_SetTevOrder (GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);

	memset (&view, 0, sizeof (Mtx));
	guLookAt(view, &cam.pos, &cam.up, &cam.view);
	GX_LoadPosMtxImm (view, GX_PNMTX0);

	GX_InvVtxCache ();	// update vertex cache
}

static void
draw_vert (u8 pos, u8 c, f32 s, f32 t)
{
	GX_Position1x8 (pos);
	GX_Color1x8 (c);
	GX_TexCoord2f32 (s, t);
}

static void
draw_square (Mtx v)
{
	Mtx m;			// model matrix.
	Mtx mv;			// modelview matrix.

	guMtxIdentity (m);
	guMtxTransApply (m, m, 0, 0, -100);
	guMtxConcat (v, m, mv);

	GX_LoadPosMtxImm (mv, GX_PNMTX0);
	GX_Begin (GX_QUADS, GX_VTXFMT0, 4);
	draw_vert (0, 0, 0.0, 0.0);
	draw_vert (1, 0, 1.0, 0.0);
	draw_vert (2, 0, 1.0, 1.0);
	draw_vert (3, 0, 0.0, 1.0);
	GX_End ();
}

/****************************************************************************
 * StartGX
 *
 * Initialises GX and sets it up for use
 ***************************************************************************/
static void
StartGX ()
{
	GXColor background = { 0, 0, 0, 0xff };

	/*** Clear out FIFO area ***/
	memset (&gp_fifo, 0, DEFAULT_FIFO_SIZE);

	/*** Initialise GX ***/
	GX_Init (&gp_fifo, DEFAULT_FIFO_SIZE);
	GX_SetCopyClear (background, 0x00ffffff);

	GX_SetDispCopyGamma (GX_GM_1_0);
	GX_SetCullMode (GX_CULL_NONE);

	vwidth = 100;
	vheight = 100;
}

/****************************************************************************
 * StopGX
 *
 * Stops GX (when exiting)
 ***************************************************************************/
void StopGX()
{
	GX_AbortFrame();
	GX_Flush();

	VIDEO_SetBlack(TRUE);
	VIDEO_Flush();
}

/****************************************************************************
 * UpdatePadsCB
 *
 * called by postRetraceCallback in InitGCVideo - scans gcpad and wpad
 ***************************************************************************/
static void
UpdatePadsCB ()
{
	#ifdef HW_RVL
	WPAD_ScanPads();
	#endif
	PAD_ScanPads();

	for(int i=3; i >= 0; i--)
	{
		#ifdef HW_RVL
		memcpy(&userInput[i].wpad, WPAD_Data(i), sizeof(WPADData));
		#endif

		userInput[i].chan = i;
		userInput[i].pad.btns_d = PAD_ButtonsDown(i);
		userInput[i].pad.btns_u = PAD_ButtonsUp(i);
		userInput[i].pad.btns_h = PAD_ButtonsHeld(i);
		userInput[i].pad.stickX = PAD_StickX(i);
		userInput[i].pad.stickY = PAD_StickY(i);
		userInput[i].pad.substickX = PAD_SubStickX(i);
		userInput[i].pad.substickY = PAD_SubStickY(i);
		userInput[i].pad.triggerL = PAD_TriggerL(i);
		userInput[i].pad.triggerR = PAD_TriggerR(i);
	}
}

/****************************************************************************
 * MakeTexture
 *
 * - modified for a buffer with an offset (border)
 ****************************************************************************/
static void
MakeTexture (const void *src, void *dst, s32 width, s32 height)
{
  register u32 tmp0 = 0, tmp1 = 0, tmp2 = 0, tmp3 = 0;

  __asm__ __volatile__ ("       srwi            %6,%6,2\n"
                        "       srwi            %7,%7,2\n"
                        "       subi            %3,%4,4\n"
                        "       mr              %4,%3\n"
                        "       subi            %4,%4,4\n"
                        "2:     mtctr           %6\n"
                        "       mr                      %0,%5\n"
                        //
                        "1:     lwz                     %1,0(%5)\n"		//1
                        "       stwu            %1,8(%4)\n"
                        "       lwz                     %2,4(%5)\n"		//1
                        "       stwu            %2,8(%3)\n"
                        "       lwz                     %1,1032(%5)\n"		//2
                        "       stwu            %1,8(%4)\n"
                        "       lwz                     %2,1036(%5)\n"		//2
                        "       stwu            %2,8(%3)\n"
                        "       lwz                     %1,2064(%5)\n"		//3
                        "       stwu            %1,8(%4)\n"
                        "       lwz                     %2,2068(%5)\n"		//3
                        "       stwu            %2,8(%3)\n"
                        "       lwz                     %1,3096(%5)\n"		//4
                        "       stwu            %1,8(%4)\n"
                        "       lwz                     %2,3100(%5)\n"		//4
                        "       stwu            %2,8(%3)\n"
                        "       addi            %5,%5,8\n"
                        "       bdnz            1b\n"
                        "       addi            %5,%0,4128\n"		//5
                        "       subic.          %7,%7,1\n"
                        "       bne                     2b"
                        // regs 0-7
                        :"=&r" (tmp0), "=&r" (tmp1), "=&r" (tmp2),
                        "=&r" (tmp3), "+r" (dst):"r" (src), "r" (width),
                        "r" (height));
}

/****************************************************************************
 * SetupVideoMode
 *
 * Finds the optimal video mode, or uses the user-specified one
 * Also configures original video modes
 ***************************************************************************/
static void SetupVideoMode()
{
	if(currentVideoMode == GCSettings.videomode)
		return; // no need to do anything

	// choose the desired video mode
	switch(GCSettings.videomode)
	{
		case 1: // NTSC (480i)
			vmode = &TVNtsc480IntDf;
			break;
		case 2: // Progressive (480p)
			vmode = &TVNtsc480Prog;
			break;
		case 3: // PAL (50Hz)
			vmode = &TVPal574IntDfScale;
			break;
		case 4: // PAL (60Hz)
			vmode = &TVEurgb60Hz480IntDf;
			break;
		default:
			vmode = VIDEO_GetPreferredMode(NULL);

			#ifdef HW_DOL
			/* we have component cables, but the preferred mode is interlaced
			 * why don't we switch into progressive?
			 * on the Wii, the user can do this themselves on their Wii Settings */
			if(VIDEO_HaveComponentCable())
				vmode = &TVNtsc480Prog;
			#endif

			// use hardware vertical scaling to fill screen
			if(vmode->viTVMode >> 2 == VI_PAL)
				vmode = &TVPal574IntDfScale;
			break;
	}

	// configure original modes
	switch (vmode->viTVMode >> 2)
	{
		case VI_PAL:
			// 576 lines (PAL 50Hz)
			vmode_60hz = 0;

			// Original Video modes (forced to PAL 50Hz)
			// set video signal mode
			TV_224p.viTVMode = VI_TVMODE_PAL_DS;
			TV_448i.viTVMode = VI_TVMODE_PAL_INT;
			// set VI position
			TV_224p.viYOrigin = (VI_MAX_HEIGHT_PAL/2 - 448/2)/2;
			TV_448i.viYOrigin = (VI_MAX_HEIGHT_PAL - 448)/2;
			break;

		case VI_NTSC:
			// 480 lines (NTSC 60Hz)
			vmode_60hz = 1;

			// Original Video modes (forced to NTSC 60hz)
			// set video signal mode
			TV_239p.viTVMode = VI_TVMODE_NTSC_DS;
			TV_478i.viTVMode = VI_TVMODE_NTSC_INT;
			TV_224p.viTVMode = VI_TVMODE_NTSC_DS;
			TV_448i.viTVMode = VI_TVMODE_NTSC_INT;
			// set VI position
			TV_239p.viYOrigin = (VI_MAX_HEIGHT_NTSC/2 - 478/2)/2;
			TV_478i.viYOrigin = (VI_MAX_HEIGHT_NTSC - 478)/2;
			TV_224p.viYOrigin = (VI_MAX_HEIGHT_NTSC/2 - 448/2)/2;
			TV_448i.viYOrigin = (VI_MAX_HEIGHT_NTSC - 448)/2;
			break;

		default:
			// 480 lines (PAL 60Hz)
			vmode_60hz = 1;

			// Original Video modes (forced to PAL 60hz)
			// set video signal mode
			TV_239p.viTVMode = VI_TVMODE(vmode->viTVMode >> 2, VI_NON_INTERLACE);
			TV_478i.viTVMode = VI_TVMODE(vmode->viTVMode >> 2, VI_INTERLACE);
			TV_224p.viTVMode = VI_TVMODE(vmode->viTVMode >> 2, VI_NON_INTERLACE);
			TV_448i.viTVMode = VI_TVMODE(vmode->viTVMode >> 2, VI_INTERLACE);
			// set VI position
			TV_239p.viYOrigin = (VI_MAX_HEIGHT_NTSC/2 - 478/2)/2;
			TV_478i.viYOrigin = (VI_MAX_HEIGHT_NTSC - 478)/2;
			TV_224p.viYOrigin = (VI_MAX_HEIGHT_NTSC/2 - 448/2)/2;
			TV_448i.viYOrigin = (VI_MAX_HEIGHT_NTSC - 448)/2;
			break;
	}

	// check for progressive scan
	if (vmode->viTVMode == VI_TVMODE_NTSC_PROG)
		progressive = true;
	else
		progressive = false;

	#ifdef HW_RVL
	// widescreen fix
	if(CONF_GetAspectRatio() == CONF_ASPECT_16_9)
	{
		vmode->viWidth = VI_MAX_WIDTH_PAL-12;
		vmode->viXOrigin = ((VI_MAX_WIDTH_PAL - vmode->viWidth) / 2) + 2;
	}
	#endif

	currentVideoMode = GCSettings.videomode;
}

/****************************************************************************
 * InitGCVideo
 *
 * This function MUST be called at startup.
 * - also sets up menu video mode
 ***************************************************************************/

void
InitGCVideo ()
{
	SetupVideoMode();
	VIDEO_Configure (vmode);

	screenheight = 480;
	screenwidth = 640;

	// Allocate the video buffers
	xfb[0] = (u32 *) MEM_K0_TO_K1 (SYS_AllocateFramebuffer (vmode));
	xfb[1] = (u32 *) MEM_K0_TO_K1 (SYS_AllocateFramebuffer (vmode));

	// A console is always useful while debugging
	console_init (xfb[0], 20, 64, vmode->fbWidth, vmode->xfbHeight, vmode->fbWidth * 2);

	// Clear framebuffers etc.
	VIDEO_ClearFrameBuffer (vmode, xfb[0], COLOR_BLACK);
	VIDEO_ClearFrameBuffer (vmode, xfb[1], COLOR_BLACK);
	VIDEO_SetNextFramebuffer (xfb[0]);

	// video callbacks
	VIDEO_SetPostRetraceCallback ((VIRetraceCallback)UpdatePadsCB);
	VIDEO_SetPreRetraceCallback ((VIRetraceCallback)copy_to_xfb);

	VIDEO_SetBlack (FALSE);
	VIDEO_Flush ();
	VIDEO_WaitVSync ();
	if (vmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync ();

	copynow = GX_FALSE;

	StartGX ();
	InitLUTs();	// init LUTs for hq2x
	InitVideoThread ();
	// Finally, the video is up and ready for use :)
}

/****************************************************************************
 * ResetVideo_Emu
 *
 * Reset the video/rendering mode for the emulator rendering
****************************************************************************/
void
ResetVideo_Emu ()
{
	SetupVideoMode();
	GXRModeObj *rmode = vmode; // same mode as menu
	Mtx44 p;
	int i = -1;

	// original render mode or hq2x
	if (GCSettings.render == 0)
	{
		for (int j=0; j<4; j++)
		{
			if (tvmodes[j]->efbHeight == vheight)
			{
				i = j;
				break;
			}
		}
	}

	if(i >= 0) // we found a matching original mode
	{
		rmode = tvmodes[i];

		// hack to fix video output for hq2x (only when actually filtering; h<=239, w<=256)
		if (GCSettings.FilterMethod != FILTER_NONE && vheight <= 239 && vwidth <= 256)
		{
			memcpy(&TV_Custom, tvmodes[i], sizeof(TV_Custom));
			rmode = &TV_Custom;

			rmode->fbWidth = 512;
			rmode->efbHeight *= 2;
			rmode->xfbHeight *= 2;
			rmode->xfbMode = VI_XFBMODE_DF;
			rmode->viTVMode |= VI_INTERLACE;
		}
	}

	VIDEO_Configure (rmode);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (rmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();
	else
		while (VIDEO_GetNextField())
			VIDEO_WaitVSync();

	GXColor background = {0, 0, 0, 255};
	GX_SetCopyClear (background, 0x00ffffff);

	GX_SetViewport (0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
	GX_SetDispCopyYScale ((f32) rmode->xfbHeight / (f32) rmode->efbHeight);
	GX_SetScissor (0, 0, rmode->fbWidth, rmode->efbHeight);

	GX_SetDispCopySrc (0, 0, rmode->fbWidth, rmode->efbHeight);
	GX_SetDispCopyDst (rmode->fbWidth, rmode->xfbHeight);
	GX_SetCopyFilter (rmode->aa, rmode->sample_pattern, (GCSettings.render == 1) ? GX_TRUE : GX_FALSE, rmode->vfilter);	// deflicker ON only for filtered mode

	GX_SetFieldMode (rmode->field_rendering, ((rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));
	GX_SetPixelFmt (GX_PF_RGB8_Z24, GX_ZC_LINEAR);

	GX_SetZMode (GX_TRUE, GX_LEQUAL, GX_TRUE);
	GX_SetColorUpdate (GX_TRUE);

	guOrtho(p, rmode->efbHeight/2, -(rmode->efbHeight/2), -(rmode->fbWidth/2), rmode->fbWidth/2, 100, 1000);	// matrix, t, b, l, r, n, f
	GX_LoadProjectionMtx (p, GX_ORTHOGRAPHIC);

	draw_init ();
}

/****************************************************************************
 * Update Video
 ***************************************************************************/
uint32 prevRenderedFrameCount = 0;
extern bool CheckVideo;
int fscale;

void
update_video (int width, int height)
{

	vwidth = width;
	vheight = height;

	// Ensure previous vb has complete
	while ((LWP_ThreadIsSuspended (vbthread) == 0) || (copynow == GX_TRUE))
	{
		usleep (50);
	}

	whichfb ^= 1;

	if ( oldvheight != vheight || oldvwidth != vwidth )	// if rendered width/height changes, update scaling
		CheckVideo = 1;

	if ( CheckVideo && (IPPU.RenderedFramesCount != prevRenderedFrameCount) )	// if we get back from the menu, and have rendered at least 1 frame
	{
		int xscale, yscale;

		fscale = GetFilterScale((RenderFilter)GCSettings.FilterMethod);

		ResetVideo_Emu ();	// reset video to emulator rendering settings

		/** Update scaling **/
		if (GCSettings.render == 0)	// original render mode
		{
			if (GCSettings.FilterMethod != FILTER_NONE && vheight <= 239 && vwidth <= 256)
			{	// filters; normal operation
				xscale = vwidth;
				yscale = vheight;
			}
			else
			{	// no filtering
				fscale = 1;
				xscale = 256;
				yscale = vheight / 2;
			}
		}
		else // unfiltered and filtered mode
		{
			xscale = 320;
			yscale = (vheight > (vmode->efbHeight/2)) ? (vheight / 2) : vheight;
		}

		// aspect ratio scaling (change width scale)
		// yes its pretty cheap and ugly, but its easy!
		if (GCSettings.widescreen)
			xscale = (3*xscale)/4;

		xscale *= GCSettings.ZoomLevel;
		yscale *= GCSettings.ZoomLevel;

		square[6] = square[3]  =  xscale + GCSettings.xshift;
		square[0] = square[9]  = -xscale + GCSettings.xshift;
		square[4] = square[1]  =  yscale - GCSettings.yshift;
		square[7] = square[10] = -yscale - GCSettings.yshift;
		DCFlushRange (square, 32); // update memory BEFORE the GPU accesses it!
    	draw_init ();

		// initialize the texture obj we are going to use
		GX_InitTexObj (&texobj, texturemem, vwidth*fscale, vheight*fscale, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);

	    if (GCSettings.render == 0 || GCSettings.render == 2)
			GX_InitTexObjLOD(&texobj,GX_NEAR,GX_NEAR_MIP_NEAR,2.5,9.0,0.0,GX_FALSE,GX_FALSE,GX_ANISO_1); // original/unfiltered video mode: force texture filtering OFF

		GX_LoadTexObj (&texobj, GX_TEXMAP0);	// load texture object so its ready to use

		oldvwidth = vwidth;
		oldvheight = vheight;
		CheckVideo = 0;
	}

	// convert image to texture
	if (GCSettings.FilterMethod != FILTER_NONE && vheight <= 239 && vwidth <= 256)	// don't do filtering on game textures > 256 x 239
	{
		FilterMethod ((uint8*) GFX.Screen, EXT_PITCH, (uint8*) filtermem, vwidth*fscale*2, vwidth, vheight);
		MakeTexture565((char *) filtermem, (char *) texturemem, vwidth*fscale, vheight*fscale);
	}
	else
	{
		MakeTexture((char *) GFX.Screen, (char *) texturemem, vwidth, vheight);
	}

	DCFlushRange (texturemem, TEXTUREMEM_SIZE);	// update the texture memory
	GX_InvalidateTexAll ();

	draw_square (view);		// draw the quad

	GX_DrawDone ();
	VIDEO_SetNextFramebuffer (xfb[whichfb]);
	VIDEO_Flush ();
	copynow = GX_TRUE;

	// Return to caller, don't waste time waiting for vb
	LWP_ResumeThread (vbthread);
}

/****************************************************************************
 * Zoom Functions
 ***************************************************************************/
void
zoom (float speed)
{
	if (GCSettings.ZoomLevel > 1)
		GCSettings.ZoomLevel += (speed / -100.0);
	else
		GCSettings.ZoomLevel += (speed / -200.0);

	if (GCSettings.ZoomLevel < 0.5)
		GCSettings.ZoomLevel = 0.5;
	else if (GCSettings.ZoomLevel > 2.0)
		GCSettings.ZoomLevel = 2.0;

	oldvheight = 0;	// update video
}

void
zoom_reset ()
{
	GCSettings.ZoomLevel = 1.0;
	oldvheight = 0;	// update video
}

void AllocGfxMem()
{
	snes9xgfx = (unsigned char *)memalign(32, SNES9XGFX_SIZE);
	memset(snes9xgfx, 0, SNES9XGFX_SIZE);
	filtermem = (unsigned char *)memalign(32, FILTERMEM_SIZE);
	memset(filtermem, 0, FILTERMEM_SIZE);

	GFX.Pitch = EXT_PITCH;
	GFX.Screen = (uint16*)(snes9xgfx + EXT_OFFSET);
}

void FreeGfxMem()
{
	if(snes9xgfx)
	{
		free(snes9xgfx);
		snes9xgfx = NULL;
	}
	if(filtermem)
	{
		free(filtermem);
		filtermem = NULL;
	}
}

/****************************************************************************
 * setGFX
 *
 * Setup the global GFX information for Snes9x
 ***************************************************************************/
void
setGFX ()
{
	GFX.Pitch = EXT_PITCH;
}

/****************************************************************************
 * TakeScreenshot
 *
 * Copies the current screen into a GX texture
 ***************************************************************************/
void TakeScreenshot()
{
	int texSize = vmode->fbWidth * vmode->efbHeight * 4;

	if(gameScreenTex) free(gameScreenTex);
	gameScreenTex = (u8 *)memalign(32, texSize);
	if(gameScreenTex == NULL) return;
	GX_SetTexCopySrc(0, 0, vmode->fbWidth, vmode->efbHeight);
	GX_SetTexCopyDst(vmode->fbWidth, vmode->efbHeight, GX_TF_RGBA8, GX_FALSE);
	GX_CopyTex(gameScreenTex, GX_FALSE);
	GX_PixModeSync();
	DCFlushRange(gameScreenTex, texSize);

	#ifdef HW_RVL
	if(gameScreenTex2) free(gameScreenTex2);
	gameScreenTex2 = (u8 *)memalign(32, texSize);
	if(gameScreenTex2 == NULL) return;
	GX_CopyTex(gameScreenTex2, GX_FALSE);
	GX_PixModeSync();
	DCFlushRange(gameScreenTex2, texSize);
	#endif
}

/****************************************************************************
 * ResetVideo_Menu
 *
 * Reset the video/rendering mode for the menu
****************************************************************************/
void
ResetVideo_Menu ()
{
	Mtx44 p;
	f32 yscale;
	u32 xfbHeight;

	SetupVideoMode();
	VIDEO_Configure (vmode);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (vmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();
	else
		while (VIDEO_GetNextField())
			VIDEO_WaitVSync();

	// clears the bg to color and clears the z buffer
	GXColor background = {0, 0, 0, 255};
	GX_SetCopyClear (background, 0x00ffffff);

	yscale = GX_GetYScaleFactor(vmode->efbHeight,vmode->xfbHeight);
	xfbHeight = GX_SetDispCopyYScale(yscale);
	GX_SetScissor(0,0,vmode->fbWidth,vmode->efbHeight);
	GX_SetDispCopySrc(0,0,vmode->fbWidth,vmode->efbHeight);
	GX_SetDispCopyDst(vmode->fbWidth,xfbHeight);
	GX_SetCopyFilter(vmode->aa,vmode->sample_pattern,GX_TRUE,vmode->vfilter);
	GX_SetFieldMode(vmode->field_rendering,((vmode->viHeight==2*vmode->xfbHeight)?GX_ENABLE:GX_DISABLE));

	if (vmode->aa)
		GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
	else
		GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

	// setup the vertex descriptor
	// tells the flipper to expect direct data
	GX_ClearVtxDesc();
	GX_InvVtxCache ();
	GX_InvalidateTexAll();

	GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc (GX_VA_CLR0, GX_DIRECT);

	GX_SetVtxAttrFmt (GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt (GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GX_SetZMode (GX_FALSE, GX_LEQUAL, GX_TRUE);

	GX_SetNumChans(1);
	GX_SetNumTexGens(1);
	GX_SetTevOp (GX_TEVSTAGE0, GX_PASSCLR);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

	guMtxIdentity(GXmodelView2D);
	guMtxTransApply (GXmodelView2D, GXmodelView2D, 0.0F, 0.0F, -50.0F);
	GX_LoadPosMtxImm(GXmodelView2D,GX_PNMTX0);

	guOrtho(p,0,479,0,639,0,300);
	GX_LoadProjectionMtx(p, GX_ORTHOGRAPHIC);

	GX_SetViewport(0,0,vmode->fbWidth,vmode->efbHeight,0,1);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetAlphaUpdate(GX_TRUE);
}

/****************************************************************************
 * Menu_Render
 *
 * Renders everything current sent to GX, and flushes video
 ***************************************************************************/
void Menu_Render()
{
	GX_DrawDone ();

	whichfb ^= 1; // flip framebuffer
	GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
	GX_SetColorUpdate(GX_TRUE);
	GX_CopyDisp(xfb[whichfb],GX_TRUE);
	VIDEO_SetNextFramebuffer(xfb[whichfb]);
	VIDEO_Flush();
	VIDEO_WaitVSync();
}

/****************************************************************************
 * Menu_DrawImg
 *
 * Draws the specified image on screen using GX
 ***************************************************************************/
void Menu_DrawImg(f32 xpos, f32 ypos, u16 width, u16 height, u8 data[],
	f32 degrees, f32 scaleX, f32 scaleY, u8 alpha)
{
	if(data == NULL)
		return;

	GXTexObj texObj;

	GX_InitTexObj(&texObj, data, width,height, GX_TF_RGBA8,GX_CLAMP, GX_CLAMP,GX_FALSE);
	GX_LoadTexObj(&texObj, GX_TEXMAP0);
	GX_InvalidateTexAll();

	GX_SetTevOp (GX_TEVSTAGE0, GX_MODULATE);
	GX_SetVtxDesc (GX_VA_TEX0, GX_DIRECT);

	Mtx m,m1,m2, mv;
	width *=.5;
	height*=.5;
	guMtxIdentity (m1);
	guMtxScaleApply(m1,m1,scaleX,scaleY,1.0);
	Vector axis = (Vector) {0 , 0, 1 };
	guMtxRotAxisDeg (m2, &axis, degrees);
	guMtxConcat(m2,m1,m);

	guMtxTransApply(m,m, xpos+width,ypos+height,0);
	guMtxConcat (GXmodelView2D, m, mv);
	GX_LoadPosMtxImm (mv, GX_PNMTX0);

	GX_Begin(GX_QUADS, GX_VTXFMT0,4);
	GX_Position3f32(-width, -height,  0);
	GX_Color4u8(0xFF,0xFF,0xFF,alpha);
	GX_TexCoord2f32(0, 0);

	GX_Position3f32(width, -height,  0);
	GX_Color4u8(0xFF,0xFF,0xFF,alpha);
	GX_TexCoord2f32(1, 0);

	GX_Position3f32(width, height,  0);
	GX_Color4u8(0xFF,0xFF,0xFF,alpha);
	GX_TexCoord2f32(1, 1);

	GX_Position3f32(-width, height,  0);
	GX_Color4u8(0xFF,0xFF,0xFF,alpha);
	GX_TexCoord2f32(0, 1);
	GX_End();
	GX_LoadPosMtxImm (GXmodelView2D, GX_PNMTX0);

	GX_SetTevOp (GX_TEVSTAGE0, GX_PASSCLR);
	GX_SetVtxDesc (GX_VA_TEX0, GX_NONE);
}

/****************************************************************************
 * Menu_DrawRectangle
 *
 * Draws a rectangle at the specified coordinates using GX
 ***************************************************************************/
void Menu_DrawRectangle(f32 x, f32 y, f32 width, f32 height, GXColor color, u8 filled)
{
	u8 fmt;
	long n;
	int i;
	f32 x2 = x+width;
	f32 y2 = y+height;
	Vector v[] = {{x,y,0.0f}, {x2,y,0.0f}, {x2,y2,0.0f}, {x,y2,0.0f}, {x,y,0.0f}};

	if(!filled)
	{
		fmt = GX_LINESTRIP;
		n = 5;
	}
	else
	{
		fmt = GX_TRIANGLEFAN;
		n = 4;
	}

	GX_Begin(fmt, GX_VTXFMT0, n);
	for(i=0; i<n; i++)
	{
		GX_Position3f32(v[i].x, v[i].y,  v[i].z);
		GX_Color4u8(color.r, color.g, color.b, color.a);
	}
	GX_End();
}
