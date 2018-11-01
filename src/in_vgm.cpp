//-----------------------------------------------------------------
// in_vgm_2
// VGM audio input plugin for Winamp
// http://www.smspower.org/music
// by Maxim <maxim\x40smspower\x2eorg> in 2001-2009
// with help from BlackAura in March and April 2002
// YM2612 PCM additions with Blargg in November 2005
//-----------------------------------------------------------------

// let me use #pragma region
#pragma warning(disable:4068)

#include <windows.h>

#include <string>
#include <xstring>
#include <algorithm>
#include <sstream>
#include <iomanip>

#include "Winamp SDK/Winamp/in2.h"
#include "Winamp SDK/Winamp/wa_ipc.h"
#include "Winamp SDK/Winamp/ipc_pe.h"
#include "GlobalConfig.h"

// Exported functions must have this decoration
#define DLLEXPORT extern "C" __declspec(dllexport)

#include "PluginConfig.h"
//#include "PluginConfigDialog.h"
#include "VGMPlayer.h"
#include "VGMMetadata.h"
#include "PluginConfigDialog.h"

// Winamp likes to get 576 samples at a time
#define SAMPLES_PER_ITERATION 576

#define BETA
#define VERSION "0.1"

#ifdef BETA
#define PLUGINNAME "VGM input plugin v2." VERSION " beta "__DATE__" "__TIME__
#else
#define PLUGINNAME "VGM input plugin v2." VERSION
#endif

#pragma region "Forward Declarations"

// Forward declarations of some functions
// Winamp-exported functions
void ShowConfigDialog(HWND parent);
void ShowAboutDialog(HWND parent);
void Initialise();
void Deinitialise();
void GetFileInfo(const char* filename, char* title, int* lengthInMs);
int ShowInfoDialog(const char* filename, HWND parent);
int IsOurFile(const char* filename);
int Play(const char* filename);
void Pause();
void Unpause();
int IsPaused();
void Stop();
int GetLength();
int GetOutputTime();
void SetOutputTime(int timeInMs);
void SetOutputTime(int time_in_ms);
void SetVolume(int volume);
void SetPan(int pan);
void SetEQ(int on, char data[10], int preamp);

// The decode thread procedure
DWORD WINAPI __stdcall PlaybackThread(void* signal);

// Helpers
const std::string& getINIFilename();
std::string GetAuxiliaryFilename(const std::string& filename);
std::string GetASCIIFilename(const std::wstring& wfilename);
void SetThreadName(DWORD id, const std::string& name);
void PumpToEndOfTrack();
bool GetMetadata(std::wstring& value, const std::string& filename, const std::string& key);
bool SetMetadata(const std::string& filename, const std::string& key, std::wstring& value);
void ApplyPluginConfigToMetadata();

/*
void ApplyPluginConfigToPlayer(PluginConfig* pluginConfig, VGMPlayer* player);
*/

#pragma endregion "Forward Declarations"



#pragma region "Global variables"

HANDLE pluginhInst;
PluginConfig pluginConfig;

// Structure containing all the info on this plugin
In_Module mod =
{
	// Out: version
	IN_VER,
	// Out: description
	PLUGINNAME,
	// In: Winamp main window handle, DLL instance handle
	0, 0,
	// Out: Open file dialog mask
	"vgm;vgz;vgm7z\0VGM Audio File (*.vgm;*.vgz;*.vgm7z)\0",
	// Out: supports seeking
	1,
	// Out: flags
	IN_MODULE_FLAG_USES_OUTPUT_PLUGIN,
	// Out: Pointer to function to show config dialog box
	ShowConfigDialog,
	// Out: Pointer to function to show info dialog box
	ShowAboutDialog,
	// Out: Pointer to initialisation function
	Initialise,
	// Out: Pointer to deinitialisation function
	Deinitialise,
	// Out: Pointer to function to get basic file metadata
	GetFileInfo,
	// Out: Pointer to function to show file info dialog box
	ShowInfoDialog,
	// Out: Pointer to function to check if file is playable by this program	
	IsOurFile,
	// Out: Pointer to function to start playback
	Play,
	// Out: Pointer to function to pause playback
	Pause,
	// Out: Pointer to function to unpause
	Unpause,
	// Out: Pointer to function to return paused status
	IsPaused,
	// Out: Pointer to function to stop playback
	Stop,
	// Out: Pointer to function to get current file length
	GetLength,
	// Out: Pointer to function to get current playback position
	GetOutputTime,
	// Out: Pointer to function to seek to a certain output time
	SetOutputTime,
	// Out: Pointer to function to set the volume 
	SetVolume,
	// Out: Pointer to function to set the panning
	SetPan,
	// In: Pointers to various Winamp functions
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	// Out: pointer to a mysterious function we don't want to implement
	SetEQ,
	// In: pointer to Winamp function to set current file info
	NULL,
	// In: pointer to output plugin info
	NULL
};

//////////////////////////////////////////////////////////////////////////

VGMPlayer* player = NULL;
int isPaused; // we have to keep track of this for some reason

int playbackThreadSignal;
HANDLE playbackThreadHandle = INVALID_HANDLE_VALUE;
int seekTime; // will be >=0 if we want to seek, negative otherwise

short* sampleBuffer = NULL; // we render into here

#pragma endregion "Global variables"




#pragma region "Exported functions"

// DLL entry point
BOOL WINAPI DllMain(HANDLE hInst, ULONG reasonForCall, LPVOID reserved)
{
	pluginhInst = hInst;
	return TRUE;
}

// Plugin details retrieval
DLLEXPORT
In_Module* winampGetInModule2()
{
	return &mod;
}

// Uninstall support
DLLEXPORT
int winampUninstallPlugin(HINSTANCE hdll, HWND parent, int param)
{
	// TODO: actually do anything?
	// Winamp won't crash if it unloads the plugin and then deletes it
	return IN_PLUGIN_UNINSTALL_NOW;
}

// Extended read functions

// Open file
// returns:
// size = output data size in bytes
// bitDepth = bits per output sample (should be a multiple of 8)
// channels = number of channels (eg. 2 for stereo)
// samplingRate = samples per second (eg. 44100)
// (return value) = pointer to some context info
DLLEXPORT
intptr_t winampGetExtendedRead_open(const char* filename, int* size, int* bitDepth, int* channels, int* samplingRate)
{
/*	VGMPlayer* player = new VGMPlayer();

	if (player)
	{
		if (player->OpenFile(filename, 44100, 0) == VGM_OK)
		{
			int length = player->GetLengthInSamples();

			// Apply config to player
			ApplyPluginConfigToPlayer(pluginConfig, player);
			// But change things that aren't suitable for transcoding, etc
			// Disable "loop forever", fall back to default loop count
			player->SetNumLoops(pluginConfig->numLoops);

			*bitDepth = 16;
			*channels = 2;
			*size = length * 2 * 2; // 16-bit stereo
			*samplingRate = 44100;

			// return on success here
			return (intptr_t)player;
		}
		else
		{
			delete player;
		}
	}
*/
	return (intptr_t)NULL;
}

// Read samples
// handle = return value from open, above
// dest = buffer for data
// len = size of buffer
// returns:
// killswitch = 1 if there are no more samples
// (return value) = number of bytes written to the buffer
DLLEXPORT
int winampGetExtendedRead_getData(intptr_t handle, char* dest, int len, int* killswitch)
{
/*
	VGMPlayer* player = (VGMPlayer*)handle;
	int numSamplesRequested = len / 2 / 2; // 16-bit stereo
	int numSamplesRendered = player->GetSamples((short*)dest, numSamplesRequested);
	if (numSamplesRendered < numSamplesRequested)
	{
		int i = 1;
	}
	return numSamplesRendered * 2 * 2; // 16-bit stereo
*/
	*killswitch = 1;
	return 0;
}

// Seek
// return 0 if seek worked
DLLEXPORT
int winampGetExtendedRead_setTime(intptr_t handle, int timeInMs)
{
/*
	VGMPlayer* player = (VGMPlayer*)handle;
	return (player->SeekTo(timeInMs) == VGM_OK);
*/
	return -1;
}

// Close file
DLLEXPORT
void winampGetExtendedRead_close(intptr_t handle)
{
/*
	VGMPlayer* player = (VGMPlayer*)handle;

	delete player;
*/
}

// Extended file info functions

// Get (Unicode)
DLLEXPORT
int winampGetExtendedFileInfoW(const wchar_t* wfilename, const char* key, wchar_t* ret, int retlen)
{
	try
	{
		std::string filename = GetASCIIFilename(wfilename);
		std::wstring value;
		if (GetMetadata(value, filename, key))
		{
			int numCharsToReturn = value.length() + 1; // for the NULL
			if (numCharsToReturn > retlen)
			{
				numCharsToReturn = retlen;
			}
			wcsncpy(ret, value.c_str(), numCharsToReturn);
			ret[numCharsToReturn] = '\0'; // for safety
			return 1;
		}
		else
		{
			return 0;
		}
	}
	catch (std::exception&)
	{
		return 0;
	}
}

// Get (ASCII)
DLLEXPORT
int winampGetExtendedFileInfo(const char* filename, const char* key, char* ret, int retlen) 
{
	try
	{
		std::wstring value;
		if (GetMetadata(value, filename, key))
		{
			WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK, value.c_str(), -1, ret, retlen, NULL, NULL);
			return 1;
		}
		else
		{
			return 0;
		}
	}
	catch (std::exception&)
	{
		return 0;
	}
}

// Set (Unicode)
DLLEXPORT
int winampSetExtendedFileInfoW(const wchar_t* wfilename, const char* key, wchar_t* value) 
{
	return (SetMetadata(GetASCIIFilename(wfilename), key, std::wstring(value)) ? 1 : 0);
}

// Set (ASCII)
DLLEXPORT
int winampSetExtendedFileInfo(const char* filename, const char* key, char* value) 
{
	// upconvert value to wstring
//	std::wstring wvalue(value);
//	return (SetMetadata(filename, key, value) ? 1 : 0);
	return FALSE;
}

// Persist
DLLEXPORT
int winampWriteExtendedFileInfo()
{
	return FALSE;
}

// Return 1 to use the standard file info dialog that uses winampGetExtendedFileInfoW
// Return 2 to ???
DLLEXPORT
int winampUseUnifiedFileInfoDlg(const char* fn)
{
	return 1;
}

// should return a child window of 513x271 pixels (341x164 in msvc dlg units), or return NULL for no tab.
// Fill in name (a buffer of namelen characters), this is the title of the tab (defaults to "Advanced").
// filename will be valid for the life of your window. n is the tab number. This function will first be
// called with n == 0, then n == 1 and so on until you return NULL (so you can add as many tabs as you like).
// The window you return will receive WM_COMMAND, IDOK/IDCANCEL messages when the user clicks OK or Cancel.
// when the user edits a field which is duplicated in another pane, do a SendMessage(GetParent(hwnd),WM_USER,(WPARAM)L"fieldname",(LPARAM)L"newvalue");
// this will be broadcast to all panes (including yours) as a WM_USER.
/*
DLLEXPORT
HWND winampAddUnifiedFileInfoPane(int n, const wchar_t * filename, HWND parent, wchar_t *name, size_t namelen)
{
	// stuff here
}
*/

#pragma endregion "Exported functions"



#pragma region "Winamp exported functions"

void ShowConfigDialog(HWND parent)
{
	// Work on a copy of the config
	PluginConfig configCopy(pluginConfig);

	if (PluginConfigDialog::Show(mod.hDllInstance, parent, configCopy, mod.description))
	{
		// Swap to new config
		pluginConfig = configCopy;
		// Apply where needed
/*		ApplyPluginConfigToPlayer(pluginConfig, player); */
		ApplyPluginConfigToMetadata();
	}
}

void ShowAboutDialog(HWND parent)
{
	MessageBox(parent,
		"VGM Input Plugin\n"
		"by Maxim\n"
		"http://www.smspower.org/maxim/",
		PLUGINNAME,
		MB_ICONINFORMATION
	);
	// TODO: more here, credit stuff I used (see old plugin)
}

void Initialise()
{
	pluginConfig.loadFromINI(getINIFilename());

	ApplyPluginConfigToMetadata();

/*
	char* iniFilename = getINIFilename();
	char* replayGainDBFilename = GetAuxiliaryFilename("in_vgm Replay Gain data.sqlite3");

	if (iniFilename)
	{
		pluginConfig = PluginConfig_LoadFromINI(iniFilename);

	}

	if (replayGainDBFilename)
	{
		VGMMetaData_SetReplayGainDB(replayGainDBFilename);
		free(replayGainDBFilename);
	}
*/
	// initialise sample buffer
	// at 16-bit stereo, x2 to allow for DSP stretching
	sampleBuffer = new short[SAMPLES_PER_ITERATION * 2 * 2];
}

void Deinitialise()
{
	pluginConfig.saveToINI(getINIFilename());
/*
	char* iniFilename = getINIFilename();
	if (iniFilename)
	{
		PluginConfig_SaveToINI(iniFilename, pluginConfig);
	}
*/
	// free player
	delete player;
	player = NULL;

	// free sample buffer
	delete [] sampleBuffer;
	sampleBuffer = NULL;
/*
	// Clean up metadata resources
	VGMMetaData_CleanUpResources();
*/
}

// if filename == NULL, return info for current file
// title = title to display if advanced formatting is off (max GETFILEINFO_TITLE_LENGTH)
// lengthInMs = length of file in ms
void GetFileInfo(const char* filename, char* title, int* lengthInMs)
{
	try
	{
		if (filename == NULL || *filename == '\0')
		{
			if (player)
			{
				*lengthInMs = player->GetLengthInMs();
			}
		}
		else
		{
			*lengthInMs = (int)VGMMetaData::getInstance().Get(filename, VGMMetaData::Double::LoopedLengthInMs);
		}
	}
	catch (std::exception)
	{
	}
}

// return IDCANCEL to interrupt a batch info sequence
// INFOBOX_UNCHANGED?
int ShowInfoDialog(const char* filename, HWND parent)
{
	return IDOK;
}

// return 1 if we can play the given file
int IsOurFile(const char* filename)
{
	return 0;
}

// return 0 for success, -1 if file cannot be opened (so Winamp will try the next file), any other value otherwise
int Play(const char* filename)
{
	// Winamp info we pass on
	int maxLatency;
	int kbps;
	// Config figures we use

	int samplingRate = pluginConfig.getInt(pluginConfig.SAMPLINGRATE);
	// Constant config
	const int channels = 2;
	const int bitsPerSample = 16;

	// Create player of it doesn't already exist
	if (player == NULL)
	{
		player = new VGMPlayer();
		// TODO ApplyPluginConfigToPlayer(pluginConfig, player);
	}

	if (player->OpenFile(filename, samplingRate, false) == false)
	{
		return -1;
	}

	// Open output plugin
	maxLatency = mod.outMod->Open(samplingRate, channels, bitsPerSample, -1, -1);
	if (maxLatency < 0)
	{
		return 1;
	}

	// Set info
	kbps = -1;// (int)(player->GetBitrate() / 1000 + 0.5);
	mod.SetInfo(kbps, samplingRate / 1000, channels, 1);

	// initialize vis stuff
	mod.SAVSAInit(maxLatency, samplingRate);
	mod.VSASetInfo(samplingRate, channels);

	// set the output plugin's default volume
	mod.outMod->SetVolume(-666);

	// Start up decode thread
	playbackThreadSignal = 0;
	playbackThreadHandle = CreateThread(
		NULL, // (in, optional) security stuff
		0, // (in) stack size (default)
		(LPTHREAD_START_ROUTINE)PlaybackThread, // (in) function to run in the thread
		NULL, // (in, optional) function parameter
		0, // (in) creation flags (default)
		NULL // (out, optional) thread ID
	);

	isPaused = 0;

	// Set its priority according to the preferences
	SetThreadPriority(playbackThreadHandle, GetPlaybackThreadPriority());

	return 0;
}

void Pause()
{
	isPaused = 1;
	mod.outMod->Pause(1);
}

void Unpause()
{
	isPaused = 0;
	mod.outMod->Pause(0);
}

// return 1 if paused, 0 otherwise
int IsPaused()
{
	return isPaused;
}

// clean up any resources from the playing file
void Stop()
{
	// Terminate the playback thread
	if (playbackThreadHandle != INVALID_HANDLE_VALUE)
	{
		// Set the flag telling it to stop
		playbackThreadSignal = 1;
		// wait for the thread to stop
		if (WaitForSingleObject(playbackThreadHandle,INFINITE) == WAIT_TIMEOUT)
		{
			MessageBox(
				mod.hMainWindow,
				"Playback thread says: \"Braaaaiiiiins!\". I think it might be a zombie. So I will now kill it.",
				mod.description,
				MB_ICONINFORMATION
			);
			TerminateThread(playbackThreadHandle,0);
		}
		CloseHandle(playbackThreadHandle);
		playbackThreadHandle = INVALID_HANDLE_VALUE;
	}

	player->CloseFile();

	// close output plugin
	mod.outMod->Close();
	// deinit vis
	mod.SAVSADeInit();
}

// return length of current file in ms
// or -1000 for endless
int GetLength()
{
	return player->GetLengthInMs();
}

// return current output position in ms
int GetOutputTime()
{
	return mod.outMod->GetOutputTime();
}

void SetOutputTime(int timeInMs)
{
	// Signal to playback thread to perform the seek
	seekTime = timeInMs;
}

void SetVolume(int volume)
{
	// Volume is controlled by the output plugin
	mod.outMod->SetVolume(volume);
}

void SetPan(int pan)
{
	// Panning is controlled by the output plugin
	mod.outMod->SetPan(pan);
}

void SetEQ(int on, char data[10], int preamp)
{
	// We do not want to perform EQ ourselves
}

#pragma endregion "Winamp exported functions"



// This runs in its own thread
DWORD WINAPI __stdcall PlaybackThread(void* ignored)
{
	// Set our thread name
	SetThreadName(-1, "in_vgm playback thread");

	// status points to an int that is zero while we are allowed to run
	while (playbackThreadSignal == 0)
	{
		// how many bytes the output plugin can accept
		int maxBytes = mod.outMod->CanWrite();

		// how many bytes of data we will be producing
		int bytesToWrite = SAMPLES_PER_ITERATION * 2 * 2;
		if (mod.dsp_isactive())
		{
			// If the DSP is active, it may up to double the number of samples
			bytesToWrite *= 2;
		}

		if (maxBytes >= bytesToWrite)
		{
			// The buffer can accept our chunk of data
			int writtenTime;
			int samplesInBuffer;

			if (seekTime >= 0)
			{
				// A seek is needed
				if (player->SeekTo(seekTime))
				{
					// Succeeded: tell the output plug we're there now
					mod.outMod->Flush(seekTime);
				}
				// Cancel the seek even if it fails (doing it again won't help)
				seekTime = -1;
			}

			// Generate audio
			samplesInBuffer = player->GetSamples(sampleBuffer, SAMPLES_PER_ITERATION);

#ifdef _DEBUG
			{
				char buf[1024];
				static int total = 0;
				total += samplesInBuffer;
				sprintf(buf,"Output %d samples, total %d\n", samplesInBuffer, total);
				OutputDebugString(buf);
				if (samplesInBuffer == 0)
				{
					total = 0;
				}
			}
#endif

			if (samplesInBuffer != 0)
			{
				// Get the written time from the output plugin, so we can pass it to 
				// the vis functions and they can stay synced
				writtenTime = mod.outMod->GetWrittenTime();
				// Add pre-DSP data to vis
				mod.SAAddPCMData((char*)sampleBuffer, 2, 16, writtenTime);
				mod.VSAAddPCMData((char*)sampleBuffer, 2, 16, writtenTime);

				if (mod.dsp_isactive())
				{
					// DSP can change the number of samples
					samplesInBuffer = mod.dsp_dosamples(
						sampleBuffer,    // sample buffer
						samplesInBuffer, // sample buffer size
						16,              // bits per sample
						2,               // number of channels
						player->GetSampleRate() // sample rate
					);
				}

				// Finally, write to output plugin
				mod.outMod->Write((char*)sampleBuffer,samplesInBuffer*2*2);
			}
			else
			{
				// No samples returned means we got to the end of the track
				PumpToEndOfTrack();
				return 0;
			}
		}
		else
		{
			// We can't write; sleep before checking the output buffer again
			Sleep(50);
		}

	}

	return 0;
}




#pragma region "Helpers"

// caller must free
std::string getINIFilename_WinampDir()
{
	// We get the program's directory
	// We don't know how big a string we need...
	int bufSize = 64;
	int charsCopied = 0;
	char* buf = NULL;
	bool success = false;
	// ...so we keep trying with bigger ones
	for (int bufSize = 64; !success && bufSize < 1024*1024; bufSize *= 2)
	{
		delete [] buf;
		buf = new char[bufSize];
		charsCopied = GetModuleFileName((HMODULE)pluginhInst, buf, bufSize);
		success = (charsCopied < bufSize && charsCopied > 0);
	}

	if (!success)
	{
		delete [] buf;
		return "";
	}

	// copy into std::string
	std::string path(buf, charsCopied);
	delete [] buf;

	// find filename part
	std::string::size_type p = path.rfind('\\');
	if (p == std::string::npos)
	{
		return "";
	}

	return path.substr(0, p + 1) + "plugin.ini";
}

// caller must free
std::string getINIFilename_Profile(const char* dir)
{
	std::string path(dir);
	path += "\\plugins";

	// make sure folder exists
	CreateDirectory(path.c_str(), NULL);

	return path + "\\in_vgm.ini";
}

void migrateSettingsFromWinampDir(const std::string& filename)
{
	// Older versions of the plugin stored their settings only in the Winamp dir.
	// If there are no settings in the given filename then we see if there are
	// any in the "old location" and copy them over.

	// (NumLoops should never be < 0)
	if (GetPrivateProfileInt(PluginConfig::getINISection().c_str(), "NumLoops", -1, filename.c_str()) == -1)
	{
		const std::string& oldFilename = getINIFilename_WinampDir();
		char section[32768]; // unlikely to be much bigger
		// copy old section into buffer
		int sectionsize = GetPrivateProfileSection(PluginConfig::getINISection().c_str(), section, 32768, oldFilename.c_str());
		if (sectionsize > 0)
		{
			// write section to new file
			WritePrivateProfileSection(PluginConfig::getINISection().c_str(), section, filename.c_str());
			// delete section from old file
			WritePrivateProfileSection(PluginConfig::getINISection().c_str(), NULL, oldFilename.c_str());
		}
	}
}

const std::string& getINIFilename()
{
	// We cache the result
	static std::string filename;

	if (filename.length() == 0)
	{
		// see if we are in Winamp 5.11+ with user profiles
		char* dir = (char*)SendMessage(mod.hMainWindow, WM_WA_IPC, 0, IPC_GETINIDIRECTORY);

		if (dir)
		{
			// Winamp told us where to store it, construct our path from it
			filename = getINIFilename_Profile(dir);

			migrateSettingsFromWinampDir(filename);
			// do not free dir
		}
		else
		{
			filename = getINIFilename_WinampDir();
		}
	}

	return filename;
}

// Set thread name for niceness to debuggers
void SetThreadName(DWORD id, const std::string& name)
{
	// This is some extreme Win32 voodoo...
	const DWORD MAGIC_EXCEPTION_NUMBER = 0x406d1388;
	const DWORD MAGIC_TYPE = 0x1000;
	const DWORD MAGIC_FLAGS = 0;
	const struct
	{
		DWORD type;
		LPCSTR threadName;
		DWORD threadId;
		DWORD flags;
	} MAGIC_STRUCT = { MAGIC_TYPE, name.c_str(), id, MAGIC_FLAGS};

	__try
	{
		RaiseException(MAGIC_EXCEPTION_NUMBER, 0, sizeof(MAGIC_STRUCT)/sizeof(DWORD), (DWORD*)&MAGIC_STRUCT);
	}
	__except (EXCEPTION_CONTINUE_EXECUTION)
	{}
}

// Tries to make a valid ASCII(ish) filename out of the wstring filename passed in
std::string GetASCIIFilename(const std::wstring& wfilename)
{
	char* cbuf = NULL;
	int cbuflen;
	BOOL usedSubstitute;
	std::string result;

	// try a straight conversion first
	// it tells me the size of the buffer needed, including the null terminator
	cbuflen = WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK, wfilename.c_str(), -1, NULL, 0, NULL, &usedSubstitute) + 1;
	if (!usedSubstitute)
	{
		cbuf = new char[cbuflen];
		if (cbuf)
		{
			WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK, wfilename.c_str(), -1, cbuf, cbuflen, NULL, NULL);
			result = cbuf;
			delete [] cbuf;
		}
	}
	else
	{
		wchar_t* wcbuf = NULL;
		int wcbuflen;

		// get wide short
		wcbuflen = GetShortPathNameW(wfilename.c_str(), NULL, 0) + 1;
		if (wcbuflen > 1)
		{
			wcbuf = new wchar_t[wcbuflen];
			if (wcbuf)
			{
				GetShortPathNameW(wfilename.c_str(), wcbuf, wcbuflen);
				// convert to ACP
				cbuflen = WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK, wcbuf, -1, NULL, 0, NULL, NULL) + 1;
				cbuf = new char[cbuflen];
				if (cbuf)
				{
					WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK, wcbuf, -1, cbuf, cbuflen, NULL, NULL);
					result = cbuf;
					delete [] cbuf;
				}
				free(wcbuf);
			}
		}
	}
	return result;
}

// caller must free
std::string GetAuxiliaryFilename(const std::string& filename)
{
	const std::string& iniFilename = getINIFilename();
	std::string::size_type p = iniFilename.rfind('\\');
	if (p != std::string::npos)
	{
		return iniFilename.substr(0, p + 1) + filename;
	}
	return "";
}

// Pumps the output plugin and then sends a "track end" signal to Winamp
void PumpToEndOfTrack()
{
	while (1) 
	{
		// We call this to pump the output buffer (?)
		mod.outMod->CanWrite();
		// If the output plugin is finished then we can notify Winamp that we've finished
		if (!mod.outMod->IsPlaying())
		{ 
			PostMessage(mod.hMainWindow, WM_WA_MPEG_EOF, 0, 0);
			return;
		}
		// avoid busy-polling
		Sleep(10);
	}
}
/*
void ApplyPluginConfigToPlayer(PluginConfig* pluginConfig, VGMPlayer* player)
{
	if ((pluginConfig == NULL) || (player == NULL))
	{
		return;
	}
	player->SetNumLoops(pluginConfig->loopForever ? -1 : pluginConfig->numLoops);
	player->SetPlaybackRate(pluginConfig->playbackRate);
	// We don't update sampling rate on the fly
}
*/

std::string FormatTime(double timeInSeconds)
{
	// Convert to the various timescales
	double secs=timeInSeconds;
	int mins   =(int)timeInSeconds / 60;
	int hours  =(int)timeInSeconds / ( 60 * 60 );
	int days   =(int)timeInSeconds / ( 60 * 60 * 24 );
	// Subtract the next largest's part to get the remainder (in smallest to largest order)
	secs  -= mins * 60;
	mins  -= hours * 60;
	hours -= days * 24;

	std::ostringstream buf;
	if (days)
	{
		// very unlikely
		buf << days << "d+" 
			<< hours << ":" 
			<< std::setfill('0') << std::setw(2) << mins << ":" 
			<< std::setfill('0') << std::setw(4) << std::fixed << std::setprecision(1) << secs; // e.g. 1d+2:34:56.7
	}
	else if (hours)
	{
		// unlikely
		buf << hours << ":" 
			<< std::setfill('0') << std::setw(2) << mins << ":" 
			<< std::setfill('0') << std::setw(4) << std::fixed << std::setprecision(1) << secs; // e.g. 1:23:45.6
	}
	else if (mins)
	{
		buf << mins << ":" 
			<< std::setfill('0') << std::setw(4) << std::fixed << std::setprecision(1) << secs; // e.g. 1:23.4
	}
	else
	{
		buf << std::fixed << std::setprecision(1) << secs << "s"; // e.g. 1.2s
	}

	return buf.str();
}

void ApplyPluginConfigToMetadata()
{
	VGMMetaData::getInstance().SetOptions(
		pluginConfig.getBool(PluginConfig::Setting::TAGSPREFERJAPANESE),
		pluginConfig.getBool(PluginConfig::Setting::TAGSADDFMTOYM2413),
		pluginConfig.getBool(PluginConfig::Setting::TAGSTRIM),
		pluginConfig.getInt(PluginConfig::Setting::PLAYBACKRATE),
		pluginConfig.getBool(PluginConfig::Setting::PLAYBACKLOOPFOREVER),
		pluginConfig.getInt(PluginConfig::Setting::PLAYBACKNUMLOOPS),
		GetAuxiliaryFilename("in_vgm Replay Gain data.sqlite3")
	);
}

// caller must not keep a reference to the result
// result = '\0' means we don't support the tag
bool GetMetadata(std::wstring& value, const std::string& filename, const std::string& key)
{
	// The key may be mixed case
	std::string keyLower(key);
	std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);

	// Some metadata doesn't require opening a file
	// and is queried with an invalid filename
	if (keyLower == "type")
	{
		// A number representing the type of content
		// 0 = audio
		// 262144 = VGM :)
		if (pluginConfig.getBool(PluginConfig::Setting::TAGSCUSTOMFILETYPE))
		{
			value = L"262144";
			return true;
		}
		else
		{
			value = L"0";
			return true;
		}
	}

	std::string::size_type dotPos = filename.find_last_of('.');
	std::string ext;
	if (dotPos != std::string::npos)
	{
		ext = filename.substr(dotPos);
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	}

	if (keyLower == "family")
	{
		// Get a description based on the file extension
		if (ext == ".vgm" || ext == ".vgz")
		{
			value = L"Video Game Music";
			return true;
		}
		else if (ext == ".vgm7z")
		{
			value = L"Video Game Music Archive";
			return true;
		}
	}

	// We don't support anything more for VGM7z
	if (ext == ".vgm7z")
	{
		return false;
	}

	VGMMetaData& metadata = VGMMetaData::getInstance();

	if (keyLower == "length")
	{
		value = metadata.GetAsWString(filename, VGMMetaData::Double::LoopedLengthInMs);
		return true;
	}

	if (keyLower == "artist")
	{
		value = metadata.Get(filename, VGMMetaData::String::Author);
		return true;
	}

	if (keyLower == "title")
	{
		value = metadata.Get(filename, VGMMetaData::String::Title);
		return true;
	}

	if (keyLower == "album")
	{
		value = metadata.Get(filename, VGMMetaData::String::Game);
		return true;
	}

	if (keyLower == "comment")
	{
		value = metadata.Get(filename, VGMMetaData::String::Comment);
		return true;
	}

	if (keyLower == "genre")
	{
		value = metadata.Get(filename, VGMMetaData::String::System);
		return true;
	}

	if (keyLower == "track")
	{
		value = metadata.GetAsWString(filename, VGMMetaData::Int::TrackNumber);
		return true;
	}

	if (keyLower == "albumartist")
	{
		value = metadata.Get(filename, VGMMetaData::String::GameAllAuthors);
		return true;
	}

	if (keyLower == "replaygain_track_gain")
	{
		value = metadata.GetAsWString(filename, VGMMetaData::Double::ReplayGainTrackGain);
		return true;
	}

	if (keyLower == "replaygain_track_peak")
	{
		value = metadata.GetAsWString(filename, VGMMetaData::Double::ReplayGainTrackPeak);
		return true;
	}

	if (keyLower == "replaygain_album_gain")
	{
		value = metadata.GetAsWString(filename, VGMMetaData::Double::ReplayGainAlbumGain);
		return true;
	}

	if (keyLower == "replaygain_album_peak")
	{
		value = metadata.GetAsWString(filename, VGMMetaData::Double::ReplayGainAlbumPeak);
		return true;
	}

	if (keyLower == "bitrate")
	{
		value = metadata.GetAsWString(filename, VGMMetaData::Double::Bitrate);
		return true;
	}

	if (keyLower == "year")
	{
		value = metadata.Get(filename, VGMMetaData::String::Date);
		return true;
	}

	if (keyLower == "composer" || keyLower == "publisher" || keyLower == "disc"
		|| keyLower == "bpm" || keyLower == "gracenotefileid" 
		|| keyLower == "gracenoteextdata")
	{
		return false;
	}

	if (keyLower == "formatinformation")
	{
		std::wostringstream buf;
		double version = metadata.Get(filename, VGMMetaData::Double::VersionAsDouble);
		double totalLength = metadata.Get(filename, VGMMetaData::Double::TrackLengthInMs) / 1000;
		double loopLength  = metadata.Get(filename, VGMMetaData::Double::LoopLengthInMs) / 1000;
		buf << "VGM version: " << std::fixed << std::setprecision(2) << version << std::endl
			<< "Length: " << FormatTime(totalLength).c_str();
		if (loopLength > 0)
		{
			double introLength = totalLength - loopLength;
			if (introLength < 0.5)
			{
				// near enough looped
				buf << " (looped)";
			}
			else
			{
				buf << " (" << FormatTime(introLength).c_str() << " intro and " << FormatTime(loopLength).c_str() << " loop)";
			}
		}
		buf << std::endl;
		int playbackRate = metadata.Get(filename, VGMMetaData::Int::RecordingRate);
		if (playbackRate > 0)
		{
			buf << "Recorded at: " << playbackRate << "Hz" << std::endl;
		}
		buf << "Chips: "
			<< (metadata.Get(filename, VGMMetaData::Bool::HasSN76489) ? "SN76489 " : "")
			<< (metadata.Get(filename, VGMMetaData::Bool::HasYM2413 ) ? "YM2413 " : "")
			<< (metadata.Get(filename, VGMMetaData::Bool::HasYM2612 ) ? "YM2612 " : "")
			<< (metadata.Get(filename, VGMMetaData::Bool::HasYM2151 ) ? "YM2151 " : "")
			<< std::endl;
		
		buf << "Bitrate: " << metadata.Get(filename, VGMMetaData::Double::Bitrate) << " bps" << std::endl;
		
		buf << "Compressed: ";
		double compression = metadata.Get(filename, VGMMetaData::Double::Compression);
		if (compression > 0)
		{
			buf << compression*100 << "%";
		}
		else
		{
			buf << "No";
		}
		buf << std::endl;

		value = buf.str();
		return true;
	}
	// unknown key
#ifdef _DEBUG
	MessageBox(mod.hMainWindow, key.c_str(), "Unknown metadata key", MB_ICONINFORMATION);
#endif // _DEBUG

	return false;
}

bool SetMetadata(const std::string& filename, const std::string& key, std::wstring& value)
{
	// The key may be mixed case
	std::string keyLower(key);
	std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);

	VGMMetaData& metadata = VGMMetaData::getInstance();

	if (keyLower == "replaygain_track_gain")
	{
		return metadata.WriteAsWString(filename, VGMMetaData::Double::ReplayGainTrackGain, value);
	}

	if (keyLower == "replaygain_track_peak")
	{
		return metadata.WriteAsWString(filename, VGMMetaData::Double::ReplayGainTrackPeak, value);
	}

	if (keyLower == "replaygain_album_gain")
	{
		return metadata.WriteAsWString(filename, VGMMetaData::Double::ReplayGainAlbumGain, value);
	}

	if (keyLower == "replaygain_album_peak")
	{
		return metadata.WriteAsWString(filename, VGMMetaData::Double::ReplayGainAlbumPeak, value);
	}

	return false;
}

#pragma endregion "Helpers"
