#include "Debug.h"
#include "Fade_Screen.h"
#include "FileMan.h"
#include "ImpTGA.h"
#include "Input.h"
#include "Local.h"
#include "MemMan.h"
#include "RenderWorld.h"
#include "Render_Dirty.h"
#include "SGP.h"
#include "Timer_Control.h"
#include "Types.h"
#include "VObject_Blitters.h"
#include "Video.h"
#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>


#define MAX_DIRTY_REGIONS     128

#define VIDEO_OFF             0x00
#define VIDEO_ON              0x01
#define VIDEO_SHUTTING_DOWN   0x02
#define VIDEO_SUSPENDED       0x04


typedef struct
{
  BOOLEAN                 fRestore;
  UINT16                  usMouseXPos, usMouseYPos;
  UINT16                  usLeft, usTop, usRight, usBottom;
} MouseCursorBackground;


static UINT16                 gusScreenWidth;
static UINT16                 gusScreenHeight;

#define			MAX_NUM_FRAMES			25

static BOOLEAN gfVideoCapture = FALSE;
static UINT32  guiFramePeriod = 1000 / 15;
static UINT32  guiLastFrame;
static UINT16* gpFrameData[MAX_NUM_FRAMES];
INT32													giNumFrames = 0;


//
// Globals for mouse cursor
//

static UINT16                 gusMouseCursorWidth;
static UINT16                 gusMouseCursorHeight;
static INT16                  gsMouseCursorXOffset;
static INT16                  gsMouseCursorYOffset;

static MouseCursorBackground  gMouseCursorBackground;

//
// Make sure we record the value of the hWindow (main window frame for the application)
//

HWND                          ghWindow;

//
// Refresh thread based variables
//

static UINT32 guiFrameBufferState;  // BUFFER_READY, BUFFER_DIRTY
static UINT32 guiMouseBufferState;  // BUFFER_READY, BUFFER_DIRTY, BUFFER_DISABLED
static UINT32 guiVideoManagerState; // VIDEO_ON, VIDEO_OFF, VIDEO_SUSPENDED, VIDEO_SHUTTING_DOWN

//
// Dirty rectangle management variables
//

static SGPRect gListOfDirtyRegions[MAX_DIRTY_REGIONS];
static UINT32  guiDirtyRegionCount;
static BOOLEAN gfForceFullScreenRefresh;


static SGPRect gDirtyRegionsEx[MAX_DIRTY_REGIONS];
static UINT32  guiDirtyRegionExCount;

//
// Screen output stuff
//

static BOOLEAN gfPrintFrameBuffer;
static UINT32  guiPrintFrameBufferIndex;

///////////////////////////////////////////////////////////////////////////////////////////////////
//
// External Variables
//
///////////////////////////////////////////////////////////////////////////////////////////////////

extern UINT16 gusRedMask;
extern UINT16 gusGreenMask;
extern UINT16 gusBlueMask;
extern INT16  gusRedShift;
extern INT16  gusBlueShift;
extern INT16  gusGreenShift;


static SDL_Surface* MouseCursor;
static SDL_Surface* FrameBuffer;
static SDL_Surface* ScreenBuffer;


static BOOLEAN GetRGBDistribution(void);


BOOLEAN InitializeVideoManager(void)
{
	RegisterDebugTopic(TOPIC_VIDEO, "Video");
	DebugMsg(TOPIC_VIDEO, DBG_LEVEL_0, "Initializing the video manager");

	ScreenBuffer = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 16, SDL_SWSURFACE | SDL_HWPALETTE);
	if (ScreenBuffer == NULL)
	{
		DebugMsg(TOPIC_VIDEO, DBG_LEVEL_0, "Failed to set up video mode");
		return FALSE;
	}

	Uint32 Rmask = ScreenBuffer->format->Rmask;
	Uint32 Gmask = ScreenBuffer->format->Gmask;
	Uint32 Bmask = ScreenBuffer->format->Bmask;
	Uint32 Amask = ScreenBuffer->format->Amask;

	FrameBuffer = SDL_CreateRGBSurface(
		SDL_SWSURFACE, SCREEN_WIDTH, SCREEN_HEIGHT, 16,
		Rmask, Gmask, Bmask, Amask
	);

	MouseCursor = SDL_CreateRGBSurface(
		SDL_SWSURFACE, MAX_CURSOR_WIDTH, MAX_CURSOR_HEIGHT, 16,
		Rmask, Gmask, Bmask, Amask
	);
	SDL_SetColorKey(MouseCursor, SDL_SRCCOLORKEY, 0);

	memset(gpFrameData, 0, sizeof(gpFrameData));

	SDL_ShowCursor(SDL_DISABLE);

	gusScreenWidth = SCREEN_WIDTH;
	gusScreenHeight = SCREEN_HEIGHT;

	// Blank out the frame buffer
	UINT32 uiPitch;
	PTR pTmpPointer = LockFrameBuffer(&uiPitch);
	memset(pTmpPointer, 0, 480 * uiPitch);
	UnlockFrameBuffer();

	gMouseCursorBackground.fRestore = FALSE;

	// Initialize state variables
	guiFrameBufferState          = BUFFER_DIRTY;
	guiMouseBufferState          = BUFFER_DISABLED;
	guiVideoManagerState         = VIDEO_ON;
	guiDirtyRegionCount          = 0;
	gfForceFullScreenRefresh     = TRUE;
	gfPrintFrameBuffer           = FALSE;
	guiPrintFrameBufferIndex     = 0;

	// This function must be called to setup RGB information
	GetRGBDistribution();

	return TRUE;
}


void ShutdownVideoManager(void)
{
	DebugMsg(TOPIC_VIDEO, DBG_LEVEL_0, "Shutting down the video manager");

  //
  // Toggle the state of the video manager to indicate to the refresh thread that it needs to shut itself
  // down
  //

	SDL_FreeSurface(MouseCursor);
	SDL_FreeSurface(FrameBuffer);
	SDL_QuitSubSystem(SDL_INIT_VIDEO);

  guiVideoManagerState = VIDEO_OFF;

  // ATE: Release mouse cursor!
  FreeMouseCursor( );

  UnRegisterDebugTopic(TOPIC_VIDEO, "Video");
}


void SuspendVideoManager(void)
{
  guiVideoManagerState = VIDEO_SUSPENDED;

}


BOOLEAN RestoreVideoManager(void)
{
#if 1 // XXX TODO
	UNIMPLEMENTED();
#else
  //
  // Make sure the video manager is indeed suspended before moving on
  //

  if (guiVideoManagerState == VIDEO_SUSPENDED)
  {
    //
    // Set the video state to VIDEO_ON
    //

    guiFrameBufferState = BUFFER_DIRTY;
    guiMouseBufferState = BUFFER_DIRTY;
    gfForceFullScreenRefresh = TRUE;
    guiVideoManagerState = VIDEO_ON;
    return TRUE;
	}
  else
  {
    return FALSE;
  }
#endif
}


void GetCurrentVideoSettings( UINT16 *usWidth, UINT16 *usHeight, UINT8 *ubBitDepth )
{
	*usWidth = (UINT16) gusScreenWidth;
	*usHeight = (UINT16) gusScreenHeight;
	*ubBitDepth = 16;
}


void InvalidateRegion(INT32 iLeft, INT32 iTop, INT32 iRight, INT32 iBottom)
{
  if (gfForceFullScreenRefresh == TRUE)
  {
    //
    // There's no point in going on since we are forcing a full screen refresh
    //

    return;
  }

  if (guiDirtyRegionCount < MAX_DIRTY_REGIONS)
  {
    //
    // Well we haven't broken the MAX_DIRTY_REGIONS limit yet, so we register the new region
    //

		// DO SOME PREMIMARY CHECKS FOR VALID RECTS
		if ( iLeft < 0 )
				iLeft = 0;

		if ( iTop < 0 )
				iTop = 0;

		if ( iRight > SCREEN_WIDTH )
				iRight = SCREEN_WIDTH;

		if ( iBottom > SCREEN_HEIGHT )
				iBottom = SCREEN_HEIGHT;

		if (  ( iRight - iLeft ) <= 0 )
				return;

		if (  ( iBottom - iTop ) <= 0 )
				return;

    gListOfDirtyRegions[guiDirtyRegionCount].iLeft   = iLeft;
    gListOfDirtyRegions[guiDirtyRegionCount].iTop    = iTop;
    gListOfDirtyRegions[guiDirtyRegionCount].iRight  = iRight;
    gListOfDirtyRegions[guiDirtyRegionCount].iBottom = iBottom;

//		gDirtyRegionFlags[ guiDirtyRegionCount ] = TRUE;

    guiDirtyRegionCount++;

  }
  else
  {
    //
    // The MAX_DIRTY_REGIONS limit has been exceeded. Therefore we arbitrarely invalidate the entire
    // screen and force a full screen refresh
    //
    guiDirtyRegionExCount = 0;
    guiDirtyRegionCount = 0;
    gfForceFullScreenRefresh = TRUE;
  }
}


static void AddRegionEx(INT32 iLeft, INT32 iTop, INT32 iRight, INT32 iBottom);


void InvalidateRegionEx(INT32 iLeft, INT32 iTop, INT32 iRight, INT32 iBottom)
{
	INT32 iOldBottom;

	iOldBottom = iBottom;

	// Check if we are spanning the rectangle - if so slit it up!
	if ( iTop <= gsVIEWPORT_WINDOW_END_Y && iBottom > gsVIEWPORT_WINDOW_END_Y )
	{
			// Add new top region
			iBottom				= gsVIEWPORT_WINDOW_END_Y;
			AddRegionEx(iLeft, iTop, iRight, iBottom);

			// Add new bottom region
			iTop   = gsVIEWPORT_WINDOW_END_Y;
			iBottom	= iOldBottom;
			AddRegionEx(iLeft, iTop, iRight, iBottom);

	}
	else
	{
		 AddRegionEx(iLeft, iTop, iRight, iBottom);
	}
}


static void AddRegionEx(INT32 iLeft, INT32 iTop, INT32 iRight, INT32 iBottom)
{

  if (guiDirtyRegionExCount < MAX_DIRTY_REGIONS)
  {

		// DO SOME PREMIMARY CHECKS FOR VALID RECTS
		if ( iLeft < 0 )
				iLeft = 0;

		if ( iTop < 0 )
				iTop = 0;

		if ( iRight > SCREEN_WIDTH )
				iRight = SCREEN_WIDTH;

		if ( iBottom > SCREEN_HEIGHT )
				iBottom = SCREEN_HEIGHT;

		if (  ( iRight - iLeft ) <= 0 )
				return;

		if (  ( iBottom - iTop ) <= 0 )
				return;



    gDirtyRegionsEx[ guiDirtyRegionExCount ].iLeft   = iLeft;
    gDirtyRegionsEx[ guiDirtyRegionExCount ].iTop    = iTop;
    gDirtyRegionsEx[ guiDirtyRegionExCount ].iRight  = iRight;
    gDirtyRegionsEx[ guiDirtyRegionExCount ].iBottom = iBottom;
    guiDirtyRegionExCount++;
  }
	else
	{
    guiDirtyRegionExCount = 0;
    guiDirtyRegionCount = 0;
    gfForceFullScreenRefresh = TRUE;

	}
}


void InvalidateScreen(void)
{
  //
  // W A R N I N G ---- W A R N I N G ---- W A R N I N G ---- W A R N I N G ---- W A R N I N G ----
  //
  // This function is intended to be called by a thread which has already locked the
  // FRAME_BUFFER_MUTEX mutual exclusion section. Anything else will cause the application to
  // yack
  //

  guiDirtyRegionCount = 0;
  guiDirtyRegionExCount = 0;
  gfForceFullScreenRefresh = TRUE;
  guiFrameBufferState = BUFFER_DIRTY;
}


//#define SCROLL_TEST

static void ScrollJA2Background(UINT32 uiDirection, INT16 sScrollXIncrement, INT16 sScrollYIncrement, BOOLEAN fRenderStrip)
{
	UINT16 usWidth, usHeight;
	UINT8	 ubBitDepth;
	static RECT		 StripRegions[ 2 ];
	UINT16				 usNumStrips = 0;
	INT32					 cnt;
	INT16					 sShiftX, sShiftY;
	INT32					 uiCountY;

	// XXX TODO0002 does not work correctly, causes graphic artifacts
	SDL_Surface* Frame  = FrameBuffer;
	SDL_Surface* Source = ScreenBuffer; // Primary
	SDL_Surface* Dest   = ScreenBuffer; // Back
	SDL_Rect SrcRect;
	SDL_Rect DstRect;

 	GetCurrentVideoSettings( &usWidth, &usHeight, &ubBitDepth );
	usHeight=(gsVIEWPORT_WINDOW_END_Y - gsVIEWPORT_WINDOW_START_Y );


	StripRegions[ 0 ].left   = gsVIEWPORT_START_X;
	StripRegions[ 0 ].right  = gsVIEWPORT_END_X;
	StripRegions[ 0 ].top    = gsVIEWPORT_WINDOW_START_Y;
	StripRegions[ 0 ].bottom = gsVIEWPORT_WINDOW_END_Y;
	StripRegions[ 1 ].left   = gsVIEWPORT_START_X;
	StripRegions[ 1 ].right  = gsVIEWPORT_END_X;
	StripRegions[ 1 ].top    = gsVIEWPORT_WINDOW_START_Y;
	StripRegions[ 1 ].bottom = gsVIEWPORT_WINDOW_END_Y;

	switch (uiDirection)
	{
		case SCROLL_LEFT:
			SrcRect.x = 0;
			SrcRect.y = gsVIEWPORT_WINDOW_START_Y;
			SrcRect.w = usWidth - sScrollXIncrement;
			SrcRect.h = usHeight;
			DstRect.x = sScrollXIncrement;
			DstRect.y = gsVIEWPORT_WINDOW_START_Y;
			SDL_BlitSurface(Source, &SrcRect, Dest, &DstRect);

			// memset z-buffer
			for(uiCountY = gsVIEWPORT_WINDOW_START_Y; uiCountY < gsVIEWPORT_WINDOW_END_Y; uiCountY++)
			{
				memset((UINT8 *)gpZBuffer+(uiCountY*1280), 0,
								sScrollXIncrement*2);

			}

			StripRegions[ 0 ].right =(INT16)(gsVIEWPORT_START_X+sScrollXIncrement);

			usNumStrips = 1;
			break;

		case SCROLL_RIGHT:
			SrcRect.x = sScrollXIncrement;
			SrcRect.y = gsVIEWPORT_WINDOW_START_Y;
			SrcRect.w = usWidth - sScrollXIncrement;
			SrcRect.h = usHeight;
			DstRect.x = 0;
			DstRect.y = gsVIEWPORT_WINDOW_START_Y;
			SDL_BlitSurface(Source, &SrcRect, Dest, &DstRect);

			// memset z-buffer
			for(uiCountY= gsVIEWPORT_WINDOW_START_Y; uiCountY < gsVIEWPORT_WINDOW_END_Y; uiCountY++)
			{
				memset((UINT8 *)gpZBuffer+(uiCountY*1280) + ( ( gsVIEWPORT_END_X - sScrollXIncrement ) * 2 ), 0,
								sScrollXIncrement*2);
			}


			//for(uiCountY=0; uiCountY < usHeight; uiCountY++)
			//{
			//	memcpy(pDestBuf+(uiCountY*uiDestPitchBYTES),
			//					pSrcBuf+(uiCountY*uiDestPitchBYTES)+sScrollXIncrement*uiBPP,
			//					uiDestPitchBYTES-sScrollXIncrement*uiBPP);
			//}

			StripRegions[ 0 ].left =(INT16)(gsVIEWPORT_END_X-sScrollXIncrement);
			usNumStrips = 1;
			break;

		case SCROLL_UP:
			SrcRect.x = 0;
			SrcRect.y = gsVIEWPORT_WINDOW_START_Y;
			SrcRect.w = usWidth;
			SrcRect.h = usHeight - sScrollYIncrement;
			DstRect.x = 0;
			DstRect.y = gsVIEWPORT_WINDOW_START_Y + sScrollYIncrement;
			SDL_BlitSurface(Source, &SrcRect, Dest, &DstRect);

			for(uiCountY=sScrollYIncrement-1+gsVIEWPORT_WINDOW_START_Y; uiCountY >= gsVIEWPORT_WINDOW_START_Y; uiCountY--)
			{
				memset((UINT8 *)gpZBuffer+(uiCountY*1280), 0,
								1280);
			}

			//for(uiCountY=usHeight-1; uiCountY >= sScrollYIncrement; uiCountY--)
			//{
			//	memcpy(pDestBuf+(uiCountY*uiDestPitchBYTES),
			//					pSrcBuf+((uiCountY-sScrollYIncrement)*uiDestPitchBYTES),
			//					uiDestPitchBYTES);
			//}
			StripRegions[ 0 ].bottom =(INT16)(gsVIEWPORT_WINDOW_START_Y+sScrollYIncrement);
			usNumStrips = 1;
			break;

		case SCROLL_DOWN:
			SrcRect.x = 0;
			SrcRect.y = gsVIEWPORT_WINDOW_START_Y + sScrollYIncrement;
			SrcRect.w = usWidth;
			SrcRect.h = usHeight - sScrollYIncrement;
			DstRect.x = 0;
			DstRect.y = gsVIEWPORT_WINDOW_START_Y;
			SDL_BlitSurface(Source, &SrcRect, Dest, &DstRect);

			// Zero out z
			for(uiCountY=(gsVIEWPORT_WINDOW_END_Y - sScrollYIncrement ); uiCountY < gsVIEWPORT_WINDOW_END_Y; uiCountY++)
			{
				memset((UINT8 *)gpZBuffer+(uiCountY*1280), 0,
								1280);
			}

			//for(uiCountY=0; uiCountY < (usHeight-sScrollYIncrement); uiCountY++)
			//{
			//	memcpy(pDestBuf+(uiCountY*uiDestPitchBYTES),
			//					pSrcBuf+((uiCountY+sScrollYIncrement)*uiDestPitchBYTES),
			//					uiDestPitchBYTES);
			//}

			StripRegions[ 0 ].top = (INT16)(gsVIEWPORT_WINDOW_END_Y-sScrollYIncrement);
			usNumStrips = 1;
			break;

		case SCROLL_UPLEFT:
			SrcRect.x = 0;
			SrcRect.y = gsVIEWPORT_WINDOW_START_Y;
			SrcRect.w = usWidth - sScrollXIncrement;
			SrcRect.h = usHeight - sScrollYIncrement;
			DstRect.x = sScrollXIncrement;
			DstRect.y = gsVIEWPORT_WINDOW_START_Y + sScrollYIncrement;
			SDL_BlitSurface(Source, &SrcRect, Dest, &DstRect);

			// memset z-buffer
			for(uiCountY=gsVIEWPORT_WINDOW_START_Y; uiCountY < gsVIEWPORT_WINDOW_END_Y; uiCountY++)
			{
				memset((UINT8 *)gpZBuffer+(uiCountY*1280), 0,
								sScrollXIncrement*2);

			}
			for(uiCountY=gsVIEWPORT_WINDOW_START_Y + sScrollYIncrement-1; uiCountY >= gsVIEWPORT_WINDOW_START_Y; uiCountY--)
			{
				memset((UINT8 *)gpZBuffer+(uiCountY*1280), 0,
								1280);
			}


			StripRegions[ 0 ].right =  (INT16)(gsVIEWPORT_START_X+sScrollXIncrement);
			StripRegions[ 1 ].bottom = (INT16)(gsVIEWPORT_WINDOW_START_Y+sScrollYIncrement);
			StripRegions[ 1 ].left	 = (INT16)(gsVIEWPORT_START_X+sScrollXIncrement);
			usNumStrips = 2;
			break;

		case SCROLL_UPRIGHT:
			SrcRect.x = sScrollXIncrement;
			SrcRect.y = gsVIEWPORT_WINDOW_START_Y;
			SrcRect.w = usWidth - sScrollXIncrement;
			SrcRect.h = usHeight - sScrollYIncrement;
			DstRect.x = 0;
			DstRect.y = gsVIEWPORT_WINDOW_START_Y + sScrollYIncrement;
			SDL_BlitSurface(Source, &SrcRect, Dest, &DstRect);

			// memset z-buffer
			for(uiCountY=gsVIEWPORT_WINDOW_START_Y; uiCountY < gsVIEWPORT_WINDOW_END_Y; uiCountY++)
			{
				memset((UINT8 *)gpZBuffer+(uiCountY*1280) + ( ( gsVIEWPORT_END_X - sScrollXIncrement ) * 2 ), 0,
								sScrollXIncrement*2);
			}
			for(uiCountY=gsVIEWPORT_WINDOW_START_Y + sScrollYIncrement-1; uiCountY >= gsVIEWPORT_WINDOW_START_Y; uiCountY--)
			{
				memset((UINT8 *)gpZBuffer+(uiCountY*1280), 0,
								1280);
			}


			StripRegions[ 0 ].left =   (INT16)(gsVIEWPORT_END_X-sScrollXIncrement);
			StripRegions[ 1 ].bottom = (INT16)(gsVIEWPORT_WINDOW_START_Y+sScrollYIncrement);
			StripRegions[ 1 ].right  = (INT16)(gsVIEWPORT_END_X-sScrollXIncrement);
			usNumStrips = 2;
			break;

		case SCROLL_DOWNLEFT:
			SrcRect.x = 0;
			SrcRect.y = gsVIEWPORT_WINDOW_START_Y + sScrollYIncrement;
			SrcRect.w = usWidth - sScrollXIncrement;
			SrcRect.h = usHeight - sScrollYIncrement;
			DstRect.x = sScrollXIncrement;
			DstRect.y = gsVIEWPORT_WINDOW_START_Y;
			SDL_BlitSurface(Source, &SrcRect, Dest, &DstRect);

			// memset z-buffer
			for(uiCountY=gsVIEWPORT_WINDOW_START_Y; uiCountY < gsVIEWPORT_WINDOW_END_Y; uiCountY++)
			{
				memset((UINT8 *)gpZBuffer+(uiCountY*1280), 0,
								sScrollXIncrement*2);

			}
			for(uiCountY=(gsVIEWPORT_WINDOW_END_Y - sScrollYIncrement); uiCountY < gsVIEWPORT_WINDOW_END_Y; uiCountY++)
			{
				memset((UINT8 *)gpZBuffer+(uiCountY*1280), 0,
								1280);
			}


			StripRegions[ 0 ].right =(INT16)(gsVIEWPORT_START_X+sScrollXIncrement);


			StripRegions[ 1 ].top		= (INT16)(gsVIEWPORT_WINDOW_END_Y-sScrollYIncrement);
			StripRegions[ 1 ].left  = (INT16)(gsVIEWPORT_START_X+sScrollXIncrement);
			usNumStrips = 2;
			break;

		case SCROLL_DOWNRIGHT:
			SrcRect.x = sScrollXIncrement;
			SrcRect.y = gsVIEWPORT_WINDOW_START_Y + sScrollYIncrement;
			SrcRect.w = usWidth - sScrollXIncrement;
			SrcRect.h = usHeight - sScrollYIncrement;
			DstRect.x = 0;
			DstRect.y = gsVIEWPORT_WINDOW_START_Y;
			SDL_BlitSurface(Source, &SrcRect, Dest, &DstRect);

			// memset z-buffer
			for(uiCountY=gsVIEWPORT_WINDOW_START_Y; uiCountY < gsVIEWPORT_WINDOW_END_Y; uiCountY++)
			{
				memset((UINT8 *)gpZBuffer+(uiCountY*1280) + ( ( gsVIEWPORT_END_X - sScrollXIncrement ) * 2 ), 0,
								sScrollXIncrement*2);
			}
			for(uiCountY=(gsVIEWPORT_WINDOW_END_Y - sScrollYIncrement); uiCountY < gsVIEWPORT_WINDOW_END_Y; uiCountY++)
			{
				memset((UINT8 *)gpZBuffer+(uiCountY*1280), 0,
								1280);
			}


			StripRegions[ 0 ].left =(INT16)(gsVIEWPORT_END_X-sScrollXIncrement);
			StripRegions[ 1 ].top = (INT16)(gsVIEWPORT_WINDOW_END_Y-sScrollYIncrement);
			StripRegions[ 1 ].right = (INT16)(gsVIEWPORT_END_X-sScrollXIncrement);
			usNumStrips = 2;
			break;

	}

	if ( fRenderStrip )
	{
#ifdef SCROLL_TEST
		SDL_FillRect(Dest, NULL, 0);
#endif

		for ( cnt = 0; cnt < usNumStrips; cnt++ )
		{
			RenderStaticWorldRect( (INT16)StripRegions[ cnt ].left, (INT16)StripRegions[ cnt ].top, (INT16)StripRegions[ cnt ].right, (INT16)StripRegions[ cnt ].bottom , TRUE );

			SrcRect.x = StripRegions[cnt].left;
			SrcRect.y = StripRegions[cnt].top;
			SrcRect.w = StripRegions[cnt].right  - StripRegions[cnt].left;
			SrcRect.h = StripRegions[cnt].bottom - StripRegions[cnt].top;
			DstRect.x = StripRegions[cnt].left;
			DstRect.y = StripRegions[cnt].top;
			SDL_BlitSurface(Frame, &SrcRect, Dest, &DstRect);
		}

		sShiftX = 0;
		sShiftY = 0;

		switch (uiDirection)
		{
			case SCROLL_LEFT:

				sShiftX = sScrollXIncrement;
				sShiftY = 0;
				break;

			case SCROLL_RIGHT:

				sShiftX = -sScrollXIncrement;
				sShiftY = 0;
				break;

			case SCROLL_UP:

				sShiftX = 0;
				sShiftY = sScrollYIncrement;
				break;

			case SCROLL_DOWN:

				sShiftX = 0;
				sShiftY = -sScrollYIncrement;
				break;

			case SCROLL_UPLEFT:

				sShiftX = sScrollXIncrement;
				sShiftY = sScrollYIncrement;
				break;

			case SCROLL_UPRIGHT:

				sShiftX = -sScrollXIncrement;
				sShiftY = sScrollYIncrement;
				break;

			case SCROLL_DOWNLEFT:

				sShiftX = sScrollXIncrement;
				sShiftY = -sScrollYIncrement;
				break;

			case SCROLL_DOWNRIGHT:

				sShiftX = -sScrollXIncrement;
				sShiftY = -sScrollYIncrement;
				break;


		}

		// RESTORE SHIFTED
		RestoreShiftedVideoOverlays( sShiftX, sShiftY );

		// SAVE NEW
		SaveVideoOverlaysArea( BACKBUFFER );

		// BLIT NEW
		ExecuteVideoOverlaysToAlternateBuffer( BACKBUFFER );
	}


	//InvalidateRegion( sLeftDraw, sTopDraw, sRightDraw, sBottomDraw );

	//UpdateSaveBuffer();
	//SaveBackgroundRects();
}


static void SnapshotSmall(void);


void RefreshScreen(void)
{
	static BOOLEAN fShowMouse = FALSE;

	if (guiVideoManagerState != VIDEO_ON) return;

	UINT16 usScreenWidth  = gusScreenWidth;
	UINT16 usScreenHeight = gusScreenHeight;

	SDL_Rect OldMouseRect;
	BOOLEAN UndrawMouse = gMouseCursorBackground.fRestore;
	if (UndrawMouse)
	{
		OldMouseRect.x = gMouseCursorBackground.usMouseXPos;
		OldMouseRect.y = gMouseCursorBackground.usMouseYPos;
		OldMouseRect.w = gMouseCursorBackground.usRight  - gMouseCursorBackground.usLeft;
		OldMouseRect.h = gMouseCursorBackground.usBottom - gMouseCursorBackground.usTop;
		SDL_BlitSurface(FrameBuffer, &OldMouseRect, ScreenBuffer, &OldMouseRect);
	}

	if (guiFrameBufferState == BUFFER_DIRTY)
	{
		if (gfFadeInitialized && gfFadeInVideo)
		{
			gFadeFunction();
		}
		else
		{
			if (gfForceFullScreenRefresh)
			{
				SDL_BlitSurface(FrameBuffer, NULL, ScreenBuffer, NULL);
			}
			else
			{
				for (UINT32 i = 0; i < guiDirtyRegionCount; i++)
				{
					SDL_Rect SrcRect;
					SrcRect.x = gListOfDirtyRegions[i].iLeft;
					SrcRect.y = gListOfDirtyRegions[i].iTop;
					SrcRect.w = gListOfDirtyRegions[i].iRight  - gListOfDirtyRegions[i].iLeft;
					SrcRect.h = gListOfDirtyRegions[i].iBottom - gListOfDirtyRegions[i].iTop;
					SDL_BlitSurface(FrameBuffer, &SrcRect, ScreenBuffer, &SrcRect);
				}

				for (UINT32 i = 0; i < guiDirtyRegionExCount; i++)
				{
					SDL_Rect SrcRect;
					SrcRect.x = gDirtyRegionsEx[i].iLeft;
					SrcRect.y = gDirtyRegionsEx[i].iTop;
					SrcRect.w = gDirtyRegionsEx[i].iRight  - gDirtyRegionsEx[i].iLeft;
					SrcRect.h = gDirtyRegionsEx[i].iBottom - gDirtyRegionsEx[i].iTop;
					if (gfRenderScroll)
					{
						// Check if we are completely out of bounds
						if (SrcRect.y <= gsVIEWPORT_WINDOW_END_Y && SrcRect.y + SrcRect.h <= gsVIEWPORT_WINDOW_END_Y)
						{
							continue;
						}
					}
					SDL_BlitSurface(FrameBuffer, &SrcRect, ScreenBuffer, &SrcRect);
				}
			}
		}
		if (gfRenderScroll)
		{
			ScrollJA2Background(guiScrollDirection, gsScrollXIncrement, gsScrollYIncrement, TRUE);
		}
		gfIgnoreScrollDueToCenterAdjust = FALSE;
		guiFrameBufferState = BUFFER_READY;
	}

	if (gfVideoCapture)
	{
		UINT32 uiTime = GetTickCount();
		if (uiTime < guiLastFrame || uiTime > guiLastFrame + guiFramePeriod)
		{
			SnapshotSmall();
			guiLastFrame = uiTime;
		}
	}

  if (gfPrintFrameBuffer)
  {
    FILE                  *OutputFile;
    UINT8                  FileName[64];
    INT32                  iIndex;
		STRING512			         ExecDir;
    UINT16                 *p16BPPData;

		GetExecutableDirectory( ExecDir );
		SetFileManCurrentDirectory( ExecDir );

    sprintf(FileName, "SCREEN%03d.TGA", guiPrintFrameBufferIndex++);
    if ((OutputFile = fopen(FileName, "wb")) != NULL)
    {
      fprintf(OutputFile, "%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c", 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0x02, 0xe0, 0x01, 0x10, 0);

      //
      // Copy 16 bit buffer to file
      //

      // 5/6/5.. create buffer...
			if (gusRedMask == 0xF800 && gusGreenMask == 0x07E0 && gusBlueMask == 0x001F)
      {
        p16BPPData = MemAlloc( 640 * 2 );
      }

      for (iIndex = 479; iIndex >= 0; iIndex--)
      {
        // ATE: OK, fix this such that it converts pixel format to 5/5/5
        // if current settings are 5/6/5....
				if (gusRedMask == 0xF800 && gusGreenMask == 0x07E0 && gusBlueMask == 0x001F)
        {
          // Read into a buffer...
					memcpy(p16BPPData, (UINT16*)ScreenBuffer->pixels + iIndex * 640, 640 * 2);

          // Convert....
          ConvertRGBDistribution565To555( p16BPPData, 640 );

          // Write
          fwrite( p16BPPData, 640 * 2, 1, OutputFile);
        }
        else
        {
					fwrite((UINT16*)ScreenBuffer->pixels + iIndex * 640, 640 * 2, 1, OutputFile);
        }
      }

      // 5/6/5.. Delete buffer...
			if (gusRedMask == 0xF800 && gusGreenMask == 0x07E0 && gusBlueMask == 0x001F)
      {
        MemFree( p16BPPData );
      }

      fclose(OutputFile);
    }

    gfPrintFrameBuffer = FALSE;

		strcat( ExecDir, "\\Data" );
		SetFileManCurrentDirectory( ExecDir );
  }

	if (guiMouseBufferState == BUFFER_DIRTY)
	{
		guiMouseBufferState = BUFFER_READY;
	}

  if (!fShowMouse)
  {
    fShowMouse = (guiMouseBufferState == BUFFER_READY);
  }
  else
  {
    fShowMouse = (guiMouseBufferState != BUFFER_DISABLED);
  }

  if (fShowMouse)
  {
  	POINT MousePos;
  	GetCursorPos(&MousePos);

		RECT Region;
    Region.left   = MousePos.x - gsMouseCursorXOffset;
    Region.top    = MousePos.y - gsMouseCursorYOffset;
    Region.right  = Region.left + gusMouseCursorWidth;
    Region.bottom = Region.top + gusMouseCursorHeight;

    if (Region.right > usScreenWidth)
    {
      Region.right = usScreenWidth;
    }

    if (Region.bottom > usScreenHeight)
    {
      Region.bottom = usScreenHeight;
    }

    if (Region.right > Region.left && Region.bottom > Region.top)
    {
      // Make sure the mouse background is marked for restore and coordinates are saved for the
      // future restore

      gMouseCursorBackground.fRestore = TRUE;
      gMouseCursorBackground.usRight  = (UINT16)Region.right  - (UINT16)Region.left;
      gMouseCursorBackground.usBottom = (UINT16)Region.bottom - (UINT16)Region.top;
      if (Region.left < 0)
      {
        gMouseCursorBackground.usLeft = (UINT16)(0 - Region.left);
        gMouseCursorBackground.usMouseXPos = 0;
        Region.left = 0;
      }
      else
      {
        gMouseCursorBackground.usMouseXPos = (UINT16)MousePos.x - gsMouseCursorXOffset;
        gMouseCursorBackground.usLeft = 0;
      }
      if (Region.top < 0)
      {
        gMouseCursorBackground.usMouseYPos = 0;
        gMouseCursorBackground.usTop = (UINT16)(0 - Region.top);
        Region.top = 0;
      }
      else
      {
        gMouseCursorBackground.usMouseYPos = (UINT16)MousePos.y - gsMouseCursorYOffset;
        gMouseCursorBackground.usTop = 0;
      }

			if (Region.right > Region.left && Region.bottom > Region.top)
			{
				SDL_Rect SrcRect;
				SrcRect.x = gMouseCursorBackground.usLeft;
				SrcRect.y = gMouseCursorBackground.usTop;
				SrcRect.w = gMouseCursorBackground.usRight  - gMouseCursorBackground.usLeft;
				SrcRect.h = gMouseCursorBackground.usBottom - gMouseCursorBackground.usTop;
				SDL_Rect DstRect;
				DstRect.x = gMouseCursorBackground.usMouseXPos;
				DstRect.y = gMouseCursorBackground.usMouseYPos;
				SDL_BlitSurface(MouseCursor, &SrcRect, ScreenBuffer, &DstRect);
				SDL_UpdateRects(ScreenBuffer, 1, &DstRect);
			}
			else
			{
				gMouseCursorBackground.fRestore = FALSE;
			}
    }
    else
    {
      gMouseCursorBackground.fRestore = FALSE;
    }
  }
  else
  {
    gMouseCursorBackground.fRestore = FALSE;
  }

	if (UndrawMouse)
	{
		SDL_UpdateRects(ScreenBuffer, 1, &OldMouseRect);
	}

	if (gfForceFullScreenRefresh)
	{
		SDL_UpdateRect(ScreenBuffer, 0, 0, 0, 0);
	}
	else
	{
		for (UINT32 i = 0; i < guiDirtyRegionCount; i++)
		{
			SDL_Rect SrcRect;
			SrcRect.x = gListOfDirtyRegions[i].iLeft;
			SrcRect.y = gListOfDirtyRegions[i].iTop;
			SrcRect.w = gListOfDirtyRegions[i].iRight  - gListOfDirtyRegions[i].iLeft;
			SrcRect.h = gListOfDirtyRegions[i].iBottom - gListOfDirtyRegions[i].iTop;
			SDL_UpdateRects(ScreenBuffer, 1, &SrcRect);
		}

		for (UINT32 i = 0; i < guiDirtyRegionExCount; i++)
		{
			SDL_Rect SrcRect;
			SrcRect.x = gDirtyRegionsEx[i].iLeft;
			SrcRect.y = gDirtyRegionsEx[i].iTop;
			SrcRect.w = gDirtyRegionsEx[i].iRight  - gDirtyRegionsEx[i].iLeft;
			SrcRect.h = gDirtyRegionsEx[i].iBottom - gDirtyRegionsEx[i].iTop;
			if (gfRenderScroll)
			{
				if (SrcRect.y <= gsVIEWPORT_WINDOW_END_Y && SrcRect.y + SrcRect.h <= gsVIEWPORT_WINDOW_END_Y)
				{
					continue;
				}
			}
			SDL_UpdateRects(ScreenBuffer, 1, &SrcRect);
		}
	}

	if (gfRenderScroll)
	{
		gfRenderScroll = FALSE;
	}

	gfForceFullScreenRefresh = FALSE;
	guiDirtyRegionCount = 0;
	guiDirtyRegionExCount = 0;
}


SDL_Surface* GetBackBufferObject(void)
{
	FIXME
	Assert(ScreenBuffer != NULL);
	return ScreenBuffer;
}


SDL_Surface* GetFrameBufferObject(void)
{
	FIXME
	Assert(FrameBuffer != NULL);
	return FrameBuffer;
}


SDL_Surface* GetMouseBufferObject(void)
{
	FIXME
	Assert(MouseCursor != NULL);
	return MouseCursor;
}


PTR LockBackBuffer(UINT32 *uiPitch)
{
	FIXME
	*uiPitch = ScreenBuffer->pitch;
	return ScreenBuffer->pixels;
}


void UnlockBackBuffer(void)
{
	FIXME
}


PTR LockFrameBuffer(UINT32 *uiPitch)
{
	*uiPitch = FrameBuffer->pitch;
	return FrameBuffer->pixels;
}


void UnlockFrameBuffer(void)
{
}


PTR LockMouseBuffer(UINT32 *uiPitch)
{
	*uiPitch = MouseCursor->pitch;
	return MouseCursor->pixels;
}


void UnlockMouseBuffer(void)
{
}


static BOOLEAN GetRGBDistribution(void)
{
	UINT16 usBit;

	gusRedMask   = ScreenBuffer->format->Rmask;
	gusGreenMask = ScreenBuffer->format->Gmask;
	gusBlueMask  = ScreenBuffer->format->Bmask;

	// RGB 5,5,5
	if((gusRedMask==0x7c00) && (gusGreenMask==0x03e0) && (gusBlueMask==0x1f))
		guiTranslucentMask=0x3def;
	// RGB 5,6,5
	else// if((gusRedMask==0xf800) && (gusGreenMask==0x03e0) && (gusBlueMask==0x1f))
		guiTranslucentMask=0x7bef;


  usBit = 0x8000;
  gusRedShift = 8;
	while(!(gusRedMask & usBit))
	{
		usBit >>= 1;
		gusRedShift--;
	}

  usBit = 0x8000;
  gusGreenShift = 8;
	while(!(gusGreenMask & usBit))
	{
		usBit >>= 1;
		gusGreenShift--;
	}

  usBit = 0x8000;
  gusBlueShift = 8;
	while(!(gusBlueMask & usBit))
	{
		usBit >>= 1;
		gusBlueShift--;
	}

  return TRUE;
}


BOOLEAN GetPrimaryRGBDistributionMasks(UINT32 *RedBitMask, UINT32 *GreenBitMask, UINT32 *BlueBitMask)
{
	*RedBitMask   = gusRedMask;
	*GreenBitMask = gusGreenMask;
	*BlueBitMask  = gusBlueMask;

	return TRUE;
}


BOOLEAN EraseMouseCursor( )
{
  PTR          pTmpPointer;
  UINT32       uiPitch;

  //
  // Erase cursor background
  //

  pTmpPointer = LockMouseBuffer(&uiPitch);
  memset(pTmpPointer, 0, MAX_CURSOR_HEIGHT * uiPitch);
  UnlockMouseBuffer();

	// Don't set dirty
	return( TRUE );
}

BOOLEAN SetMouseCursorProperties( INT16 sOffsetX, INT16 sOffsetY, UINT16 usCursorHeight, UINT16 usCursorWidth )
{
  gsMouseCursorXOffset = sOffsetX;
  gsMouseCursorYOffset = sOffsetY;
  gusMouseCursorWidth	 = usCursorWidth;
  gusMouseCursorHeight = usCursorHeight;
	return( TRUE );
}

BOOLEAN BltToMouseCursor(UINT32 uiVideoObjectHandle, UINT16 usVideoObjectSubIndex, UINT16 usXPos, UINT16 usYPos )
{
  BOOLEAN      ReturnValue;

  ReturnValue = BltVideoObjectFromIndex(MOUSE_BUFFER, uiVideoObjectHandle, usVideoObjectSubIndex, usXPos, usYPos);

  return ReturnValue;
}

void DirtyCursor( )
{
  guiMouseBufferState = BUFFER_DIRTY;
}


void StartFrameBufferRender(void)
{
	return;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void EndFrameBufferRender(void)
{

  guiFrameBufferState = BUFFER_DIRTY;

	return;

}

///////////////////////////////////////////////////////////////////////////////////////////////////

void PrintScreen(void)
{
  gfPrintFrameBuffer = TRUE;
}


void FatalError(const char *pError, ...)
{
	char gFatalErrorString[512];

	va_list argptr;

	va_start(argptr, pError);       	// Set up variable argument pointer
	vsprintf(gFatalErrorString, pError, argptr);
	va_end(argptr);

	SDL_QuitSubSystem(SDL_INIT_VIDEO);

	gfProgramIsRunning = FALSE;

	fprintf(stderr, "FATAL ERROR: %s\n", gFatalErrorString);
}


/*********************************************************************************
* SnapshotSmall
*
*		Grabs a screen from the [rimary surface, and stuffs it into a 16-bit (RGB 5,5,5),
* uncompressed Targa file. Each time the routine is called, it increments the
* file number by one. The files are create in the current directory, usually the
* EXE directory. This routine produces 1/4 sized images.
*
*********************************************************************************/

#pragma pack (push, 1)

typedef struct {

		UINT8		ubIDLength;
		UINT8		ubColorMapType;
		UINT8		ubTargaType;
		UINT16	usColorMapOrigin;
		UINT16	usColorMapLength;
		UINT8		ubColorMapEntrySize;
		UINT16	usOriginX;
		UINT16	usOriginY;
		UINT16	usImageWidth;
		UINT16	usImageHeight;
		UINT8		ubBitsPerPixel;
		UINT8		ubImageDescriptor;

} TARGA_HEADER;

#pragma pack (pop)


static void RefreshMovieCache(void);


static void SnapshotSmall(void)
{
	INT32 iCountX, iCountY;
	UINT16 *pVideo, *pDest;

//	sprintf( cFilename, "JA%5.5d.TGA", uiPicNum++ );

//	if( ( disk = fopen(cFilename, "wb"))==NULL )
//		return;

//	memset(&Header, 0, sizeof(TARGA_HEADER));

//	Header.ubTargaType=2;			// Uncompressed 16/24/32 bit
//	Header.usImageWidth=320;
//	Header.usImageHeight=240;
//	Header.ubBitsPerPixel=16;

//	fwrite(&Header, sizeof(TARGA_HEADER), 1, disk);

		// Get the write pointer
	pVideo = (UINT16*)ScreenBuffer->pixels;

	pDest = gpFrameData[ giNumFrames ];

	for(iCountY=SCREEN_HEIGHT-1; iCountY >=0 ; iCountY-=1)
	{
		for(iCountX=0; iCountX < SCREEN_WIDTH; iCountX+= 1)
		{
	//		uiData=(UINT16)*(pVideo+(iCountY*640*2)+ ( iCountX * 2 ) );

//				1111 1111 1100 0000
//				f		 f		c
	//		usPixel555=	(UINT16)(uiData&0xffff);
//			usPixel555= ((usPixel555 & 0xffc0) >> 1) | (usPixel555 & 0x1f);

	//		usPixel555=	(UINT16)(uiData);

		//	fwrite( &usPixel555, sizeof(UINT16), 1, disk);
	//		fwrite(	(void *)(((UINT8 *)ScreenBuffer->pixels) + ( iCountY * 640 * 2) + ( iCountX * 2 ) ), 2 * sizeof( BYTE ), 1, disk );

		 *( pDest + ( iCountY * 640 ) + ( iCountX ) ) = *( pVideo + ( iCountY * 640 ) + ( iCountX ) );
		}

	}

	giNumFrames++;

	if ( giNumFrames == MAX_NUM_FRAMES )
	{
		RefreshMovieCache( );
	}

//	fclose(disk);
}


void VideoCaptureToggle(void)
{
#ifdef JA2TESTVERSION
	INT32 cnt;

	gfVideoCapture = !gfVideoCapture;
	if (gfVideoCapture)
	{
		for ( cnt = 0; cnt < MAX_NUM_FRAMES; cnt++ )
		{
			gpFrameData[ cnt ] = MemAlloc( 640 * 480 * 2 );
		}

		giNumFrames = 0;

		guiLastFrame=GetTickCount();
	}
	else
	{
		RefreshMovieCache( );

		for ( cnt = 0; cnt < MAX_NUM_FRAMES; cnt++ )
		{
			if ( gpFrameData[ cnt ] != NULL )
			{
				MemFree( gpFrameData[ cnt ] );
			}
		}
		giNumFrames = 0;
	}
#endif
}


static void RefreshMovieCache(void)
{
	TARGA_HEADER Header;
	INT32 iCountX, iCountY;
	FILE *disk;
	CHAR8 cFilename[2048];
	static UINT32 uiPicNum=0;
	UINT16 *pDest;
	INT32	cnt;
	STRING512			ExecDir;

	PauseTime( TRUE );

	GetExecutableDirectory( ExecDir );
	SetFileManCurrentDirectory( ExecDir );

	for ( cnt = 0; cnt < giNumFrames; cnt++ )
	{
		sprintf( cFilename, "JA%5.5d.TGA", uiPicNum++ );

		if( ( disk = fopen(cFilename, "wb"))==NULL )
			return;

		memset(&Header, 0, sizeof(TARGA_HEADER));

		Header.ubTargaType=2;			// Uncompressed 16/24/32 bit
		Header.usImageWidth=640;
		Header.usImageHeight=480;
		Header.ubBitsPerPixel=16;

		fwrite(&Header, sizeof(TARGA_HEADER), 1, disk);

		pDest = gpFrameData[ cnt ];

		for(iCountY=480-1; iCountY >=0 ; iCountY-=1)
		{
			for(iCountX=0; iCountX < 640; iCountX ++ )
			{
				 fwrite( ( pDest + ( iCountY * 640 ) + iCountX ), sizeof(UINT16), 1, disk);
			}

		}

		fclose(disk);
	}

	PauseTime( FALSE );

	giNumFrames = 0;

	strcat( ExecDir, "\\Data" );
	SetFileManCurrentDirectory( ExecDir );
}
