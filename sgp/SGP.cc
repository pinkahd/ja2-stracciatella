/* The implementation of swprintf() is broken on FreeBSD and sometimes fails if
 * LC_TYPE is not set to UTF-8.  This happens when characters, which cannot be
 * represented by the current LC_CTYPE, are printed. */
#if defined __FreeBSD__
#	define BROKEN_SWPRINTF
#endif

#if defined BROKEN_SWPRINTF
#	include <locale.h>
#endif

#include <exception>
#include <new>

#include "Button_System.h"
#include "Debug.h"
#include "FileMan.h"
#include "Font.h"
#include "GameLoop.h"
#include "Init.h" // XXX should not be used in SGP
#include "Input.h"
#include "Intro.h"
#include "JA2_Splash.h"
#include "MemMan.h"
#include "Random.h"
#include "SGP.h"
#include "SaveLoadGame.h" // XXX should not be used in SGP
#include "SoundMan.h"
#include "VObject.h"
#include "Video.h"
#include "VSurface.h"
#include <SDL.h>
#include "GameRes.h"
#include "Logger.h"
#include "UILayout.h"

#if defined _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>

#	include "Local.h"
#endif

#include "Multi_Language_Graphic_Utils.h"


#ifdef JA2
extern BOOLEAN gfPauseDueToPlayerGamePause;
#endif


static BOOLEAN gfApplicationActive;
BOOLEAN gfProgramIsRunning;
static BOOLEAN gfGameInitialized = FALSE;


#if 0 // XXX TODO
INT32 FAR PASCAL WindowProcedure(HWND hWindow, UINT16 Message, WPARAM wParam, LPARAM lParam)
{
	static BOOLEAN fRestore = FALSE;

	switch(Message)
  {
    case WM_ACTIVATEAPP:
      switch(wParam)
      {
        case TRUE: // We are restarting DirectDraw
					if (fRestore)
          {
#ifdef JA2
	          RestoreVideoManager();

						// unpause the JA2 Global clock
            if ( !gfPauseDueToPlayerGamePause )
            {
						  PauseTime( FALSE );
            }
#endif
            gfApplicationActive = TRUE;
          }
          break;
        case FALSE: // We are suspending direct draw
#ifdef JA2
						// pause the JA2 Global clock
						PauseTime( TRUE );
						SuspendVideoManager();
#endif

          gfApplicationActive = FALSE;
          fRestore = TRUE;
          break;
      }
      break;

    case WM_CREATE:
			break;

    case WM_DESTROY:
			ShutdownStandardGamingPlatform();
      ShowCursor(TRUE);
      PostQuitMessage(0);
      break;

		case WM_SETFOCUS:
      RestoreCursorClipRect( );
			break;

		case WM_KILLFOCUS:
			// Set a flag to restore surfaces once a WM_ACTIVEATEAPP is received
			fRestore = TRUE;
			break;

    default
    : return DefWindowProc(hWindow, Message, wParam, lParam);
  }
  return 0L;
}
#endif


static void SGPExit(void);


static void InitializeStandardGamingPlatform(void)
{
	// now required by all (even JA2) in order to call ShutdownSGP
	atexit(SGPExit);

	SDL_Init(SDL_INIT_VIDEO);
	SDL_EnableUNICODE(SDL_ENABLE);

#ifdef SGP_DEBUG
	// Initialize the Debug Manager - success doesn't matter
	InitializeDebugManager();
#endif

  // this one needs to go ahead of all others (except Debug), for MemDebugCounter to work right...
	FastDebugMsg("Initializing Memory Manager");
	// Initialize the Memory Manager
	InitializeMemoryManager();

	FastDebugMsg("Initializing File Manager");
	InitializeFileManager();

	FastDebugMsg("Initializing Video Manager");
	InitializeVideoManager();

	FastDebugMsg("Initializing Video Object Manager");
	InitializeVideoObjectManager();

	FastDebugMsg("Initializing Video Surface Manager");
	InitializeVideoSurfaceManager();

  InitGameResources();

#ifdef JA2
	InitJA2SplashScreen();
#endif

	// Initialize Font Manager
	FastDebugMsg("Initializing the Font Manager");
	// Init the manager and copy the TransTable stuff into it.
	InitializeFontManager();

	FastDebugMsg("Initializing Sound Manager");
#ifndef UTIL
	InitializeSoundManager();
#endif

	FastDebugMsg("Initializing Random");
  // Initialize random number generator
  InitializeRandom(); // no Shutdown

	FastDebugMsg("Initializing Game Manager");
	// Initialize the Game
	InitializeGame();

	gfGameInitialized = TRUE;
}


static void ShutdownStandardGamingPlatform(void)
{
	// Shut down the different components of the SGP

	// TEST
	SoundServiceStreams();

	if (gfGameInitialized)
	{
		ShutdownGame();
	}

	ShutdownButtonSystem();
	MSYS_Shutdown();

#ifndef UTIL
  ShutdownSoundManager();
#endif

#ifdef SGP_VIDEO_DEBUGGING
	PerformVideoInfoDumpIntoFile( "SGPVideoShutdownDump.txt", FALSE );
#endif

	ShutdownVideoSurfaceManager();
  ShutdownVideoObjectManager();
  ShutdownVideoManager();

#ifdef EXTREME_MEMORY_DEBUGGING
	DumpMemoryInfoIntoFile( "ExtremeMemoryDump.txt", FALSE );
#endif

  ShutdownMemoryManager();  // must go last, for MemDebugCounter to work right...
}


static void MainLoop()
{
	gfApplicationActive = TRUE;
	gfProgramIsRunning  = TRUE;

  while (gfProgramIsRunning)
  {
		SDL_Event event;
		if (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
				case SDL_ACTIVEEVENT:
					if (event.active.state & SDL_APPACTIVE)
					{
						gfApplicationActive = (event.active.gain != 0);
						break;
					}
					break;

				case SDL_KEYDOWN: KeyDown(&event.key.keysym); break;
				case SDL_KEYUP:   KeyUp(  &event.key.keysym); break;

				case SDL_MOUSEBUTTONDOWN: MouseButtonDown(&event.button); break;
				case SDL_MOUSEBUTTONUP:   MouseButtonUp(&event.button);   break;

				case SDL_MOUSEMOTION:
					gusMouseXPos = event.motion.x;
					gusMouseYPos = event.motion.y;
					break;

				case SDL_QUIT:
					gfProgramIsRunning = FALSE;
					break;
			}
		}
		else
		{
			if (gfApplicationActive)
			{
				GameLoop();
				SDL_Delay(1); // XXX HACK0001
			}
			else
			{
				SDL_WaitEvent(NULL);
			}
		}
  }
}


static int Failure(char const* const msg)
{
	fprintf(stderr, "%s\n", msg);
#if defined _WIN32
	MessageBox(0, msg, APPLICATION_NAME, MB_OK | MB_ICONERROR | MB_TASKMODAL);
#endif
	return EXIT_FAILURE;
}


static BOOLEAN ParseParameters(int argc, char* const argv[]);


int main(int argc, char* argv[])
try
{
#if defined BROKEN_SWPRINTF
	if (setlocale(LC_CTYPE, "UTF-8") == NULL)
	{
		fprintf(stderr, "WARNING: Failed to set LC_CTYPE to UTF-8. Some strings might get garbled.\n");
	}
#endif

  setGameVersion(GV_ENGLISH);

	if (!ParseParameters(argc, argv)) return EXIT_FAILURE;

	InitializeStandardGamingPlatform();

#if defined JA2 && defined ENGLISH
	SetIntroType(INTRO_SPLASH);
#endif

	FastDebugMsg("Running Game");

	/* At this point the SGP is set up, which means all I/O, Memory, tools, etc.
	 * are available. All we need to do is attend to the gaming mechanics
	 * themselves */
	MainLoop();

	FastDebugMsg("Exiting Game");

	// SGPExit() will be called next through the atexit() mechanism
	return EXIT_SUCCESS;
}
catch (const std::bad_alloc&)
{
	return Failure("ERROR: out of memory");
}
catch (const std::exception& e)
{
	char msg[2048];
	snprintf(msg, lengthof(msg), "ERROR: caught unhandled exception:\n%s", e.what());
	return Failure(msg);
}
catch (...)
{
	return Failure("ERROR: caught unhandled unknown exception");
}


static void SGPExit(void)
{
	static BOOLEAN fAlreadyExiting = FALSE;

	// helps prevent heap crashes when multiple assertions occur and call us
	if ( fAlreadyExiting )
	{
		return;
	}

	fAlreadyExiting = TRUE;
	gfProgramIsRunning = FALSE;

	ShutdownStandardGamingPlatform();
}


/** Set game resources version. */
static BOOLEAN setResourceVersion(const char *version)
{
  if(strcasecmp(version, "ENGLISH") == 0)
  {
    setGameVersion(GV_ENGLISH);
  }
  else if(strcasecmp(version, "DUTCH") == 0)
  {
    setGameVersion(GV_DUTCH);
  }
  else if(strcasecmp(version, "FRENCH") == 0)
  {
    setGameVersion(GV_FRENCH);
  }
  else if(strcasecmp(version, "GERMAN") == 0)
  {
    setGameVersion(GV_GERMAN);
  }
  else if(strcasecmp(version, "ITALIAN") == 0)
  {
    setGameVersion(GV_ITALIAN);
  }
  else if(strcasecmp(version, "POLISH") == 0)
  {
    setGameVersion(GV_POLISH);
  }
  else if(strcasecmp(version, "RUSSIAN") == 0)
  {
    setGameVersion(GV_RUSSIAN);
  }
  else if(strcasecmp(version, "RUSSIAN_GOLD") == 0)
  {
    setGameVersion(GV_RUSSIAN_GOLD);
  }
  else
  {
    LOG_ERROR("Unknown version of the game: %s\n", version);
    return false;
  }
  LOG_INFO("Game version: %s\n", version);
  return true;
}

static BOOLEAN ParseParameters(int argc, char* const argv[])
{
	const char* const name = *argv;
	if (name == NULL) return TRUE; // argv does not even contain the program name

	BOOLEAN success = TRUE;
  for(int i = 1; i < argc; i++)
  {
    bool haveNextParameter = (i + 1) < argc;

		if (strcmp(argv[i], "-fullscreen") == 0)
		{
			VideoSetFullScreen(TRUE);
		}
		else if (strcmp(argv[i], "-nosound") == 0)
		{
			SoundEnableSound(FALSE);
		}
		else if (strcmp(argv[i], "-window") == 0)
		{
			VideoSetFullScreen(FALSE);
		}
		else if (strcmp(arg, "-width") == 0)
		{
      g_ui.setScreenWidth(atoi(*++argv));
		}
		else if (strcmp(arg, "-height") == 0)
		{
      g_ui.setScreenHeight(atoi(*++argv));
		}
#if defined JA2BETAVERSION
		else if (strcmp(argv[i], "-quicksave") == 0)
		{
			/* This allows the QuickSave Slots to be autoincremented, i.e. everytime
			 * the user saves, there will be a new quick save file */
			gfUseConsecutiveQuickSaveSlots = TRUE;
		}
		else if (strcmp(argv[i], "-domaps") == 0)
		{
			g_game_mode = GAME_MODE_MAP_UTILITY;
		}
#	if defined JA2EDITOR
		else if (strcmp(argv[i], "-editor") == 0)
		{
			g_game_mode = GAME_MODE_EDITOR;
		}
		else if (strcmp(argv[i], "-editorauto") == 0)
		{
			g_game_mode = GAME_MODE_EDITOR_AUTO;
		}
#	endif
#endif
    else if (strcmp(argv[i], "-resversion") == 0)
    {
      if(haveNextParameter)
      {
        success = setResourceVersion(argv[++i]);
      }
      else
      {
        LOG_ERROR("Missing value for command-line key '-resversion'\n");
        success = FALSE;
      }
    }
		else
		{
			if (strcmp(argv[i], "-help") != 0)
			{
				fprintf(stderr, "Unknown switch \"%s\"\n", argv[i]);
			}
			success = FALSE;
		}
	}

	if (!success)
	{
		fprintf(stderr,
			"Usage: %s [options]\n"
#if defined JA2BETAVERSION && defined JA2EDITOR
			"  -editor      Start the map editor\n"
#endif
			"  -fullscreen  Start the game in fullscreen mode\n"
			"  -help        Display this information\n"
			"  -nosound     Turn the sound and music off\n"
			"  -window      Start the game in a window\n"
			"  -width XXX   Set window width to XXX pixels\n"
			"  -height XXX  Set window height to XXX pixels\n",
			"  -resversion  Version of the game resources (data files)\n"
			"                 Possible values: DUTCH, ENGLISH, FRENCH, GERMAN, ITALIAN, POLISH, RUSSIAN, RUSSIAN_GOLD\n"
			"                 Default value is ENGLISH\n"
			"                 RUSSIAN is for BUKA Agonia Vlasty release\n"
			"                 RUSSIAN_GOLD is for Gold release\n"
            ,
			name
		);
	}
	return success;
}
