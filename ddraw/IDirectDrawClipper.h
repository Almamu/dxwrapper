#pragma once

class m_IDirectDrawClipper : public IDirectDrawClipper, public AddressLookupTableDdrawObject
{
private:
	IDirectDrawClipper *ProxyInterface = nullptr;
	REFIID WrapperID = IID_IDirectDrawClipper;
	ULONG RefCount = 1;
	DWORD clipperCaps = 0;						// Clipper flags
	HWND cliphWnd = nullptr;
	RGNDATA ClipList;
	bool IsClipListSet = false;
	bool IsClipListChangedFlag = false;

public:
	m_IDirectDrawClipper(IDirectDrawClipper *aOriginal) : ProxyInterface(aOriginal)
	{
		LOG_LIMIT(3, "Creating device " << __FUNCTION__ << "(" << this << ")");

		ProxyAddressLookupTable.SaveAddress(this, ProxyInterface);
	}
	m_IDirectDrawClipper(DWORD dwFlags) : clipperCaps(dwFlags)
	{
		LOG_LIMIT(3, "Creating device " << __FUNCTION__ << "(" << this << ")");
	}
	~m_IDirectDrawClipper()
	{
		LOG_LIMIT(3, __FUNCTION__ << "(" << this << ")" << " deleting device!");

		ProxyAddressLookupTable.DeleteAddress(this);
	}

	/*** IUnknown methods ***/
	STDMETHOD(QueryInterface) (THIS_ REFIID riid, LPVOID FAR * ppvObj);
	STDMETHOD_(ULONG, AddRef) (THIS);
	STDMETHOD_(ULONG, Release) (THIS);

	/*** IDirectDrawClipper methods ***/
	STDMETHOD(GetClipList)(THIS_ LPRECT, LPRGNDATA, LPDWORD);
	STDMETHOD(GetHWnd)(THIS_ HWND FAR *);
	STDMETHOD(Initialize)(THIS_ LPDIRECTDRAW, DWORD);
	STDMETHOD(IsClipListChanged)(THIS_ BOOL FAR *);
	STDMETHOD(SetClipList)(THIS_ LPRGNDATA, DWORD);
	STDMETHOD(SetHWnd)(THIS_ DWORD, HWND);
};
