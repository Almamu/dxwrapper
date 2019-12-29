/**
* Copyright (C) 2019 Elisha Riedlinger
*
* This software is  provided 'as-is', without any express  or implied  warranty. In no event will the
* authors be held liable for any damages arising from the use of this software.
* Permission  is granted  to anyone  to use  this software  for  any  purpose,  including  commercial
* applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*   1. The origin of this software must not be misrepresented; you must not claim that you  wrote the
*      original  software. If you use this  software  in a product, an  acknowledgment in the product
*      documentation would be appreciated but is not required.
*   2. Altered source versions must  be plainly  marked as such, and  must not be  misrepresented  as
*      being the original software.
*   3. This notice may not be removed or altered from any source distribution.
*
* Exception handling code taken from source code found in DxWnd v2.03.99
* https://sourceforge.net/projects/dxwnd/
*
* SetAppCompatData code created based on information from here:
* http://www.mojolabs.nz/posts.php?topic=99477
*
* ASI plugin loader taken from source code found in Ultimate ASI Loader
* https://github.com/ThirteenAG/Ultimate-ASI-Loader
*
* DDrawResolutionHack taken from source code found in LegacyD3DResolutionHack
* https://github.com/UCyborg/LegacyD3DResolutionHack
*/

#include "Settings\Settings.h"
#include "Dllmain\Dllmain.h"
#include "Wrappers\wrapper.h"
extern "C"
{
#include "Disasm\disasm.h"
}
#include "External\Hooking\Hook.h"
#include "Utils.h"
#include "Logging\Logging.h"

#undef LoadLibrary

typedef enum PROCESS_DPI_AWARENESS {
	PROCESS_DPI_UNAWARE = 0,
	PROCESS_SYSTEM_DPI_AWARE = 1,
	PROCESS_PER_MONITOR_DPI_AWARE = 2
} PROCESS_DPI_AWARENESS;
typedef void(WINAPI *PFN_InitializeASI)(void);
typedef HRESULT(WINAPI *SetProcessDpiAwarenessProc)(PROCESS_DPI_AWARENESS value);
typedef BOOL(WINAPI *SetProcessDPIAwareProc)();
typedef BOOL(WINAPI *SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT value);
typedef FARPROC(WINAPI *GetProcAddressProc)(HMODULE, LPSTR);
typedef DWORD(WINAPI *GetModuleFileNameAProc)(HMODULE, LPSTR, DWORD);
typedef DWORD(WINAPI *GetModuleFileNameWProc)(HMODULE, LPWSTR, DWORD);
typedef HRESULT(WINAPI *SetAppCompatDataFunc)(DWORD, DWORD);
typedef LPTOP_LEVEL_EXCEPTION_FILTER(WINAPI *PFN_SetUnhandledExceptionFilter)(LPTOP_LEVEL_EXCEPTION_FILTER);
typedef BOOL(WINAPI *CreateProcessAFunc)(LPCSTR lpApplicationName, LPSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags,
	LPVOID lpEnvironment, LPCSTR lpCurrentDirectory, LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation);
typedef BOOL(WINAPI *CreateProcessWFunc)(LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags,
	LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation);

namespace Utils
{
	// Strictures
	struct type_dll
	{
		HMODULE dll;
		std::string name;
		std::string fullname;
	};

	// FontSmoothing
	struct SystemSettings
	{
		BOOL isEnabled = FALSE;
		UINT type = 0;
		UINT contrast = 0;
		UINT orientation = 0;
	} fontSystemSettings;

	// Screen settings
	HDC hDC = nullptr;
	WORD lpRamp[3 * 256] = { NULL };

	// Declare variables
	FARPROC pGetProcAddress = nullptr;
	FARPROC pGetModuleFileNameA = nullptr;
	FARPROC pGetModuleFileNameW = nullptr;
	FARPROC p_CreateProcessA = nullptr;
	FARPROC p_CreateProcessW = nullptr;
	std::vector<type_dll> custom_dll;		// Used for custom dll's and asi plugins
	LPTOP_LEVEL_EXCEPTION_FILTER pOriginalSetUnhandledExceptionFilter = SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)EXCEPTION_CONTINUE_EXECUTION);
	PFN_SetUnhandledExceptionFilter pSetUnhandledExceptionFilter = reinterpret_cast<PFN_SetUnhandledExceptionFilter>(SetUnhandledExceptionFilter);
	WNDPROC OriginalWndProc = nullptr;

	// Function declarations
	void InitializeASI(HMODULE hModule);
	void FindFiles(WIN32_FIND_DATA*);
	LONG WINAPI myUnhandledExceptionFilter(LPEXCEPTION_POINTERS);
	LPTOP_LEVEL_EXCEPTION_FILTER WINAPI extSetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER);
	void *memmem(const void *l, size_t l_len, const void *s, size_t s_len);
	LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
}

// Execute a specified string
void Utils::Shell(const char* fileName)
{
	Logging::Log() << "Running process: " << fileName;

	// Get StartupInfo and ProcessInfo memory size and set process window to hidden
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.wShowWindow = SW_HIDE;
	ZeroMemory(&pi, sizeof(pi));

	// Start the child process
	if (!CreateProcess(nullptr, const_cast<LPSTR>(fileName), nullptr, nullptr, true, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
	{
		// Failed to launch process!
		Logging::Log() << "Failed to launch process!";
	}
	else
	{
		// Wait for process to exit
		if (Config.WaitForProcess) WaitForSingleObjectEx(pi.hThread, INFINITE, true);

		// Close thread handle
		CloseHandle(pi.hThread);

		// Close process handle
		CloseHandle(pi.hProcess);
	}
	// Quit function
	return;
}

// Sets the proccess to single core affinity
void Utils::SetProcessAffinity()
{
	DWORD_PTR ProcessAffinityMask, SystemAffinityMask;
	HANDLE hCurrentProcess = GetCurrentProcess();
	if (GetProcessAffinityMask(hCurrentProcess, &ProcessAffinityMask, &SystemAffinityMask))
	{
		DWORD_PTR AffinityMask = 1;
		while (AffinityMask && (AffinityMask & ProcessAffinityMask) == 0)
		{
			AffinityMask <<= 1;
		}
		if (AffinityMask)
		{
			SetProcessAffinityMask(hCurrentProcess, ((AffinityMask << (Config.SingleProcAffinity - 1)) & ProcessAffinityMask) ? (AffinityMask << (Config.SingleProcAffinity - 1)) : AffinityMask);
		}
	}
	CloseHandle(hCurrentProcess);
}

// Sets application DPI aware which disables DPI virtulization/High DPI scaling for this process
void Utils::DisableHighDPIScaling()
{
	Logging::Log() << "Disabling High DPI Scaling...";

	BOOL setDpiAware = FALSE;
	HMODULE hUser32 = LoadLibrary("user32.dll");
	HMODULE hShcore = LoadLibrary("shcore.dll");
	if (hUser32 && !setDpiAware)
	{
		SetProcessDpiAwarenessContextProc setProcessDpiAwarenessContext = (SetProcessDpiAwarenessContextProc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");

		if (setProcessDpiAwarenessContext)
		{
			setDpiAware |= setProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
		}
	}
	if (hShcore && !setDpiAware)
	{
		SetProcessDpiAwarenessProc setProcessDpiAwareness = (SetProcessDpiAwarenessProc)GetProcAddress(hShcore, "SetProcessDpiAwareness");

		if (setProcessDpiAwareness)
		{
			setDpiAware |= SUCCEEDED(setProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE));
		}
	}
	if (hUser32 && !setDpiAware)
	{
		SetProcessDPIAwareProc setProcessDPIAware = (SetProcessDPIAwareProc)GetProcAddress(hUser32, "SetProcessDPIAware");

		if (setProcessDPIAware)
		{
			setDpiAware |= setProcessDPIAware();
		}
	}

	if (!setDpiAware)
	{
		Logging::Log() << "Failed to disable High DPI Scaling!";
	}
}

// Sets Application Compatibility Toolkit options for DXPrimaryEmulation using SetAppCompatData API
void Utils::SetAppCompat()
{
	// Check if any DXPrimaryEmulation flags is set
	bool appCompatFlag = false;
	for (UINT x = 1; x <= 12; x++)
	{
		if (Config.DXPrimaryEmulation[x])
		{
			appCompatFlag = true;
		}
	}

	// SetAppCompatData
	if (appCompatFlag)
	{
		HMODULE module = LoadLibrary("ddraw.dll");
		FARPROC SetAppCompatDataPtr = (module != nullptr) ? GetProcAddress(module, "SetAppCompatData") : nullptr;
		if (module && SetAppCompatDataPtr)
		{
			SetAppCompatDataFunc SetAppCompatData = (SetAppCompatDataFunc)SetAppCompatDataPtr;
			for (DWORD x = 1; x <= 12; x++)
			{
				if (Config.DXPrimaryEmulation[x])
				{
					if (SetAppCompatData)
					{
						Logging::Log() << "SetAppCompatData: " << x;
						// For LockColorkey, this one uses the second parameter
						if (x == AppCompatDataType.LockColorkey)
						{
							(SetAppCompatData)(x, Config.LockColorkey);
						}
						// For all the other items
						else
						{
							(SetAppCompatData)(x, 0);
						}
					}
				}
			}
		}
		else
		{
			Logging::Log() << "Cannnot open ddraw.dll to SetAppCompatData";
		}
	}
}

FARPROC Utils::GetProcAddress(HMODULE hModule, LPCSTR lpProcName, FARPROC SetReturnValue)
{
	if (!lpProcName || !hModule)
	{
		return SetReturnValue;
	}

	FARPROC ProcAddress = GetProcAddress(hModule, lpProcName);

	if (!ProcAddress)
	{
		ProcAddress = SetReturnValue;
	}

	return ProcAddress;
}

// Update GetProcAddress to check for bad addresses
FARPROC WINAPI Utils::GetProcAddressHandler(HMODULE hModule, LPSTR lpProcName)
{
	FARPROC ProAddr = nullptr;

	if (InterlockedCompareExchangePointer((PVOID*)&pGetProcAddress, nullptr, nullptr))
	{
		ProAddr = ((GetProcAddressProc)InterlockedCompareExchangePointer((PVOID*)&pGetProcAddress, nullptr, nullptr))(hModule, lpProcName);
	}
	if (!(Wrapper::ValidProcAddress(ProAddr)))
	{
		ProAddr = nullptr;
		SetLastError(127);
	}

	return ProAddr;
}

// Update GetModuleFileNameA to fix module name
DWORD WINAPI Utils::GetModuleFileNameAHandler(HMODULE hModule, LPSTR lpFilename, DWORD nSize)
{
	GetModuleFileNameAProc org_GetModuleFileName = (GetModuleFileNameAProc)InterlockedCompareExchangePointer((PVOID*)&pGetModuleFileNameA, nullptr, nullptr);

	if (org_GetModuleFileName)
	{
		DWORD ret = org_GetModuleFileName(hModule, lpFilename, nSize);

		if (lpFilename[0] != '\\' && lpFilename[1] != '\\' && lpFilename[2] != '\\' && lpFilename[3] != '\\')
		{
			DWORD lSize = org_GetModuleFileName(nullptr, lpFilename, nSize);
			char *pdest = strrchr(lpFilename, '\\');
			if (pdest && lSize > 0 && nSize - lSize + strlen(dtypename[dtype.dxwrapper]) > 0)
			{
				strcpy_s(pdest + 1, nSize - lSize, dtypename[dtype.dxwrapper]);
				return strlen(lpFilename);
			}
			return lSize;
		}

		return ret;
	}

	SetLastError(5);
	return 0;
}

// Update GetModuleFileNameW to fix module name
DWORD WINAPI Utils::GetModuleFileNameWHandler(HMODULE hModule, LPWSTR lpFilename, DWORD nSize)
{
	GetModuleFileNameWProc org_GetModuleFileName = (GetModuleFileNameWProc)InterlockedCompareExchangePointer((PVOID*)&pGetModuleFileNameW, nullptr, nullptr);

	if (org_GetModuleFileName)
	{
		DWORD ret = org_GetModuleFileName(hModule, lpFilename, nSize);

		if (lpFilename[0] != '\\' && lpFilename[1] != '\\' && lpFilename[2] != '\\' && lpFilename[3] != '\\')
		{
			DWORD lSize = org_GetModuleFileName(nullptr, lpFilename, nSize);
			wchar_t *pdest = wcsrchr(lpFilename, '\\');
			std::string str(dtypename[dtype.dxwrapper]);
			std::wstring wrappername(str.begin(), str.end());
			if (pdest && lSize > 0 && nSize - lSize + strlen(dtypename[dtype.dxwrapper]) > 0)
			{
				wcscpy_s(pdest + 1, nSize - lSize, wrappername.c_str());
				return wcslen(lpFilename);
			}
			return lSize;
		}

		return ret;
	}

	SetLastError(5);
	return 0;
}

// Add filter for UnhandledExceptionFilter used by the exception handler to catch exceptions
LONG WINAPI Utils::myUnhandledExceptionFilter(LPEXCEPTION_POINTERS ExceptionInfo)
{
	Logging::Log() << "UnhandledExceptionFilter: exception code=" << ExceptionInfo->ExceptionRecord->ExceptionCode <<
		" flags=" << ExceptionInfo->ExceptionRecord->ExceptionFlags << std::showbase << std::hex <<
		" addr=" << ExceptionInfo->ExceptionRecord->ExceptionAddress << std::dec << std::noshowbase;
	DWORD oldprot;
	static HMODULE disasmlib = nullptr;
	PVOID target = ExceptionInfo->ExceptionRecord->ExceptionAddress;
	switch (ExceptionInfo->ExceptionRecord->ExceptionCode)
	{
	case 0xc0000094: // IDIV reg (Ultim@te Race Pro)
	case 0xc0000095: // DIV by 0 (divide overflow) exception (SonicR)
	case 0xc0000096: // CLI Priviliged instruction exception (Resident Evil), FB (Asterix & Obelix)
	case 0xc000001d: // FEMMS (eXpendable)
	case 0xc0000005: // Memory exception (Tie Fighter)
	{
		int cmdlen;
		t_disasm da;
		Preparedisasm();
		if (!VirtualProtect(target, 10, PAGE_READWRITE, &oldprot))
		{
			return EXCEPTION_CONTINUE_SEARCH; // error condition
		}
		cmdlen = Disasm((BYTE *)target, 10, 0, &da, 0, nullptr, nullptr);
		Logging::Log() << "UnhandledExceptionFilter: NOP opcode=" << std::showbase << std::hex << *(BYTE *)target << std::dec << std::noshowbase << " len=" << cmdlen;
		memset((BYTE *)target, 0x90, cmdlen);
		VirtualProtect(target, 10, oldprot, &oldprot);
		HANDLE hCurrentProcess = GetCurrentProcess();
		if (!FlushInstructionCache(hCurrentProcess, target, cmdlen))
		{
			Logging::Log() << "UnhandledExceptionFilter: FlushInstructionCache ERROR target=" << std::showbase << std::hex << target << std::dec << std::noshowbase << ", err=" << GetLastError();
		}
		CloseHandle(hCurrentProcess);
		// skip replaced opcode
		ExceptionInfo->ContextRecord->Eip += cmdlen; // skip ahead op-code length
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	break;
	default:
		return EXCEPTION_CONTINUE_SEARCH;
	}
}

// Add filter for SetUnhandledExceptionFilter used by the exception handler to catch exceptions
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI Utils::extSetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter)
{
#ifdef _DEBUG
	Logging::Log() << "SetUnhandledExceptionFilter: lpExceptionFilter=" << lpTopLevelExceptionFilter;
#else
	UNREFERENCED_PARAMETER(lpTopLevelExceptionFilter);
#endif
	extern LONG WINAPI myUnhandledExceptionFilter(LPEXCEPTION_POINTERS);
	return pSetUnhandledExceptionFilter(myUnhandledExceptionFilter);
}

// Sets the exception handler by hooking UnhandledExceptionFilter
void Utils::HookExceptionHandler(void)
{
	void *tmp;

	Logging::Log() << "Set exception handler";
	HMODULE dll = LoadLibrary("kernel32.dll");
	if (!dll)
	{
		Logging::Log() << "Failed to load kernel32.dll!";
		return;
	}
	// override default exception handler, if any....
	LONG WINAPI myUnhandledExceptionFilter(LPEXCEPTION_POINTERS);
	tmp = Hook::HookAPI(dll, "kernel32.dll", UnhandledExceptionFilter, "UnhandledExceptionFilter", myUnhandledExceptionFilter);
	// so far, no need to save the previous handler, but anyway...
	tmp = Hook::HookAPI(dll, "kernel32.dll", SetUnhandledExceptionFilter, "SetUnhandledExceptionFilter", extSetUnhandledExceptionFilter);
	if (tmp)
	{
		pSetUnhandledExceptionFilter = reinterpret_cast<PFN_SetUnhandledExceptionFilter>(tmp);
	}

	SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX | SEM_FAILCRITICALERRORS);
	pSetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)myUnhandledExceptionFilter);
	Logging::Log() << "Finished setting exception handler";
}

// Unhooks the exception handler
void Utils::UnHookExceptionHandler(void)
{
	Logging::Log() << "Unloading exception handlers";
	SetErrorMode(0);
	SetUnhandledExceptionFilter(pOriginalSetUnhandledExceptionFilter);
	Finishdisasm();
}

// Add HMODULE to vector
void Utils::AddHandleToVector(HMODULE dll, const char *name)
{
	if (dll)
	{
		type_dll newCustom_dll;
		newCustom_dll.dll = dll;
		newCustom_dll.name.assign((strrchr(name, '\\')) ? strrchr(name, '\\') + 1 : name);
		newCustom_dll.fullname.assign(name);
		custom_dll.push_back(newCustom_dll);
	}
}

// Load real dll file that is being wrapped
HMODULE Utils::LoadLibrary(const char *dllname, bool EnableLogging)
{
	// Declare vars
	HMODULE dll = nullptr;
	const char *loadpath;
	char path[MAX_PATH] = { 0 };

	// Check if dll is already loaded
	for (size_t x = 0; x < custom_dll.size(); x++)
	{
		if (_strcmpi(custom_dll[x].name.c_str(), dllname) == 0 || _strcmpi(custom_dll[x].fullname.c_str(), dllname) == 0)
		{
			return custom_dll[x].dll;
		}
	}

	// Logging
	if (EnableLogging)
	{
		Logging::Log() << "Loading " << dllname;
	}

	// Load default dll if not loading current dll
	if (_strcmpi(Config.WrapperName.c_str(), dllname) != 0)
	{
		loadpath = dllname;
		dll = ::LoadLibraryA(loadpath);
	}

	// Load system dll
	if (!dll)
	{
		//Load library
		GetSystemDirectory(path, MAX_PATH);
		strcat_s(path, MAX_PATH, "\\");
		strcat_s(path, MAX_PATH, dllname);
		loadpath = path;
		dll = ::LoadLibraryA(loadpath);
	}

	// Store handle and dll name
	if (dll)
	{
		Logging::Log() << "Loaded library: " << loadpath;
		AddHandleToVector(dll, dllname);
	}

	// Return dll handle
	return dll;
}

// Load custom dll files
void Utils::LoadCustomDll()
{
	for (size_t x = 0; x < Config.LoadCustomDllPath.size(); ++x)
	{
		// Check if path is empty
		if (!Config.LoadCustomDllPath[x].empty())
		{
			// Load dll from ini
			auto h = LoadLibrary(Config.LoadCustomDllPath[x].c_str());

			// Cannot load dll
			if (h)
			{
				AddHandleToVector(h, Config.LoadCustomDllPath[x].c_str());
			}
			else
			{
				Logging::Log() << "Cannot load custom library: " << Config.LoadCustomDllPath[x];
			}
		}
	}
}

// Initialize ASI module
void Utils::InitializeASI(HMODULE hModule)
{
	if (!hModule)
	{
		return;
	}

	PFN_InitializeASI p_InitializeASI = (PFN_InitializeASI)GetProcAddress(hModule, "InitializeASI");

	if (!p_InitializeASI)
	{
		return;
	}

	p_InitializeASI();
}

// Find asi plugins to load
void Utils::FindFiles(WIN32_FIND_DATA* fd)
{
	char dir[MAX_PATH] = { 0 };
	GetCurrentDirectory(MAX_PATH, dir);

	HANDLE asiFile = FindFirstFile("*.asi", fd);
	if (asiFile != INVALID_HANDLE_VALUE)
	{
		do {
			if (!(fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				auto pos = strlen(fd->cFileName);

				if (fd->cFileName[pos - 4] == '.' &&
					(fd->cFileName[pos - 3] == 'a' || fd->cFileName[pos - 3] == 'A') &&
					(fd->cFileName[pos - 2] == 's' || fd->cFileName[pos - 2] == 'S') &&
					(fd->cFileName[pos - 1] == 'i' || fd->cFileName[pos - 1] == 'I'))
				{
					char path[MAX_PATH] = { 0 };
					sprintf_s(path, "%s\\%s", dir, fd->cFileName);

					auto h = LoadLibrary(path);
					SetCurrentDirectory(dir); //in case asi switched it

					if (h)
					{
						AddHandleToVector(h, path);
						InitializeASI(h);
					}
					else
					{
						Logging::LogFormat("Unable to load '%s'. Error: %d", fd->cFileName, GetLastError());
					}
				}
			}
		} while (FindNextFile(asiFile, fd));
		FindClose(asiFile);
	}
}

// Load asi plugins
void Utils::LoadPlugins()
{
	Logging::Log() << "Loading ASI Plugins";

	char oldDir[MAX_PATH] = { 0 }; // store the current directory
	GetCurrentDirectory(MAX_PATH, oldDir);

	char selfPath[MAX_PATH] = { 0 };
	HMODULE hModule = NULL;
	GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCTSTR)Utils::LoadPlugins, &hModule);
	GetModuleFileName(hModule, selfPath, MAX_PATH);
	*strrchr(selfPath, '\\') = '\0';
	SetCurrentDirectory(selfPath);

	WIN32_FIND_DATA fd;
	if (!Config.LoadFromScriptsOnly)
		FindFiles(&fd);

	SetCurrentDirectory(selfPath);

	if (SetCurrentDirectory("scripts\\"))
		FindFiles(&fd);

	SetCurrentDirectory(selfPath);

	if (SetCurrentDirectory("plugins\\"))
		FindFiles(&fd);

	SetCurrentDirectory(oldDir); // Reset the current directory
}

// Unload all dll files loaded by the wrapper
void Utils::UnloadAllDlls()
{
	// Logging
	Logging::Log() << "Unloading libraries...";

	// Unload custom libraries
	while (custom_dll.size() != 0)
	{
		// Unload dll
		FreeLibrary(custom_dll.back().dll);
		custom_dll.pop_back();
	}
}

// Searches the memory
void *Utils::memmem(const void *l, size_t l_len, const void *s, size_t s_len)
{
	register char *cur, *last;
	const char *cl = (const char *)l;
	const char *cs = (const char *)s;

	/* we need something to compare */
	if (!l_len || !s_len)
	{
		return nullptr;
	}

	/* "s" must be smaller or equal to "l" */
	if (l_len < s_len)
	{
		return nullptr;
	}

	/* special case where s_len == 1 */
	if (s_len == 1)
	{
		return (void*)memchr(l, (int)*cs, l_len);
	}

	/* the last position where it's possible to find "s" in "l" */
	last = (char *)cl + l_len - s_len;

	for (cur = (char *)cl; cur <= last; cur++)
	{
		if (cur[0] == cs[0] && !memcmp(cur, cs, s_len))
		{
			return cur;
		}
	}

	return nullptr;
}

// Removes the artificial resolution limit from Direct3D7 and below
void Utils::DDrawResolutionHack(HMODULE hD3DIm)
{
	const BYTE wantedBytes[] = { 0xB8, 0x00, 0x08, 0x00, 0x00, 0x39 };

	PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)hD3DIm;
	PIMAGE_NT_HEADERS pNtHeader = (PIMAGE_NT_HEADERS)((char *)pDosHeader + pDosHeader->e_lfanew);
	DWORD dwCodeBase = (DWORD)hD3DIm + pNtHeader->OptionalHeader.BaseOfCode;
	DWORD dwCodeSize = pNtHeader->OptionalHeader.SizeOfCode;
	DWORD dwOldProtect;

	DWORD dwPatchBase = (DWORD)memmem((void *)dwCodeBase, dwCodeSize, wantedBytes, sizeof(wantedBytes));
	if (dwPatchBase)
	{
		Logging::LogDebug() << __FUNCTION__ << " Found resolution check at: " << (void*)dwPatchBase;
		dwPatchBase++;
		VirtualProtect((LPVOID)dwPatchBase, 4, PAGE_EXECUTE_READWRITE, &dwOldProtect);
		*(DWORD *)dwPatchBase = (DWORD)-1;
		VirtualProtect((LPVOID)dwPatchBase, 4, dwOldProtect, &dwOldProtect);
	}
}

void Utils::GetScreenSettings()
{
	// Store screen settings
	//hDC = GetDC(nullptr);
	//GetDeviceGammaRamp(hDC, lpRamp);

	// Store font settings
	SystemParametersInfo(SPI_GETFONTSMOOTHING, 0, &fontSystemSettings.isEnabled, 0);
	SystemParametersInfo(SPI_GETFONTSMOOTHINGTYPE, 0, &fontSystemSettings.type, 0);
	SystemParametersInfo(SPI_GETFONTSMOOTHINGCONTRAST, 0, &fontSystemSettings.contrast, 0);
	SystemParametersInfo(SPI_GETFONTSMOOTHINGORIENTATION, 0, &fontSystemSettings.orientation, 0);
}

void Utils::ResetScreenSettings()
{
	// Reset screen settings
	Logging::Log() << "Reseting screen resolution";
	if (hDC)
	{
		SetDeviceGammaRamp(hDC, lpRamp);
		ReleaseDC(nullptr, hDC);
	}
	ChangeDisplaySettingsEx(nullptr, nullptr, nullptr, CDS_RESET, nullptr);

	// Reset font settings
	if (fontSystemSettings.isEnabled)
	{
		Logging::Log() << "Reseting font smoothing";
		SystemParametersInfo(SPI_SETFONTSMOOTHING, fontSystemSettings.isEnabled, nullptr, 0);
		SystemParametersInfo(SPI_SETFONTSMOOTHINGTYPE, 0,
			reinterpret_cast<void*>(fontSystemSettings.type), 0);
		SystemParametersInfo(SPI_SETFONTSMOOTHINGCONTRAST, 0,
			reinterpret_cast<void*>(fontSystemSettings.contrast), 0);
		SystemParametersInfo(SPI_SETFONTSMOOTHINGORIENTATION, 0,
			reinterpret_cast<void*>(fontSystemSettings.orientation), 0);

		/*const char* regKey = "FontSmoothing";
		SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETFONTSMOOTHING,
			reinterpret_cast<LPARAM>(regKey), SMTO_BLOCK, 100, nullptr);*/
		RedrawWindow(nullptr, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
	}
}

BOOL WINAPI CreateProcessAHandler(LPCSTR lpApplicationName, LPSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags,
	LPVOID lpEnvironment, LPCSTR lpCurrentDirectory, LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
	static CreateProcessAFunc org_CreateProcess = (CreateProcessAFunc)InterlockedCompareExchangePointer((PVOID*)&Utils::p_CreateProcessA, nullptr, nullptr);

	if (!org_CreateProcess)
	{
		Logging::Log() << __FUNCTION__ << " Error: invalid proc address!";

		if (lpProcessInformation)
		{
			lpProcessInformation->dwProcessId = 0;
			lpProcessInformation->dwThreadId = 0;
			lpProcessInformation->hProcess = nullptr;
			lpProcessInformation->hThread = nullptr;
		}
		SetLastError(ERROR_ACCESS_DENIED);
		return FALSE;
	}

	if (stristr(lpCommandLine, "gameux.dll,GameUXShim", MAX_PATH))
	{
		Logging::Log() << __FUNCTION__ << " " << lpCommandLine;

		char CommandLine[MAX_PATH] = { '\0' };

		for (int x = 0; x < MAX_PATH && lpCommandLine && lpCommandLine[x] != ',' && lpCommandLine[x] != '\0'; x++)
		{
			CommandLine[x] = lpCommandLine[x];
		}

		return org_CreateProcess(lpApplicationName, CommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags,
			lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
	}

	return org_CreateProcess(lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags,
		lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
}

BOOL WINAPI CreateProcessWHandler(LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags,
	LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
	static CreateProcessWFunc org_CreateProcess = (CreateProcessWFunc)InterlockedCompareExchangePointer((PVOID*)&Utils::p_CreateProcessW, nullptr, nullptr);

	if (!org_CreateProcess)
	{
		Logging::Log() << __FUNCTION__ << " Error: invalid proc address!";

		if (lpProcessInformation)
		{
			lpProcessInformation->dwProcessId = 0;
			lpProcessInformation->dwThreadId = 0;
			lpProcessInformation->hProcess = nullptr;
			lpProcessInformation->hThread = nullptr;
		}
		SetLastError(ERROR_ACCESS_DENIED);
		return FALSE;
	}

	if (wcsistr(lpCommandLine, L"gameux.dll,GameUXShim", MAX_PATH))
	{
		Logging::Log() << __FUNCTION__ << " " << lpCommandLine;

		wchar_t CommandLine[MAX_PATH] = { '\0' };

		for (int x = 0; x < MAX_PATH && lpCommandLine && lpCommandLine[x] != ',' && lpCommandLine[x] != '\0'; x++)
		{
			CommandLine[x] = lpCommandLine[x];
		}

		return org_CreateProcess(lpApplicationName, CommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags,
			lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
	}

	return org_CreateProcess(lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags,
		lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
}

void Utils::DisableGameUX()
{
	// Logging
	Logging::Log() << "Disabling Microsoft Game Explorer...";

	// Hook CreateProcess APIs
	Logging::Log() << "Hooking 'CreateProcess' API...";
	HMODULE h_kernel32 = GetModuleHandle("kernel32");
	InterlockedExchangePointer((PVOID*)&p_CreateProcessA, Hook::HotPatch(Hook::GetProcAddress(h_kernel32, "CreateProcessA"), "CreateProcessA", *CreateProcessAHandler));
	InterlockedExchangePointer((PVOID*)&p_CreateProcessW, Hook::HotPatch(Hook::GetProcAddress(h_kernel32, "CreateProcessW"), "CreateProcessW", *CreateProcessWHandler));
}

bool Utils::SetWndProcFilter(HWND hWnd)
{
	// Check window handle
	if (!IsWindow(hWnd))
	{
		Logging::Log() << __FUNCTION__ << " Error: hWnd invalid!";
		return false;
	}

	// Check if WndProc is already overloaded
	if (OriginalWndProc)
	{
		Logging::Log() << __FUNCTION__ << " Error: WndProc already overloaded!";
		return false;
	}

	LOG_LIMIT(3, __FUNCTION__ << " Setting new WndProc " << hWnd);

	// Store existing WndProc
	OriginalWndProc = (WNDPROC)GetWindowLong(hWnd, GWL_WNDPROC);

	// Set new WndProc
	if (!OriginalWndProc || !SetWindowLong(hWnd, GWL_WNDPROC, (LONG)WndProc))
	{
		Logging::Log() << __FUNCTION__ << " Failed to overload WndProc!";
		OriginalWndProc = nullptr;
		return false;
	}

	return true;
}

bool Utils::RestoreWndProcFilter(HWND hWnd)
{
	// Check window handle
	if (!IsWindow(hWnd))
	{
		Logging::Log() << __FUNCTION__ << " Error: hWnd invalid!";
		return false;
	}

	// Check if WndProc is overloaded
	if (!OriginalWndProc)
	{
		Logging::Log() << __FUNCTION__ << " Error: WndProc is not yet overloaded!";
		return false;
	}

	// Get current WndProc
	WNDPROC CurrentWndProc = (WNDPROC)GetWindowLong(hWnd, GWL_WNDPROC);

	// Check if WndProc is overloaded
	if (CurrentWndProc != WndProc)
	{
		Logging::Log() << __FUNCTION__ << " Error: WndProc does not match!";
		return false;
	}

	// Resetting WndProc
	if (!SetWindowLong(hWnd, GWL_WNDPROC, (LONG)OriginalWndProc))
	{
		Logging::Log() << __FUNCTION__ << " Failed to reset WndProc";
		return false;
	}

	OriginalWndProc = nullptr;
	return true;
}

bool Utils::IsWindowMessageFiltered(UINT uMsg, LRESULT *lpReturn)
{
	if (!lpReturn)
	{
		return false;
	}

	switch (uMsg)
	{
	case WM_ACTIVATEAPP:
	case WM_CANCELMODE:
	case WM_CHILDACTIVATE:
	case WM_CLOSE:
	case WM_COMPACTING:
	case WM_CREATE:
	case WM_DESTROY:
	case WM_DPICHANGED:
	case WM_ENABLE:
	case WM_ENTERSIZEMOVE:
	case WM_EXITSIZEMOVE:
	case WM_GETICON:
	case WM_GETMINMAXINFO:
	case WM_INPUTLANGCHANGE:
	case WM_INPUTLANGCHANGEREQUEST:
	case WM_MOVE:
	case WM_MOVING:
	case WM_NCACTIVATE:
	case WM_NCCALCSIZE:
	case WM_NCCREATE:
	case WM_NCDESTROY:
	case WM_NULL:
	case WM_QUERYDRAGICON:
	case WM_QUERYOPEN:
	case WM_QUIT:
	case WM_SHOWWINDOW:
	case WM_SIZE:
	case WM_SIZING:
	case WM_STYLECHANGED:
	case WM_STYLECHANGING:
	case WM_THEMECHANGED:
	case WM_USERCHANGED:
	case WM_WINDOWPOSCHANGED:
	case WM_WINDOWPOSCHANGING:
	case WM_DISPLAYCHANGE:
	case WM_NCPAINT:
	case WM_PAINT:
	case WM_PRINT:
	case WM_PRINTCLIENT:
	case WM_SETREDRAW:
	case WM_SYNCPAINT:
	case MN_GETHMENU:
	case WM_GETFONT:
	case WM_GETTEXT:
	case WM_GETTEXTLENGTH:
	case WM_SETFONT:
	case WM_SETICON:
	case WM_SETTEXT:
		*lpReturn = 0;
		return true;
	case WM_ERASEBKGND:
		*lpReturn = 1;
		return true;
	}

	return false;
}

LRESULT CALLBACK Utils::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	Logging::LogDebug() << __FUNCTION__ << " " << Logging::hex(uMsg);

	// Filter window message events
	LRESULT ret = 0;
	if (Utils::IsWindowMessageFiltered(uMsg, &ret))
	{
		SetLastError(0);
		return ret;
	}

	if (!OriginalWndProc)
	{
		LOG_LIMIT(100, __FUNCTION__ << " Error: no WndProc specified " << Logging::hex(uMsg));
		return NULL;
	}

	return OriginalWndProc(hWnd, uMsg, wParam, lParam);
}
