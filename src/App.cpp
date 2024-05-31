#include "App.h"
#include "ResourceUploadBatch.h"
#include "DDSTextureLoader.h"
#include "VertexTypes.h"
#include "FileUtil.h"
#include <cassert>
#include <iostream>
#include <NsGui/Button.h>
#include <NsGui/IntegrationAPI.h>


namespace {
	const auto ClassName = TEXT("SampleWindowClass");


	template<typename T>
	void SafeRelease(T*& ptr)
	{
		if(ptr!=nullptr)
		{
			ptr->Release();
			ptr = nullptr;
		}
	}

}

static Noesis::IView* _view;
static Noesis::Ptr<Noesis::RenderDevice> _device;
void (*pResizeFunc)(int width, int height);
void (* pPassiveMotionFunc)(int x, int y);
void (* pMouseFunc)(int button, int state, int x, int y);


App::App(uint32_t width, uint32_t height)
	:m_hInst(nullptr)
	, m_hWnd(nullptr)
	, m_Width(width)
	,m_Height(height)
	,m_FrameIndex(0)
{
	
	for (auto i = 0u; i < FrameCount; ++i)
	{
		m_pColorBuffer[i] = nullptr;
		m_pCmdAllocator[i] = nullptr;
		m_FenceCounter[i] = 0;
	}
}

App::~App()
{}

void App::Run()
{
	if(InitApp())
	{
		MainLoop();
	}
	TermApp();
}

bool App::InitApp()
{
	if (!InitWnd())
	{
		return false;
	}
	if(!InitD3D())
	{
		return false;
	}

	if (!OnInit())
	{
		return false;
	}
	return true;
}






bool App::InitWnd()
{
	auto hInst = GetModuleHandle(nullptr);
	if(hInst==nullptr)
	{
		return false;
	}

	WNDCLASSEX wc = {};
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.hIcon = LoadIcon(hInst, IDI_APPLICATION);
	wc.hCursor = LoadCursor(hInst, IDC_ARROW);
	wc.hbrBackground = GetSysColorBrush(COLOR_BACKGROUND);
	wc.lpszMenuName = nullptr;
	wc.lpszClassName = ClassName;
	wc.hIconSm = LoadIcon(hInst, IDI_APPLICATION);
	if (!RegisterClassEx(&wc))
	{
		return false;
	}

	m_hInst = hInst;
	
	RECT rc = {};
	rc.right = static_cast<LONG>(m_Width);
	rc.bottom = static_cast<LONG>(m_Height);

	auto style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
	AdjustWindowRect(&rc, style, FALSE);

	m_hWnd = CreateWindowEx(0, ClassName, TEXT("Sample"), style, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, m_hInst, nullptr);
	if(m_hWnd==nullptr)
	{
		return false;
	}

	ShowWindow(m_hWnd, SW_SHOWNORMAL);
	UpdateWindow(m_hWnd);
	SetFocus(m_hWnd);
	return true;
}

void App::TermWnd	()
{
if(m_hInst!=nullptr)
{
	UnregisterClass(ClassName, m_hInst);
}
m_hInst = nullptr;
m_hWnd = nullptr;
}

bool App::InitD3D()
{
//デバッグレイヤー
#if defined(DEBUG)||defined(_DEBUG)
	{
		ComPtr<ID3D12Debug> pDebug;
		ComPtr<ID3D12Debug1> pDebug1;

		auto hr = D3D12GetDebugInterface(IID_PPV_ARGS(pDebug.GetAddressOf()));

		if (SUCCEEDED(hr))
		{
			//pDebug->EnableDebugLayer();
			hr = pDebug->QueryInterface(IID_PPV_ARGS(pDebug1.GetAddressOf()));
			if (SUCCEEDED(hr))
			{
				//GPUベースの検証機能
				pDebug1->SetEnableGPUBasedValidation(true);
				
			}
		}
		
	}
#endif
	//デバイス
	auto hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(m_pDevice.GetAddressOf()));
	if(FAILED(hr))
	{
		return false;
	}
	//デバッグレイヤー時にメッセージが出る箇所でブレークをかける
#if defined(DEBUG)||defined(_DEBUG)
	{
		ComPtr<ID3D12InfoQueue> pInfoQueue;
		auto hr = m_pDevice->QueryInterface(IID_PPV_ARGS(pInfoQueue.GetAddressOf()));
		if (SUCCEEDED(hr))
		{
			pInfoQueue->GetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR);
		}
	}
#endif
	//コマンドキュー
	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 0;
		hr = m_pDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(m_pQueue.GetAddressOf()));
		if(FAILED(hr))
		{
			return false;
		}
	}
	//スワップチェイン
	{
		//DXGIファクトリ作成
		ComPtr<IDXGIFactory4> pFactory = nullptr;
		hr = CreateDXGIFactory1(IID_PPV_ARGS(pFactory.GetAddressOf()));
		if (FAILED(hr))
		{
			return false;
		}

		DXGI_SWAP_CHAIN_DESC desc = {};
		desc.BufferDesc.Width = m_Width;
		desc.BufferDesc.Height = m_Height;
		desc.BufferDesc.RefreshRate.Numerator = 60;
		desc.BufferDesc.RefreshRate.Denominator = 1;
		desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = FrameCount;
		desc.OutputWindow = m_hWnd;
		desc.Windowed = TRUE;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

		//スワップチェイン作成
		ComPtr<IDXGISwapChain> pSwapChain;
		hr = pFactory->CreateSwapChain(m_pQueue.Get(), &desc, pSwapChain.GetAddressOf());
		if (FAILED(hr))
		{
			return false;
		}
		//IDXGISwapChain3
        hr = pSwapChain.As(&m_pSwapChain);
		if(FAILED(hr))
		{
			return false;
		}
		
		m_FrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();

		pFactory.Reset();
		pSwapChain.Reset();
	}
	//コマンドアロケータ
	{
		for (auto i = 0u; i < FrameCount; ++i)
		{
			hr = m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_pCmdAllocator[i].GetAddressOf()));
			if (FAILED(hr))
			{
				return false;
			}
		}
	}
	//コマンドリスト
	{
		hr = m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_pCmdAllocator[m_FrameIndex].Get(), nullptr, IID_PPV_ARGS(m_pCmdList.GetAddressOf()));
		if(FAILED(hr))
		{
			return false;
		}
	}
	//レンダーターゲットビュー
	{
		
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = FrameCount;
			desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NodeMask = 0;
		//ディスクリプタヒープ配列を作成
		hr = m_pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_pHeapRTV.GetAddressOf()));
		if (FAILED(hr))
		{
			return false;
		}
		
		auto handle = m_pHeapRTV->GetCPUDescriptorHandleForHeapStart();
		auto incrementSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		for (auto i = 0u; i < FrameCount; ++i)
		{
			//カラーバッファを取得
			hr = m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(m_pColorBuffer[i].GetAddressOf()));
			if(FAILED(hr))
			{
				return false;
			}

			D3D12_RENDER_TARGET_VIEW_DESC viewDesc = {};
			viewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
			viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			viewDesc.Texture2D.MipSlice = 0;
			viewDesc.Texture2D.PlaneSlice = 0;
			
			//レンダーターゲットビューを作成(オブジェクトを取得しない)
			m_pDevice->CreateRenderTargetView(m_pColorBuffer[i].Get(), &viewDesc, handle);
			//ハンドルを取得
			m_HandleRTV[i] = handle;
			//次のレンダーターゲットビューのハンドルのオブジェクトを取得
			handle.ptr += incrementSize;
		}
	}
	//深度ステンシルバッファ
	{
		D3D12_HEAP_PROPERTIES prop = {};
		prop.Type = D3D12_HEAP_TYPE_DEFAULT;
		prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		prop.CreationNodeMask = 1;
		prop.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC resDesc = {};
		resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resDesc.Alignment = 0;
		resDesc.Width = m_Width;
		resDesc.Height = m_Height;
		resDesc.DepthOrArraySize = 1;
		resDesc.MipLevels = 1;
		resDesc.Format = DXGI_FORMAT_D32_FLOAT;
		resDesc.SampleDesc.Count = 1;
		resDesc.SampleDesc.Quality = 0;
		resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE clearValue;
		clearValue.Format = DXGI_FORMAT_D32_FLOAT;
		clearValue.DepthStencil.Depth = 1.0;
		clearValue.DepthStencil.Stencil = 0;

		hr = m_pDevice->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS(m_pDepthBuffer.GetAddressOf()));
		if(FAILED(hr))
		{
			return false;
		}

		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = 1;
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		heapDesc.NodeMask = 0;

		hr = m_pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(m_pHeapDSV.GetAddressOf()));
	if(FAILED(hr))
	{
		return false;
	}

	auto handle = m_pHeapDSV->GetCPUDescriptorHandleForHeapStart();
	auto incrementSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc = {};
	viewDesc.Format = DXGI_FORMAT_D32_FLOAT;
	viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	viewDesc.Texture2D.MipSlice = 0;
	viewDesc.Flags = D3D12_DSV_FLAG_NONE;

	m_pDevice->CreateDepthStencilView(m_pDepthBuffer.Get(), &viewDesc, handle);
	m_HandleDSV = handle;
	}
	//フェンス
	{
		for (auto i = 0u; i < FrameCount; ++i)
		{
			m_FenceCounter[i] = 0;
		}

		hr = m_pDevice->CreateFence(m_FenceCounter[m_FrameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_pFence.GetAddressOf()));
		if (FAILED(hr))
			{
				return false;
			}
		std::cout << m_FrameIndex << std::endl;
		m_FenceCounter[m_FrameIndex]++;
		std::cout << m_FenceCounter[m_FrameIndex] << std::endl;
		m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if(m_FenceEvent==nullptr)
		{
			return false;
		}
	}
	//コマンドリストを閉じる。
	m_pCmdList->Close();

	{
		DXGI_SAMPLE_DESC desc = {};
		desc.Count = 1;
		desc.Quality = 0;
		NoesisInit(m_pDevice.Get(), m_pFence.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_D32_FLOAT, desc, false);
		_view->SetSize(m_Width, m_Height);
		setResizeFunc(&ResizeFunc);
		setPassiveMotionFunc(&MouseMoveFunc);
		setMouseFunc(&MouseFunc);
	}

	return true;
}

void App::setResizeFunc(void (*func)(int width, int height))
{
	pResizeFunc = func;
}

void App::setPassiveMotionFunc(void (*func)(int x, int y))
{
	pPassiveMotionFunc = func;
}
void App::setMouseFunc(void (*func)(int button, int state, int x, int y))
{
	pMouseFunc = func;
}

 void App::ResizeFunc(int width, int height)
{

	_view->SetSize(width, height);
}

  void App::MouseMoveFunc(int x, int y)
 {
	 _view->MouseMove(x, y);
 }

  void App::MouseFunc(int button, int state, int x, int y)
 {
	 if (button == LEFT_BUTTON)
	 {
		 if (state == DOWN)
		 {
			 _view->MouseButtonDown(x, y, Noesis::MouseButton_Left);
		 }
		 else
		 {
			 _view->MouseButtonUp(x, y, Noesis::MouseButton_Left);
		 }
	 }
 }

void App::NoesisInit(ID3D12Device* pDevice, ID3D12Fence* pFence, DXGI_FORMAT colorFormat, DXGI_FORMAT stencilFormat,
	DXGI_SAMPLE_DESC sampleDesc, bool sRGB)
{

	// A logging handler is installed here. You can also install a custom error handler and memory
	// allocator. By default errors are redirected to the logging handler
	Noesis::GUI::SetLogHandler([](const char*, uint32_t, uint32_t level, const char*, const char* msg)
		{
			// [TRACE] [DEBUG] [INFO] [WARNING] [ERROR]
			const char* prefixes[] = { "T", "D", "I", "W", "E" };
			printf("[NOESIS/%s] %s\n", prefixes[level], msg);
		});

	// https://www.noesisengine.com/docs/Gui.Core.Licensing.html
	Noesis::GUI::SetLicense(NS_LICENSE_NAME, NS_LICENSE_KEY);

	// Noesis initialization. This must be the first step before using any NoesisGUI functionality
	Noesis::GUI::Init();

	// Install providers for the theme and set default font
	NoesisApp::SetThemeProviders();

	// Load the Dark Blue theme
	Noesis::GUI::LoadApplicationResources(NoesisApp::Theme::DarkBlue());

	// For simplicity purposes, we are not using resource providers in this sample. ParseXaml() is
	// enough if there are no XAML dependencies (Dictionaries, Textures or Fonts)
	Noesis::Ptr<Noesis::Grid> xaml(Noesis::GUI::ParseXaml<Noesis::Grid>(
		R"(
        <Grid 
			xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
            xmlns:noesis="clr-namespace:NoesisGUIExtensions;assembly=Noesis.GUI.Extensions">

          <Viewbox>
            <StackPanel Margin="50">
              <Button Content="Hello World!" Margin="0,30,0,0"/>
              <Rectangle Height="5" Margin="-10,20,-10,0">
                <Rectangle.Fill>
                  <RadialGradientBrush>
                    <GradientStop Offset="0" Color="#40000000"/>
                    <GradientStop Offset="1" Color="#00000000"/>
                  </RadialGradientBrush>
                </Rectangle.Fill>
              </Rectangle>
            </StackPanel>
          </Viewbox>
        </Grid>
    )"));

	//View creation to render and interact with the user interface
	//We transfer the ownership to a global raw pointer instead of a Ptr<> because there is
	//no way in GLUT to shutdown and we don't want the Ptr<> to be released at global time
	 _view = Noesis::GUI::CreateView(xaml).GiveOwnership();
	_view->SetFlags(Noesis::RenderFlags_PPAA | Noesis::RenderFlags_LCD);
	_device = NoesisApp::D3D12Factory::CreateDevice(pDevice, pFence, colorFormat, stencilFormat, sampleDesc, sRGB);
	//IRendererをD3D１２デバイスから初期化	
	_view->GetRenderer()->Init(_device);



}
void App::TermApp()
{
	OnTerm();
	TermD3D();
	TermWnd();
}
void App::TermD3D()
{
	WaitGpu();
	if (m_FenceEvent != nullptr)
	{
		CloseHandle(m_FenceEvent);
		m_FenceEvent = nullptr;
	}
	m_pFence.Reset();
	m_pHeapRTV.Reset();
	for (auto i=0u; i < FrameCount; ++i)
	{
		m_pColorBuffer[i].Reset();
	}
	m_pCmdList.Reset();
	for (auto i = 0u; i < FrameCount; ++i)
	{
		m_pCmdAllocator[i].Reset();
	}
		m_pSwapChain.Reset();
		m_pQueue.Reset();
		m_pDevice.Reset();
	

}

bool App::OnInit()
{
	{
		std::wstring path;
		if(!SearchFilePath(L"res/teapot/teapot.obj",path))
		{
			return false;
		}
		if(!LoadMesh(path.c_str(),m_Meshes,m_Materials))
		{
			return false;
		}
		assert(m_Meshes.size() == 1);
	}
	//頂点バッファ
	{
		auto vertices = m_Meshes[0].Vertices.data();
		auto size = sizeof(MeshVertex) * m_Meshes[0].Vertices.size();
	
		D3D12_HEAP_PROPERTIES prop = {};
		prop.Type = D3D12_HEAP_TYPE_UPLOAD;
		prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		prop.CreationNodeMask = 1;
		prop.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Alignment = 0;
		desc.Width = size;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		auto hr = m_pDevice->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(m_pVB.GetAddressOf()));
		if (FAILED(hr))
		{
			return false;
		}
	
		void* ptr = nullptr;
		hr = m_pVB->Map(0, nullptr, &ptr);
		if(FAILED(hr))
		{
			return false;
		}

		memcpy(ptr, vertices, size);
		m_pVB->Unmap(0, nullptr);

		m_VBV.BufferLocation = m_pVB->GetGPUVirtualAddress();
		m_VBV.SizeInBytes = static_cast<UINT>(size);
		m_VBV.StrideInBytes = static_cast<UINT>(sizeof(MeshVertex));
	}
	//インデックスバッファ
	{
		auto indices = m_Meshes[0].Indices.data(); 
		auto size = sizeof(uint32_t) * m_Meshes[0].Indices.size();

		D3D12_HEAP_PROPERTIES prop = {};
		prop.Type = D3D12_HEAP_TYPE_UPLOAD;
		prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		prop.CreationNodeMask = 1;
		prop.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Alignment = 0;
		desc.Width = size;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		auto hr = m_pDevice->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(m_pIB.GetAddressOf()));
		if (FAILED(hr)) {
			return false;
		}
		void* ptr = nullptr;
		hr = m_pIB->Map(0, nullptr, &ptr);
		if(FAILED(hr))
		{
			return false;
		}
		memcpy(ptr, indices, size);
		m_pIB->Unmap(0, nullptr);

		m_IBV.BufferLocation = m_pIB->GetGPUVirtualAddress();
		m_IBV.Format = DXGI_FORMAT_R32_UINT;
		m_IBV.SizeInBytes = size;
	}
	//ディスクリプタヒープ
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 2 * FrameCount;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		desc.NodeMask = 0;
		auto hr = m_pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_pHeapCBV_SRV_UAV.GetAddressOf()));
		if (FAILED(hr))
		{
			return false;
		}
	}
	
	//定数バッファ
	{
		D3D12_HEAP_PROPERTIES prop = {};
		prop.Type = D3D12_HEAP_TYPE_UPLOAD;
		prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		prop.CreationNodeMask = 1;
		prop.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Alignment = 0;
		desc.Width = sizeof(Transform);
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		auto incrementSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		
		for (auto i = 0; i < FrameCount ; ++i)
		{
			auto hr = m_pDevice->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(m_pCB[i].GetAddressOf()));
			if (FAILED(hr))
			{
				return false;
			}

			//ディスクリプタヒープの先頭アドレス
			auto handleCPU = m_pHeapCBV_SRV_UAV->GetCPUDescriptorHandleForHeapStart();
			auto handleGPU = m_pHeapCBV_SRV_UAV->GetGPUDescriptorHandleForHeapStart();

			handleCPU.ptr += incrementSize * i;
			handleGPU.ptr += incrementSize * i;

			m_CBV[i].HandleCPU = handleCPU;
			m_CBV[i].HandleGPU = handleGPU;
			//ディスクリプタが示すリリースがおいてある位置
			m_CBV[i].Desc.BufferLocation = m_pCB[i]->GetGPUVirtualAddress();
			m_CBV[i].Desc.SizeInBytes = sizeof(Transform);
			//ディスクリプタヒープの先頭アドレスにCBV_SRV_UAV用のDescriptorを作成する。
			m_pDevice->CreateConstantBufferView(&m_CBV[i].Desc, handleCPU);

			//バッファのサイズは既に伝えているのでその分のメモリを確保してそのメモリの開始アドレスを返す。
			hr = m_pCB[i]->Map(0, nullptr, reinterpret_cast<void**>(&m_CBV[i].pBuffer));
			if(FAILED(hr))
			{
				return false;
			}

			auto eyePos = DirectX::XMVectorSet(0.0f, 1.0f, 2.0f, 0.0f);
			auto targetPos = DirectX::XMVectorZero();
			auto upward = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			auto fovY = DirectX::XMConvertToRadians(37.5f);
			auto aspect = static_cast<float>(m_Width) / static_cast<float>(m_Height);
			//定数バッファはデータが１つなのでストライドが要らない＋コードで生成した値を使いたいからmemcpyではなくポインタの関節演算子でメモリに直接値を設定してる。
			m_CBV[i].pBuffer->World = DirectX::XMMatrixIdentity();
			m_CBV[i].pBuffer->View = DirectX::XMMatrixLookAtRH(eyePos, targetPos, upward);
			m_CBV[i].pBuffer->Proj = DirectX::XMMatrixPerspectiveFovRH(fovY, aspect, 1.0f, 1000.0f);
		}
	}
	//テクスチャ
	{
		std::wstring texturePath;
		if (!SearchFilePath(L"res/teapot/default.dds", texturePath))
		{
			std::cout << "Texture not found!" << std::endl;
			return false;
		}

		DirectX::ResourceUploadBatch batch(m_pDevice.Get());
		batch.Begin();

		auto hr = DirectX::CreateDDSTextureFromFile(m_pDevice.Get(), batch, texturePath.c_str(), m_Texture.pResource.GetAddressOf(), true);
		if (FAILED(hr))
		{
			return false;
		}

		auto future = batch.End(m_pQueue.Get());
		future.wait();

		auto incrementSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		auto handleCPU = m_pHeapCBV_SRV_UAV->GetCPUDescriptorHandleForHeapStart();
		auto handleGPU = m_pHeapCBV_SRV_UAV->GetGPUDescriptorHandleForHeapStart();

		//CPUから参照するヒープポインタ
		handleCPU.ptr += incrementSize *2;
		//GPUから参照するヒープポインタ
		//インデックスがずれてると違うディスクリプタを参照することになるから不具合の原因になる。
		handleGPU.ptr += incrementSize *2;
		m_Texture.HandleCPU = handleCPU;
		m_Texture.HandleGPU = handleGPU;


		auto textureDesc = m_Texture.pResource->GetDesc();

		D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
		viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		viewDesc.Format = textureDesc.Format;
		viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		viewDesc.Texture2D.MostDetailedMip = 0;
		viewDesc.Texture2D.MipLevels = textureDesc.MipLevels;
		viewDesc.Texture2D.PlaneSlice = 0;
		viewDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		//ディスクリプタヒープにディスクリプタを作成
		m_pDevice->CreateShaderResourceView(m_Texture.pResource.Get(), &viewDesc,handleCPU);
	}
	
	//ルートシグニチャ
	{
		auto flag = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		flag |= D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS;
		flag |= D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;
		flag |= D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		D3D12_ROOT_PARAMETER param[2] = {};
		//定数バッファ（座標変換用）
		param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		param[0].Descriptor.ShaderRegister = 0;
		param[0].Descriptor.RegisterSpace = 0;
		param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		//テクスチャ
		D3D12_DESCRIPTOR_RANGE range = {};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		range.NumDescriptors = 1;
		range.BaseShaderRegister = 0;
		range.RegisterSpace = 0;
		range.OffsetInDescriptorsFromTableStart = 0;

		param[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		param[1].DescriptorTable.NumDescriptorRanges = 1;
		param[1].DescriptorTable.pDescriptorRanges = &range;
		param[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		//スタティックサンプラー
		//サンプラーはテクスチャアドレッシングモードやテクスチャフィルタリングを使うために必要。
		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.MipLODBias = D3D12_DEFAULT_MIP_LOD_BIAS;
		sampler.MaxAnisotropy = 1;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = -D3D12_FLOAT32_MAX;
		sampler.MaxLOD = +D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC desc = {};
		desc.NumParameters = 2;
		desc.NumStaticSamplers = 1;
		desc.pParameters = param;
		desc.pStaticSamplers = &sampler;
		desc.Flags = flag;

		ComPtr<ID3DBlob> pBlob;
		ComPtr<ID3DBlob> pErrorBlob;

		auto hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, pBlob.GetAddressOf(), pErrorBlob.GetAddressOf());
		if (FAILED(hr))
		{
			return false;
		}

		hr = m_pDevice->CreateRootSignature(0, pBlob->GetBufferPointer(), pBlob->GetBufferSize(), IID_PPV_ARGS(m_pRootSignature.GetAddressOf()));
		if (FAILED(hr))
		{
			return false;
		}

	}
	

	//パイプラインステート
	{
		D3D12_RASTERIZER_DESC descRS;
		descRS.FillMode = D3D12_FILL_MODE_SOLID;
		descRS.CullMode = D3D12_CULL_MODE_NONE;
		descRS.FrontCounterClockwise = FALSE;
		descRS.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		descRS.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		descRS.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		descRS.DepthClipEnable = FALSE;
		descRS.MultisampleEnable = FALSE;
		descRS.AntialiasedLineEnable = FALSE;
		descRS.ForcedSampleCount = 0;
		descRS.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

		D3D12_RENDER_TARGET_BLEND_DESC descRTBS = {
			FALSE,FALSE,
			D3D12_BLEND_ONE,D3D12_BLEND_ZERO,D3D12_BLEND_OP_ADD,
			D3D12_BLEND_ONE,D3D12_BLEND_ZERO,D3D12_BLEND_OP_ADD,
			D3D12_LOGIC_OP_NOOP,
			D3D12_COLOR_WRITE_ENABLE_ALL
		};
		D3D12_BLEND_DESC descBS;
		descBS.AlphaToCoverageEnable = FALSE;
		descBS.IndependentBlendEnable = FALSE;
		for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
		{
			descBS.RenderTarget[i] = descRTBS;
		}
		D3D12_DEPTH_STENCIL_DESC descDSS = {};
		descDSS.DepthEnable = TRUE;
		descDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		descDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		descDSS.StencilEnable = FALSE;

		ComPtr<ID3DBlob> pVSBlob;
		ComPtr<ID3DBlob> pPSBlob;

		std::wstring vsPath;
		std::wstring psPath;

		if (!SearchFilePath(L"SimpleTexVS.cso", vsPath))
		{
			return false;
		}
		if(!SearchFilePath(L"SimpleTexPS.cso",psPath))
		{
			return false;
		}

		auto hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			printf("頂点シェーダーの読み込みに失敗");
			return false;
		}
		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			printf("ピクセルシェーダーの読み込みに失敗");
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.InputLayout = MeshVertex::InputLayout;
		desc.pRootSignature = m_pRootSignature.Get();
		desc.VS = { pVSBlob->GetBufferPointer(),pVSBlob->GetBufferSize() };
			desc.PS= { pPSBlob->GetBufferPointer(),pPSBlob->GetBufferSize() };
			desc.RasterizerState = descRS;
			desc.BlendState = descBS;
			desc.DepthStencilState = descDSS;
			//desc.DepthStencilState.DepthEnable = FALSE;
			//desc.DepthStencilState.StencilEnable = FALSE;
			desc.SampleMask = UINT_MAX;
			desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			desc.NumRenderTargets = 1;
			desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
			desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;

			hr = m_pDevice->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(m_pPSO.GetAddressOf()));
			if(FAILED(hr))
			{
				//シェーダーコードのセマンティックの誤字などするとパイプラインステートオブジェクトの作成に失敗する
				std::cout << "グラフィックスパイプラインステートの生成に失敗\nシェーダーの誤字や設定ミスが大いに考えられる" << std::endl;
				return false;
			}
	}

	//ビューポートとシザー矩形
	{
		m_Viewport.TopLeftX = 0;
		m_Viewport.TopLeftY = 0;
		m_Viewport.Width = static_cast<float>(m_Width);
		m_Viewport.Height = static_cast<float>(m_Height);
		m_Viewport.MinDepth = 0.0f;
		m_Viewport.MaxDepth = 1.0f;

		m_Scissor.left = 0;
		m_Scissor.right = m_Width;
		m_Scissor.top = 0;
		m_Scissor.bottom = m_Height;

	}
	
	return true;

}

void App::OnTerm()
{
	
	for (auto i = 0; i < FrameCount; ++i)
	{
		if (m_pCB[i].Get() != nullptr)
		{
			m_pCB[i]->Unmap(0, nullptr);
			memset(&m_CBV[i], 0, sizeof(m_CBV[i]));
		}
		m_pCB[i].Reset();
	}
	m_pIB.Reset();
	m_pVB.Reset();
	m_pPSO.Reset();
	m_pHeapCBV_SRV_UAV.Reset();

	m_VBV.BufferLocation = 0;
	m_VBV.SizeInBytes = 0;
	m_VBV.StrideInBytes = 0;
	m_IBV.BufferLocation = 0;
	m_IBV.Format = DXGI_FORMAT_UNKNOWN;
	m_IBV.SizeInBytes = 0;
	m_pRootSignature.Reset();
	m_Texture.pResource.Reset();
	m_Texture.HandleCPU.ptr = 0;
	m_Texture.HandleGPU.ptr = 0;
	
}
void App::MainLoop()
{
	
	MSG msg = {};
	while(WM_QUIT!=msg.message)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE) == TRUE)
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			Render();
		}
	}
}
//コマンドリストに描画命令を積んでいく
void App::Render()
{
	{
		static uint64_t startTime = Noesis::HighResTimer::Ticks();
		// Update (layout, animations). Note that global time is used, not a delta
		uint64_t time = Noesis::HighResTimer::Ticks();
		_view->Update(Noesis::HighResTimer::Seconds(time - startTime));

		// Offscreen rendering phase populates textures needed by the on-screen rendering
		_view->GetRenderer()->UpdateRenderTree();
		_view->GetRenderer()->RenderOffscreen();
	}

	{
		m_RotateAngle += 0.025f;
		m_CBV[m_FrameIndex].pBuffer->World = DirectX::XMMatrixRotationY(m_RotateAngle);
	}
	//コマンドアロケータとコマンドリストの記録開始
	m_pCmdAllocator[m_FrameIndex]->Reset();
	m_pCmdList->Reset(m_pCmdAllocator[m_FrameIndex].Get(), nullptr);

	
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = m_pColorBuffer[m_FrameIndex].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_pCmdList->ResourceBarrier(1, &barrier);

	//レンダーターゲットをセット
	m_pCmdList->OMSetRenderTargets(1, &m_HandleRTV[m_FrameIndex], FALSE,&m_HandleDSV);
	float clearColor[] = { 0.25f,0.25f,0.25f,1.0f };
	m_pCmdList->ClearRenderTargetView(m_HandleRTV[m_FrameIndex], clearColor, 0, nullptr);
	m_pCmdList->ClearDepthStencilView(m_HandleDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	
	{
		//ルートシグネチャ（ディスクリプタテーブルたちの管理）
		m_pCmdList->SetGraphicsRootSignature(m_pRootSignature.Get());
		//ディスクリプタテーブルがどのディスクリプタヒープを参照するか
		m_pCmdList->SetDescriptorHeaps(1, m_pHeapCBV_SRV_UAV.GetAddressOf());
		//定数バッファの開始場所
		//ルートシグネチャが直接参照するCBVを指定する。
		m_pCmdList->SetGraphicsRootConstantBufferView(0, m_CBV[m_FrameIndex].Desc.BufferLocation);
		//ディスクリプタテーブルが参照するディスクリプタを指定する
		m_pCmdList->SetGraphicsRootDescriptorTable(1, m_Texture.HandleGPU);
		//パイプラインステート(シェーダの情報)
		m_pCmdList->SetPipelineState(m_pPSO.Get());

		m_pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		//VBVポインタ
		m_pCmdList->IASetVertexBuffers(0, 1, &m_VBV);
		m_pCmdList->IASetIndexBuffer(&m_IBV);
		//ビューポート設定
		m_pCmdList->RSSetViewports(1, &m_Viewport);
		//矩形設定
		m_pCmdList->RSSetScissorRects(1, &m_Scissor);
		//描画命令(マテリアルの数だけ実行される)
		auto count = static_cast<uint32_t>(m_Meshes[0].Indices.size());
		m_pCmdList->DrawIndexedInstanced(count, 1, 0, 0, 0);
		NoesisApp::D3D12Factory::SetCommandList(_device, m_pCmdList.Get(), m_FenceCounter[m_FrameIndex]);
		_view->GetRenderer()->Render();

	}
	
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = m_pColorBuffer[m_FrameIndex].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_pCmdList->ResourceBarrier(1, &barrier);
	//コマンドリストを閉じる
	m_pCmdList->Close();
	ID3D12CommandList* ppCmdLists[] = { m_pCmdList.Get()};
	m_pQueue->ExecuteCommandLists(1, ppCmdLists);
	Present(1);


}
//present関数が呼び出されるまでそのフレームで描画されている画面はずっと１フレーム前のもの。
void App::Present(uint32_t interval)
{
	//バッファスワップ
	m_pSwapChain->Present(interval, 0);
	const auto currentValue = m_FenceCounter[m_FrameIndex];
	std::cout << m_FenceCounter[m_FrameIndex] << std::endl;

	//GPU内のコマンドキューの処理がすべて終わったらインクリメントされた値に変更する。
	m_pQueue->Signal(m_pFence.Get(), currentValue);
	//更新後の現在のバックバッファのインデックスを取得する。
	//m_FrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();
	//まだコマンドキューが終了していないことを確認する
	//もし既に完了してしまっていた場合、setEventOnCompletion関数が無限に待ってしまいイベントを起こさないから確認している。
	if (m_pFence->GetCompletedValue() < currentValue)
	{
		//フェンスの値が指定値になったらイベントを起こす。
		m_pFence->SetEventOnCompletion(currentValue, m_FenceEvent);
		//イベントが発生するまで待つ。
		WaitForSingleObjectEx(m_FenceEvent, INFINITE,FALSE);
	}
	m_FrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();
	m_FenceCounter[m_FrameIndex]++;
}

void App::WaitGpu()
{
	assert(m_pQueue != nullptr);
	assert(m_pFence != nullptr);
	assert(m_FenceEvent != nullptr);

	m_pQueue->Signal(m_pFence.Get(), m_FenceCounter[m_FrameIndex]);
	m_pFence->SetEventOnCompletion(m_FenceCounter[m_FrameIndex], m_FenceEvent);
	WaitForSingleObjectEx(m_FenceEvent, INFINITE, FALSE);
	m_FenceCounter[m_FrameIndex]++;
}


LRESULT CALLBACK App::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
		int x, y;
		TCHAR szTemp[1024];

	case WM_SIZE:
	{
		if (pResizeFunc != nullptr)
		{
			pResizeFunc(LOWORD(lParam), HIWORD(lParam));
		}

		break;
	}
	case WM_LBUTTONUP:
	{
		if (pMouseFunc != nullptr)
		{
			pMouseFunc(LEFT_BUTTON, UP, LOWORD(lParam), HIWORD(lParam));

		}

		y = HIWORD(lParam);
		x = LOWORD(lParam);
		wsprintf(szTemp, TEXT("WM_LBUTTONUP (%d, %d)"), x, y);
		SetWindowText(hWnd, szTemp);
		break;
	}

	case WM_LBUTTONDOWN:
	{
		if (pMouseFunc != nullptr)
		{
			pMouseFunc(LEFT_BUTTON, DOWN, LOWORD(lParam), HIWORD(lParam));

		}

		y = HIWORD(lParam);
		x = LOWORD(lParam);
		wsprintf(szTemp, TEXT("WM_LBUTTONDOWN (%d, %d)"), x, y);
		SetWindowText(hWnd, szTemp);
		break;
	}

	case WM_MOUSEMOVE:
	{
		if (pPassiveMotionFunc != nullptr)
		{
			pPassiveMotionFunc(LOWORD(lParam), HIWORD(lParam));
		}
		
		y = HIWORD(lParam);
		x = LOWORD(lParam);
		wsprintf(szTemp, TEXT("WM_MOUSEMOVE (%d, %d)"), x, y);
		SetWindowText(hWnd, szTemp);
		break;
	}

	case WM_DESTROY:
	{ PostQuitMessage(0); }
	break;

	default:
	{}
	break;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}