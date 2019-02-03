
#include "pch.h"
#include "simulator.h"
#include "Bridge.h"
#include "Wire.h"
#include "Resource.h"

#pragma comment (lib, "d2d1.lib")
#pragma comment (lib, "dwrite.lib")
#pragma comment (lib, "D3D11.lib")
#pragma comment (lib, "Dxgi.lib")
#pragma comment (lib, "Shlwapi")
#pragma comment (lib, "Version")
#pragma comment (lib, "Comctl32")
#pragma comment (lib, "msxml6.lib")
#pragma comment (lib, "comsuppwd.lib")

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace std;
using namespace D2D1;
using namespace edge;

static const char company_name[] = "Adi Gostin";
static const char app_name[] = "STP Simulator";
static const char app_version_string[] = "2.2";

#pragma region project_i
pair<Wire*, size_t> project_i::GetWireConnectedToPort (const Port* port) const
{
	for (auto& w : wires())
	{
		if (holds_alternative<ConnectedWireEnd>(w->GetP0()) && (get<ConnectedWireEnd>(w->GetP0()) == port))
			return { w.get(), 0 };
		else if (holds_alternative<ConnectedWireEnd>(w->GetP1()) && (get<ConnectedWireEnd>(w->GetP1()) == port))
			return { w.get(), 1 };
	}

	return { };
}

Port* project_i::FindConnectedPort (Port* txPort) const
{
	for (auto& w : wires())
	{
		for (size_t i = 0; i < 2; i++)
		{
			auto& thisEnd = w->GetPoints()[i];
			if (holds_alternative<ConnectedWireEnd>(thisEnd) && (get<ConnectedWireEnd>(thisEnd) == txPort))
			{
				auto& otherEnd = w->GetPoints()[1 - i];
				if (holds_alternative<ConnectedWireEnd>(otherEnd))
					return get<ConnectedWireEnd>(otherEnd);
				else
					return nullptr;
			}
		}
	}

	return nullptr;
}

std::unique_ptr<Wire> project_i::remove_wire (Wire* w)
{
	auto& wires = this->wires();
	for (size_t wi = 0; wi < wires.size(); wi++)
	{
		if (wires[wi].get() == w)
			return this->remove_wire(wi);
	}

	assert(false); return nullptr;
}

std::unique_ptr<Bridge> project_i::remove_bridge (Bridge* b)
{
	auto& bridges = this->bridges();
	for (size_t bi = 0; bi < bridges.size(); bi++)
	{
		if (bridges[bi].get() == b)
			return this->remove_bridge(bi);
	}

	assert(false); return nullptr;
}
#pragma endregion

class SimulatorApp : public event_manager, public simulator_app_i
{
	HINSTANCE const _hInstance;
	wstring _regKeyPath;
	vector<std::unique_ptr<project_window_i>> _projectWindows;

public:
	SimulatorApp (HINSTANCE hInstance)
		: _hInstance(hInstance)
	{
		wstringstream ss;
		ss << L"SOFTWARE\\" << company_name << L"\\" << ::app_name << L"\\" << ::app_version_string;
		_regKeyPath = ss.str();
	}

	virtual HINSTANCE GetHInstance() const override final { return _hInstance; }

	virtual void add_project_window (std::unique_ptr<project_window_i>&& pw) override final
	{
		pw->destroying().add_handler(&OnProjectWindowDestroying, this);
		_projectWindows.push_back(std::move(pw));
		this->event_invoker<project_window_added_e>()(_projectWindows.back().get());
	}

	static void OnProjectWindowDestroying (void* callbackArg, edge::win32_window_i* w)
	{
		auto pw = dynamic_cast<project_window_i*>(w);
		auto app = static_cast<SimulatorApp*>(callbackArg);

		pw->destroying().remove_handler (&OnProjectWindowDestroying, app);

		auto it = find_if (app->_projectWindows.begin(), app->_projectWindows.end(), [pw](auto& p) { return p.get() == pw; });
		assert (it != app->_projectWindows.end());
		app->event_invoker<project_window_removing_e>()(pw);
		auto pwLastRef = std::move(*it);
		app->_projectWindows.erase(it);
		app->event_invoker<project_window_removed_e>()(pwLastRef.get());
		if (app->_projectWindows.empty())
			PostQuitMessage(0);
	}

	virtual const std::vector<std::unique_ptr<project_window_i>>& project_windows() const override final { return _projectWindows; }

	virtual const wchar_t* GetRegKeyPath() const override final { return _regKeyPath.c_str(); }

	virtual const char* app_name() const override final { return ::app_name; }

	virtual const char* app_version_string() const override final { return ::app_version_string; }

	virtual project_window_added_e::subscriber project_window_added() override final { return project_window_added_e::subscriber(this); }

	virtual project_window_removing_e::subscriber project_window_removing() override final { return project_window_removing_e::subscriber(this); }

	virtual project_window_removed_e::subscriber project_window_removed() override final { return project_window_removed_e::subscriber(this); }

	WPARAM RunMessageLoop()
	{
		auto accelerators = LoadAccelerators (_hInstance, MAKEINTRESOURCE(IDR_ACCELERATOR1));

		MSG msg;
		while (GetMessage(&msg, nullptr, 0, 0))
		{
			if (msg.message == WM_MOUSEWHEEL)
			{
				HWND h = WindowFromPoint ({ GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam) });
				if (h != nullptr)
				{
					SendMessage (h, msg.message, msg.wParam, msg.lParam);
					continue;
				}
			}

			int translatedAccelerator = 0;
			for (auto& pw : _projectWindows)
			{
				if ((msg.hwnd == pw->hwnd()) || ::IsChild(pw->hwnd(), msg.hwnd))
				{
					translatedAccelerator = TranslateAccelerator (pw->hwnd(), accelerators, &msg);
					break;
				}
			}

			if (!translatedAccelerator)
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}

		return msg.wParam;
	}
};

static void RegisterApplicationAndFileTypes()
{
	auto exePath = make_unique<wchar_t[]>(MAX_PATH);
	DWORD dwRes = GetModuleFileName (nullptr, exePath.get(), MAX_PATH); assert(dwRes);
	wstringstream ss;
	ss << L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" << PathFindFileName(exePath.get());
	auto appPathKeyName = ss.str();

	bool notifyShell = false;
	auto buffer = make_unique<wchar_t[]>(MAX_PATH);
	DWORD cbData = MAX_PATH;
	auto ls = RegGetValue (HKEY_CURRENT_USER, appPathKeyName.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, buffer.get(), &cbData);
	if ((ls != ERROR_SUCCESS) || (_wcsicmp (buffer.get(), exePath.get()) != 0))
	{
		RegSetValue (HKEY_CURRENT_USER, appPathKeyName.c_str(), REG_SZ, exePath.get(), 0);
		notifyShell = true;
	}

	static constexpr wchar_t ProgID[] = L"AGO.StpFile.1";
	ss.str(L"");
	ss << L"SOFTWARE\\Classes\\" << ProgID << L"\\shell\\open\\command";
	auto progIdKeyName = ss.str();
	ss.str(L"");
	ss << L"\"" << exePath.get() << L"\" \"%%1\"";
	auto progIdKeyValue = ss.str();
	cbData = MAX_PATH;
	ls = RegGetValue (HKEY_CURRENT_USER, progIdKeyName.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, buffer.get(), &cbData);
	if ((ls != ERROR_SUCCESS) || (_wcsicmp (buffer.get(), progIdKeyValue.c_str()) != 0))
	{
		RegSetValue (HKEY_CURRENT_USER, progIdKeyName.c_str(), REG_SZ, progIdKeyValue.c_str(), 0);
		notifyShell = true;
	}

	ss.str(L"");
	ss << L"SOFTWARE\\Classes\\" << FileExtensionWithDot;
	auto fileExtKeyName = ss.str();
	cbData = MAX_PATH;
	ls = RegGetValue (HKEY_CURRENT_USER, fileExtKeyName.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, buffer.get(), &cbData);
	if ((ls != ERROR_SUCCESS) || (_wcsicmp (buffer.get(), ProgID) != 0))
	{
		RegSetValue (HKEY_CURRENT_USER, fileExtKeyName.c_str(), REG_SZ, ProgID, 0);
		notifyShell = true;
	}

	if (notifyShell)
		SHChangeNotify (SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

int APIENTRY wWinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	int tmp = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
	_CrtSetDbgFlag(tmp | _CRTDBG_LEAK_CHECK_DF);

	// We set DPI awareness programatically because Windows 10 won't run an executable whose manifest contains both <dpiAwareness> and <dpiAware>.
	// (Windows 10 looks only for first, Windows 7 looks only for the second, and we want to run on both.)
	if (auto proc_addr = GetProcAddress (GetModuleHandleA("User32.dll"), "SetProcessDpiAwarenessContext"))
	{
		auto proc = reinterpret_cast<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>(proc_addr);
		proc (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	}
	else
		SetProcessDPIAware();

	HRESULT hr = CoInitialize(0);

	RegisterApplicationAndFileTypes();

	bool tryDebugFirst = false;
#ifdef _DEBUG
	tryDebugFirst = true;
#endif

	com_ptr<ID3D11Device> d3d_device;
	com_ptr<ID3D11DeviceContext1> d3d_dc;

	{
		auto d3dFeatureLevel = D3D_FEATURE_LEVEL_9_1;
		com_ptr<ID3D11DeviceContext> deviceContext;

		if (tryDebugFirst)
		{
			hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
								   D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG,
								   &d3dFeatureLevel, 1,
								   D3D11_SDK_VERSION, &d3d_device, nullptr, &deviceContext);
		}

		if (!tryDebugFirst || FAILED(hr))
		{
			hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
								   D3D11_CREATE_DEVICE_BGRA_SUPPORT,
								   &d3dFeatureLevel, 1,
								   D3D11_SDK_VERSION, &d3d_device, nullptr, &deviceContext);
			assert(SUCCEEDED(hr));
		}

		d3d_dc = deviceContext;
	}

	com_ptr<IDWriteFactory> dwrite_factory;
	hr = DWriteCreateFactory (DWRITE_FACTORY_TYPE_SHARED, __uuidof (IDWriteFactory), reinterpret_cast<IUnknown**>(&dwrite_factory)); assert(SUCCEEDED(hr));

	int processExitValue;
	{
		SimulatorApp app (hInstance);

		auto project = project_factory();
		project_window_create_params params = 
		{
			&app, project, selection_factory, edit_area_factory, true, true, 1, SW_SHOW, d3d_dc, dwrite_factory
		};

		auto projectWindow = projectWindowFactory (params);
		app.add_project_window(move(projectWindow));

		processExitValue = (int)app.RunMessageLoop();
	}
	/*
	if (d3d_device->GetCreationFlags() & D3D11_CREATE_DEVICE_DEBUG)
	{
		deviceContext = nullptr;
		ID3D11DebugPtr debug;
		hr = d3d_device->QueryInterface(&debug);
		if (SUCCEEDED(hr))
			debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
	}
	*/
	CoUninitialize();

	return processExitValue;
}
