#pragma once
#include <vector>
#include <d3d9.h>

enum ExtraDataType { DATA_NONE, DATA_CUSTOM, DATA_TIME };
enum ExtraSamplerType { SAMPLER_DEPTH, SAMPLER_SCREEN, SAMPLER_TEXTURE };

class cExtraDataIFace
{
private:
	IDirect3DTexture9* depthTexture;
public:
	virtual IDirect3DBaseTexture9* getSampler(ExtraSamplerType, IDirect3DDevice9*);
	virtual bool testSampler(ExtraSamplerType, IDirect3DDevice9*);
};

static cExtraDataIFace* extraDataIFace;

class cExtraConstant
{
public:
	int registerID = 0;
	ExtraDataType dataTypes[4] = { DATA_NONE, DATA_NONE, DATA_NONE, DATA_NONE };
	float customData[4] = { 0.0F, 0.0F, 0.0F, 0.0F };
	~cExtraConstant();
};

class cExtraSampler
{
public:
	int samplerID = 0;
	ExtraSamplerType samplerType = SAMPLER_TEXTURE;
	IDirect3DTexture9* samplerTexture;
};

typedef std::vector<cExtraConstant*> ConstantVector;
typedef std::vector<cExtraSampler*> SamplerVector;

class cExtraData
{
	private:
		ConstantVector constants;
		SamplerVector samplers;
	public:
		bool fromFile(wchar_t* file, LPDIRECT3DDEVICE9 pDevice);
		bool applyData(LPDIRECT3DDEVICE9 pDevice);
		~cExtraData();
};