#if defined RW_GL3 && defined __IPHONEOS__

#include <sys/sysctl.h>
#include <mach/mach_host.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <locale.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include "common.h"
#include "rwcore.h"
#include "skeleton.h"
#include "platform.h"
#include "crossplatform.h"

#include "main.h"
#include "FileMgr.h"
#include "Text.h"
#include "Pad.h"
#include "Timer.h"
#include "DMAudio.h"
#include "sampman.h"
#include "ControllerConfig.h"
#include "Frontend.h"
#include "Game.h"
#include "PCSave.h"
#include "MemoryCard.h"
#include "Sprite2d.h"
#include "AnimViewer.h"
#include "Font.h"
#include "MemoryMgr.h"

#include <SDL.h>
#include <SDL_main.h>
#include <SDL_filesystem.h>
#undef main

#define MAX_SUBSYSTEMS (16)

rw::EngineOpenParams openParams;

static RwBool RwInitialised = FALSE;
static RwBool ForegroundApp = TRUE;

static RwSubSystemInfo GsubSysInfo[MAX_SUBSYSTEMS];
static RwInt32 GnumSubSystems = 0;
static RwInt32 GcurSel = 0, GcurSelVM = 0;

static RwBool useDefault;

#ifndef IMPROVED_VIDEOMODE
static RwBool defaultFullscreenRes = TRUE;
#else
static RwBool defaultFullscreenRes = FALSE;
static RwInt32 bestWndMode = -1;
#endif

static psGlobalType PsGlobal;
#define PSGLOBAL(var) (((psGlobalType *)(RsGlobal.ps))->var)

size_t _dwMemAvailPhys;
RwUInt32 gGameState;

long _dwOperatingSystemVersion;

struct iosTouch {
	bool down;
	long id;
	float sx, sy;
	float cx, cy;
};

static iosTouch g_touchMove;
static iosTouch g_touchCam;
static iosTouch g_touchBtn;

#define BTN_X0 0.55f
#define BTN_Y0 0.62f
#define BTN_W  0.45f
#define BTN_H  0.38f

// menu navigation: virtual mouse for frontend
struct iosVMouse { bool down; };
static iosVMouse g_vmouse;

extern "C" int SDL_main(int argc, char **argv);
int gameRun(int argc, char *argv[]);

int SDL_main(int argc, char **argv)
{
	const char *basePath = SDL_GetBasePath();
	if (basePath)
		chdir(basePath);
	return gameRun(argc, argv);
}

/* -------------------------------------------------------------------------
 * filesystem helpers
 * ------------------------------------------------------------------------- */
void _psCreateFolder(const char *path)
{
	struct stat info;
	char fullpath[PATH_MAX];
	realpath(path, fullpath);

	if (lstat(fullpath, &info) != 0) {
		if (errno == ENOENT || (errno != EACCES && !S_ISDIR(info.st_mode)))
			mkdir(fullpath, 0755);
	}
}

const char *_psGetUserFilesFolder()
{
	static char szUserFiles[512];
	const char *home = getenv("HOME");
	snprintf(szUserFiles, sizeof(szUserFiles), "%s/userfiles", home ? home : "/");
	_psCreateFolder(szUserFiles);
	return szUserFiles;
}

RwBool IsForegroundApp() { return ForegroundApp; }

/* -------------------------------------------------------------------------
 * ps* skeleton functions
 * ------------------------------------------------------------------------- */
RwBool psCameraBeginUpdate(RwCamera *camera)
{
	if (!RwCameraBeginUpdate(Scene.camera))
	{
		ForegroundApp = FALSE;
		RsEventHandler(rsACTIVATE, (void *)FALSE);
		return FALSE;
	}
	return TRUE;
}

void psCameraShowRaster(RwCamera *camera)
{
#ifdef LEGACY_MENU_OPTIONS
	if (FrontEndMenuManager.m_PrefsVsync || FrontEndMenuManager.m_bMenuActive)
#else
	if (FrontEndMenuManager.m_PrefsFrameLimiter || FrontEndMenuManager.m_bMenuActive)
#endif
		RwCameraShowRaster(camera, PSGLOBAL(window), rwRASTERFLIPWAITVSYNC);
	else
		RwCameraShowRaster(camera, PSGLOBAL(window), rwRASTERFLIPDONTWAIT);
}

RwImage *psGrabScreen(RwCamera *pCamera)
{
	rw::Image *image = RwCameraGetRaster(pCamera)->toImage();
	image->removeMask();
	return image;
}

double psTimer(void)
{
	struct timespec start;
	clock_gettime(CLOCK_MONOTONIC_RAW, &start);
	return start.tv_sec * 1000.0 + start.tv_nsec / 1000000.0;
}

void psMouseSetPos(RwV2d *pos) { }

RwMemoryFunctions *psGetMemoryFunctions(void)
{
#ifdef USE_CUSTOM_ALLOCATOR
	return &memFuncs;
#else
	return nil;
#endif
}

RwBool psInstallFileSystem(void) { return TRUE; }
RwBool psNativeTextureSupport(void) { return true; }

void InitialiseLanguage()
{
	setlocale(LC_ALL, "");
	char *systemLang = setlocale(LC_ALL, NULL);
	short lang = !strncmp(systemLang, "fr_", 3) ? LANG_FRENCH :
				 !strncmp(systemLang, "de_", 3) ? LANG_GERMAN :
				 !strncmp(systemLang, "en_", 3) ? LANG_ENGLISH :
				 !strncmp(systemLang, "it_", 3) ? LANG_ITALIAN :
				 !strncmp(systemLang, "es_", 3) ? LANG_SPANISH :
				 LANG_OTHER;

	if (lang == LANG_ITALIAN)
		FrontEndMenuManager.m_PrefsLanguage = CMenuManager::LANGUAGE_ITALIAN;
	else if (lang == LANG_SPANISH)
		FrontEndMenuManager.m_PrefsLanguage = CMenuManager::LANGUAGE_SPANISH;
	else if (lang == LANG_GERMAN) {
		CGame::nastyGame = false;
		FrontEndMenuManager.m_PrefsAllowNastyGame = false;
		CGame::germanGame = true;
		FrontEndMenuManager.m_PrefsLanguage = CMenuManager::LANGUAGE_GERMAN;
	} else if (lang == LANG_FRENCH) {
		CGame::nastyGame = false;
		FrontEndMenuManager.m_PrefsAllowNastyGame = false;
		CGame::frenchGame = true;
		FrontEndMenuManager.m_PrefsLanguage = CMenuManager::LANGUAGE_FRENCH;
	} else
		FrontEndMenuManager.m_PrefsLanguage = CMenuManager::LANGUAGE_AMERICAN;

	FrontEndMenuManager.OS_Language = lang;

	setlocale(LC_CTYPE, "C");
	setlocale(LC_COLLATE, "C");
	setlocale(LC_NUMERIC, "C");

	TheText.Unload();
	TheText.Load();
}

RwBool psInitialize(void)
{
	RsGlobal.ps = &PsGlobal;

	PsGlobal.fullScreen = TRUE;
	PsGlobal.cursorIsInWindow = FALSE;
	PsGlobal.joy1id = -1;
	PsGlobal.joy2id = -1;

	CFileMgr::Initialise();

	C_PcSave::SetSaveDirectory(_psGetUserFilesFolder());

	InitialiseLanguage();

	gGameState = GS_START_UP;
	TRACE("gGameState = GS_START_UP");

	{
		uint64_t size = 0, page_size = 0;
		size_t len = sizeof(uint64_t);
		sysctl((int[]){ CTL_HW, HW_PAGESIZE }, 2, &page_size, &len, NULL, 0);
		sysctl((int[]){ CTL_HW, HW_MEMSIZE }, 2, &size, &len, NULL, 0);
		vm_statistics_data_t vm_stat;
		mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
		host_statistics(mach_host_self(), HOST_VM_INFO, (host_info_t)&vm_stat, &count);
		_dwMemAvailPhys = (size_t)(vm_stat.free_count * page_size);
		debug("Physical memory size %llu\n", (unsigned long long)size);
	}

	_dwOperatingSystemVersion = OS_WINXP;

	FrontEndMenuManager.LoadSettings();

	TheText.Unload();

	return TRUE;
}

void psTerminate(void) { return; }

/* input stubs */
void _InputInitialiseJoys() { PSGLOBAL(joy1id) = -1; PSGLOBAL(joy2id) = -1; }
void _InputShutdownMouse() { }
bool _InputMouseNeedsExclusive() { return false; }
long _InputInitialiseMouse(bool) { return 0; }
void joysChangeCB(int, int) { }

extern "C" void _InputTranslateShiftKeyUpDown(RsKeyCodes *rs) { }
void CapturePad(RwInt32 padID) { }
extern "C" RwInt32 _psGetNumVideModes() { return 1; }
extern "C" RwChar** _psGetVideoModeList() { return NULL; }
extern "C" void _psSelectScreenVM(RwInt32 videoMode) { }

void cSampleManager::SetMP3BoostVolume(uint8 nVolume) { }

void TerminateHandler(int, siginfo_t*, void*) { RsGlobal.quit = TRUE; }
void HandleExit() { }

/* -------------------------------------------------------------------------
 * pad <-> touch
 * ------------------------------------------------------------------------- */
static void ApplyTouchToPads(void)
{
	CPad *pad = CPad::GetPad(0);
	if (!pad) return;

	// clear joystick input each frame to avoid ghost inputs
	pad->PCTempJoyState.Square = 0;
	pad->PCTempJoyState.Triangle = 0;
	pad->PCTempJoyState.Cross = 0;
	pad->PCTempJoyState.Circle = 0;
	pad->PCTempJoyState.LeftStickX = 0;
	pad->PCTempJoyState.LeftStickY = 0;
	pad->PCTempJoyState.RightStickX = 0;
	pad->PCTempJoyState.RightStickY = 0;
	pad->PCTempJoyState.DPadUp = 0;
	pad->PCTempJoyState.DPadDown = 0;
	pad->PCTempJoyState.DPadLeft = 0;
	pad->PCTempJoyState.DPadRight = 0;

	if (g_touchMove.down) {
		float dx = g_touchMove.cx - g_touchMove.sx;
		float dy = g_touchMove.cy - g_touchMove.sy;
		int lx = (int)(dx * 320.0f);
		int ly = (int)(dy * 320.0f);
		if (lx > 128) lx = 128; if (lx < -128) lx = -128;
		if (ly > 128) ly = 128; if (ly < -128) ly = -128;
		pad->PCTempJoyState.LeftStickX = -ly;	// up = forward (negative Y)
		pad->PCTempJoyState.LeftStickY = ly;
	}
	if (g_touchCam.down) {
		float dx = g_touchCam.cx - g_touchCam.sx;
		float dy = g_touchCam.cy - g_touchCam.sy;
		int rx = (int)(dx * 150.0f);
		int ry = (int)(dy * 150.0f);
		if (rx > 128) rx = 128; if (rx < -128) rx = -128;
		if (ry > 128) ry = 128; if (ry < -128) ry = -128;
		pad->PCTempJoyState.RightStickX = rx;
		pad->PCTempJoyState.RightStickY = ry;
	}
	if (g_touchBtn.down) {
		float x = g_touchBtn.cx, y = g_touchBtn.cy;
		if (x >= BTN_X0 && x <= 1.0f && y >= BTN_Y0 && y <= 1.0f) {
			float nx = (x - BTN_X0) / BTN_W;
			float ny = (y - BTN_Y0) / BTN_H;
			if (nx < 0.4f && ny < 0.45f)	pad->PCTempJoyState.Square = 255;	// brake
			else if (nx < 0.4f)				pad->PCTempJoyState.Cross = 255;	// jump
			else if (ny < 0.45f)			pad->PCTempJoyState.Circle = 255;	// fire
			else							pad->PCTempJoyState.Triangle = 255; // special
		}
	}
}

static void HandleTouch(const SDL_Event &ev)
{
	float x = ev.tfinger.x, y = ev.tfinger.y;
	long id = ev.tfinger.fingerId;

	iosTouch *t = NULL;
	if (ev.type == SDL_FINGERDOWN) {
		if (x < 0.45f && y > 0.45f) t = &g_touchMove;
		else if (x >= 0.45f && y < BTN_Y0) t = &g_touchCam;
		else t = &g_touchBtn;
		t->down = true;
		t->id = id;
		t->sx = x; t->sy = y;
		t->cx = x; t->cy = y;
	} else {
		if (g_touchMove.down && g_touchMove.id == id) t = &g_touchMove;
		else if (g_touchCam.down && g_touchCam.id == id) t = &g_touchCam;
		else if (g_touchBtn.down && g_touchBtn.id == id) t = &g_touchBtn;
		if (!t) return;
		if (ev.type == SDL_FINGERUP) t->down = false;
		t->cx = x; t->cy = y;
	}

	// frontend: map tap to virtual mouse for the menu
	if (gGameState == GS_FRONTEND) {
		CPad *pad = CPad::GetPad(0);
		if (ev.type != SDL_FINGERMOTION || t == &g_touchBtn) {
			FrontEndMenuManager.m_nMouseTempPosX = (int)(x * RsGlobal.maximumWidth);
			FrontEndMenuManager.m_nMouseTempPosY = (int)(y * RsGlobal.maximumHeight);
		}
		if (pad) {
			CMouseControllerState ms;
			ms.Clear();
			ms.LMB = (ev.type != SDL_FINGERUP);
			CPad::PCTempMouseControllerState = ms;
		}
	}
}

/* cache touch struct declarations */
// (declarations minimal)

RwBool
_psSetVideoMode(RwInt32 subSystem, RwInt32 videoMode)
{
	RwInitialised = FALSE;

	RsEventHandler(rsRWTERMINATE, nil);

	GcurSelVM = videoMode;
	GcurSel = subSystem;
	useDefault = TRUE;

	if (RsEventHandler(rsRWINITIALIZE, &openParams) == rsEVENTERROR)
		return FALSE;

	RwInitialised = TRUE;
	useDefault = FALSE;

	RwRect r;
	r.x = 0; r.y = 0;
	r.w = RsGlobal.maximumWidth;
	r.h = RsGlobal.maximumHeight;
	RsEventHandler(rsCAMERASIZE, &r);

	// Make sure all keys are released
	CPad::GetPad(0)->Clear(true);
	CPad::GetPad(1)->Clear(true);

	return TRUE;
}

RwBool
psSelectDevice(void)
{
	RwVideoMode vm;
	RwInt32 subSysNum;

	GnumSubSystems = RwEngineGetNumSubSystems();
	if (!GnumSubSystems)
		return FALSE;
	GnumSubSystems = (GnumSubSystems > MAX_SUBSYSTEMS) ? MAX_SUBSYSTEMS : GnumSubSystems;

	for (subSysNum = 0; subSysNum < GnumSubSystems; subSysNum++)
		RwEngineGetSubSystemInfo(&GsubSysInfo[subSysNum], subSysNum);

	GcurSel = RwEngineGetCurrentSubSystem();

	if (!RwEngineSetSubSystem(GcurSel))
		return FALSE;

	// find the largest exclusive video mode
	RwInt32 bestVM = 0, bestW = 0, bestH = 0;
	for (RwInt32 i = 0; i < RwEngineGetNumVideoModes(); i++) {
		RwEngineGetVideoModeInfo(&vm, i);
		if ((vm.flags & rwVIDEOMODEEXCLUSIVE) && vm.width >= bestW) {
			bestW = vm.width;
			bestH = vm.height;
			bestVM = i;
		}
	}
	GcurSelVM = bestVM;

	RwEngineGetVideoModeInfo(&vm, GcurSelVM);

	FrontEndMenuManager.m_nCurrOption = 0;

	if (!RwEngineSetVideoMode(GcurSelVM)) {
		printf("psSelectDevice: RwEngineSetVideoMode failed\n");
		return FALSE;
	}

	RsGlobal.maximumWidth = vm.width;
	RsGlobal.maximumHeight = vm.height;
	RsGlobal.width = vm.width;
	RsGlobal.height = vm.height;
	PsGlobal.fullScreen = TRUE;

	return TRUE;
}

/* -------------------------------------------------------------------------
 * game loop
 * ------------------------------------------------------------------------- */
int
gameRun(int argc, char *argv[])
{
	int i;

#ifdef USE_CUSTOM_ALLOCATOR
	InitMemoryMgr();
#endif

#ifndef FLUSHABLE_STREAMING
	struct sigaction act;
	act.sa_sigaction = TerminateHandler;
	act.sa_flags = SA_SIGINFO;
	sigaction(SIGTERM, &act, NULL);
#endif

	if (RsEventHandler(rsINITIALIZE, nil) == rsEVENTERROR)
		return FALSE;

	for (i = 1; i < argc; i++)
		RsEventHandler(rsPREINITCOMMANDLINE, argv[i]);

	openParams.width = RsGlobal.maximumWidth;
	openParams.height = RsGlobal.maximumHeight;
	openParams.windowtitle = RsGlobal.appName;
	SDL_Window *iosWindow = NULL;
	openParams.window = &iosWindow;

	ControlsManager.MakeControllerActionsBlank();
	ControlsManager.InitDefaultControlConfiguration();

	if (rsEVENTERROR == RsEventHandler(rsRWINITIALIZE, &openParams)) {
		RsEventHandler(rsTERMINATE, nil);
		return 0;
	}

	{
		RwRect r;
		r.x = 0; r.y = 0;
		r.w = RsGlobal.maximumWidth;
		r.h = RsGlobal.maximumHeight;
		RsEventHandler(rsCAMERASIZE, &r);
	}

	_InputInitialiseJoys();
	_InputInitialiseMouse(false);

	ControlsManager.InitDefaultControlConfigMouse(MousePointerStateHelper.GetMouseSetUp());

	for (i = 1; i < argc; i++)
		RsEventHandler(rsCOMMANDLINE, argv[i]);

	while (!RsGlobal.quit) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT || event.type == SDL_APP_TERMINATING) {
				RsGlobal.quit = TRUE;
			} else if (event.type == SDL_WINDOWEVENT) {
				if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED
				 && RwInitialised && gGameState == GS_PLAYING_GAME) {
					RwRect r;
					r.x = 0; r.y = 0;
					r.w = event.window.data1;
					r.h = event.window.data2;
					RsGlobal.maximumWidth = r.w;
					RsGlobal.maximumHeight = r.h;
					RsEventHandler(rsCAMERASIZE, &r);
				}
			} else if (event.type == SDL_FINGERDOWN
					|| event.type == SDL_FINGERMOTION
					|| event.type == SDL_FINGERUP) {
				HandleTouch(event);
			}
		}

		CPad::UpdatePads();
		ApplyTouchToPads();

		if (ForegroundApp) {
			switch (gGameState) {
			case GS_START_UP:
			{
				if (!CGame::InitialiseOnceBeforeRW())
					return 0;
				gGameState = GS_INIT_ONCE;
				break;
			}
			case GS_INIT_ONCE:
			{
				LoadingScreen(nil, nil, "loadsc0");
				if (!CGame::InitialiseOnceAfterRW())
					RsGlobal.quit = TRUE;
				gGameState = GS_INIT_FRONTEND;
				break;
			}
			case GS_INIT_FRONTEND:
			{
				LoadingScreen(nil, nil, "loadsc0");
				FrontEndMenuManager.m_bGameNotLoaded = true;
				FrontEndMenuManager.m_bStartUpFrontEndRequested = true;
				gGameState = GS_FRONTEND;
				break;
			}
			case GS_FRONTEND:
			{
				RsEventHandler(rsFRONTENDIDLE, nil);

				if (FrontEndMenuManager.m_bWantToLoad) {
					InitialiseGame();
					FrontEndMenuManager.m_bGameNotLoaded = false;
					gGameState = GS_PLAYING_GAME;
				}
				break;
			}
			case GS_PLAYING_GAME:
			{
				float ms = (float)CTimer::GetCurrentTimeInCycles() / (float)CTimer::GetCyclesPerMillisecond();
				if (RwInitialised) {
					if (!FrontEndMenuManager.m_PrefsFrameLimiter
					 || (1000.0f / (float)RsGlobal.maxFPS) < ms)
						RsEventHandler(rsIDLE, (void *)TRUE);
				}
				break;
			}
			}
		} else {
			if (RwCameraBeginUpdate(Scene.camera)) {
				RwCameraEndUpdate(Scene.camera);
				ForegroundApp = TRUE;
				RsEventHandler(rsACTIVATE, (void *)TRUE);
			}
		}
	}

	RwInitialised = FALSE;
	FrontEndMenuManager.UnloadTextures();

	CPad::ResetCheats();
	CPad::StopPadsShaking();

	DMAudio.ChangeMusicMode(MUSICMODE_DISABLE);
	CTimer::Stop();

	if (gGameState == GS_PLAYING_GAME)
		CGame::ShutDown();

	DMAudio.Terminate();
	RsEventHandler(rsRWTERMINATE, nil);
	RsEventHandler(rsTERMINATE, nil);
	return 0;
}

#endif // __IPHONEOS__
