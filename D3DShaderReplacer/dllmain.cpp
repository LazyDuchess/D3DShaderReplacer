#include "pch.h"
#include <Windows.h>
#include <d3d9.h> 
#include "D3Dhook.h"
#include <fstream>
#include <string>
#include <filesystem>
#include <d3dcommon.h>
#include <vector>
#include <map>
#include <sstream>
#include <d3dx9.h>
#include <d3dcompiler.h>
#include "ExtraData.h"

#if (_WIN64)
	#pragma comment(lib, "D3D Hook x64.lib")
#else
	#pragma comment(lib, "D3D Hook x86.lib")
#endif
#pragma comment(lib, "d3dcompiler")
#pragma comment(lib, "D3dx9.lib")

std::map<std::wstring, std::wstring> aliases;
std::map<int, cExtraData*> vExtraData;
std::map<IDirect3DPixelShader9*, int> compiledShaders;
std::map<IDirect3DPixelShader9**, int> pCompiledShaders;



//std::vector<IDirect3DPixelShader9**> compiledShaders;

wchar_t extractionDirectory[MAX_PATH];
wchar_t replacementDirectory[MAX_PATH];
wchar_t destinationPath[MAX_PATH];


inline bool exists(const std::wstring& name) {
	struct _stat buffer;
	return (_wstat(name.c_str(), &buffer) == 0);
}

bool bPrompt = true;
bool bDebugMode = false;
bool bCmd = true;
bool bExtraData = true;

void ReadAliases() {
	wchar_t aliasDirectory[MAX_PATH];
	wcscpy_s(aliasDirectory, replacementDirectory);
	wcscat_s(aliasDirectory, L"aliases.cfg");
	if (exists(aliasDirectory))
	{
		std::wifstream file(aliasDirectory);
		std::wstring str;
		while (std::getline(file, str))
		{
			str.erase(std::remove(str.begin(), str.end(), ' '), str.end());
			if (wcslen(str.c_str()) > 0)
			{
				if (wcscmp(str.substr(0, 1).c_str(), L"#"))
				{
					std::wstring temp;
					std::vector<std::wstring> split;
					std::wstringstream wss(str);
					while (std::getline(wss, temp, L'='))
					{
						split.push_back(temp);
					}
					aliases[split[1]] = split[0];
				}
			}
		}
		file.close();
	}
}

enum ProgramMode { Extract, Replace, Nothing };
enum ExtractionMode { Assembly, Precompiled };

ProgramMode mode = Extract;
ExtractionMode exMode = Precompiled;
int shaderAmount = 0;



char matchB[] = { 0xFF, 0xFF, 0x00, 0x00 };
typedef long(__stdcall* tCreatePixelShader)(LPDIRECT3DDEVICE9 pDevice, DWORD*, IDirect3DPixelShader9**);
tCreatePixelShader oD3D9CreatePixelShader = NULL;

typedef long(__stdcall* tSetPixelShader)(LPDIRECT3DDEVICE9 pDevice, IDirect3DPixelShader9*);
tSetPixelShader oD3D9SetPixelShader = NULL;

std::wifstream::pos_type filesize(const wchar_t* filename)
{
	std::wifstream in(filename, std::ifstream::ate | std::ifstream::binary);
	return in.tellg();
}

DWORD* pShader = NULL;
int size = 0;
FILE* f;
wchar_t shaderAmountStr[256];
wchar_t sourceShaderStr[MAX_PATH];
char sourceShaderStrMB[MAX_PATH];

IDirect3DPixelShader9* pDebugShader;
bool bInitializedDebugShader = false;
int currentDebuggingShader = 0;
bool buttonUp = false;
int lastPressTick;
int lastPressHoldThresh = 650;
int lastPressRepeat = 16;
bool lastPressHolding = false;
LPDIRECT3DDEVICE9 pGameDevice = NULL;
bool queuedReload = false;

long CreatePixelShaderWrapper(int shaderId, bool replacementOnly, LPDIRECT3DDEVICE9 pDevice, DWORD* pFunction, IDirect3DPixelShader9** ppShader)
{
	bool replaced = false;
	DWORD* pOldFunction = pFunction;
	if (mode == Replace)
	{
		wchar_t extraDataPath[MAX_PATH];
		wcscpy_s(shaderAmountStr, std::to_wstring(shaderId).c_str());
		wcscpy_s(destinationPath, replacementDirectory);
		auto it = aliases.find(shaderAmountStr);
		if (it != aliases.end())
		{
			wcscat_s(destinationPath, it->second.c_str());
		}
		else
		{
			wcscat_s(destinationPath, shaderAmountStr);
		}
		//wcscat_s(destinationPath, L"20");
		wcscpy_s(sourceShaderStr, destinationPath);

		if (bExtraData)
		{
			wcscpy_s(extraDataPath, destinationPath);
			wcscat_s(extraDataPath, L".data");
			if (exists(extraDataPath))
			{
				cExtraData* exData = new cExtraData();
				exData->fromFile(extraDataPath,pDevice);
				vExtraData[shaderId] = exData;
			}
		}
		wcscat_s(sourceShaderStr, L".msasm");
		
		LPD3DXBUFFER compiled_shader = NULL;
		LPD3DXBUFFER errs = NULL;
		ID3DBlob* compiled_shader2;
		ID3DBlob* errs2;

		if (exists(sourceShaderStr))
		{
			wprintf(L"Replacing Pixel Shader #");
			wprintf(shaderAmountStr);
			wprintf(L" with assembly shader.");
			wprintf(L"\n");
			HRESULT res = D3DXAssembleShaderFromFile(sourceShaderStr, NULL, NULL, 0, &compiled_shader, &errs);
			if (!SUCCEEDED(res) || &compiled_shader2 == nullptr)
			{
				wprintf(L"Failed! ");
				switch (res)
				{
				case D3DERR_INVALIDCALL:
					wprintf(L"Invalid call.");
					break;
				case D3DXERR_INVALIDDATA:
					wprintf(L"Invalid data.");
					break;
				case E_OUTOFMEMORY:
					wprintf(L"Out of memory.");
					break;
				}
				wprintf(L"\n");
			}
			else
			{
				wprintf(L"Success.");
				wprintf(L"\n");
				pFunction = (DWORD*)compiled_shader->GetBufferPointer();
				replaced = true;
			}

		}
		else
		{
			wcscpy_s(sourceShaderStr, destinationPath);
			wcscat_s(sourceShaderStr, L".hlsl");
			if (exists(sourceShaderStr))
			{
				wprintf(L"Replacing Pixel Shader #");
				wprintf(shaderAmountStr);
				wprintf(L" with source shader.");
				wprintf(L"\n");
				//_wfopen_s(&f, sourceShaderStr, L"r");
				//size = filesize(sourceShaderStr);
				//pShader = (DWORD*)malloc(size);
				//fread(pShader, size, 1, f);
				ID3DXBuffer* compiled_shader;
				ID3DXBuffer* errs;
				//D3DXAssembleShaderFromFile(sourceShaderStr, NULL, NULL, 0, &compiled_shader, &errs);
				// Prefer higher CS shader profile when possible as CS 5.0 provides better performance on 11-class hardware.
				/*
				LPCSTR profile = (device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0) ? "cs_5_0" : "cs_4_0";
				const D3D_SHADER_MACRO defines[] =
				{
					"EXAMPLE_DEFINE", "1",
					NULL, NULL
				};
				ID3DBlob* shaderBlob = nullptr;
				ID3DBlob* errorBlob = nullptr;
				HRESULT hr = D3DCompileFromFile(srcFile, defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
					entryPoint, profile,*/
				ZeroMemory(sourceShaderStrMB, MAX_PATH);
				WideCharToMultiByte(CP_ACP, 0, sourceShaderStr, wcslen(sourceShaderStr), sourceShaderStrMB, MAX_PATH, NULL, NULL);
				/*
				int fsize = filesize(sourceShaderStr);
				//DWORD* pShaderSource = (DWORD*)malloc(fsize);
				
				char* shaderBuff = new char[fsize];
				fopen_s(&f, sourceShaderStrMB, "r");
				fread(shaderBuff, fsize, 1, f);
				fclose(f);
				HRESULT res = D3DCompile(shaderBuff, strlen(shaderBuff), "shader", NULL, NULL, "main", "ps_3_0", 0, 0, &compiled_shader2, &errs2);
				*/
				std::ifstream ifs(sourceShaderStrMB);
				std::string content((std::istreambuf_iterator<char>(ifs)),
					(std::istreambuf_iterator<char>()));
				ifs.close();
				HRESULT res = D3DCompile(content.c_str(), strlen(content.c_str()), "shader", NULL, NULL, "main", "ps_3_0", 0, 0, &compiled_shader2, &errs2);
				
				if (!SUCCEEDED(res) || &compiled_shader2 == nullptr)
				{
					wprintf(L"Failed! Error code: ");
					std::wstring errCode = std::to_wstring(res);
					wprintf(errCode.c_str());
					/*
					switch (res)
					{
					case D3DERR_INVALIDCALL:
						wprintf(L"Invalid call.");
						break;
					case D3DXERR_INVALIDDATA:
						wprintf(L"Invalid data.");
						break;
					case E_OUTOFMEMORY:
						wprintf(L"Out of memory.");
						break;
					}*/
					wprintf(L"\n");
				}
				else
				{
					wprintf(L"Success.");
					wprintf(L"\n");
					pFunction = (DWORD*)compiled_shader2->GetBufferPointer();
					replaced = true;
				}
				
			}
			else
			{
				wcscat_s(destinationPath, L".cso");
				if (exists(destinationPath))
				{
					wprintf(L"Replacing Pixel Shader #");
					wprintf(shaderAmountStr);
					wprintf(L" with precompiled shader.");
					wprintf(L"\n");
					_wfopen_s(&f, destinationPath, L"r");
					size = filesize(destinationPath);
					pShader = (DWORD*)malloc(size);
					fread(pShader, size, 1, f);
					pFunction = pShader;
					fclose(f);
					replaced = true;
				}
			}
		}
	}
	if ((mode == Extract) && shaderId != 0)
	{
		wprintf(L"Extracting Pixel Shader #");
		std::wstring shaderAmountStr = std::to_wstring(shaderId);
		wprintf(shaderAmountStr.c_str());
		wprintf(L"\n");
		wcscpy_s(destinationPath, extractionDirectory);
		wcscat_s(destinationPath, shaderAmountStr.c_str());
		if (exMode == Precompiled)
			wcscat_s(destinationPath, L".cso");
		else
			wcscat_s(destinationPath, L".msasm");
		_wfopen_s(&f, destinationPath, L"wb");
		bool match = false;
		int size = 0;
		//char val[4];
		while (!match)
		{
			//memcpy(&val, pFunction + size, 4);
			match = true;
			for (unsigned int i = 0; i < 4; i++)
			{
				if (matchB[i] != *(char*)((intptr_t)pFunction + size + i))
				{
					match = false;
					break;
				}
			}
			size += 1;
		}
		if (exMode == Precompiled)
		{
			fwrite(pFunction, 1, size + 3, f);
			fclose(f);
		}
		else
		{
			ID3DBlob* disasm = NULL;
			D3DDisassemble(pFunction, size + 3, 0, "Extracted with D3DShaderReplacer", &disasm);
			fwrite(disasm->GetBufferPointer(), 1, disasm->GetBufferSize() - 1, f);
			fclose(f);
		}
	}
	if (replacementOnly && replaced == false)
		return 0;
	long res = oD3D9CreatePixelShader(pDevice, pFunction, ppShader);
	//if (!replacementOnly)
	for (std::map<IDirect3DPixelShader9*, int> ::iterator it = compiledShaders.begin(); it != compiledShaders.end(); ++it)
	{
		if (it->second == shaderId)
		{
			compiledShaders.erase(it);
			break;
		}
	}
	compiledShaders[*ppShader] = shaderId;
	pCompiledShaders[ppShader] = shaderId;
	return res;
}

void ReloadShaders() {
	//std::map<IDirect3DPixelShader9*, int> compiledShaders
	if (bExtraData)
	{
		for (std::map<int, cExtraData*>::iterator it = vExtraData.begin(); it != vExtraData.end(); ++it)
		{
			delete it->second;
		}
	}
	vExtraData.clear();
	aliases.clear();
	ReadAliases();
	wprintf(L"Reloading all replaced shaders and aliases.");
	wprintf(L"\n");
	for (std::map<IDirect3DPixelShader9**, int> ::iterator it = pCompiledShaders.begin(); it != pCompiledShaders.end(); ++it)
	{
		CreatePixelShaderWrapper(it->second, true, pGameDevice, nullptr, it->first);
	}
}

long __stdcall hkD3D9SetPixelShader(LPDIRECT3DDEVICE9 pDevice, IDirect3DPixelShader9* pShader)
{
	if (queuedReload)
	{
		queuedReload = false;
		ReloadShaders();
	}
	if (pGameDevice == NULL)
		pGameDevice = pDevice;
	int shaderId = 0;

	auto it = compiledShaders.find(pShader);
	if (it != compiledShaders.end())
	{
		if (it->second == currentDebuggingShader && bDebugMode)
		{
			pShader = pDebugShader;
			shaderId = 0;
		}
		else
			shaderId = it->second;
	}
	long res = oD3D9SetPixelShader(pDevice, pShader);
	if (bExtraData)
	{
		auto it2 = vExtraData.find(shaderId);
		if (it2 != vExtraData.end())
		{
			it2->second->applyData(pDevice);
		}
	}
	return res;
}


long __stdcall hkD3D9CreatePixelShader(LPDIRECT3DDEVICE9 pDevice, DWORD* pFunction, IDirect3DPixelShader9** ppShader)
{
	if (pGameDevice == NULL)
		pGameDevice = pDevice;
	if (!bInitializedDebugShader)
	{
		shaderAmount -= 1;
		bInitializedDebugShader = true;
		hkD3D9CreatePixelShader(pDevice, pFunction, &pDebugShader);
	}
	shaderAmount += 1;
	return CreatePixelShaderWrapper(shaderAmount,false,pDevice,pFunction,ppShader);
}

bool bShowMenu = false;
WNDPROC oWndProc = NULL;
LRESULT APIENTRY WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (bShowMenu)
	{
		// do your wndProc stuff here
	}

	return CallWindowProc(oWndProc, hwnd, uMsg, wParam, lParam);
}

std::wstring ExePath() {
	TCHAR buffer[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, buffer, MAX_PATH);
	std::wstring::size_type pos = std::wstring(buffer).find_last_of(L"\\/");
	return std::wstring(buffer).substr(0, pos);
}

void ReadSettings() {
	wchar_t settingsPath[MAX_PATH];
	wcscpy_s(settingsPath, ExePath().c_str());
	wcscat_s(settingsPath, L"\\D3DShaderReplacer.cfg");
	if (exists(settingsPath))
	{
		std::wifstream file(settingsPath);
		std::wstring str;
		while (std::getline(file, str))
		{
			str.erase(std::remove(str.begin(), str.end(), ' '), str.end());
			if (wcslen(str.c_str()) > 0)
			{
				if (wcscmp(str.substr(0, 1).c_str(), L"#"))
				{
					std::wstring temp;
					std::vector<std::wstring> split;
					std::wstringstream wss(str);
					while (std::getline(wss, temp, L'='))
					{
						split.push_back(temp);
					}
					bool boolValue = false;
					if (wcscmp(split[1].c_str(), L"True") == 0)
						boolValue = true;
					if (wcscmp(split[0].c_str(), L"Debug") == 0)
					{
						bDebugMode = boolValue;
					}
					if (wcscmp(split[0].c_str(), L"Prompt") == 0)
					{
						bPrompt = boolValue;
					}
					if (wcscmp(split[0].c_str(), L"CMD") == 0)
					{
						bCmd = boolValue;
					}
					if (wcscmp(split[0].c_str(), L"ExtraData") == 0)
					{
						bExtraData = boolValue;
					}
					if (wcscmp(split[0].c_str(), L"Mode") == 0)
					{
						if (wcscmp(split[1].c_str(), L"Extract") == 0)
							mode = Extract;
						if (wcscmp(split[1].c_str(), L"Replace") == 0)
							mode = Replace;
						if (wcscmp(split[1].c_str(), L"Nothing") == 0)
							mode = Nothing;
					}
					if (wcscmp(split[0].c_str(), L"Format") == 0)
					{
						if (wcscmp(split[1].c_str(), L"Assembly") == 0)
							exMode = Assembly;
						if (wcscmp(split[1].c_str(), L"Bytecode") == 0)
							exMode = Precompiled;
					}
				}
			}
		}
		file.close();
	}
}


bool bExit = false;
HMODULE g_Module;
BOOL WINAPI DllMain(HMODULE hModule, DWORD dwReason, LPVOID)
{
	switch (dwReason)
	{
	case DLL_PROCESS_ATTACH:
		extraDataIFace = new cExtraDataIFace();
		ReadSettings();
		int msgChoice2;
		if (bPrompt)
		{
			
			int msgChoice = MessageBox(NULL, L"Yes = Extract Shaders, No = Replace Shaders, Cancel = Do Nothing", L"Shader Replacer", MB_YESNOCANCEL);
			switch (msgChoice)
			{
			case IDCANCEL:
				mode = Nothing;
				break;
			case IDYES:
				mode = Extract;
				msgChoice2 = MessageBox(NULL, L"Yes = Extract to Assembly, No = Extract to Bytecode (Like in Precomp file)", L"Shader Replacer", MB_YESNO);
				if (msgChoice2 == IDYES)
					exMode = Assembly;
				else
					exMode = Precompiled;
				break;
			case IDNO:
				mode = Replace;
				break;
			}
		}
		if (mode != Nothing)
		{
			if (bPrompt)
			{
				msgChoice2 = MessageBox(NULL, L"Enable Debug Mode? (Debug shaders by pressing the Shift and Ctrl buttons)", L"Shader Replacer", MB_YESNO);
				if (msgChoice2 == IDYES)
					bDebugMode = true;
				else
					bDebugMode = false;
			}
			g_Module = hModule;
			CreateThread(nullptr, 0, [](LPVOID) -> DWORD
				{
					if (bCmd)
					{
						AllocConsole();
						freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
					}
					wcscpy_s(extractionDirectory, ExePath().c_str());
					wcscat_s(extractionDirectory, L"\\");
					wcscpy_s(replacementDirectory, extractionDirectory);
					wcscat_s(extractionDirectory, L"shader_extract");
					wcscat_s(replacementDirectory, L"shader_replace");
					wcscat_s(extractionDirectory, L"\\");
					wcscat_s(replacementDirectory, L"\\");
					wprintf(L"Extraction Directory is: ");
					wprintf(extractionDirectory);
					wprintf(L"\n");
					wprintf(L"Replacement Directory is: ");
					wprintf(replacementDirectory);
					wprintf(L"\n");
					if (mode == Extract)
						CreateDirectory(extractionDirectory, NULL);
					if (mode == Replace)
					{
						wcscpy_s(extractionDirectory, replacementDirectory);
						wcscat_s(extractionDirectory, L"cache");
						wcscat_s(extractionDirectory, L"\\");
					}
					ReadAliases();

					bool d3dInitialized = false;
					while (d3dInitialized == false)
					{
						d3dInitialized = init_D3D();
					}
					/*
					if (init_D3D())	// gets D3D methodes table
					{*/
						if (return_D3D() != 9)
						{
							wprintf(L"Shader replacer is currently only compatible with D3D9 software.");
							FreeLibraryAndExitThread(g_Module, 0);
						}
						methodesHook(106, hkD3D9CreatePixelShader, (LPVOID*)&oD3D9CreatePixelShader); // hook endscene
						if (bDebugMode || bExtraData)
							methodesHook(107, hkD3D9SetPixelShader, (LPVOID*)&oD3D9SetPixelShader); // hook endscene
					//}

					while (!bExit)
					{
						if (bDebugMode)
						{
							int curTick = GetTickCount();
							int diff = curTick - lastPressTick;
							bool valid = false;
							bool validHold = false;
							if (buttonUp == true)
							{
								if (!lastPressHolding)
								{
									if (diff > lastPressHoldThresh)
									{
										valid = true;
										validHold = true;
									}
								}
								else
								{
									if (diff > lastPressRepeat)
									{
										valid = true;
										validHold = true;
									}
								}
							}
							else
								valid = true;
							if ((GetKeyState(0x10) & 0x800))
							{
								if (valid == true)
								{
									lastPressTick = curTick;
									buttonUp = true;
									lastPressHolding = validHold;
									currentDebuggingShader += 1;
									if (currentDebuggingShader > shaderAmount)
										currentDebuggingShader = 0;
									wprintf(L"Now debugging shader #");
									std::wstring shadNum = std::to_wstring(currentDebuggingShader);
									wprintf(shadNum.c_str());
									wprintf(L"\n");
								}
							}
							else if ((GetKeyState(0x11) & 0x800))
							{
								if (valid == true)
								{
									lastPressTick = curTick;
									buttonUp = true;
									lastPressHolding = validHold;
									currentDebuggingShader -= 1;
									if (currentDebuggingShader < 0)
										currentDebuggingShader = shaderAmount;
									wprintf(L"Now debugging shader #");
									std::wstring shadNum = std::to_wstring(currentDebuggingShader);
									wprintf(shadNum.c_str());
									wprintf(L"\n");
								}
							}
							else if ((GetKeyState(0x23) & 0x800))
							{
								if (buttonUp == false)
								{
									lastPressTick = curTick;
									buttonUp = true;
									queuedReload = true;
								}
							}
							else
							{
								buttonUp = false;
								lastPressHolding = false;
							}
						}
						Sleep(1); // Sleeps until shutdown
					}

					methodesUnhook(); // disables and removes all hooks

					FreeLibraryAndExitThread(g_Module, 0);
				}, nullptr, 0, nullptr);
		}
		break;
	}

	return TRUE;
}