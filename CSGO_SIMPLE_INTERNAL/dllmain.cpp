#pragma comment(lib, "d3d9.lib")
#pragma comment (lib, "d3dx9.lib")
#pragma comment(lib, "detours.lib")

#include <Windows.h>
#include <iostream>
#include <string>
#include <Detours.h>
#include <imgui.h>
#include "imgui.h"
#include "imgui/backends/imgui_impl_dx9.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "definitions.h"
#include "offsets.h"
#include <d3d9.h>
#include <d3dx9.h>

LPDIRECT3D9			g_pD3D = NULL;
LPDIRECT3DDEVICE9	g_pd3dDevice = NULL;
HWND				window = NULL;
WNDPROC				wndproc_orig = NULL;

D3DPRESENT_PARAMETERS g_d3dpp = {};
IDirect3DPixelShader9* shaderback;
IDirect3DPixelShader9* shaderfront;

UINT Stride;
D3DVERTEXELEMENT9 decl[MAXD3DDECLLENGTH];
UINT numElements, mStartregister, mVectorCount, vSize, pSize;
IDirect3DVertexShader9* vShader;
IDirect3DPixelShader9* pShader;

bool show_menu = false;
bool chams = true;
bool fov = true;
int fov_value = 90;

typedef HRESULT(__stdcall* EndScene)(LPDIRECT3DDEVICE9 pDevice);
HRESULT __stdcall hkEndScene(LPDIRECT3DDEVICE9 pDevice);
EndScene oEndScene;

typedef HRESULT(APIENTRY* SetStreamSource)(IDirect3DDevice9*, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
SetStreamSource SetStreamSource_orig = 0;

typedef HRESULT(APIENTRY* SetVertexDeclaration)(IDirect3DDevice9*, IDirect3DVertexDeclaration9*);
SetVertexDeclaration SetVertexDeclaration_orig = 0;

typedef HRESULT(APIENTRY* SetVertexShaderConstantF)(IDirect3DDevice9*, UINT, const float*, UINT);
SetVertexShaderConstantF SetVertexShaderConstantF_orig = 0;

typedef HRESULT(APIENTRY* SetVertexShader)(IDirect3DDevice9*, IDirect3DVertexShader9*);
SetVertexShader SetVertexShader_orig = 0;

typedef HRESULT(APIENTRY* SetPixelShader)(IDirect3DDevice9*, IDirect3DPixelShader9*);;
SetPixelShader SetPixelShader_orig = 0;

typedef HRESULT(APIENTRY* DrawIndexedPrimitive)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
DrawIndexedPrimitive DrawIndexedPrimitive_orig = 0;

HRESULT GenerateShader(IDirect3DDevice9* pD3Ddev, IDirect3DPixelShader9** pShader, float r, float g, float b)
{
	char szShader[256];
	ID3DXBuffer* pShaderBuf = NULL;
	sprintf_s(szShader, "ps.1.1\ndef c0, %f, %f, %f, %f\nmov r0,c0", r, g, b, 1.0f);
	D3DXAssembleShader(szShader, sizeof(szShader), NULL, NULL, NULL, &pShaderBuf, NULL);
	if (FAILED(pD3Ddev->CreatePixelShader((const DWORD*)pShaderBuf->GetBufferPointer(), pShader)))return E_FAIL;
	return D3D_OK;
}

BOOL CALLBACK EnumWindowsCallback(HWND handle, LPARAM lParam)
{
	DWORD wndProcID;
	GetWindowThreadProcessId(handle, &wndProcID);

	if (GetCurrentProcessId() != wndProcID)
	{
		return TRUE;
	}

	window = handle;
	return FALSE;
}

HWND GetProcessWindow()
{
	EnumWindows(EnumWindowsCallback, NULL);
	return window;
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (show_menu && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
	{
		return true;
	}

	return CallWindowProc(wndproc_orig, hWnd, msg, wParam, lParam);
}

bool GetD3D9Device(void** pTable, size_t Size)
{
	if (!pTable)
		return false;

	g_pD3D = Direct3DCreate9(D3D_SDK_VERSION);

	if (!g_pD3D)
		return false;
	D3DPRESENT_PARAMETERS d3dpp = {};
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.hDeviceWindow = GetProcessWindow();
	d3dpp.Windowed = false;

	g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow, D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, &g_pd3dDevice);

	if (!g_pd3dDevice)
	{
		d3dpp.Windowed = !d3dpp.Windowed;

		g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow, D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, &g_pd3dDevice);

		if (!g_pd3dDevice)
		{
			g_pD3D->Release();
			return false;
		}

		memcpy(pTable, *reinterpret_cast<void***>(g_pd3dDevice), Size);

		g_pd3dDevice->Release();
		g_pD3D->Release();
		return true;
	}
}

void CleanupDeviceD3D()
{
	if (g_pd3dDevice)
	{
		g_pd3dDevice->Release();
		g_pd3dDevice = NULL;
	}

	if (g_pD3D)
	{
		g_pD3D->Release();
		g_pD3D = NULL;
	}
}

HRESULT __stdcall hkEndScene(LPDIRECT3DDEVICE9 pDevice)
{
	static bool init = false;

	if (GetAsyncKeyState(VK_INSERT))
	{
		show_menu = !show_menu;
	}

	if (!init)
	{
		wndproc_orig = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)WndProc);

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();

		ImGui::StyleColorsDark();

		ImGui_ImplWin32_Init(window);
		ImGui_ImplDX9_Init(pDevice);

		if (!shaderback)
			GenerateShader(pDevice, &shaderback, 221.0f / 255.0f, 177.0f / 255.0f, 31.0f / 255.0f);


		if (!shaderfront)
			GenerateShader(pDevice, &shaderfront, 31.0f / 255.0f, 99.0f / 255.0f, 155.0f / 255.0f);

		init = true;
	}

	if (init)
	{
		if (show_menu)
		{
			ImGui_ImplDX9_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			ImGui::Begin("Menu", &show_menu);
			{
				ImGui::Checkbox("Chams", &chams);
				ImGui::Spacing();
				ImGui::Checkbox("FOV", &fov);
				if (fov)
				{
					ImGui::SliderInt("Fov Value: ", &fov_value, 30, 179);
				}
			}

			ImGui::Render();
			ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
		}
	}

	return oEndScene(pDevice);
}

HRESULT APIENTRY SetStreamSource_hook(LPDIRECT3DDEVICE9 pDevice, UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT sStride)
{
	if (StreamNumber == 0)
	{
		Stride = sStride;
	}

	return SetStreamSource_orig(pDevice, StreamNumber, pStreamData, OffsetInBytes, sStride);
}

HRESULT APIENTRY SetVertexDeclaration_hook(LPDIRECT3DDEVICE9 pDevice, IDirect3DVertexDeclaration9* pdecl)
{
	if (pdecl != NULL)
	{
		pdecl->GetDeclaration(decl, &numElements);
	}

	return SetVertexDeclaration_orig(pDevice, pdecl);
}

HRESULT APIENTRY SetVertexShaderConstantF_hook(LPDIRECT3DDEVICE9 pDevice, UINT StartRegister, const float* pConstantData, UINT Vector4fCount)
{
	if (pConstantData != NULL)
	{
		mStartregister = StartRegister;
		mVectorCount = Vector4fCount;
	}

	return SetVertexShaderConstantF_orig(pDevice, StartRegister, pConstantData, Vector4fCount);
}

HRESULT APIENTRY SetVertexShader_hook(LPDIRECT3DDEVICE9 pDevice, IDirect3DVertexShader9* veShader)
{
	if (veShader != NULL)
	{
		vShader = veShader;
		vShader->GetFunction(NULL, &vSize);
	}

	return SetVertexShader_orig(pDevice, veShader);
}

HRESULT APIENTRY SetPixelShader_hook(LPDIRECT3DDEVICE9 pDevice, IDirect3DPixelShader9* piShader)
{
	if (piShader != NULL)
	{
		pShader = piShader;
		pShader->GetFunction(NULL, &pSize);
	}

	return SetPixelShader_orig(pDevice, piShader);
}

HRESULT APIENTRY DrawIndexedPrimitive_hook(IDirect3DDevice9* pDevice, D3DPRIMITIVETYPE Type, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount)
{
	if (chams)
	{
		if (T_Models || CT_Models)
		{
			pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
			pDevice->SetPixelShader(shaderback);
			DrawIndexedPrimitive_orig(pDevice, Type, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
			pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
			pDevice->SetPixelShader(shaderfront);
		}
	}

	return DrawIndexedPrimitive_orig(pDevice, Type, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
}

DWORD WINAPI FOVThread(LPVOID lp)
{
	DWORD gameMod = (DWORD)GetModuleHandle("client.dll");
	DWORD localPlayer = *(DWORD*)(gameMod + hazedumper::signatures::dwLocalPlayer); // Local player
	if (localPlayer == NULL)
	{
		while (localPlayer == NULL)
		{
			localPlayer = *(DWORD*)(gameMod + hazedumper::signatures::dwLocalPlayer);
		}
	}
	while (true)
	{
		if (localPlayer != NULL)
		{
			if (fov)
			{
				*(int*)(localPlayer + hazedumper::netvars::m_iDefaultFOV) = fov_value;
			}
		}
	}
}

DWORD WINAPI mainThread(PVOID base)
{
	void* d3d9Device[119];

	if (GetD3D9Device(d3d9Device, sizeof(d3d9Device)))
	{
		oEndScene = (EndScene)Detours::X86::DetourFunction((uintptr_t)d3d9Device[42], (uintptr_t)hkEndScene);
		SetStreamSource_orig = (SetStreamSource)Detours::X86::DetourFunction((uintptr_t)d3d9Device[100], (uintptr_t)SetStreamSource_hook);
		SetVertexDeclaration_orig = (SetVertexDeclaration)Detours::X86::DetourFunction((uintptr_t)d3d9Device[87], (uintptr_t)SetVertexDeclaration_hook);
		SetVertexShaderConstantF_orig = (SetVertexShaderConstantF)Detours::X86::DetourFunction((uintptr_t)d3d9Device[94], (uintptr_t)SetVertexShaderConstantF_hook);
		SetVertexShader_orig = (SetVertexShader)Detours::X86::DetourFunction((uintptr_t)d3d9Device[92], (uintptr_t)SetVertexShader_hook);
		SetPixelShader_orig = (SetPixelShader)Detours::X86::DetourFunction((uintptr_t)d3d9Device[107], (uintptr_t)SetPixelShader_hook);
		DrawIndexedPrimitive_orig = (DrawIndexedPrimitive)Detours::X86::DetourFunction((uintptr_t)d3d9Device[82], (uintptr_t)DrawIndexedPrimitive_hook);

		while (true)
		{
			if (GetKeyState(VK_F10))
			{
				ImGui_ImplDX9_Shutdown();
				ImGui_ImplWin32_Shutdown();
				ImGui::DestroyContext();
				CleanupDeviceD3D();
				FreeLibraryAndExitThread(static_cast<HMODULE>(base), 1);
			}
		}
	}

	FreeLibraryAndExitThread(static_cast<HMODULE>(base), 1);
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		CreateThread(nullptr, NULL, mainThread, hModule, NULL, nullptr);
		CreateThread(nullptr, NULL, FOVThread, hModule, NULL, nullptr);
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}


