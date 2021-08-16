#include "pch.h"
#include "ExtraData.h"
#include <fstream>
#include <string>
#include <sstream>
#include <d3d9.h>
#include <d3dx9.h>
#include <DxErr.h>

#pragma comment(lib, "DxErr")
#define DEBUG

bool cExtraDataIFace::testSampler(ExtraSamplerType samplerType, IDirect3DDevice9* pDevice) {
	return true;
}

IDirect3DBaseTexture9* cExtraDataIFace::getSampler(ExtraSamplerType samplerType, IDirect3DDevice9* pDevice) {
	IDirect3DSurface9* pSurface;
	pDevice->GetDepthStencilSurface(&pSurface);
	//D3DSURFACE_DESC* pSurfaceDescription = new D3DSURFACE_DESC();
	if (depthTexture == NULL)
	{
		//pSurface->GetDesc(pSurfaceDescription);
		pDevice->CreateTexture(512, 512, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &depthTexture, NULL);
	}
		//IDirect3DTexture9* texture; // needs to be created, of course
		IDirect3DSurface9* dest = NULL; // to be our level0 surface of the texture
		depthTexture->GetSurfaceLevel(0, &pSurface);
		pDevice->StretchRect(pSurface, NULL, dest, NULL, D3DTEXF_LINEAR);
		return depthTexture;
}

cExtraConstant::~cExtraConstant() {
	delete[] dataTypes;
	delete[] customData;
}

std::wstring ExePathw() {
	TCHAR buffer[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, buffer, MAX_PATH);
	std::wstring::size_type pos = std::wstring(buffer).find_last_of(L"\\/");
	return std::wstring(buffer).substr(0, pos);
}

bool cExtraData::fromFile(wchar_t* file, LPDIRECT3DDEVICE9 pDevice)
{
	std::wifstream wFile(file);
	std::wstring str;
	while (std::getline(wFile, str))
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
				std::wstring lhand = split[0];
				std::wstring rhand = split[1];
				split.clear();
				std::wstring substrlhand = lhand.substr(0, 1);
				std::wstringstream lhandstream(lhand);
				while (std::getline(lhandstream, temp, L'.'))
				{
					split.push_back(temp);
				}
				lhand = split[0];
				int rID = stoi(lhand.substr(1));
				int offset = 0;
				if (split.size() > 1)
				{
					if (wcscmp(split[1].c_str(), L"y") == 0 || wcscmp(split[1].c_str(), L"g") == 0)
						offset = 1;
					if (wcscmp(split[1].c_str(), L"z") == 0 || wcscmp(split[1].c_str(), L"b") == 0)
						offset = 2;
					if (wcscmp(split[1].c_str(), L"w") == 0 || wcscmp(split[1].c_str(), L"a") == 0)
						offset = 3;
				}
				split.clear();

				
				if (wcscmp(substrlhand.c_str(), L"s") == 0)
				{
					cExtraSampler* extraSampler = new cExtraSampler();
					extraSampler->samplerID = rID;
					extraSampler->samplerType = SAMPLER_TEXTURE;
					std::wstring filePath = ExePathw();
					wchar_t fFilePath[MAX_PATH];
					wcscpy_s(fFilePath, filePath.c_str());
					wcscat_s(fFilePath, L"\\");
					wcscat_s(fFilePath, rhand.c_str());
					HRESULT texLoad = D3DXCreateTextureFromFile(
						pDevice,
						fFilePath,
						&extraSampler->samplerTexture
					);
					if (!SUCCEEDED(texLoad))
					{
						wprintf(L"Failed to load custom texture ");
						wprintf(fFilePath);
						wprintf(L", Error: ");
						wprintf(DXGetErrorString(texLoad));
						wprintf(L" :(\n");
						
					}
					samplers.push_back(extraSampler);
				}
				if (wcscmp(substrlhand.c_str(), L"c") == 0)
				{
					cExtraConstant* extraConstant = new cExtraConstant();
					extraConstant->registerID = rID;
					std::wstringstream rhandstream(rhand);
					while (std::getline(rhandstream, temp, L'.'))
					{
						split.push_back(temp);
					}
					for (unsigned int i = offset; i < split.size(); i++)
					{
						if (wcscmp(split[i].c_str(), L"time") == 0)
							extraConstant->dataTypes[i] = DATA_TIME;
						else
						{
							float finNumber = _wtof(split[i].c_str());
							extraConstant->dataTypes[i] = DATA_CUSTOM;
							extraConstant->customData[i] = finNumber;
						}
					}
#ifdef DEBUG
					wprintf(L"Loading constant to offset ");
					std::wstring offString = std::to_wstring(offset);
					wprintf(offString.c_str());
					wprintf(L", Register ID ");
					offString = std::to_wstring(rID);
					wprintf(offString.c_str());
					wprintf(L"\n");
#endif // DEBUG

					constants.push_back(extraConstant);
				}
			}
		}
	}
	wFile.close();
	return true;
}

bool cExtraData::applyData(LPDIRECT3DDEVICE9 pDevice)
{
	for (auto& element : constants)
	{
		float newData[4] = { 0.0F,0.0F,0.0F,0.0F };
		for (unsigned int i = 0; i < 4; i++)
		{
			if (element->dataTypes[i] == DATA_TIME)
			{
				newData[i] = (float)GetTickCount();
			}
			if (element->dataTypes[i] == DATA_CUSTOM)
			{
				newData[i] = element->customData[i];
			}
		}
		pDevice->SetPixelShaderConstantF(element->registerID, newData, 1);
	}
	for (auto& element : samplers)
	{
		if (extraDataIFace == NULL)
		{
			wprintf(L"Can't find cExtraDataIFace!!! Creating new one.\n");
			extraDataIFace = new cExtraDataIFace();
		}
		//auto finalSampler = extraDataIFace->getSampler(element->samplerType, pDevice);
		pDevice->SetTexture(element->samplerID, element->samplerTexture);
	}
	return true;
}

cExtraData::~cExtraData()
{
	for (auto &element : constants)
	{
		delete element;
	}
	for (auto& element : samplers)
	{
		delete element;
	}
	delete& constants;
	delete& samplers;
	return;
}