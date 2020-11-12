// CanvasModelViewer.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "CanvasModelViewer.h"

class CConsole
{
    FILE *m_stdin;
    FILE *m_stdout;
    FILE *m_stderr;
public:
    CConsole()
    {
        AllocConsole();

        freopen_s(&m_stdin, "CONIN$", "r", stdin); 
        freopen_s(&m_stdout, "CONOUT$", "w", stdout); 
        freopen_s(&m_stderr, "CONOUT$", "w", stderr); 
    }
    ~CConsole()
    {
        fclose(m_stdin);
        fclose(m_stdout);
        fclose(m_stderr);

        FreeConsole();
    }
};

//------------------------------------------------------------------------------------------------
struct QLogHostDeleter
{
    void operator()(QLog::CLogHost *p)
    {
        QLogDestroyLogHost(p);
    }
};

//------------------------------------------------------------------------------------------------
struct QLogClientDeleter
{
    void operator()(QLog::CLogClient *p)
    {
        QLogDestroyLogClient(p);
    }
};

class CLogOutput : public QLog::CLogOutput
{
    void OutputString(PCSTR sz)
    {
        OutputDebugStringA(sz); // Debugger
        fputs(sz, stdout); // Console
    }

    virtual void OutputBegin(QLog::Category LogCategory, INT64 DTimeInNS, const char *szLogSource, const char *szMessage)
    {
        auto CategoryToString = [](QLog::Category c)
        {
            switch (c)
            {
            default:
                throw(std::exception("Invalid Category"));
                break;
            case QLog::Category::None:
                return "NONE";
            case QLog::Category::Critical:
                return "CRITICA";
            case QLog::Category::Error:
                return "ERROR";
            case QLog::Category::Warning:
                return "WARNING";
            case QLog::Category::Info:
                return "INFO";
            case QLog::Category::Debug:
                return "DEBUG";
            }
        };

        static char szTimeString[256];

        PCSTR szDelim = ": ";

        if (DTimeInNS >= 0)
        {
            int hours = int(DTimeInNS / (1000ll * 1000ll * 60ll * 60ll));
            int rem = int(DTimeInNS - hours * (1000ll * 1000ll * 60ll * 60ll));
            int minutes = rem / (1000 * 1000 * 60);
            rem = rem - minutes * (1000 * 1000 * 60);
            int seconds = rem / (1000 * 1000);
            rem = rem - seconds * (1000 * 1000);
            int nanoseconds = rem;
            std::snprintf(szTimeString, sizeof(szTimeString), "[%i:%02i:%02i.%06i] ", hours, minutes, seconds, nanoseconds);
            OutputString(szTimeString);
        }

        OutputString(szLogSource);
        OutputString(szDelim);
        OutputString(CategoryToString(LogCategory));
        OutputString(szDelim);
        OutputString(szMessage);
    }
    virtual void OutputProperty(const char *szName, const char *szValue)
    {

    }
    virtual void OutputEnd()
    {
        OutputString("\n");
    }
};

//------------------------------------------------------------------------------------------------
class CApp
{
    std::string m_Title;
    std::unique_ptr<ThinWin::CWindow> m_pWindow;
    HINSTANCE m_hInstance;
    TGemPtr<XCanvas> m_pCanvas;
    TGemPtr<XGfxDevice> m_pGfxDevice;
    TGemPtr<XGfxGraphicsContext> m_pGfxContext;
    TGemPtr<XGfxSwapChain> m_pGfxSwapChain;
    CLogOutput m_LogOutput;
    CConsole m_Console;
    std::unique_ptr < QLog::CLogHost, QLogHostDeleter> m_pLogHost;
    std::unique_ptr < QLog::CLogClient, QLogClientDeleter> m_pLogClient;
    QLog::CBasicLogger m_Logger;

public:
    CApp(HINSTANCE hInstance, PCSTR szTitle) :
        m_Title(szTitle),
        m_hInstance(hInstance),
        m_pLogHost(QLogCreateLogHost("\\\\.\\pipe\\ModelViewerLog", 65536)),
        m_pLogClient(QLogCreateLogClient("\\\\.\\pipe\\ModelViewerLog")),
        m_Logger(m_pLogClient.get(), "MODEL VIEWER")
        {
            m_pLogClient->SetCategoryMask(QLog::CategoryMaskNotice);
            m_pLogHost->Execute(&this->m_LogOutput);
        };
    ~CApp() 
    {
        m_pGfxSwapChain = nullptr;
        m_pGfxContext = nullptr;
        m_pGfxDevice = nullptr;
        m_pCanvas = nullptr;
        m_pLogClient->Close();
        m_pLogHost->FlushAndFinish();
    };

    bool Initialize(int nCmdShow)
    {
        CFunctionSentinel Sentinel(m_Logger, "CApp::Initialize");

        // Construct the CWindow
        try
        {
            std::unique_ptr<ThinWin::CWindow> pWindow = std::make_unique<ThinWin::CWindow>(m_Title.c_str(), m_hInstance, WS_OVERLAPPEDWINDOW);
            if (!pWindow.get())
            {
                ThrowGemError(Result::OutOfMemory);
            }

            pWindow->ShowWindow(nCmdShow);
            pWindow->UpdateWindow();

            TGemPtr<XCanvas> pCanvas;
            ThrowGemError(CreateCanvas(XCanvas::IId, (void **)&pCanvas, m_pLogClient.get()));
            m_pCanvas = pCanvas;

            bool Windowed = true;
            UINT WidthIfWindowed = 1280;
            UINT HeightIfWindowed = 768;

            if (Windowed)
            {
                // Resize the window
                RECT rcWnd;
                GetWindowRect(pWindow->m_hWnd, &rcWnd);
                SetWindowPos(pWindow->m_hWnd, NULL, rcWnd.left, rcWnd.top, WidthIfWindowed, HeightIfWindowed, SWP_NOZORDER);
            }

            // Initialize canvas graphics system
            TGemPtr<Canvas::XGfxInstance> pCanvasGfx;
            ThrowGemError(pCanvas->InitCanvasGfx("CanvasGfxVk.dll", &pCanvasGfx));

            // Create the canvas graphics device
            TGemPtr<Canvas::XGfxDevice> pGfxDevice;
            ThrowGemError(pCanvasGfx->CreateGfxDevice(&pGfxDevice));

            // Create the graphics context
            TGemPtr<Canvas::XGfxGraphicsContext> pGfxContext;
            ThrowGemError(pGfxDevice->CreateGraphicsContext(&pGfxContext));

            // Create the swapchain
            TGemPtr<Canvas::XGfxSwapChain> pSwapChain;
            ThrowGemError(pGfxContext->CreateSwapChain(pWindow->m_hWnd, true, &pSwapChain, GfxFormat::R8G8B8A8_UNorm, 4));

            m_pWindow = std::move(pWindow);

            Canvas::ModelData::CAMERA_DATA CameraData = {};
            CameraData.NearClip = 0.1f;
            CameraData.FarClip = 1000.0f;
            CameraData.FovAngle = 45.0f;
            TGemPtr<Canvas::XCamera> pCamera;
            ThrowGemError(pCanvas->CreateCameraNode(&CameraData, &pCamera, "MainCamera"));

            //Canvas::ModelData::STATIC_MESH_DATA CubeMeshData = {};
            //Canvas::ModelData::TRIANGLE_GROUP_DATA CubeTriangleGroupData = {};
            //std::vector<Canvas::Math::FloatVector3> CubeVertices;
            //std::vector<Canvas::Math::FloatVector3> CubeNormals;
            //std::vector<Canvas::Math::UIntVector3> CubeIndices;
            //
            //// Cube top
            //CubeVertices.emplace_back( 1.f,  1.f,  1.f);
            //CubeVertices.emplace_back( 1.f, -1.f,  1.f);
            //CubeVertices.emplace_back(-1.f, -1.f,  1.f);
            //CubeVertices.emplace_back(-1.f,  1.f,  1.f);
            //CubeNormals.emplace_back( 0.f,  0.f,  1.f);
            //CubeNormals.emplace_back( 0.f,  0.f,  1.f);
            //CubeNormals.emplace_back( 0.f,  0.f,  1.f);
            //CubeNormals.emplace_back( 0.f,  0.f,  1.f);
            //
            //// Cube bottom
            //CubeVertices.emplace_back(-1.f, -1.f, -1.f);
            //CubeVertices.emplace_back(-1.f,  1.f, -1.f);
            //CubeVertices.emplace_back( 1.f,  1.f, -1.f);        
            //CubeVertices.emplace_back( 1.f, -1.f, -1.f);
            //CubeNormals.emplace_back( 0.f,  0.f, -1.f);
            //CubeNormals.emplace_back( 0.f,  0.f, -1.f);
            //CubeNormals.emplace_back( 0.f,  0.f, -1.f);
            //CubeNormals.emplace_back( 0.f,  0.f, -1.f);
            //
            //// Cube front
            //CubeVertices.emplace_back( 1.f, -1.f,  1.f);
            //CubeVertices.emplace_back(-1.f, -1.f,  1.f);
            //CubeVertices.emplace_back(-1.f, -1.f, -1.f);
            //CubeVertices.emplace_back( 1.f, -1.f, -1.f);
            //CubeNormals.emplace_back( 0.f, -1.f,  0.f);
            //CubeNormals.emplace_back( 0.f, -1.f,  0.f);
            //CubeNormals.emplace_back( 0.f, -1.f,  0.f);
            //CubeNormals.emplace_back( 0.f, -1.f,  0.f);
            //
            //// Cube back
            //CubeVertices.emplace_back(-1.f,  1.f, -1.f);
            //CubeVertices.emplace_back( 1.f,  1.f, -1.f);
            //CubeVertices.emplace_back( 1.f,  1.f,  1.f);
            //CubeVertices.emplace_back(-1.f,  1.f,  1.f);
            //CubeNormals.emplace_back( 0.f,  1.f,  0.f);
            //CubeNormals.emplace_back( 0.f,  1.f,  0.f);
            //CubeNormals.emplace_back( 0.f,  1.f,  0.f);
            //CubeNormals.emplace_back( 0.f,  1.f,  0.f);
            //
            //// Cube right
            //CubeVertices.emplace_back(  1.f,  1.f,  1.f);
            //CubeVertices.emplace_back(  1.f, -1.f,  1.f);
            //CubeVertices.emplace_back(  1.f, -1.f, -1.f);
            //CubeVertices.emplace_back(  1.f,  1.f, -1.f);
            //CubeNormals.emplace_back(  1.f,  0.f, 0.f);
            //CubeNormals.emplace_back(  1.f,  0.f, 0.f);
            //CubeNormals.emplace_back(  1.f,  0.f, 0.f);
            //CubeNormals.emplace_back(  1.f,  0.f, 0.f);
            //
            //// Cube left
            //CubeVertices.emplace_back( -1.f, -1.f, -1.f);
            //CubeVertices.emplace_back( -1.f,  1.f, -1.f);
            //CubeVertices.emplace_back( -1.f,  1.f,  1.f);
            //CubeVertices.emplace_back( -1.f, -1.f,  1.f);
            //CubeNormals.emplace_back( -1.f,  0.f, 0.f);
            //CubeNormals.emplace_back( -1.f,  0.f, 0.f);
            //CubeNormals.emplace_back( -1.f,  0.f, 0.f);
            //CubeNormals.emplace_back( -1.f,  0.f, 0.f);

            //// Cube indices
            //CubeIndices.emplace_back(0, 1, 2);
            //CubeIndices.emplace_back(2, 3, 0);
            //CubeIndices.emplace_back(4, 5, 6);
            //CubeIndices.emplace_back(6, 7, 4);
            //CubeIndices.emplace_back(8, 9, 10);
            //CubeIndices.emplace_back(10, 11, 8);
            //CubeIndices.emplace_back(12, 13, 14);
            //CubeIndices.emplace_back(14, 15, 12);
            //CubeIndices.emplace_back(16, 17, 18);
            //CubeIndices.emplace_back(18, 19, 16);
            //CubeIndices.emplace_back(20, 21, 22);
            //CubeIndices.emplace_back(22, 23, 20);

            //CubeMeshData.NumVertices = (UINT) CubeVertices.size();
            //CubeMeshData.pVertices = CubeVertices.data();
            //CubeMeshData.pNormals = CubeNormals.data();

            //CubeMeshData.pVertices = CubeVertices.data();
            //CubeMeshData.pNormals = CubeNormals.data();
            //CubeMeshData.NumTriangleGroups = 1;
            //CubeMeshData.pTriangleGroups = &CubeTriangleGroupData;
            //CubeTriangleGroupData.NumTriangles = (UINT) CubeIndices.size();
            //CubeTriangleGroupData.pTriangles = CubeIndices.data();

            //TGemPtr<XMesh> pMesh;
            //ThrowGemError(pGfxDevice->CreateStaticMesh(&CubeMeshData, &pMesh));

            m_pGfxDevice.Attach(pGfxDevice.Detach());
            m_pGfxContext.Attach(pGfxContext.Detach());
            m_pGfxSwapChain.Attach(pSwapChain.Detach());

            m_Logger.LogInfo("CanvasModelViewer: Initialization Complete");
            return true;
        }
        catch (std::bad_alloc &)
        {
            Sentinel.SetResultCode(Result::OutOfMemory);
            return false;
        }
        catch (Gem::GemError &e)
        {
            Sentinel.SetResultCode(e.Result());
            return false;
        }
    }

    ThinWin::CWindow *GetWindow() const
    { 
        return m_pWindow.get(); 
    }

    int Execute()
    {
        HACCEL hAccelTable = LoadAccelerators(m_hInstance, MAKEINTRESOURCE(IDC_CANVASMODELVIEWER));

        bool running = true;

        for (;running;)
        {
            const float ClearColors[2][4] =
            {
                { 1.f, 0.f, 0.f, 0.f },
                { 0.f, 0.f, 1.f, 0.f },
            };

            static UINT clearColorIndex = 0;

            m_pCanvas->FrameTick();
            TGemPtr<XGfxSurface> pSurface;
            m_pGfxSwapChain->GetSurface(&pSurface);
            m_pGfxContext->ClearSurface(pSurface, ClearColors[clearColorIndex]);
            clearColorIndex ^= 1;
            m_pGfxContext->FlushAndPresent(m_pGfxSwapChain);
            m_pGfxContext->Wait();

            // Main message loop:
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
                {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }

                if (msg.message == WM_QUIT)
                {
                    running = false;
                }
            }
        }
        return 0;
    }
};

const UINT MAX_LOADSTRING = 100;

// Forward declarations of functions included in this code module:
int APIENTRY WinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPSTR    lpCmdLine,
                     _In_ int       nCmdShow)
    {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Register the application window class
    char szTitle[MAX_LOADSTRING];
    LoadStringA(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    auto Atom = ThinWin::CWindow::RegisterWindowClass(hInstance);

    // Create the application object
    std::unique_ptr<CApp> pApp(std::make_unique<CApp>(hInstance, szTitle));

    // Initialize the application
    if (!pApp->Initialize (nCmdShow))
    {
        return FALSE;
    }

    return pApp->Execute();
}
