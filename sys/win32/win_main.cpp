/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include <errno.h>
#include <float.h>
#include <fcntl.h>
#include <direct.h>
#include <io.h>
#include <conio.h>
#if 0 // flibit removed this include, VS2022 was _not_ happy about it
#include <mapi.h>
#endif
#include <ShellAPI.h>

// flibit added these to backport Skin Deep's crash handler
#include <shlobj.h>
#include "rc/resource.h"
#include <DbgHelp.h>
#include <winnt.h>
#include <commdlg.h>
#include <ctime>
#include <thread>
#include "miniz.h"
#include "framework/Session_local.h"

#ifndef __MRC__
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include "../sys_local.h"
#include "win_local.h"
#include "rc/CreateResourceIDs.h"
#include "../../renderer/tr_local.h"

idCVar Win32Vars_t::sys_arch( "sys_arch", "", CVAR_SYSTEM | CVAR_INIT, "" );
idCVar Win32Vars_t::sys_cpustring( "sys_cpustring", "detect", CVAR_SYSTEM | CVAR_INIT, "" );
idCVar Win32Vars_t::in_mouse( "in_mouse", "1", CVAR_SYSTEM | CVAR_BOOL, "enable mouse input" );
idCVar Win32Vars_t::win_allowAltTab( "win_allowAltTab", "0", CVAR_SYSTEM | CVAR_BOOL, "allow Alt-Tab when fullscreen" );
idCVar Win32Vars_t::win_notaskkeys( "win_notaskkeys", "0", CVAR_SYSTEM | CVAR_INTEGER, "disable windows task keys" );
idCVar Win32Vars_t::win_username( "win_username", "", CVAR_SYSTEM | CVAR_INIT, "windows user name" );
idCVar Win32Vars_t::win_xpos( "win_xpos", "3", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "horizontal position of window" );
idCVar Win32Vars_t::win_ypos( "win_ypos", "22", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "vertical position of window" );
idCVar Win32Vars_t::win_outputDebugString( "win_outputDebugString", "1", CVAR_SYSTEM | CVAR_BOOL, "" );
idCVar Win32Vars_t::win_outputEditString( "win_outputEditString", "1", CVAR_SYSTEM | CVAR_BOOL, "" );
idCVar Win32Vars_t::win_viewlog( "win_viewlog", "0", CVAR_SYSTEM | CVAR_INTEGER, "" );
idCVar Win32Vars_t::win_timerUpdate( "win_timerUpdate", "0", CVAR_SYSTEM | CVAR_BOOL, "allows the game to be updated while dragging the window" );
idCVar Win32Vars_t::win_allowMultipleInstances( "win_allowMultipleInstances", "0", CVAR_SYSTEM | CVAR_BOOL, "allow multiple instances running concurrently" );

Win32Vars_t	win32;

static char		sys_cmdline[MAX_STRING_CHARS];

// not a hard limit, just what we keep track of for debugging
xthreadInfo *g_threads[MAX_THREADS];

int g_thread_count = 0;

static sysMemoryStats_t exeLaunchMemoryStats;

static	xthreadInfo	threadInfo;
static	HANDLE		hTimer;

/*
================
Sys_GetExeLaunchMemoryStatus
================
*/
void Sys_GetExeLaunchMemoryStatus( sysMemoryStats_t &stats ) {
	stats = exeLaunchMemoryStats;
}

/*
==================
Sys_Createthread
==================
*/
void Sys_CreateThread(  xthread_t function, void *parms, xthreadPriority priority, xthreadInfo &info, const char *name, xthreadInfo *threads[MAX_THREADS], int *thread_count ) {
	HANDLE temp = CreateThread(	NULL,	// LPSECURITY_ATTRIBUTES lpsa,
									0,		// DWORD cbStack,
									(LPTHREAD_START_ROUTINE)function,	// LPTHREAD_START_ROUTINE lpStartAddr,
									parms,	// LPVOID lpvThreadParm,
									0,		//   DWORD fdwCreate,
									&info.threadId);
	info.threadHandle = (int) temp;
	if (priority == THREAD_HIGHEST) {
		SetThreadPriority( (HANDLE)info.threadHandle, THREAD_PRIORITY_HIGHEST );		//  we better sleep enough to do this
	} else if (priority == THREAD_ABOVE_NORMAL ) {
		SetThreadPriority( (HANDLE)info.threadHandle, THREAD_PRIORITY_ABOVE_NORMAL );
	}
	info.name = name;
	if ( *thread_count < MAX_THREADS ) {
		threads[(*thread_count)++] = &info;
	} else {
		common->DPrintf("WARNING: MAX_THREADS reached\n");
	}
}

/*
==================
Sys_DestroyThread
==================
*/
void Sys_DestroyThread( xthreadInfo& info ) {
	WaitForSingleObject( (HANDLE)info.threadHandle, INFINITE);
	CloseHandle( (HANDLE)info.threadHandle );
	info.threadHandle = 0;
}

/*
==================
Sys_Sentry
==================
*/
void Sys_Sentry() {
	int j = 0;
}

/*
==================
Sys_GetThreadName
==================
*/
const char* Sys_GetThreadName(int *index) {
	int id = GetCurrentThreadId();
	for( int i = 0; i < g_thread_count; i++ ) {
		if ( id == g_threads[i]->threadId ) {
			if ( index ) {
				*index = i;
			}
			return g_threads[i]->name;
		}
	}
	if ( index ) {
		*index = -1;
	}
	return "main";
}


/*
==================
Sys_EnterCriticalSection
==================
*/
void Sys_EnterCriticalSection( int index ) {
	assert( index >= 0 && index < MAX_CRITICAL_SECTIONS );
	if ( TryEnterCriticalSection( &win32.criticalSections[index] ) == 0 ) {
		EnterCriticalSection( &win32.criticalSections[index] );
//		Sys_DebugPrintf( "busy lock '%s' in thread '%s'\n", lock->name, Sys_GetThreadName() );
	}
}

/*
==================
Sys_LeaveCriticalSection
==================
*/
void Sys_LeaveCriticalSection( int index ) {
	assert( index >= 0 && index < MAX_CRITICAL_SECTIONS );
	LeaveCriticalSection( &win32.criticalSections[index] );
}

/*
==================
Sys_WaitForEvent
==================
*/
void Sys_WaitForEvent( int index ) {
	assert( index == 0 );
	if ( !win32.backgroundDownloadSemaphore ) {
		win32.backgroundDownloadSemaphore = CreateEvent( NULL, TRUE, FALSE, NULL );
	}
	WaitForSingleObject( win32.backgroundDownloadSemaphore, INFINITE );
	ResetEvent( win32.backgroundDownloadSemaphore );
}

/*
==================
Sys_TriggerEvent
==================
*/
void Sys_TriggerEvent( int index ) {
	assert( index == 0 );
	SetEvent( win32.backgroundDownloadSemaphore );
}



#pragma optimize( "", on )

#ifdef DEBUG


static unsigned int debug_total_alloc = 0;
static unsigned int debug_total_alloc_count = 0;
static unsigned int debug_current_alloc = 0;
static unsigned int debug_current_alloc_count = 0;
static unsigned int debug_frame_alloc = 0;
static unsigned int debug_frame_alloc_count = 0;

idCVar sys_showMallocs( "sys_showMallocs", "0", CVAR_SYSTEM, "" );

// _HOOK_ALLOC, _HOOK_REALLOC, _HOOK_FREE

typedef struct CrtMemBlockHeader
{
	struct _CrtMemBlockHeader *pBlockHeaderNext;	// Pointer to the block allocated just before this one:
	struct _CrtMemBlockHeader *pBlockHeaderPrev;	// Pointer to the block allocated just after this one
   char *szFileName;    // File name
   int nLine;           // Line number
   size_t nDataSize;    // Size of user block
   int nBlockUse;       // Type of block
   long lRequest;       // Allocation number
	byte		gap[4];								// Buffer just before (lower than) the user's memory:
} CrtMemBlockHeader;

#include <crtdbg.h>

/*
==================
Sys_AllocHook

	called for every malloc/new/free/delete
==================
*/
int Sys_AllocHook( int nAllocType, void *pvData, size_t nSize, int nBlockUse, long lRequest, const unsigned char * szFileName, int nLine ) 
{
	CrtMemBlockHeader	*pHead;
	byte				*temp;

	if ( nBlockUse == _CRT_BLOCK )
	{
      return( TRUE );
	}

	// get a pointer to memory block header
	temp = ( byte * )pvData;
	temp -= 32;
	pHead = ( CrtMemBlockHeader * )temp;

	switch( nAllocType ) {
		case	_HOOK_ALLOC:
			debug_total_alloc += nSize;
			debug_current_alloc += nSize;
			debug_frame_alloc += nSize;
			debug_total_alloc_count++;
			debug_current_alloc_count++;
			debug_frame_alloc_count++;
			break;

		case	_HOOK_FREE:
			assert( pHead->gap[0] == 0xfd && pHead->gap[1] == 0xfd && pHead->gap[2] == 0xfd && pHead->gap[3] == 0xfd );

			debug_current_alloc -= pHead->nDataSize;
			debug_current_alloc_count--;
			debug_total_alloc_count++;
			debug_frame_alloc_count++;
			break;

		case	_HOOK_REALLOC:
			assert( pHead->gap[0] == 0xfd && pHead->gap[1] == 0xfd && pHead->gap[2] == 0xfd && pHead->gap[3] == 0xfd );

			debug_current_alloc -= pHead->nDataSize;
			debug_total_alloc += nSize;
			debug_current_alloc += nSize;
			debug_frame_alloc += nSize;
			debug_total_alloc_count++;
			debug_current_alloc_count--;
			debug_frame_alloc_count++;
			break;
	}
	return( TRUE );
}

/*
==================
Sys_DebugMemory_f
==================
*/
void Sys_DebugMemory_f( void ) {
  	common->Printf( "Total allocation %8dk in %d blocks\n", debug_total_alloc / 1024, debug_total_alloc_count );
  	common->Printf( "Current allocation %8dk in %d blocks\n", debug_current_alloc / 1024, debug_current_alloc_count );
}

/*
==================
Sys_MemFrame
==================
*/
void Sys_MemFrame( void ) {
	if( sys_showMallocs.GetInteger() ) {
		common->Printf("Frame: %8dk in %5d blocks\n", debug_frame_alloc / 1024, debug_frame_alloc_count );
	}

	debug_frame_alloc = 0;
	debug_frame_alloc_count = 0;
}

#endif

/*
==================
Sys_FlushCacheMemory

On windows, the vertex buffers are write combined, so they
don't need to be flushed from the cache
==================
*/
void Sys_FlushCacheMemory( void *base, int bytes ) {
}

/*
=============
Sys_Error

Show the early console as an error dialog
=============
*/
void Sys_Error( const char *error, ... ) {
	va_list		argptr;
	char		text[4096];
    MSG        msg;

	va_start( argptr, error );
	vsprintf( text, error, argptr );
	va_end( argptr);

	Conbuf_AppendText( text );
	Conbuf_AppendText( "\n" );

	Win_SetErrorText( text );
	Sys_ShowConsole( 1, true );

	timeEndPeriod( 1 );

	Sys_ShutdownInput();

	GLimp_Shutdown();

	// wait for the user to quit
	while ( 1 ) {
		if ( !GetMessage( &msg, NULL, 0, 0 ) ) {
			common->Quit();
		}
		TranslateMessage( &msg );
      	DispatchMessage( &msg );
	}

	Sys_DestroyConsole();

	exit (1);
}

/*
==============
Sys_Quit
==============
*/
void Sys_Quit( void ) {
	timeEndPeriod( 1 );
	Sys_ShutdownInput();
	Sys_DestroyConsole();
	ExitProcess( 0 );
}


/*
==============
Sys_Printf
==============
*/
#define MAXPRINTMSG 4096
void Sys_Printf( const char *fmt, ... ) {
	char		msg[MAXPRINTMSG];

	va_list argptr;
	va_start(argptr, fmt);
	idStr::vsnPrintf( msg, MAXPRINTMSG-1, fmt, argptr );
	va_end(argptr);
	msg[sizeof(msg)-1] = '\0';

	if ( win32.win_outputDebugString.GetBool() ) {
		OutputDebugString( msg );
	}
	if ( win32.win_outputEditString.GetBool() ) {
		Conbuf_AppendText( msg );
	}
}

/*
==============
Sys_DebugPrintf
==============
*/
#define MAXPRINTMSG 4096
void Sys_DebugPrintf( const char *fmt, ... ) {
	char msg[MAXPRINTMSG];

	va_list argptr;
	va_start( argptr, fmt );
	idStr::vsnPrintf( msg, MAXPRINTMSG-1, fmt, argptr );
	msg[ sizeof(msg)-1 ] = '\0';
	va_end( argptr );

	OutputDebugString( msg );
}

/*
==============
Sys_DebugVPrintf
==============
*/
void Sys_DebugVPrintf( const char *fmt, va_list arg ) {
	char msg[MAXPRINTMSG];

	idStr::vsnPrintf( msg, MAXPRINTMSG-1, fmt, arg );
	msg[ sizeof(msg)-1 ] = '\0';

	OutputDebugString( msg );
}

/*
==============
Sys_Sleep
==============
*/
void Sys_Sleep( int msec ) {
	Sleep( msec );
}

/*
==============
Sys_ShowWindow
==============
*/
void Sys_ShowWindow( bool show ) {
	::ShowWindow( win32.hWnd, show ? SW_SHOW : SW_HIDE );
}

/*
==============
Sys_IsWindowVisible
==============
*/
bool Sys_IsWindowVisible( void ) {
	return ( ::IsWindowVisible( win32.hWnd ) != 0 );
}

/*
==============
Sys_Mkdir
==============
*/
void Sys_Mkdir( const char *path ) {
	_mkdir (path);
}

/*
=================
Sys_FileTimeStamp
=================
*/
ID_TIME_T Sys_FileTimeStamp( FILE *fp ) {
	struct _stat st;
	_fstat( _fileno( fp ), &st );
	return (long) st.st_mtime;
}

/*
==============
Sys_Cwd
==============
*/
const char *Sys_Cwd( void ) {
	static char cwd[MAX_OSPATH];

	_getcwd( cwd, sizeof( cwd ) - 1 );
	cwd[MAX_OSPATH-1] = 0;

	return cwd;
}

/*
==============
Sys_DefaultCDPath
==============
*/
const char *Sys_DefaultCDPath( void ) {
	return "";
}

/*
==============
Sys_DefaultBasePath
==============
*/
const char *Sys_DefaultBasePath( void ) {
	return Sys_Cwd();
}

/*
==============
Sys_DefaultSavePath
==============
*/
const char *Sys_DefaultSavePath( void ) {
#ifdef STEAM
	return cvarSystem->GetCVarString( "fs_basepath" );
#elif defined(USE_SDL)
#if defined( ID_DEMO_BUILD )
	static char* prefPath = SDL_GetPrefPath("BlendoGames", "Quadrilateral Cowboy Demo");
#else
	static char* prefPath = SDL_GetPrefPath("BlendoGames", "Quadrilateral Cowboy");
#endif
	return prefPath;
#else
#error TODO:
#endif
}

/*
==============
Sys_EXEPath
==============
*/
const char *Sys_EXEPath( void ) {
	static char exe[ MAX_OSPATH ];
	GetModuleFileName( NULL, exe, sizeof( exe ) - 1 );
	return exe;
}

/*
==============
Sys_ListFiles
==============
*/
int Sys_ListFiles( const char *directory, const char *extension, idStrList &list ) {
	idStr		search;
	struct _finddata_t findinfo;
	int			findhandle;
	int			flag;

	if ( !extension) {
		extension = "";
	}

	// passing a slash as extension will find directories
	if ( extension[0] == '/' && extension[1] == 0 ) {
		extension = "";
		flag = 0;
	} else {
		flag = _A_SUBDIR;
	}

	sprintf( search, "%s\\*%s", directory, extension );

	// search
	list.Clear();

	findhandle = _findfirst( search, &findinfo );
	if ( findhandle == -1 ) {
		return -1;
	}

	do {
		if ( flag ^ ( findinfo.attrib & _A_SUBDIR ) ) {
			list.Append( findinfo.name );
		}
	} while ( _findnext( findhandle, &findinfo ) != -1 );

	_findclose( findhandle );

	return list.Num();
}


/*
================
Sys_GetClipboardData
================
*/
char *Sys_GetClipboardData( void ) {
	char *data = NULL;
	char *cliptext;

	if ( OpenClipboard( NULL ) != 0 ) {
		HANDLE hClipboardData;

		if ( ( hClipboardData = GetClipboardData( CF_TEXT ) ) != 0 ) {
			if ( ( cliptext = (char *)GlobalLock( hClipboardData ) ) != 0 ) {
				data = (char *)Mem_Alloc( GlobalSize( hClipboardData ) + 1 );
				strcpy( data, cliptext );
				GlobalUnlock( hClipboardData );
				
				strtok( data, "\n\r\b" );
			}
		}
		CloseClipboard();
	}
	return data;
}

/*
================
Sys_SetClipboardData
================
*/
void Sys_SetClipboardData( const char *string ) {
	HGLOBAL HMem;
	char *PMem;

	// allocate memory block
	HMem = (char *)::GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, strlen( string ) + 1 );
	if ( HMem == NULL ) {
		return;
	}
	// lock allocated memory and obtain a pointer
	PMem = (char *)::GlobalLock( HMem );
	if ( PMem == NULL ) {
		return;
	}
	// copy text into allocated memory block
	lstrcpy( PMem, string );
	// unlock allocated memory
	::GlobalUnlock( HMem );
	// open Clipboard
	if ( !OpenClipboard( 0 ) ) {
		::GlobalFree( HMem );
		return;
	}
	// remove current Clipboard contents
	EmptyClipboard();
	// supply the memory handle to the Clipboard
	SetClipboardData( CF_TEXT, HMem );
	HMem = 0;
	// close Clipboard
	CloseClipboard();
}

/*
========================================================================

DLL Loading

========================================================================
*/

// flibit: 64 bit fix, changed int to void*

/*
=====================
Sys_DLL_Load
=====================
*/
void* Sys_DLL_Load( const char *dllName ) {
	HINSTANCE	libHandle;
	libHandle = LoadLibrary( dllName );
	if ( libHandle ) {
		// since we can't have LoadLibrary load only from the specified path, check it did the right thing
		char loadedPath[ MAX_OSPATH ];
		GetModuleFileName( libHandle, loadedPath, sizeof( loadedPath ) - 1 );
		if ( idStr::IcmpPath( dllName, loadedPath ) ) {
			Sys_Printf( "ERROR: LoadLibrary '%s' wants to load '%s'\n", dllName, loadedPath );
			Sys_DLL_Unload( libHandle );
			return 0;
		}
	}
	return (void*)libHandle;
}

/*
=====================
Sys_DLL_GetProcAddress
=====================
*/
void *Sys_DLL_GetProcAddress( void* dllHandle, const char *procName ) {
	return GetProcAddress( (HINSTANCE)dllHandle, procName ); 
}

/*
=====================
Sys_DLL_Unload
=====================
*/
void Sys_DLL_Unload( void* dllHandle ) {
	if ( !dllHandle ) {
		return;
	}
	if ( FreeLibrary( (HINSTANCE)dllHandle ) == 0 ) {
		int lastError = GetLastError();
		LPVOID lpMsgBuf;
		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER,
		    NULL,
			lastError,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
			(LPTSTR) &lpMsgBuf,
			0,
			NULL 
		);
		Sys_Error( "Sys_DLL_Unload: FreeLibrary failed - %s (%d)", lpMsgBuf, lastError );
	}
}

// flibit end

/*
========================================================================

EVENT LOOP

========================================================================
*/

#ifndef USE_SDL

#define	MAX_QUED_EVENTS		256
#define	MASK_QUED_EVENTS	( MAX_QUED_EVENTS - 1 )

sysEvent_t	eventQue[MAX_QUED_EVENTS];
int			eventHead = 0;
int			eventTail = 0;

/*
================
Sys_QueEvent

Ptr should either be null, or point to a block of data that can
be freed by the game later.
================
*/
void Sys_QueEvent( int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr ) {
	sysEvent_t	*ev;

	ev = &eventQue[ eventHead & MASK_QUED_EVENTS ];

	if ( eventHead - eventTail >= MAX_QUED_EVENTS ) {
		common->Printf("Sys_QueEvent: overflow\n");
		// we are discarding an event, but don't leak memory
		if ( ev->evPtr ) {
			Mem_Free( ev->evPtr );
		}
		eventTail++;
	}

	eventHead++;

	ev->evType = type;
	ev->evValue = value;
	ev->evValue2 = value2;
	ev->evPtrLength = ptrLength;
	ev->evPtr = ptr;
}

/*
=============
Sys_PumpEvents

This allows windows to be moved during renderbump
=============
*/
void Sys_PumpEvents( void ) {
    MSG msg;

	// pump the message loop
	while( PeekMessage( &msg, NULL, 0, 0, PM_NOREMOVE ) ) {
		if ( !GetMessage( &msg, NULL, 0, 0 ) ) {
			common->Quit();
		}

		// save the msg time, because wndprocs don't have access to the timestamp
		if ( win32.sysMsgTime && win32.sysMsgTime > (int)msg.time ) {
			// don't ever let the event times run backwards	
//			common->Printf( "Sys_PumpEvents: win32.sysMsgTime (%i) > msg.time (%i)\n", win32.sysMsgTime, msg.time );
		} else {
			win32.sysMsgTime = msg.time;
		}

#ifdef ID_ALLOW_TOOLS
		if ( GUIEditorHandleMessage ( &msg ) ) {	
			continue;
		}
#endif
 
		TranslateMessage (&msg);
      	DispatchMessage (&msg);
	}
}

/*
================
Sys_GenerateEvents
================
*/
void Sys_GenerateEvents( void ) {
	static int entered = false;
	char *s;

	if ( entered ) {
		return;
	}
	entered = true;

	// pump the message loop
	Sys_PumpEvents();

	// make sure mouse and joystick are only called once a frame
	IN_Frame();

	// check for console commands
	s = Sys_ConsoleInput();
	if ( s ) {
		char	*b;
		int		len;

		len = strlen( s ) + 1;
		b = (char *)Mem_Alloc( len );
		strcpy( b, s );
		Sys_QueEvent( 0, SE_CONSOLE, 0, 0, len, b );
	}

	entered = false;
}

/*
================
Sys_ClearEvents
================
*/
void Sys_ClearEvents( void ) {
	eventHead = eventTail = 0;
}

/*
================
Sys_GetEvent
================
*/
sysEvent_t Sys_GetEvent( void ) {
	sysEvent_t	ev;

	// return if we have data
	if ( eventHead > eventTail ) {
		eventTail++;
		return eventQue[ ( eventTail - 1 ) & MASK_QUED_EVENTS ];
	}

	// return the empty event 
	memset( &ev, 0, sizeof( ev ) );

	return ev;
}
#endif

//================================================================

/*
=================
Sys_In_Restart_f

Restart the input subsystem
=================
*/
void Sys_In_Restart_f( const idCmdArgs &args ) {
	Sys_ShutdownInput();
	Sys_InitInput();
}


/*
==================
Sys_AsyncThread
==================
*/
extern int SEH_Filter( _EXCEPTION_POINTERS* ex, bool isMainThread = true );
static void Sys_AsyncThread( void *parm ) {
#if ENABLE_FP_EXCEPTIONS
	FPExceptionEnabler enabled(_EM_UNDERFLOW | _EM_OVERFLOW | _EM_ZERODIVIDE | _EM_INVALID);
#endif
	int		wakeNumber;
	int		startTime;

	startTime = Sys_Milliseconds();
	wakeNumber = 0;

	while ( 1 ) {
#ifdef WIN32	
		// this will trigger 60 times a second
		int r = WaitForSingleObject( hTimer, 100 );
		if ( r != WAIT_OBJECT_0 ) {
			OutputDebugString( "idPacketServer::PacketServerInterrupt: bad wait return" );
		}
#endif

#if 0
		wakeNumber++;
		int		msec = Sys_Milliseconds();
		int		deltaTime = msec - startTime;
		startTime = msec;

		char	str[1024];
		sprintf( str, "%i ", deltaTime );
		OutputDebugString( str );
#endif


		// SKINDEEP // SM: On Windows, AsyncTimer should use __try/__except to catch crashes
		__try {
			common->Async();
		}
		__except ( SEH_Filter( GetExceptionInformation(), false ) ) {
			__debugbreak();
		}
	}
}

/*
==============
Sys_StartAsyncThread

Start the thread that will call idCommon::Async()
==============
*/
void Sys_StartAsyncThread( void ) {
	// create an auto-reset event that happens 60 times a second
	hTimer = CreateWaitableTimer( NULL, false, NULL );
	if ( !hTimer ) {
		common->Error( "idPacketServer::Spawn: CreateWaitableTimer failed" );
	}

	LARGE_INTEGER	t;
	t.HighPart = t.LowPart = 0;
	SetWaitableTimer( hTimer, &t, USERCMD_MSEC, NULL, NULL, TRUE );

	Sys_CreateThread( (xthread_t)Sys_AsyncThread, NULL, THREAD_ABOVE_NORMAL, threadInfo, "Async", g_threads,  &g_thread_count );

#ifdef SET_THREAD_AFFINITY 
	// give the async thread an affinity for the second cpu
	SetThreadAffinityMask( (HANDLE)threadInfo.threadHandle, 2 );
#endif

	if ( !threadInfo.threadHandle ) {
		common->Error( "Sys_StartAsyncThread: failed" );
	}
}

/*
================
Sys_AlreadyRunning

returns true if there is a copy of D3 running already
================
*/
bool Sys_AlreadyRunning( void ) {
#ifndef DEBUG
	if ( !win32.win_allowMultipleInstances.GetBool() ) {
		HANDLE hMutexOneInstance = ::CreateMutex( NULL, FALSE, "DOOM3" );
		if ( ::GetLastError() == ERROR_ALREADY_EXISTS || ::GetLastError() == ERROR_ACCESS_DENIED ) {
			return true;
		}
	}
#endif
	return false;
}

/*
================
Sys_Init

The cvar system must already be setup
================
*/
#define OSR2_BUILD_NUMBER 1111
#define WIN98_BUILD_NUMBER 1998

void Sys_Init( void ) {

	CoInitialize( NULL );

	// make sure the timer is high precision, otherwise
	// NT gets 18ms resolution
	timeBeginPeriod( 1 );

	// get WM_TIMER messages pumped every millisecond
//	SetTimer( NULL, 0, 100, NULL );

	cmdSystem->AddCommand( "in_restart", Sys_In_Restart_f, CMD_FL_SYSTEM, "restarts the input system" );
#ifdef DEBUG
	cmdSystem->AddCommand( "createResourceIDs", CreateResourceIDs_f, CMD_FL_TOOL, "assigns resource IDs in _resouce.h files" );
#endif
#if 0
	cmdSystem->AddCommand( "setAsyncSound", Sys_SetAsyncSound_f, CMD_FL_SYSTEM, "set the async sound option" );
#endif

	//
	// Windows user name
	//
	win32.win_username.SetString( Sys_GetCurrentUser() );

	//
	// Windows version
	//
	win32.osversion.dwOSVersionInfoSize = sizeof( win32.osversion );

	if ( !GetVersionEx( (LPOSVERSIONINFO)&win32.osversion ) )
		Sys_Error( "Couldn't get OS info" );

	if ( win32.osversion.dwMajorVersion < 4 ) {
		Sys_Error( GAME_NAME " requires Windows version 4 (NT) or greater" );
	}
	if ( win32.osversion.dwPlatformId == VER_PLATFORM_WIN32s ) {
		Sys_Error( GAME_NAME " doesn't run on Win32s" );
	}

	if( win32.osversion.dwPlatformId == VER_PLATFORM_WIN32_NT ) {
		if( win32.osversion.dwMajorVersion <= 4 ) {
			win32.sys_arch.SetString( "WinNT (NT)" );
		} else if( win32.osversion.dwMajorVersion == 5 && win32.osversion.dwMinorVersion == 0 ) {
			win32.sys_arch.SetString( "Win2K (NT)" );
		} else if( win32.osversion.dwMajorVersion == 5 && win32.osversion.dwMinorVersion == 1 ) {
			win32.sys_arch.SetString( "WinXP (NT)" );
		} else if ( win32.osversion.dwMajorVersion == 6 ) {
			win32.sys_arch.SetString( "Vista" );
		} else {
			win32.sys_arch.SetString( "Unknown NT variant" );
		}
	} else if( win32.osversion.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS ) {
		if( win32.osversion.dwMajorVersion == 4 && win32.osversion.dwMinorVersion == 0 ) {
			// Win95
			if( win32.osversion.szCSDVersion[1] == 'C' ) {
				win32.sys_arch.SetString( "Win95 OSR2 (95)" );
			} else {
				win32.sys_arch.SetString( "Win95 (95)" );
			}
		} else if( win32.osversion.dwMajorVersion == 4 && win32.osversion.dwMinorVersion == 10 ) {
			// Win98
			if( win32.osversion.szCSDVersion[1] == 'A' ) {
				win32.sys_arch.SetString( "Win98SE (95)" );
			} else {
				win32.sys_arch.SetString( "Win98 (95)" );
			}
		} else if( win32.osversion.dwMajorVersion == 4 && win32.osversion.dwMinorVersion == 90 ) {
			// WinMe
		  	win32.sys_arch.SetString( "WinMe (95)" );
		} else {
		  	win32.sys_arch.SetString( "Unknown 95 variant" );
		}
	} else {
		win32.sys_arch.SetString( "unknown Windows variant" );
	}

	//
	// CPU type
	//
	if ( !idStr::Icmp( win32.sys_cpustring.GetString(), "detect" ) ) {
		idStr string;

		common->Printf( "%1.0f MHz ", Sys_ClockTicksPerSecond() / 1000000.0f );

		win32.cpuid = Sys_GetCPUId();

		string.Clear();

		if ( win32.cpuid & CPUID_AMD ) {
			string += "AMD CPU";
		} else if ( win32.cpuid & CPUID_INTEL ) {
			string += "Intel CPU";
		} else if ( win32.cpuid & CPUID_UNSUPPORTED ) {
			string += "unsupported CPU";
		} else {
			string += "generic CPU";
		}

		string += " with ";
		if ( win32.cpuid & CPUID_MMX ) {
			string += "MMX & ";
		}
		if ( win32.cpuid & CPUID_3DNOW ) {
			string += "3DNow! & ";
		}
		if ( win32.cpuid & CPUID_SSE ) {
			string += "SSE & ";
		}
		if ( win32.cpuid & CPUID_SSE2 ) {
            string += "SSE2 & ";
		}
		if ( win32.cpuid & CPUID_SSE3 ) {
			string += "SSE3 & ";
		}
		if ( win32.cpuid & CPUID_HTT ) {
			string += "HTT & ";
		}
		string.StripTrailing( " & " );
		string.StripTrailing( " with " );
		win32.sys_cpustring.SetString( string );
	} else {
		common->Printf( "forcing CPU type to " );
		idLexer src( win32.sys_cpustring.GetString(), idStr::Length( win32.sys_cpustring.GetString() ), "sys_cpustring" );
		idToken token;

		int id = CPUID_NONE;
		while( src.ReadToken( &token ) ) {
			if ( token.Icmp( "generic" ) == 0 ) {
				id |= CPUID_GENERIC;
			} else if ( token.Icmp( "intel" ) == 0 ) {
				id |= CPUID_INTEL;
			} else if ( token.Icmp( "amd" ) == 0 ) {
				id |= CPUID_AMD;
			} else if ( token.Icmp( "mmx" ) == 0 ) {
				id |= CPUID_MMX;
			} else if ( token.Icmp( "3dnow" ) == 0 ) {
				id |= CPUID_3DNOW;
			} else if ( token.Icmp( "sse" ) == 0 ) {
				id |= CPUID_SSE;
			} else if ( token.Icmp( "sse2" ) == 0 ) {
				id |= CPUID_SSE2;
			} else if ( token.Icmp( "sse3" ) == 0 ) {
				id |= CPUID_SSE3;
			} else if ( token.Icmp( "htt" ) == 0 ) {
				id |= CPUID_HTT;
			}
		}
		if ( id == CPUID_NONE ) {
			common->Printf( "WARNING: unknown sys_cpustring '%s'\n", win32.sys_cpustring.GetString() );
			id = CPUID_GENERIC;
		}
		win32.cpuid = (cpuid_t) id;
	}

	common->Printf( "%s\n", win32.sys_cpustring.GetString() );
	common->Printf( "%d MB System Memory\n", Sys_GetSystemRam() );
	common->Printf( "%d MB Video Memory\n", Sys_GetVideoRam() );
}

/*
================
Sys_Shutdown
================
*/
void Sys_Shutdown( void ) {
	CoUninitialize();
}

/*
================
Sys_GetProcessorId
================
*/
cpuid_t Sys_GetProcessorId( void ) {
    return win32.cpuid;
}

/*
================
Sys_GetProcessorString
================
*/
const char *Sys_GetProcessorString( void ) {
	return win32.sys_cpustring.GetString();
}

//=======================================================================

//#define SET_THREAD_AFFINITY


/*
====================
Win_Frame
====================
*/
void Win_Frame( void ) {
	// if "viewlog" has been modified, show or hide the log console
	if ( win32.win_viewlog.IsModified() ) {
		if ( !com_skipRenderer.GetBool() && idAsyncNetwork::serverDedicated.GetInteger() != 1 ) {
			Sys_ShowConsole( win32.win_viewlog.GetInteger(), false );
		}
		win32.win_viewlog.ClearModified();
	}
}



// ==========================================================================================
// SM: Added code to catch exceptions and generate a stack trace in both the log and a dialog
static mz_bool AddFileToZip( mz_zip_archive* zip, const char* fileName )
{
	FILE* file = fopen( fileName, "rb" );
	if ( !file )
	{
		return MZ_FALSE;
	}

	fclose( file );
	return mz_zip_writer_add_file( zip, fileName, fileName, nullptr, 0, 9 );
}

idStr outputMsg;

static _EXCEPTION_POINTERS* crashExPtr = nullptr;
BOOL CALLBACK CrashHandlerProc( HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam )
{
	switch ( message )
	{
	case WM_INITDIALOG:
		{
			HWND hwndOwner;
			RECT rc, rcDlg, rcOwner;
			// Get the owner window and dialog box rectangles. 
			if ( ( hwndOwner = GetParent( hwndDlg ) ) == NULL )
			{
				hwndOwner = GetDesktopWindow();
			}

			GetWindowRect( hwndOwner, &rcOwner );
			GetWindowRect( hwndDlg, &rcDlg );
			CopyRect( &rc, &rcOwner );

			// Offset the owner and dialog box rectangles so that right and bottom 
			// values represent the width and height, and then offset the owner again 
			// to discard space taken up by the dialog box. 

			OffsetRect( &rcDlg, -rcDlg.left, -rcDlg.top );
			OffsetRect( &rc, -rc.left, -rc.top );
			OffsetRect( &rc, -rcDlg.right, -rcDlg.bottom );

			// The new position is the sum of half the remaining space and the owner's 
			// original position. 

			SetWindowPos( hwndDlg,
				HWND_TOP,
				rcOwner.left + ( rc.right / 2 ),
				rcOwner.top + ( rc.bottom / 2 ),
				0, 0,          // Ignores size arguments. 
				SWP_NOSIZE );

			// Setup the stack frame message
			idStr* inMsg = ( idStr* )lParam;
			inMsg->Replace( "\n", "\r\n" );
			SetDlgItemText( hwndDlg, IDC_STACK, inMsg->c_str() );

			FILE* crashFile = fopen( "crashinfo.txt", "w" );
			fprintf( crashFile, inMsg->c_str() );
			fclose( crashFile );

			SendDlgItemMessage(hwndDlg, IDC_ERRORICON, STM_SETICON, (WPARAM)LoadIcon( nullptr, IDI_ERROR ), 0);

#if 0 // We're native, so whatever... -flibit
			if (common && common->g_SteamUtilities && common->g_SteamUtilities->IsOnSteamDeck())
			{
				HWND breakButton = GetDlgItem(hwndDlg, IDC_BREAK);
				ShowWindow(breakButton, SW_HIDE);

				HWND dumpButton = GetDlgItem(hwndDlg, IDC_DUMP);
				ShowWindow(dumpButton, SW_HIDE);
			}
			return TRUE;
#endif
		}
	case WM_COMMAND:
		switch ( LOWORD( wParam ) )
		{
		case IDC_BREAK:
		{
			__debugbreak();
			return TRUE;
		}
		case IDC_COPY:
		{
			idStr crashStr = sessLocal.GetSanitizedURLArgument(outputMsg);
			idStr locationStr = sessLocal.GetPlayerLocationString();
			idStr URL = idStr::Format("https://docs.google.com/forms/d/e/1FAIpQLScejbJ0SFfkHb0iWhFagXAikLwnyHOSie-n7tHUtFheFQ6oiQ/viewform?usp=dialog&entry.561524408=%s&entry.1770058426=%s", locationStr.c_str(), crashStr.c_str());
#if 0 // We're native, so whatever... -flibit
			if (common && common->g_SteamUtilities && common->g_SteamUtilities->IsOnSteamDeck())
			{
				common->g_SteamUtilities->OpenSteamOverlaypage(URL.c_str());
			}
			else
#endif
			{
				ShellExecute(nullptr, nullptr, URL.c_str(), nullptr, nullptr, SW_SHOW);
			}

			return TRUE;
		}
		case IDC_DUMP:
		{
			// Create the crash dump file
			HANDLE dmpFile = CreateFile( "skindeep.dmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
				FILE_ATTRIBUTE_NORMAL, NULL );
			MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
			dumpInfo.ThreadId = GetCurrentThreadId();
			dumpInfo.ExceptionPointers = crashExPtr;
			dumpInfo.ClientPointers = TRUE;

			int dumpFlags = MiniDumpNormal
				| MiniDumpWithIndirectlyReferencedMemory
				| MiniDumpWithDataSegs
				| MiniDumpWithHandleData
				| MiniDumpWithUnloadedModules
				| MiniDumpWithProcessThreadData
				| MiniDumpWithThreadInfo
				| MiniDumpIgnoreInaccessibleMemory;
			
			MiniDumpWriteDump( GetCurrentProcess(), GetCurrentProcessId(), dmpFile, ( MINIDUMP_TYPE )dumpFlags,
				&dumpInfo, NULL, NULL );
			CloseHandle( dmpFile );

			// Get the current time
			time_t currTime = std::time( nullptr );
			char dateStr[256];
			strftime( dateStr, 256, "%F-%H-%M-%S", std::localtime( &currTime ) );

			// Have to save the original CWD as the path will change after the save file dialog
			char originalCWD[1024];
			GetCurrentDirectory( 1024, originalCWD );

			// Create a save file dialog
			const int MAX_ZIP_NAME = 1024;
			char fileName[MAX_ZIP_NAME];
			
			idStr::snPrintf( fileName, MAX_ZIP_NAME, "SkinDeep-Crash-%s.zip", dateStr );
			OPENFILENAME openInfo;
			ZeroMemory( &openInfo, sizeof( openInfo ) );
			openInfo.lStructSize = sizeof( OPENFILENAME );
			openInfo.hwndOwner = hwndDlg;
			openInfo.lpstrFilter = "ZIP file (*.zip)\0*.ZIP\0";
			openInfo.lpstrFile = fileName;
			openInfo.nMaxFile = MAX_ZIP_NAME;
			if ( GetSaveFileName( &openInfo ) )
			{
				IProgressDialog *pd;
				HRESULT hr;
				hr = CoCreateInstance( CLSID_ProgressDialog, NULL, CLSCTX_INPROC_SERVER,
					IID_IProgressDialog, ( LPVOID * )( &pd ) );
				pd->SetTitle( L"Saving crash dump..." );
				pd->StartProgressDialog( hwndDlg, NULL,
					PROGDLG_MODAL | PROGDLG_NOTIME | PROGDLG_NOMINIMIZE | PROGDLG_NOCANCEL | PROGDLG_MARQUEEPROGRESS,
					NULL );
				// Set back to original CWD
				SetCurrentDirectory( originalCWD );

				std::thread zipThread( [&openInfo, pd, hwndDlg]() {
					mz_zip_archive zip;
					memset( &zip, 0, sizeof( zip ) );
					mz_bool success = MZ_TRUE;
					idStr errorMsg;
					if ( mz_zip_writer_init_file( &zip, openInfo.lpstrFile, 0 ) )
					{
						success &= AddFileToZip( &zip, "crashinfo.txt" );
						if (!success && errorMsg.Length() == 0)
							errorMsg = "Failed to add crashinfo.txt to zip";
						
						success &= AddFileToZip( &zip, "skindeep.pdb" );
						if (!success && errorMsg.Length() == 0)
							errorMsg = "Failed to add skindeep.pdb to zip";
						
						success &= AddFileToZip( &zip, "skindeep.dmp" );
						if (!success && errorMsg.Length() == 0)
							errorMsg = "Failed to add skindeep.dmp to zip";
						
						success &= AddFileToZip( &zip, "skindeep.exe" );
						if (!success && errorMsg.Length() == 0)
							errorMsg = "Failed to add skindeep.exe to zip";
						
						success &= mz_zip_writer_finalize_archive( &zip );
						if (!success && errorMsg.Length() == 0)
							errorMsg = "Failed to finalize zip archive";
						
						success &= mz_zip_writer_end( &zip );
						if (!success && errorMsg.Length() == 0)
							errorMsg = "Failed to close zip file writer";
					}
					else
					{
						errorMsg = idStr::Format("Could not create zip file: '%s'", openInfo.lpstrFile);
						success = MZ_FALSE;
					}

					pd->StopProgressDialog();
					pd->Release();

					if (!success)
					{
						idStr msgBox = idStr::Format("Failed to save crash dump with error: %s. Try to SAVE CRASH DUMP again.", errorMsg.c_str());
						MessageBox(hwndDlg, msgBox.c_str(), NULL, MB_OK);
					}
				});

				zipThread.detach();
			}
			return TRUE;
		}
		case IDOK:
		case IDCANCEL:
			EndDialog( hwndDlg, wParam );
			return TRUE;
		}
	}
	return FALSE;
}

int SEH_Filter( _EXCEPTION_POINTERS* ex, bool isMainThread = true )
{
	static const int MAX_STACK_COUNT = 64;
	void* stack[MAX_STACK_COUNT];
	unsigned short frames;
	SYMBOL_INFO* symbol;
	HANDLE process;

	crashExPtr = ex;

	disableAssertPrintf = true;

	idCVar* versionCvar = cvarSystem->Find( "g_version" );
	outputMsg += "Build: ";
	outputMsg += versionCvar->GetString();
	outputMsg += '\n';

	outputMsg += "==================FATAL ERROR====================\n";
	outputMsg += idStr::Format( "UNHANDLED EXCEPTION: 0x%X\n", ex->ExceptionRecord->ExceptionCode );
	outputMsg += idStr::Format("OCCURRED AT: %s\n", Sys_TimeStampToStr(Sys_GetTime()));

	process = GetCurrentProcess();

	SymInitialize( process, NULL, TRUE );

	frames = CaptureStackBackTrace( 0, 100, stack, NULL );
	symbol = ( SYMBOL_INFO* )calloc( sizeof( SYMBOL_INFO ) + 256 * sizeof( char ), 1 );
	symbol->MaxNameLen = 255;
	symbol->SizeOfStruct = sizeof( SYMBOL_INFO );

	IMAGEHLP_LINE64* line = ( IMAGEHLP_LINE64* )malloc( sizeof( IMAGEHLP_LINE64 ) );
	DWORD displacement;

	outputMsg += idStr::Format( "===================CALL STACK====================\n" );
	idStrList stackFrames;
	for ( int i = 1; i < frames; i++ )
	{
		SymFromAddr( process, ( DWORD64 )( stack[i] ), 0, symbol );

		memset( line, 0, sizeof( IMAGEHLP_LINE64 ) );
		line->SizeOfStruct = sizeof( IMAGEHLP_LINE64 );
		if ( SymGetLineFromAddr64( process, ( DWORD64 )( stack[i] ), &displacement, line ) )
		{
			stackFrames.Append( idStr::Format( "%i: %s - %s:%lu\n", frames - i - 1, symbol->Name, line->FileName, line->LineNumber ));
		}
		else
		{
			stackFrames.Append( idStr::Format( "%i: %s - 0x%0llX\n", frames - i - 1, symbol->Name, symbol->Address ) );
		}
	}

	// Try to see if we can find the exception handler frame and omit everything above it
	int userExceptionIdx = -1;
	for ( int i = 0; i < stackFrames.Num(); i++ )
	{
		if ( stackFrames[i].Find( "KiUserExceptionDispatcher" ) != -1 )
		{
			userExceptionIdx = i;
			break;
		}
	}

	for ( int i = userExceptionIdx + 1; i < stackFrames.Num(); i++ )
	{
		outputMsg += stackFrames[i];
	}

	outputMsg += "===================RECENT LOG====================\n";

	outputMsg += console->GetLastLines(win_dumploglines.GetInteger(), true);

	outputMsg += "====================HARDWARE=====================\n";

	idStr cpuVendor, cpuBrand;
	idLib::sys->GetCPUInfo( cpuVendor, cpuBrand );

	outputMsg += idStr::Format( "CPU: %s; %s\n", cpuVendor.c_str(), cpuBrand.c_str() );

	outputMsg += idStr::Format( "Total Memory (MB): %d\n", SDL_GetSystemRAM() );

	outputMsg += idStr::Format( "GPU: %s; %s; %s\n", glConfig.vendor_string, glConfig.renderer_string, glConfig.version_string );

	outputMsg += "=================================================\n";

	// If this handler gets called from the non-main thread, using common->Printf may hang
	if ( isMainThread ) {
		common->Printf( outputMsg.c_str() );

		// Close the game window
		GLimp_Shutdown();
	}

	PlaySound( "SystemExclamation", NULL, SND_ALIAS );

	// Show dialog box
	DialogBoxParam( GetModuleHandle( NULL ), MAKEINTRESOURCE( IDD_CRASHHANDLER ), GetActiveWindow(), ( DLGPROC )CrashHandlerProc, ( LPARAM )&outputMsg );

	free( symbol );
	free( line );

	return EXCEPTION_EXECUTE_HANDLER;
}

// SM: End code for stack traces/catch exceptions
// ==========================================================================================

extern "C" { void _chkstk( int size ); };
void clrstk( void );

/*
====================
TestChkStk
====================
*/
void TestChkStk( void ) {
	int		buffer[0x1000];

	buffer[0] = 1;
}

/*
====================
HackChkStk
====================
*/
void HackChkStk( void ) {
	DWORD	old;
	VirtualProtect( _chkstk, 6, PAGE_EXECUTE_READWRITE, &old );
	*(byte *)_chkstk = 0xe9;
	*(int *)((int)_chkstk+1) = (int)clrstk - (int)_chkstk - 5;

	TestChkStk();
}

/*
====================
GetExceptionCodeInfo
====================
*/
const char *GetExceptionCodeInfo( UINT code ) {
	switch( code ) {
		case EXCEPTION_ACCESS_VIOLATION: return "The thread tried to read from or write to a virtual address for which it does not have the appropriate access.";
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "The thread tried to access an array element that is out of bounds and the underlying hardware supports bounds checking.";
		case EXCEPTION_BREAKPOINT: return "A breakpoint was encountered.";
		case EXCEPTION_DATATYPE_MISALIGNMENT: return "The thread tried to read or write data that is misaligned on hardware that does not provide alignment. For example, 16-bit values must be aligned on 2-byte boundaries; 32-bit values on 4-byte boundaries, and so on.";
		case EXCEPTION_FLT_DENORMAL_OPERAND: return "One of the operands in a floating-point operation is denormal. A denormal value is one that is too small to represent as a standard floating-point value.";
		case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "The thread tried to divide a floating-point value by a floating-point divisor of zero.";
		case EXCEPTION_FLT_INEXACT_RESULT: return "The result of a floating-point operation cannot be represented exactly as a decimal fraction.";
		case EXCEPTION_FLT_INVALID_OPERATION: return "This exception represents any floating-point exception not included in this list.";
		case EXCEPTION_FLT_OVERFLOW: return "The exponent of a floating-point operation is greater than the magnitude allowed by the corresponding type.";
		case EXCEPTION_FLT_STACK_CHECK: return "The stack overflowed or underflowed as the result of a floating-point operation.";
		case EXCEPTION_FLT_UNDERFLOW: return "The exponent of a floating-point operation is less than the magnitude allowed by the corresponding type.";
		case EXCEPTION_ILLEGAL_INSTRUCTION: return "The thread tried to execute an invalid instruction.";
		case EXCEPTION_IN_PAGE_ERROR: return "The thread tried to access a page that was not present, and the system was unable to load the page. For example, this exception might occur if a network connection is lost while running a program over the network.";
		case EXCEPTION_INT_DIVIDE_BY_ZERO: return "The thread tried to divide an integer value by an integer divisor of zero.";
		case EXCEPTION_INT_OVERFLOW: return "The result of an integer operation caused a carry out of the most significant bit of the result.";
		case EXCEPTION_INVALID_DISPOSITION: return "An exception handler returned an invalid disposition to the exception dispatcher. Programmers using a high-level language such as C should never encounter this exception.";
		case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "The thread tried to continue execution after a noncontinuable exception occurred.";
		case EXCEPTION_PRIV_INSTRUCTION: return "The thread tried to execute an instruction whose operation is not allowed in the current machine mode.";
		case EXCEPTION_SINGLE_STEP: return "A trace trap or other single-instruction mechanism signaled that one instruction has been executed.";
		case EXCEPTION_STACK_OVERFLOW: return "The thread used up its stack.";
		default: return "Unknown exception";
	}
}

/*
====================
EmailCrashReport

  emailer originally from Raven/Quake 4
====================
*/
void EmailCrashReport( LPSTR messageText ) {
#if 0 // QC disables the exception handler anyway -flibit
	LPMAPISENDMAIL	MAPISendMail;
	MapiMessage		message;
	static int lastEmailTime = 0;

	if ( Sys_Milliseconds() < lastEmailTime + 10000 ) {
		return;
	}

	lastEmailTime = Sys_Milliseconds();

	HINSTANCE mapi = LoadLibrary( "MAPI32.DLL" ); 
	if( mapi ) {
		MAPISendMail = ( LPMAPISENDMAIL )GetProcAddress( mapi, "MAPISendMail" );
		if( MAPISendMail ) {
			MapiRecipDesc toProgrammers =
			{
				0,										// ulReserved
					MAPI_TO,							// ulRecipClass
					"DOOM 3 Crash",						// lpszName
					"SMTP:programmers@idsoftware.com",	// lpszAddress
					0,									// ulEIDSize
					0									// lpEntry
			};

			memset( &message, 0, sizeof( message ) );
			message.lpszSubject = "DOOM 3 Fatal Error";
			message.lpszNoteText = messageText;
			message.nRecipCount = 1;
			message.lpRecips = &toProgrammers;

			MAPISendMail(
				0,									// LHANDLE lhSession
				0,									// ULONG ulUIParam
				&message,							// lpMapiMessage lpMessage
				MAPI_DIALOG,						// FLAGS flFlags
				0									// ULONG ulReserved
				);
		}
		FreeLibrary( mapi );
	}
#endif
}

int Sys_FPU_PrintStateFlags( char *ptr, int ctrl, int stat, int tags, int inof, int inse, int opof, int opse );

/*
====================
_except_handler
====================
*/
EXCEPTION_DISPOSITION __cdecl _except_handler( struct _EXCEPTION_RECORD *ExceptionRecord, void * EstablisherFrame,
												struct _CONTEXT *ContextRecord, void * DispatcherContext ) {

	static char msg[ 8192 ];
	char FPUFlags[2048];

	Sys_FPU_PrintStateFlags( FPUFlags, ContextRecord->FloatSave.ControlWord,
										ContextRecord->FloatSave.StatusWord,
										ContextRecord->FloatSave.TagWord,
										ContextRecord->FloatSave.ErrorOffset,
										ContextRecord->FloatSave.ErrorSelector,
										ContextRecord->FloatSave.DataOffset,
										ContextRecord->FloatSave.DataSelector );


	sprintf( msg, 
		"Please describe what you were doing when DOOM 3 crashed!\n"
		"If this text did not pop into your email client please copy and email it to programmers@idsoftware.com\n"
			"\n"
			"-= FATAL EXCEPTION =-\n"
			"\n"
			"%s\n"
			"\n"
			"0x%x at address 0x%08x\n"
			"\n"
			"%s\n"
			"\n"
			"EAX = 0x%08x EBX = 0x%08x\n"
			"ECX = 0x%08x EDX = 0x%08x\n"
			"ESI = 0x%08x EDI = 0x%08x\n"
			"EIP = 0x%08x ESP = 0x%08x\n"
			"EBP = 0x%08x EFL = 0x%08x\n"
			"\n"
			"CS = 0x%04x\n"
			"SS = 0x%04x\n"
			"DS = 0x%04x\n"
			"ES = 0x%04x\n"
			"FS = 0x%04x\n"
			"GS = 0x%04x\n"
			"\n"
			"%s\n",
			com_version.GetString(),
			ExceptionRecord->ExceptionCode,
			ExceptionRecord->ExceptionAddress,
			GetExceptionCodeInfo( ExceptionRecord->ExceptionCode ),
			ContextRecord->Eax, ContextRecord->Ebx,
			ContextRecord->Ecx, ContextRecord->Edx,
			ContextRecord->Esi, ContextRecord->Edi,
			ContextRecord->Eip, ContextRecord->Esp,
			ContextRecord->Ebp, ContextRecord->EFlags,
			ContextRecord->SegCs,
			ContextRecord->SegSs,
			ContextRecord->SegDs,
			ContextRecord->SegEs,
			ContextRecord->SegFs,
			ContextRecord->SegGs,
			FPUFlags
		);

	EmailCrashReport( msg );
	common->FatalError( msg );

    // Tell the OS to restart the faulting instruction
    return ExceptionContinueExecution;
}

#define TEST_FPU_EXCEPTIONS	/*	FPU_EXCEPTION_INVALID_OPERATION |		*/	\
							/*	FPU_EXCEPTION_DENORMALIZED_OPERAND |	*/	\
							/*	FPU_EXCEPTION_DIVIDE_BY_ZERO |			*/	\
							/*	FPU_EXCEPTION_NUMERIC_OVERFLOW |		*/	\
							/*	FPU_EXCEPTION_NUMERIC_UNDERFLOW |		*/	\
							/*	FPU_EXCEPTION_INEXACT_RESULT |			*/	\
								0



#if		defined(MACOS_X)
#define UMP_SKU "OSX"
#elif	defined( __linux__ )
#define UMP_SKU "Linux"
#else
#define UMP_SKU "Win32"
#endif



void MiniDumpFunction( unsigned int nExceptionCode, EXCEPTION_POINTERS *pException )
{
#ifdef STEAM
	SteamAPI_SetMiniDumpComment( va("Crash comment\ndate: %s %s\nsku: %s", __DATE__, __TIME__, UMP_SKU) );


	//GENERATE BUILD NUMBER 

	//build year.
	idStr year = __DATE__;
	year = year.Mid(year.Length() - 2 , 2 );

	//get month.
	idStr month;
	if (__DATE__[0] == 'J' && __DATE__[1] == 'a' && __DATE__[2] == 'n')			month = "01";		
	else if (__DATE__[0] == 'F' && __DATE__[1] == 'e' && __DATE__[2] == 'b')	month = "02";
	else if (__DATE__[0] == 'M' && __DATE__[1] == 'a' && __DATE__[2] == 'r')	month = "03";
	else if (__DATE__[0] == 'A' && __DATE__[1] == 'p' && __DATE__[2] == 'r')	month = "04";
	else if (__DATE__[0] == 'M' && __DATE__[1] == 'a' && __DATE__[2] == 'y')	month = "05";
	else if (__DATE__[0] == 'J' && __DATE__[1] == 'u' && __DATE__[2] == 'n')	month = "06";
	else if (__DATE__[0] == 'J' && __DATE__[1] == 'u' && __DATE__[2] == 'l')	month = "07";
	else if (__DATE__[0] == 'A' && __DATE__[1] == 'u' && __DATE__[2] == 'g')	month = "08";
	else if (__DATE__[0] == 'S' && __DATE__[1] == 'e' && __DATE__[2] == 'p')	month = "09";
	else if (__DATE__[0] == 'O' && __DATE__[1] == 'c' && __DATE__[2] == 't')	month = "10";
	else if (__DATE__[0] == 'N' && __DATE__[1] == 'o' && __DATE__[2] == 'v')	month = "11";
	else {month = "12";}
	
	//get day.
	idStr day = __DATE__;
	day = day.Mid(4 , 2 );
	day.StripLeading( ' ' );
	if (day.Length() <= 1)
		day = va("0%s", day.c_str()); //leading zero.


	//get hour.... limited to one digit because build # has a limited amount of digits
	int colonIndex;
	int k;
	idStr timestamp = __TIME__;
	for (k = 0; k < timestamp.Length(); k++)
	{
		if (timestamp[k] == ':')
		{
			colonIndex = k;
			break;
		}
	}
	timestamp = timestamp.Mid(0, colonIndex);
	float hour = atof(timestamp);
	float adjustedHour = hour / 24.0f;
	int intHour = idMath::ClampInt(0,9,adjustedHour * 10);
	
	idStr buildnumber = va("%s%s%s%d", year.c_str(), month.c_str(), day.c_str(), intHour);
	uint result;
	result = atoi( buildnumber.c_str() );


	SteamAPI_WriteMiniDump( nExceptionCode, pException, result );
	#endif
}



/*
==================
WinMain
==================
*/
int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow ) {

	const HCURSOR hcurSave = ::SetCursor( LoadCursor( 0, IDC_WAIT ) );

	Sys_SetPhysicalWorkMemory( 192 << 20, 1024 << 20 );

	Sys_GetCurrentMemoryStatus( exeLaunchMemoryStats );

	


#if 0
    DWORD handler = (DWORD)_except_handler;
    __asm
    {                           // Build EXCEPTION_REGISTRATION record:
        push    handler         // Address of handler function
        push    FS:[0]          // Address of previous handler
        mov     FS:[0],ESP      // Install new EXECEPTION_REGISTRATION
    }
#endif


	win32.hInstance = hInstance;
	idStr::Copynz( sys_cmdline, lpCmdLine, sizeof( sys_cmdline ) );

#ifndef _DEBUG
	_set_se_translator( MiniDumpFunction );
#endif

	// done before Com/Sys_Init since we need this for error output
	Sys_CreateConsole();

	// no abort/retry/fail errors
	SetErrorMode( SEM_FAILCRITICALERRORS );

	for ( int i = 0; i < MAX_CRITICAL_SECTIONS; i++ ) {
		InitializeCriticalSection( &win32.criticalSections[i] );
	}

	// get the initial time base
	Sys_Milliseconds();

#ifdef DEBUG
	// disable the painfully slow MS heap check every 1024 allocs
	_CrtSetDbgFlag( 0 );
#endif

//	Sys_FPU_EnableExceptions( TEST_FPU_EXCEPTIONS );
	Sys_FPU_SetPrecision( FPU_PRECISION_DOUBLE_EXTENDED );

	common->Init( 0, NULL, lpCmdLine );

#if TEST_FPU_EXCEPTIONS != 0
	common->Printf( Sys_FPU_GetState() );
#endif

#ifndef	ID_DEDICATED
	if ( win32.win_notaskkeys.GetInteger() ) {
		DisableTaskKeys( TRUE, FALSE, /*( win32.win_notaskkeys.GetInteger() == 2 )*/ FALSE );
	}
#endif

	Sys_StartAsyncThread();

	// hide or show the early console as necessary
	if ( win32.win_viewlog.GetInteger() || com_skipRenderer.GetBool() || idAsyncNetwork::serverDedicated.GetInteger() ) {
		Sys_ShowConsole( 1, true );
	} else {
		Sys_ShowConsole( 0, false );
	}

#ifdef SET_THREAD_AFFINITY 
	// give the main thread an affinity for the first cpu
	SetThreadAffinityMask( GetCurrentThread(), 1 );
#endif

	::SetCursor( hcurSave );

	// Launch the script debugger
	if ( strstr( lpCmdLine, "+debugger" ) ) {
		// DebuggerClientInit( lpCmdLine );
		return 0;
	}

	::SetFocus( win32.hWnd );


    // main game loop
	while( 1 ) {



		Win_Frame();

#ifdef DEBUG
		Sys_MemFrame();
#endif

		// set exceptions, even if some crappy syscall changes them!
		Sys_FPU_EnableExceptions( TEST_FPU_EXCEPTIONS );

#if ENABLE_FP_EXCEPTIONS
		FPExceptionEnabler enabled(_EM_UNDERFLOW | _EM_OVERFLOW | _EM_ZERODIVIDE | _EM_INVALID);
#endif

#ifdef ID_ALLOW_TOOLS
		if ( com_editors ) {
			if ( com_editors & EDITOR_GUI ) {
				// GUI editor
				GUIEditorRun();
			} else if ( com_editors & EDITOR_RADIANT ) {
				// Level Editor
				RadiantRun();
			}
			else if (com_editors & EDITOR_MATERIAL ) {
				//BSM Nerve: Add support for the material editor
				MaterialEditorRun();
			}
			else {
				if ( com_editors & EDITOR_LIGHT ) {
					// in-game Light Editor
					LightEditorRun();
				}
				if ( com_editors & EDITOR_SOUND ) {
					// in-game Sound Editor
					SoundEditorRun();
				}
				if ( com_editors & EDITOR_DECL ) {
					// in-game Declaration Browser
					DeclBrowserRun();
				}
				if ( com_editors & EDITOR_AF ) {
					// in-game Articulated Figure Editor
					AFEditorRun();
				}
				if ( com_editors & EDITOR_PARTICLE ) {
					// in-game Particle Editor
					ParticleEditorRun();
				}
				if ( com_editors & EDITOR_SCRIPT ) {
					// in-game Script Editor
					ScriptEditorRun();
				}
				if ( com_editors & EDITOR_PDA ) {
					// in-game PDA Editor
					PDAEditorRun();
				}
			}
		}
#endif

		// run the game
		common->Frame();
	}

	// never gets here
	return 0;
}

/*
====================
clrstk

I tried to get the run time to call this at every function entry, but
====================
*/
static int	parmBytes;
__declspec( naked ) void clrstk( void ) {
	// eax = bytes to add to stack
	__asm {
		mov		[parmBytes],eax
        neg     eax                     ; compute new stack pointer in eax
        add     eax,esp
        add     eax,4
        xchg    eax,esp
        mov     eax,dword ptr [eax]		; copy the return address
        push    eax
        
        ; clear to zero
        push	edi
        push	ecx
        mov		edi,esp
        add		edi,12
        mov		ecx,[parmBytes]
		shr		ecx,2
        xor		eax,eax
		cld
        rep	stosd
        pop		ecx
        pop		edi
        
        ret
	}
}

/*
==================
idSysLocal::OpenURL
==================
*/
void idSysLocal::OpenURL( const char *url, bool doexit ) {
	static bool doexit_spamguard = false;
	HWND wnd;

	if (doexit_spamguard) {
		common->DPrintf( "OpenURL: already in an exit sequence, ignoring %s\n", url );
		return;
	}

	common->Printf("Open URL: %s\n", url);

	if ( !ShellExecute( NULL, "open", url, NULL, NULL, SW_RESTORE ) ) {
		common->Error( "Could not open url: '%s' ", url );
		return;
	}

	wnd = GetForegroundWindow();
	if ( wnd ) {
		ShowWindow( wnd, SW_MAXIMIZE );
	}

	if ( doexit ) {
		doexit_spamguard = true;
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
	}
}

/*
==================
idSysLocal::StartProcess
==================
*/
void idSysLocal::StartProcess( const char *exePath, bool doexit ) {
	TCHAR				szPathOrig[_MAX_PATH];
	STARTUPINFO			si;
	PROCESS_INFORMATION	pi;

	ZeroMemory( &si, sizeof(si) );
	si.cb = sizeof(si);

	strncpy( szPathOrig, exePath, _MAX_PATH );

	if( !CreateProcess( NULL, szPathOrig, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ) ) {
        common->Error( "Could not start process: '%s' ", szPathOrig );
	    return;
	}

	if ( doexit ) {
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
	}
}

/*
==================
Sys_SetFatalError
==================
*/
void Sys_SetFatalError( const char *error ) {
}

/*
==================
Sys_DoPreferences
==================
*/
void Sys_DoPreferences( void ) {
}


