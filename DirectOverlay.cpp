#include "DirectOverlay.h"
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <fstream>
#include <comdef.h>
#include <iostream>
#include <ctime>
#include <tchar.h>
#include <mutex>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "dwrite.lib")

ID2D1Factory* factory = NULL;
ID2D1HwndRenderTarget* target = NULL;
ID2D1SolidColorBrush* solid_brush = NULL;
IDWriteFactory* w_factory = NULL;
IDWriteTextFormat* w_format = NULL;
IDWriteTextLayout* w_layout = NULL;
HWND overlayWindow = NULL;
HINSTANCE appInstance = NULL;
HWND(*targetWindow)(void) = NULL;
HWND selfWindow = NULL;
time_t preTime = clock();
time_t showTime = clock();
int fps = 0;

bool o_Foreground = true;
bool o_DrawFPS = false;
bool o_VSync = false;
std::wstring fontname = L"Courier";

std::mutex mutexEnable;
BOOL enable = TRUE;

std::mutex mutexEnd;
BOOL end = TRUE;

DirectOverlayCallback drawLoopCallback = NULL;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

void DrawString(const std::wstring& str, float fontSize, float x, float y, 
	float r, float g, float b, float a/* = 1*/, float opacity/* = 1*/)
{
	RECT re;
	GetClientRect(overlayWindow, &re);
	FLOAT dpix, dpiy;
	dpix = static_cast<float>(re.right - re.left);
	dpiy = static_cast<float>(re.bottom - re.top);
	HRESULT res = w_factory->CreateTextLayout(
		str.c_str(), str.length(), w_format, dpix, dpiy, &w_layout);
	if (SUCCEEDED(res))
	{
		DWRITE_TEXT_RANGE range = { 0, str.length() };
		w_layout->SetFontSize(fontSize, range);
		solid_brush->SetColor(D2D1::ColorF(r, g, b, a));
		solid_brush->SetOpacity(opacity);
		target->DrawTextLayout(D2D1::Point2F(x, y), w_layout, solid_brush);
		w_layout->Release();
		w_layout = NULL;
	}
}

void DrawBox(float x, float y, float width, float height, float thickness, float r, float g, float b, float a, bool filled, float opacity/* = 1*/)
{
	solid_brush->SetColor(D2D1::ColorF(r, g, b, a));
	solid_brush->SetOpacity(opacity);
	if (filled)  target->FillRectangle(D2D1::RectF(x, y, x + width, y + height), solid_brush);
	else target->DrawRectangle(D2D1::RectF(x, y, x + width, y + height), solid_brush, thickness);
}

void DrawLine(float x1, float y1, float x2, float y2, float thickness, float r, float g, float b, float a, float opacity/* = 1*/) 
{
	solid_brush->SetColor(D2D1::ColorF(r, g, b, a));
	solid_brush->SetOpacity(opacity);
	target->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), solid_brush, thickness);
}

void DrawCircle(float x, float y, float radius, float thickness, float r, float g, float b, float a, bool filled, float opacity/* = 1*/)
{
	solid_brush->SetColor(D2D1::ColorF(r, g, b, a));
	solid_brush->SetOpacity(opacity);
	if (filled) target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), radius, radius), solid_brush);
	else target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), radius, radius), solid_brush, thickness);
}

void DrawEllipse(float x, float y, float width, float height, float thickness, float r, float g, float b, float a, bool filled, float opacity/* = 1*/)
{
	solid_brush->SetColor(D2D1::ColorF(r, g, b, a));
	solid_brush->SetOpacity(opacity);
	if (filled) target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), width, height), solid_brush);
	else target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), width, height), solid_brush, thickness);
}

void d2oSetup(HWND(*_targetWindow)(void)) {
	targetWindow = _targetWindow;

	WNDCLASS wc = { };
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = GetModuleHandle(0);
	wc.lpszClassName = _T("d2do");
	RegisterClass(&wc);
	overlayWindow = CreateWindowEx(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
		wc.lpszClassName, _T("D2D Overlay"), WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, NULL, wc.hInstance, NULL);

	MARGINS mar = { -1 };
	DwmExtendFrameIntoClientArea(overlayWindow, &mar);
	D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, &factory);
	factory->CreateHwndRenderTarget(
		D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
			D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)),
		D2D1::HwndRenderTargetProperties(overlayWindow, D2D1::SizeU(200, 200),
			D2D1_PRESENT_OPTIONS_IMMEDIATELY), &target);
	target->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f), &solid_brush);
	target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&w_factory));
	w_factory->CreateTextFormat(fontname.c_str(), NULL, DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"zh-cn", &w_format);
}

void mainLoop() 
{
	if (!overlayWindow)
		return;
		
	MSG message;
	message.message = WM_NULL; 
	if (PeekMessage(&message, overlayWindow, NULL, NULL, PM_REMOVE))
	{
		TranslateMessage(&message);
		DispatchMessage(&message);
	}
	
	if (message.message != WM_QUIT)
	{
		ShowWindow(overlayWindow, SW_SHOWNORMAL);
		UpdateWindow(overlayWindow);
		SetLayeredWindowAttributes(overlayWindow, RGB(0, 0, 0), 255, LWA_ALPHA);
		UpdateWindow(overlayWindow);

		if (!targetWindow)
		{
			Sleep(1);
			return;
		}
		HWND hwndTarget = targetWindow();
		if (!::IsWindow(hwndTarget))
		{
			Sleep(1);
			return;
		}

		WINDOWINFO info;
		ZeroMemory(&info, sizeof(info));
		info.cbSize = sizeof(info);
		GetWindowInfo(hwndTarget, &info);
		D2D1_SIZE_U siz;
		siz.height = ((info.rcClient.bottom) - (info.rcClient.top));
		siz.width = ((info.rcClient.right) - (info.rcClient.left));
		if (!IsIconic(overlayWindow)) {
			SetWindowPos(overlayWindow, NULL, info.rcClient.left, info.rcClient.top, siz.width, siz.height, SWP_SHOWWINDOW);
			target->Resize(&siz);
		}
		target->BeginDraw();
		target->Clear(D2D1::ColorF(0, 0, 0, 0));

		//enable
		std::lock_guard<std::mutex> _locker(mutexEnable);
		if (!enable)
			goto noDraw;

		if (drawLoopCallback != NULL) 
		{
			if (o_Foreground) 
			{
				if (GetForegroundWindow() == hwndTarget)
					goto toDraw;
				else
					goto noDraw;
			}

		toDraw:
			time_t postTime = clock();
			time_t frameTime = postTime - preTime;
			preTime = postTime;

			if (o_DrawFPS) {
				if (postTime - showTime > 100) {
					fps = 1000 / (float)frameTime;
					showTime = postTime;
				}
				DrawString(std::to_wstring(fps), 20, siz.width - 50, 0, 0, 1, 0);
			}

			if (o_VSync) {
				int pausetime = 17 - frameTime;
				if (pausetime > 0 && pausetime < 30) {
					Sleep(pausetime);
				}
			}

			drawLoopCallback(siz.width, siz.height);
		}

	noDraw:
		target->EndDraw();
		Sleep(1);
	}
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uiMessage, WPARAM wParam, LPARAM lParam)
{
	switch (uiMessage)
	{
	case WM_CREATE:
		//::SetTimer(hWnd, 1, 1000, NULL);
		break;
	case WM_CLOSE:
		DestroyWindow(hWnd);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
		//case WM_TIMER:
		//	if (!::IsWindow(targetWindow))
		//		DestroyWindow(hWnd);
		//	break;
	default:
		return DefWindowProc(hWnd, uiMessage, wParam, lParam);
	}
	return 0;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	DWORD lpdwProcessId = 0;
	GetWindowThreadProcessId(hwnd, &lpdwProcessId);
	if (lpdwProcessId == GetCurrentProcessId())
	{
		selfWindow = hwnd;
		return FALSE;
	}
	return TRUE;
}

HWND selfWndCallback()
{
	return selfWindow;
}

HANDLE evOverlayWindowCreate = NULL;
DWORD WINAPI OverlayThread(LPVOID lpParam)
{
	if (lpParam == NULL) 
	{
		EnumWindows(EnumWindowsProc, NULL);
		if (!selfWindow)
			return 0;
		lpParam = selfWndCallback;
	}

	d2oSetup((HWND(*)())lpParam);
	SetEvent(evOverlayWindowCreate);

	while (IsDirectOverlayRunning()) 
	{
		std::lock_guard<std::mutex> _locker(mutexEnd);
		if (end)
			break;
		mainLoop();
	}

	return 0;
}

void DirectOverlaySetup(DirectOverlayCallback callback) {
	drawLoopCallback = callback;
	end = FALSE;

	evOverlayWindowCreate = CreateEvent(NULL, TRUE, FALSE, NULL);
	CreateThread(0, 0, OverlayThread, NULL, 0, NULL);
	WaitForSingleObject(evOverlayWindowCreate, INFINITE);
	CloseHandle(evOverlayWindowCreate);

}

void DirectOverlaySetup(DirectOverlayCallback callback, HWND(*_targetWindow)(void)) {
	drawLoopCallback = callback;
	end = FALSE;

	evOverlayWindowCreate = CreateEvent(NULL, TRUE, FALSE, NULL);
	CreateThread(0, 0, OverlayThread, _targetWindow, 0, NULL);
	WaitForSingleObject(evOverlayWindowCreate, INFINITE);
	CloseHandle(evOverlayWindowCreate);
}

BOOL IsDirectOverlayRunning()
{
	return !end && ::IsWindow(overlayWindow);
}

void DirectOverlayEnable(BOOL bEnable)
{
	std::lock_guard<std::mutex> _locker(mutexEnable);
	enable = bEnable;
}

BOOL IsDirectOverlayEnable()
{
	return enable;
}

void DirectOverlayStop()
{
	::DestroyWindow(overlayWindow);
	overlayWindow = NULL;

	std::lock_guard<std::mutex> _locker(mutexEnd);
	end = TRUE;
}

void DirectOverlaySetOption(DWORD option) {
	if (option & D2DOV_REQUIRE_FOREGROUND) o_Foreground = true;
	if (option & D2DOV_DRAW_FPS) o_DrawFPS = true;
	if (option & D2DOV_VSYNC) o_VSync = true;
	//if (option & D2DOV_FONT_ARIAL) fontname = L"arial";
	//if (option & D2DOV_FONT_COURIER) fontname = L"Courier";
	//if (option & D2DOV_FONT_CALIBRI) fontname = L"Calibri";
	//if (option & D2DOV_FONT_GABRIOLA) fontname = L"Gabriola";
	//if (option & D2DOV_FONT_IMPACT) fontname = L"Impact";
}

void DirectOverlaySetFontName(const std::wstring& _fontname)
{
	fontname = _fontname;
}
