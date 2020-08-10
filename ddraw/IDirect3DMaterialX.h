#pragma once

class m_IDirect3DMaterialX : public IUnknown, public AddressLookupTableDdrawObject
{
private:
	IDirect3DMaterial3 *ProxyInterface = nullptr;
	DWORD ProxyDirectXVersion;
	ULONG RefCount = 1;

	// Convert Material
	m_IDirect3DDeviceX **D3DDeviceInterface = nullptr;
	D3DMATERIAL Material;
	bool MaterialSet = false;

	// Store d3d material version wrappers
	m_IDirect3DMaterial *WrapperInterface;
	m_IDirect3DMaterial2 *WrapperInterface2;
	m_IDirect3DMaterial3 *WrapperInterface3;

	// Wrapper interface functions
	REFIID GetWrapperType(DWORD DirectXVersion)
	{
		return (DirectXVersion == 1) ? IID_IDirect3DMaterial :
			(DirectXVersion == 2) ? IID_IDirect3DMaterial2 :
			(DirectXVersion == 3) ? IID_IDirect3DMaterial3 : IID_IUnknown;
	}
	bool CheckWrapperType(REFIID IID)
	{
		return (IID == IID_IDirect3DMaterial ||
			IID == IID_IDirect3DMaterial2 ||
			IID == IID_IDirect3DMaterial3) ? true : false;
	}
	IDirect3DMaterial *GetProxyInterfaceV1() { return (IDirect3DMaterial *)ProxyInterface; }
	IDirect3DMaterial2 *GetProxyInterfaceV2() { return (IDirect3DMaterial2 *)ProxyInterface; }
	IDirect3DMaterial3 *GetProxyInterfaceV3() { return ProxyInterface; }

	// Interface initialization functions
	void InitMaterial();
	void ReleaseMaterial();

public:
	m_IDirect3DMaterialX(IDirect3DMaterial3 *aOriginal, DWORD DirectXVersion) : ProxyInterface(aOriginal)
	{
		ProxyDirectXVersion = GetGUIDVersion(ConvertREFIID(GetWrapperType(DirectXVersion)));

		if (ProxyDirectXVersion != DirectXVersion)
		{
			LOG_LIMIT(3, "Creating interface " << __FUNCTION__ << "(" << this << ")" << " converting interface from v" << DirectXVersion << " to v" << ProxyDirectXVersion);
		}
		else
		{
			LOG_LIMIT(3, "Creating interface " << __FUNCTION__ << "(" << this << ") v" << DirectXVersion);
		}

		InitMaterial();

		ProxyAddressLookupTable.SaveAddress(this, (ProxyInterface) ? ProxyInterface : (void*)this);
	}
	m_IDirect3DMaterialX(m_IDirect3DDeviceX **D3DDInterface, DWORD DirectXVersion) : D3DDeviceInterface(D3DDInterface)
	{
		ProxyDirectXVersion = (!Config.Dd7to9) ? 3 : 9;

		if (ProxyDirectXVersion != DirectXVersion)
		{
			LOG_LIMIT(3, "Creating interface " << __FUNCTION__ << "(" << this << ")" << " converting interface from v" << DirectXVersion << " to v" << ProxyDirectXVersion);
		}
		else
		{
			LOG_LIMIT(3, "Creating interface " << __FUNCTION__ << "(" << this << ") v" << DirectXVersion);
		}

		InitMaterial();

		ProxyAddressLookupTable.SaveAddress(this, (ProxyInterface) ? ProxyInterface : (void*)this);
	}
	~m_IDirect3DMaterialX()
	{
		LOG_LIMIT(3, __FUNCTION__ << "(" << this << ")" << " deleting interface!");

		ReleaseMaterial();

		ProxyAddressLookupTable.DeleteAddress(this);
	}

	/*** IUnknown methods ***/
	STDMETHOD(QueryInterface) (THIS_ REFIID riid, LPVOID FAR * ppvObj, DWORD DirectXVersion);
	STDMETHOD(QueryInterface) (THIS_ REFIID riid, LPVOID FAR * ppvObj) { return QueryInterface(riid, ppvObj, GetGUIDVersion(riid)); }
	STDMETHOD_(ULONG, AddRef)(THIS);
	STDMETHOD_(ULONG, Release)(THIS);

	/*** IDirect3DMaterial methods ***/
	STDMETHOD(Initialize)(THIS_ LPDIRECT3D);
	STDMETHOD(SetMaterial)(THIS_ LPD3DMATERIAL);
	STDMETHOD(GetMaterial)(THIS_ LPD3DMATERIAL);
	STDMETHOD(GetHandle)(THIS_ LPDIRECT3DDEVICE3, LPD3DMATERIALHANDLE);
	STDMETHOD(Reserve)(THIS);
	STDMETHOD(Unreserve)(THIS);

	// Helper function
	void *GetWrapperInterfaceX(DWORD DirectXVersion);
};
