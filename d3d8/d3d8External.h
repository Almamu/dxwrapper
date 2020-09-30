#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "Wrappers\d3d8.h"

class Direct3D8;

typedef void(WINAPI *Direct3D8EnableMaximizedWindowedModeShimProc)();

void WINAPI d8_Direct3D8EnableMaximizedWindowedModeShim();
HRESULT WINAPI d8_ValidatePixelShader(DWORD* pixelshader, DWORD* reserved1, BOOL flag, DWORD* toto);
HRESULT WINAPI d8_ValidateVertexShader(DWORD* vertexshader, DWORD* reserved1, DWORD* reserved2, BOOL flag, DWORD* toto);
void WINAPI d8_DebugSetMute();
Direct3D8 *WINAPI d8_Direct3DCreate8(UINT SDKVersion);

void Loadd3dx9();

extern FARPROC p_D3DXAssembleShader;
extern FARPROC p_D3DXDisassembleShader;
extern FARPROC p_D3DXLoadSurfaceFromSurface;

#define DECLARE_IN_WRAPPED_PROC(procName, unused) \
	const FARPROC procName ## _in = (FARPROC)*d8_ ## procName;

namespace D3d8Wrapper
{
	VISIT_PROCS_D3D8(DECLARE_IN_WRAPPED_PROC);
	VISIT_PROCS_D3D8_SHARED(DECLARE_IN_WRAPPED_PROC);

	extern FARPROC Direct3DCreate9_out;
	extern FARPROC Direct3D9EnableMaximizedWindowedModeShim_out;
	extern FARPROC DebugSetMute_out;
}

#undef DECLARE_IN_WRAPPED_PROC
