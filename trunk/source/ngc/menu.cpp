/****************************************************************************
 * Snes9x 1.50
 *
 * Nintendo Gamecube Menu
 *
 * softdev July 2006
 * crunchy2 May-June 2007
 ****************************************************************************/
#include <gccore.h>
#include <ogcsys.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wiiuse/wpad.h>
#include "snes9x.h"
#include "snes9xGx.h"
#include "memmap.h"
#include "debug.h"
#include "cpuexec.h"
#include "ppu.h"
#include "apu.h"
#include "display.h"
#include "gfx.h"
#include "soundux.h"
#include "spc700.h"
#include "spc7110.h"
#include "controls.h"
#include "aram.h"
#include "ftfont.h"
#include "video.h"
#include "mcsave.h"
#include "filesel.h"
#include "unzip.h"
#include "smbload.h"
#include "mcsave.h"
#include "sdload.h"
#include "memfile.h"
#include "dvd.h"
#include "s9xconfig.h"
#include "sram.h"
#include "preferences.h"

#include "button_mapping.h"
#include "ftfont.h"

#include "cheats.h"
#include "cheatmgr.h"

extern void DrawMenu (char items[][50], char *title, int maxitems, int selected, int fontsize);

extern SCheatData Cheat;


#define PSOSDLOADID 0x7c6000a6
extern int menu;
extern unsigned long ARAM_ROMSIZE;

#define SOFTRESET_ADR ((volatile u32*)0xCC003024)

/****************************************************************************
 * Reboot
 ****************************************************************************/

void Reboot() {
#ifdef HW_RVL
    SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
#else
#define SOFTRESET_ADR ((volatile u32*)0xCC003024)
    *SOFTRESET_ADR = 0x00000000;
#endif
}

/****************************************************************************
 * Load Manager
 ****************************************************************************/

int
LoadManager ()
{
	int loadROM = OpenROM(GCSettings.LoadMethod);

	/***
	* check for autoloadsram / freeze
	***/
	if ( loadROM == 1 ) // if ROM was loaded, load the SRAM & settings
	{
		if ( GCSettings.AutoLoad == 1 )
			quickLoadSRAM ( SILENT );
		else if ( GCSettings.AutoLoad == 2 )
			quickLoadFreeze ( SILENT );
			
		// setup cheats
		SetupCheats();
	}

	return loadROM;
}

/****************************************************************************
 * Preferences Menu
 ****************************************************************************/
static int prefmenuCount = 15;
static char prefmenu[][50] = {

	"Load Method",
	"Load Folder",
	"Save Method",
	"Save Folder",

	"Auto Load",
	"Auto Save",
	"Verify MC Saves",

	"Reverse Stereo",
	"Interpolated Sound",
	"Transparency",
	"Display Frame Rate",
	"C-Stick Zoom",
	"Video Filtering",

	"Save Preferences",
	"Back to Main Menu"
};

void
PreferencesMenu ()
{
	int ret = 0;
	int quit = 0;
	int oldmenu = menu;
	menu = 0;
	while (quit == 0)
	{
		// some load/save methods are not implemented - here's where we skip them
		
		#ifndef HW_RVL // GameCube mode
			if(GCSettings.LoadMethod == METHOD_USB)
				GCSettings.LoadMethod++;
			if(GCSettings.SaveMethod == METHOD_USB)
				GCSettings.SaveMethod++;
		#else // Wii mode
			if(GCSettings.LoadMethod == METHOD_DVD)
				GCSettings.LoadMethod++;
		#endif
		
		if(GCSettings.SaveMethod == METHOD_DVD) // saving to DVD is impossible
			GCSettings.SaveMethod++;
		
		if(GCSettings.SaveMethod == METHOD_SMB) // disable SMB - network saving needs some work
			GCSettings.SaveMethod++;
		
		if(GCSettings.LoadMethod == METHOD_SMB) // disable SMB - network loading needs some work
			GCSettings.LoadMethod++;
				
		// correct load/save methods out of bounds
		if(GCSettings.LoadMethod > 4)
			GCSettings.LoadMethod = 0;
		if(GCSettings.SaveMethod > 6)
			GCSettings.SaveMethod = 0;
		
		if (GCSettings.LoadMethod == METHOD_AUTO) sprintf (prefmenu[0],"Load Method AUTO");
		else if (GCSettings.LoadMethod == METHOD_SD) sprintf (prefmenu[0],"Load Method SD");
		else if (GCSettings.LoadMethod == METHOD_USB) sprintf (prefmenu[0],"Load Method USB");
		else if (GCSettings.LoadMethod == METHOD_DVD) sprintf (prefmenu[0],"Load Method DVD");
		else if (GCSettings.LoadMethod == METHOD_SMB) sprintf (prefmenu[0],"Load Method Network");

		sprintf (prefmenu[1], "Load Folder %s",	GCSettings.LoadFolder);

		if (GCSettings.SaveMethod == METHOD_AUTO) sprintf (prefmenu[2],"Save Method AUTO");
		else if (GCSettings.SaveMethod == METHOD_SD) sprintf (prefmenu[2],"Save Method SD");
		else if (GCSettings.SaveMethod == METHOD_USB) sprintf (prefmenu[2],"Save Method USB");
		else if (GCSettings.SaveMethod == METHOD_SMB) sprintf (prefmenu[2],"Save Method Network");
		else if (GCSettings.SaveMethod == METHOD_MC_SLOTA) sprintf (prefmenu[2],"Save Method MC Slot A");
		else if (GCSettings.SaveMethod == METHOD_MC_SLOTB) sprintf (prefmenu[2],"Save Method MC Slot B");
		
		sprintf (prefmenu[3], "Save Folder %s",	GCSettings.SaveFolder);
		
		// disable changing load/save directories for now
		prefmenu[1][0] = '\0';
		prefmenu[3][0] = '\0';

		if (GCSettings.AutoLoad == 0) sprintf (prefmenu[4],"Auto Load OFF");
		else if (GCSettings.AutoLoad == 1) sprintf (prefmenu[4],"Auto Load SRAM");
		else if (GCSettings.AutoLoad == 2) sprintf (prefmenu[4],"Auto Load FREEZE");

		if (GCSettings.AutoSave == 0) sprintf (prefmenu[5],"Auto Save OFF");
		else if (GCSettings.AutoSave == 1) sprintf (prefmenu[5],"Auto Save SRAM");
		else if (GCSettings.AutoSave == 2) sprintf (prefmenu[5],"Auto Save FREEZE");
		else if (GCSettings.AutoSave == 3) sprintf (prefmenu[5],"Auto Save BOTH");

		sprintf (prefmenu[6], "Verify MC Saves %s",
			GCSettings.VerifySaves == true ? " ON" : "OFF");

		sprintf (prefmenu[7], "Reverse Stereo %s",
			Settings.ReverseStereo == true ? " ON" : "OFF");

		sprintf (prefmenu[8], "Interpolated Sound %s",
			Settings.InterpolatedSound == true ? " ON" : "OFF");

		sprintf (prefmenu[9], "Transparency %s",
			Settings.Transparency == true ? " ON" : "OFF");

		sprintf (prefmenu[10], "Display Frame Rate %s",
			Settings.DisplayFrameRate == true ? " ON" : "OFF");

		sprintf (prefmenu[11], "C-Stick Zoom %s",
			GCSettings.NGCZoom == true ? " ON" : "OFF");
			
		sprintf (prefmenu[12], "Video Filtering %s",
			GCSettings.render == true ? " ON" : "OFF");
			
		ret = RunMenu (prefmenu, prefmenuCount, (char*)"Preferences", 16);

		switch (ret)
		{
			case 0:
				GCSettings.LoadMethod ++;
				break;
				
			case 1:
				break;
				
			case 2:
				GCSettings.SaveMethod ++;
				break;
				
			case 3:
				break;
				
			case 4:
				GCSettings.AutoLoad ++;
				if (GCSettings.AutoLoad > 2)
					GCSettings.AutoLoad = 0;
				break;
				
			case 5:
				GCSettings.AutoSave ++;
				if (GCSettings.AutoSave > 3)
					GCSettings.AutoSave = 0;
				break;

			case 6:
				GCSettings.VerifySaves ^= 1;
				break;

			case 7:
				Settings.ReverseStereo ^= 1;
				break;

			case 8:
				Settings.InterpolatedSound ^= 1;
				break;

			case 9:
				Settings.Transparency ^= 1;
				break;

			case 10:
				Settings.DisplayFrameRate ^= 1;
				break;

			case 11:
				GCSettings.NGCZoom ^= 1;
				break;
				
			case 12:
				GCSettings.render ^= 1;
				break;

			case 13:
				quickSavePrefs(NOTSILENT);
				break;

			case -1: /*** Button B ***/
			case 14:
				quit = 1;
				break;

		}
	}
	menu = oldmenu;
}

/****************************************************************************
 * Cheat Menu
 ****************************************************************************/
static int cheatmenuCount = 0;
static char cheatmenu[MAX_CHEATS][50];
 
void CheatMenu()
{
	int ret = -1;
	int oldmenu = menu;
	menu = 0;
		
	if(Cheat.num_cheats > 0)
	{
		cheatmenuCount = Cheat.num_cheats + 1;
		
		sprintf (cheatmenu[cheatmenuCount-1], "Back to Game Menu");
				
		while(ret != cheatmenuCount-1)
		{
			if(ret >= 0)
			{
				if(Cheat.c[ret].enabled)
					S9xDisableCheat(ret);
				else
					S9xEnableCheat(ret);
			}
			
			for(uint16 i=0; i < Cheat.num_cheats; i++)
				sprintf (cheatmenu[i], "%s %s", Cheat.c[i].name, Cheat.c[i].enabled == true ? " ON" : "OFF");
				
			ret = RunMenu (cheatmenu, cheatmenuCount, (char*)"Cheats", 16);
		}
	}
	else
	{
		WaitPrompt((char*)"Cheat file not found!");
	}
	menu = oldmenu;
}

/****************************************************************************
 * Game Options Menu
 ****************************************************************************/
static int gamemenuCount = 9;
static char gamemenu[][50] = {
  "Return to Game",
  "Reset Game",
  "ROM Information",
  "Cheats",
  "Load SRAM", "Save SRAM",
  "Load Freeze", "Save Freeze",
  "Back to Main Menu"
};

int
GameMenu ()
{
	int ret, retval = 0;
	int quit = 0;
	int oldmenu = menu;
	menu = 0;
	
	while (quit == 0)
	{
		ret = RunMenu (gamemenu, gamemenuCount, (char*)"Game Options");

		switch (ret)
		{
			case 0: // Return to Game
				quit = retval = 1;
				break;

			case 1: // Reset Game
				S9xSoftReset ();
				quit = retval = 1;
				break;

			case 2: // ROM Information
				RomInfo();
				WaitButtonA ();
				break;

			case 3: // load cheats
				CheatMenu();
				break;

			case 4: // Load SRAM
				LoadSRAM(GCSettings.SaveMethod, NOTSILENT);
				break;

			case 5: // Save SRAM
				SaveSRAM(GCSettings.SaveMethod, NOTSILENT);
				break;

			case 6: // Load Freeze
				quit = retval = NGCUnfreezeGame (GCSettings.SaveMethod, SILENT);
				break;

			case 7: // Save Freeze
				NGCFreezeGame (GCSettings.SaveMethod, NOTSILENT);
				break;

			case -1: // Button B
			case 8: // Return to previous menu
				retval = 0;
				quit = 1;
				break;
		}
	}

	menu = oldmenu;
	
	return retval;
}

/****************************************************************************
 * Controller Configuration
 *
 * Snes9x 1.50 uses a cmd system to work out which button has been pressed.
 * Here, I simply move the designated value to the gcpadmaps array, which saves
 * on updating the cmd sequences.
 ****************************************************************************/
u32
GetInput (u16 ctrlr_type)
{
	//u32 exp_type;
	u32 pressed;
	pressed=0;
	s8 gc_px = 0;

	while( PAD_ButtonsHeld(0)
#ifdef HW_RVL
	| WPAD_ButtonsHeld(0)
#endif
	) VIDEO_WaitVSync();	// button 'debounce'

	while (pressed == 0)
	{
		VIDEO_WaitVSync();
		// get input based on controller type
		if (ctrlr_type == CTRLR_GCPAD)
		{
			pressed = PAD_ButtonsHeld (0);
			gc_px = PAD_SubStickX (0);
		}
#ifdef HW_RVL
		else
		{
		//	if ( WPAD_Probe( 0, &exp_type) == 0)	// check wiimote and expansion status (first if wiimote is connected & no errors)
		//	{
				pressed = WPAD_ButtonsHeld (0);

		//		if (ctrlr_type != CTRLR_WIIMOTE && exp_type != ctrlr_type+1)	// if we need input from an expansion, and its not connected...
		//			pressed = 0;
		//	}
		}
#endif
		/*** check for exit sequence (c-stick left OR home button) ***/
		if ( (gc_px < -70) || (pressed & WPAD_BUTTON_HOME) || (pressed & WPAD_CLASSIC_BUTTON_HOME) )
			return 0;
	}	// end while
	while( pressed == (PAD_ButtonsHeld(0)
#ifdef HW_RVL
						| WPAD_ButtonsHeld(0)
#endif
						) ) VIDEO_WaitVSync();

	return pressed;
}	// end GetInput()

int cfg_text_count = 7;
char cfg_text[][50] = {
"Remapping          ",
"Press Any Button",
"on the",
"       ",	// identify controller
"                   ",
"Press C-Left or",
"Home to exit"
};

u32
GetButtonMap(u16 ctrlr_type, char* btn_name)
{
	u32 pressed, previous;
	char temp[50] = "";
	int k;
	pressed = 0; previous = 1;

	switch (ctrlr_type) {
		case CTRLR_NUNCHUK:
			strncpy (cfg_text[3], (char*)"NUNCHUK", 7);
			break;
		case CTRLR_CLASSIC:
			strncpy (cfg_text[3], (char*)"CLASSIC", 7);
			break;
		case CTRLR_GCPAD:
			strncpy (cfg_text[3], (char*)"GC PAD", 7);
			break;
		case CTRLR_WIIMOTE:
			strncpy (cfg_text[3], (char*)"WIIMOTE", 7);
			break;
	};

	/*** note which button we are remapping ***/
	sprintf (temp, (char*)"Remapping ");
	for (k=0; k<9-strlen(btn_name) ;k++) strcat(temp, " "); // add whitespace padding to align text
	strncat (temp, btn_name, 9);		// snes button we are remapping
	strncpy (cfg_text[0], temp, 19);	// copy this all back to the text we wish to display

	DrawMenu(&cfg_text[0], NULL, cfg_text_count, 1);	// display text

//	while (previous != pressed && pressed == 0);	// get two consecutive button presses (which are the same)
//	{
//		previous = pressed;
//		VIDEO_WaitVSync();	// slow things down a bit so we don't overread the pads
		pressed = GetInput(ctrlr_type);
//	}
	return pressed;
}	// end getButtonMap()

int cfg_btns_count = 13;
char cfg_btns_menu[][50] = {
	"A        -         ",
	"B        -         ",
	"X        -         ",
	"Y        -         ",
	"L TRIG   -         ",
	"R TRIG   -         ",
	"SELECT   -         ",
	"START    -         ",
	"UP       -         ",
	"DOWN     -         ",
	"LEFT     -         ",
	"RIGHT    -         ",
	"Return to previous"
};

extern unsigned int gcpadmap[];
extern unsigned int wmpadmap[];
extern unsigned int ccpadmap[];
extern unsigned int ncpadmap[];

void
ConfigureButtons (u16 ctrlr_type)
{
	int quit = 0;
	int ret = 0;
	int oldmenu = menu;
	menu = 0;
	char* menu_title;
	u32 pressed;

	unsigned int* currentpadmap;
	char temp[50] = "";
	int i, j, k;

	/*** Update Menu Title (based on controller we're configuring) ***/
	switch (ctrlr_type) {
		case CTRLR_NUNCHUK:
			menu_title = (char*)"SNES     -  NUNCHUK";
			currentpadmap = ncpadmap;
			break;
		case CTRLR_CLASSIC:
			menu_title = (char*)"SNES     -  CLASSIC";
			currentpadmap = ccpadmap;
			break;
		case CTRLR_GCPAD:
			menu_title = (char*)"SNES     -   GC PAD";
			currentpadmap = gcpadmap;
			break;
		case CTRLR_WIIMOTE:
			menu_title = (char*)"SNES     -  WIIMOTE";
			currentpadmap = wmpadmap;
			break;
	};

	while (quit == 0)
	{
		/*** Update Menu with Current ButtonMap ***/
		for (i=0; i<12; i++) // snes pad has 12 buttons to config (go thru them)
		{
			// get current padmap button name to display
			for ( j=0;
					j < ctrlr_def[ctrlr_type].num_btns &&
					currentpadmap[i] != ctrlr_def[ctrlr_type].map[j].btn	// match padmap button press with button names
				; j++ );

			memset (temp, 0, sizeof(temp));
			strncpy (temp, cfg_btns_menu[i], 12);	// copy snes button information
			if (currentpadmap[i] == ctrlr_def[ctrlr_type].map[j].btn)		// check if a match was made
			{
				for (k=0; k<7-strlen(ctrlr_def[ctrlr_type].map[j].name) ;k++) strcat(temp, " "); // add whitespace padding to align text
				strncat (temp, ctrlr_def[ctrlr_type].map[j].name, 6);		// update button map display
			}
			else
				strcat (temp, (char*)"---");								// otherwise, button is 'unmapped'
			strncpy (cfg_btns_menu[i], temp, 19);	// move back updated information

		}

		ret = RunMenu (cfg_btns_menu, cfg_btns_count, menu_title, 16);

		switch (ret)
		{
			case 0:
			case 1:
			case 2:
			case 3:
			case 4:
			case 5:
			case 6:
			case 7:
			case 8:
			case 9:
			case 10:
			case 11:
				/*** Change button map ***/
				// wait for input
				memset (temp, 0, sizeof(temp));
				strncpy(temp, cfg_btns_menu[ret], 6);			// get the name of the snes button we're changing
				pressed = GetButtonMap(ctrlr_type, temp);	// get a button selection from user
				// FIX: check if input is valid for this controller
				if (pressed != 0)	// check if a the button was configured, or if the user exited.
					currentpadmap[ret] = pressed;	// update mapping
				break;

			case -1: /*** Button B ***/
			case 12:
				/*** Return ***/
				quit = 1;
				break;
		}
	}
	menu = oldmenu;
}	// end configurebuttons()

int ctlrmenucount = 8;
char ctlrmenu[][50] = {
	"MultiTap",
	"SuperScope",
	"Nunchuk",
	"Classic Controller",
	"Gamecube Pad",
	"Wiimote",
	"Save Preferences",
	"Go Back"
};

void
ConfigureControllers ()
{
	int quit = 0;
	int ret = 0;
	int oldmenu = menu;
	menu = 0;

	// disable unavailable controller options if in GC mode
	#ifndef HW_RVL
		ctlrmenu[1][0] = '\0';
		ctlrmenu[2][0] = '\0';
		ctlrmenu[4][0] = '\0';
	#endif

	while (quit == 0)
	{
		sprintf (ctlrmenu[0], "MultiTap %s", Settings.MultiPlayer5Master == true ? " ON" : "OFF");
		
		if (GCSettings.Superscope > 0)
			sprintf (ctlrmenu[1], "Superscope: Pad %d", GCSettings.Superscope);
		else 
			sprintf (ctlrmenu[1], "Superscope     OFF");

		/*** Controller Config Menu ***/
        ret = RunMenu (ctlrmenu, ctlrmenucount, (char*)"Configure Controllers");

		switch (ret)
		{
			case 0:
				Settings.MultiPlayer5Master = (Settings.MultiPlayer5Master == false ? true : false);
				if (Settings.MultiPlayer5Master)
					S9xSetController (1, CTL_MP5, 1, 2, 3, -1);
				else
					S9xSetController (1, CTL_JOYPAD, 1, 0, 0, 0);
				break;
			case 1:
				GCSettings.Superscope ++;
				if (GCSettings.Superscope > 4)
					GCSettings.Superscope = 0;
			case 2:
				/*** Configure Nunchuk ***/
				ConfigureButtons (CTRLR_NUNCHUK);
				break;

			case 3:
				/*** Configure Classic ***/
				ConfigureButtons (CTRLR_CLASSIC);
				break;

			case 4:
				/*** Configure GC Pad ***/
				ConfigureButtons (CTRLR_GCPAD);
				break;

			case 5:
				/*** Configure Wiimote ***/
				ConfigureButtons (CTRLR_WIIMOTE);
				break;

			case 6:
				/*** Save Preferences Now ***/
				quickSavePrefs(NOTSILENT);
				break;

			case -1: /*** Button B ***/
			case 7:
				/*** Return ***/
				quit = 1;
				break;
		}
	}

	menu = oldmenu;
}

/****************************************************************************
 * Main Menu
 ****************************************************************************/
int menucount = 7;
char menuitems[][50] = {
  "Choose Game", "Controller Configuration", "Preferences",
  "Game Menu",
  "Credits", "Reset System", "Exit"
};

void
mainmenu (int selectedMenu)
{
	int quit = 0;
	int ret;
	int *psoid = (int *) 0x80001800;
	void (*PSOReload) () = (void (*)()) 0x80001800;

	// disable game-specific menu items if a ROM isn't loaded
	if ( ARAM_ROMSIZE == 0 )
    	menuitems[3][0] = '\0';
	else
		sprintf (menuitems[3], "Game Menu");

	VIDEO_WaitVSync ();

	while (quit == 0)
	{
		if(selectedMenu >= 0)
		{
			ret = selectedMenu;
			selectedMenu = -1; // default back to main menu
		}
		else
		{
			ret = RunMenu (menuitems, menucount, (char*)"Main Menu");
		}

		switch (ret)
		{
			case 0:
				// Load ROM Menu
				quit = LoadManager ();
				break;

			case 1:
				// Configure Controllers
				ConfigureControllers ();
				break;

			case 2:
				// Preferences
				PreferencesMenu ();
				break;

			case 3:
				// Game Options
				quit = GameMenu ();
				break;

			case 4:
				// Credits
				Credits ();
				WaitButtonA ();
                break;

			case 5:
				// Reset the Gamecube/Wii
			    Reboot();
                break;

			case 6:
				// Exit to Loader
				#ifdef HW_RVL
					exit(0);
				#else	// gamecube
					if (psoid[0] == PSOSDLOADID)
						PSOReload ();
				#endif
				break;

			case -1: // Button B
				// Return to Game
				quit = 1;
				break;
		}

	}

	/*** Remove any still held buttons ***/
	#ifdef HW_RVL
		while( PAD_ButtonsHeld(0) || WPAD_ButtonsHeld(0) )
		    VIDEO_WaitVSync();
	#else
		while( PAD_ButtonsHeld(0) )
		    VIDEO_WaitVSync();
	#endif

	ReInitGCVideo();	// update video after reading settings
	
	Settings.SuperScopeMaster = (GCSettings.Superscope > 0 ? true : false);	// update superscope settings
	// update mouse/justifier info?
	SetControllers();
}
