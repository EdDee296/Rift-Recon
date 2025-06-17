#include "DXGIFullscreenCapture.h"
#include <iostream>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

DXGIFullscreenCapture::DXGIFullscreenCapture() {}

DXGIFullscreenCapture::~DXGIFullscreenCapture() {
    CleanUp();
}

bool DXGIFullscreenCapture::Initialize() {
    // Try to load saved region first
    if (LoadCaptureRegion("capture_region.cfg")) {
        hasCustomRegion = true;
    }

    // Create D3D11 device
    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, nullptr, 0, D3D11_SDK_VERSION,
        &d3dDevice, &featureLevel, &d3dContext);

    if (FAILED(hr)) {
        std::cerr << "Failed to create D3D11 device" << std::endl;
        return false;
    }

    // Get DXGI device
    IDXGIDevice* dxgiDevice = nullptr;
    hr = d3dDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
    if (FAILED(hr)) {
        std::cerr << "Failed to get DXGI device" << std::endl;
        return false;
    }

    // Get DXGI adapter
    IDXGIAdapter* dxgiAdapter = nullptr;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    dxgiDevice->Release();
    if (FAILED(hr)) {
        std::cerr << "Failed to get DXGI adapter" << std::endl;
        return false;
    }

    // Get output (monitor)
    IDXGIOutput* dxgiOutput = nullptr;
    hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput); // Primary monitor = 0
    dxgiAdapter->Release();
    if (FAILED(hr)) {
        std::cerr << "Failed to get DXGI output" << std::endl;
        return false;
    }

    // Get output description to determine screen dimensions
    DXGI_OUTPUT_DESC outputDesc;
    hr = dxgiOutput->GetDesc(&outputDesc);
    if (FAILED(hr)) {
        dxgiOutput->Release();
        std::cerr << "Failed to get output description" << std::endl;
        return false;
    }

    // Calculate full width and height
    fullWidth = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    fullHeight = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

    if (!hasCustomRegion) {
        captureWidth = fullWidth;
        captureHeight = fullHeight;
    }

    // QI for Output 1
    IDXGIOutput1* dxgiOutput1 = nullptr;
    hr = dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&dxgiOutput1));
    dxgiOutput->Release();
    if (FAILED(hr)) {
        std::cerr << "Failed to get DXGI output1" << std::endl;
        return false;
    }

    // Create desktop duplication
    hr = dxgiOutput1->DuplicateOutput(d3dDevice, &deskDupl);
    dxgiOutput1->Release();
    if (FAILED(hr)) {
        std::cerr << "Failed to create desktop duplication, error code: 0x"
            << std::hex << hr << std::endl;
        return false;
    }

    return true;
}

bool DXGIFullscreenCapture::SaveCaptureRegion(const std::string& filename) {
    std::ofstream file(filename, std::ios::out);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << filename << std::endl;
        return false;
    }

    file << captureX << "\n"
        << captureY << "\n"
        << captureWidth << "\n"
        << captureHeight << "\n";

    return file.good();
}

bool DXGIFullscreenCapture::LoadCaptureRegion(const std::string& filename) {
    // Check if file exists first
    if (!std::filesystem::exists(filename)) {
        return false;
    }

    std::ifstream file(filename, std::ios::in);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading: " << filename << std::endl;
        return false;
    }

    UINT x, y, width, height;
    if (file >> x >> y >> width >> height) {
        SetCaptureRegion(x, y, width, height);
        return true;
    }

    return false;
}

bool DXGIFullscreenCapture::CaptureScreen(std::vector<uint8_t>& buffer) {
    if (!deskDupl) {
        // Try to reinitialize if desktop duplication is null
        if (!Initialize()) {
            return false;
        }
    }

    // Release previous frame
    if (acquiredDesktopImage) {
        acquiredDesktopImage->Release();
        acquiredDesktopImage = nullptr;
    }

    // Get new frame
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    IDXGIResource* desktopResource = nullptr;
    HRESULT hr = deskDupl->AcquireNextFrame(100, &frameInfo, &desktopResource);

    // Store current capture region settings before cleanup
    bool hadCustomRegion = hasCustomRegion;
    UINT savedCaptureX = captureX;
    UINT savedCaptureY = captureY;
    UINT savedCaptureWidth = captureWidth;
    UINT savedCaptureHeight = captureHeight;


    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return false; // No new frame
    }

    if (hr == DXGI_ERROR_ACCESS_LOST) {
        // This happens when display mode changes or application goes fullscreen
        std::cerr << "Access lost to desktop duplication, reinitializing..." << std::endl;
        Sleep(1000); // Wait 1 second before reinitializing
        CleanUp();
        if (!Initialize()) {
            std::cerr << "Failed to reinitialize after access lost" << std::endl;
            return false;
        }

        // Explicitly reapply the saved capture region
        if (hadCustomRegion) {
            SetCaptureRegion(savedCaptureX, savedCaptureY, savedCaptureWidth, savedCaptureHeight);
        }
        return false; // Skip this frame and try again next time
    }

    if (FAILED(hr)) {
        std::cerr << "Failed to acquire next frame, error code: 0x"
            << std::hex << hr << std::dec << std::endl;
        return false;
    }

    // QI for ID3D11Texture2D
    hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(&acquiredDesktopImage));
    desktopResource->Release();
    if (FAILED(hr)) {
        std::cerr << "Failed to QI for ID3D11Texture2D" << std::endl;
        return false;
    }

    // Create staging texture for CPU access
    D3D11_TEXTURE2D_DESC desc;
    acquiredDesktopImage->GetDesc(&desc);

    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;

    ID3D11Texture2D* stagingTexture = nullptr;
    hr = d3dDevice->CreateTexture2D(&desc, nullptr, &stagingTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to create staging texture" << std::endl;
        return false;
    }

    // Copy full screen texture to staging texture
    d3dContext->CopyResource(stagingTexture, acquiredDesktopImage);

    // Map staging texture
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = d3dContext->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr)) {
        stagingTexture->Release();
        std::cerr << "Failed to map staging texture" << std::endl;
        return false;
    }

    // Validate capture region to ensure we're not going out of bounds
    UINT actualX = min(captureX, fullWidth);
    UINT actualY = min(captureY, fullHeight);
    UINT actualWidth = min(captureWidth, fullWidth - actualX);
    UINT actualHeight = min(captureHeight, fullHeight - actualY);

    // Copy data to our buffer - only the captured region
    buffer.resize(actualWidth * actualHeight * 4); // RGBA format

    uint8_t* src = static_cast<uint8_t*>(mappedResource.pData);
    for (UINT row = 0; row < actualHeight; row++) {
        memcpy(
            buffer.data() + row * actualWidth * 4,
            src + (actualY + row) * mappedResource.RowPitch + actualX * 4,
            actualWidth * 4
        );
    }

    // Unmap and release staging texture
    d3dContext->Unmap(stagingTexture, 0);
    stagingTexture->Release();

    // Release frame
    deskDupl->ReleaseFrame();

    return true;
}

// Global callback function for the window procedure
LRESULT CALLBACK RegionSelectProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static POINT startPt = { 0, 0 };
    static bool selecting = false;
    static RECT prevRect = { 0, 0, 0, 0 };

    switch (msg) {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // Get window dimensions
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        // Create a compatible DC for double buffering
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        // Fill entire window with semi-transparent grey
        HBRUSH greyBrush = CreateSolidBrush(RGB(128, 128, 128));
        FillRect(memDC, &clientRect, greyBrush);
        DeleteObject(greyBrush);

        // If selection is active, clear the selection rectangle
        if (selecting && prevRect.right > prevRect.left) {
            // Create a transparent brush for the selection area
            HBRUSH clearBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
            SelectObject(memDC, clearBrush);

            // Draw a border around the selection
            HPEN bluePen = CreatePen(PS_SOLID, 2, RGB(0, 120, 215));
            HPEN oldPen = (HPEN)SelectObject(memDC, bluePen);

            // Draw the selection rectangle with no fill
            Rectangle(memDC, prevRect.left, prevRect.top, prevRect.right, prevRect.bottom);

            // Clean up
            SelectObject(memDC, oldPen);
            DeleteObject(bluePen);
        }

        // Copy the memory DC to the window DC
        BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);

        // Clean up
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
        selecting = true;
        startPt.x = LOWORD(lParam);
        startPt.y = HIWORD(lParam);
        // Reset previous rectangle
        SetRectEmpty(&prevRect);
        return 0;

    case WM_MOUSEMOVE:
        if (selecting) {
            // Update the selection rectangle
            POINT currentPt = { LOWORD(lParam), HIWORD(lParam) };
            RECT newRect = {
                min(startPt.x, currentPt.x),
                min(startPt.y, currentPt.y),
                max(startPt.x, currentPt.x),
                max(startPt.y, currentPt.y)
            };

            // Only redraw if the rectangle changed
            if (memcmp(&prevRect, &newRect, sizeof(RECT)) != 0) {
                prevRect = newRect;
                // Force a repaint of the window
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (selecting) {
            selecting = false;
            POINT endPt = { LOWORD(lParam), HIWORD(lParam) };

            // Post a custom message with the selection coordinates
            PostMessage(hwnd, WM_USER + 1,
                MAKEWPARAM(startPt.x, startPt.y),
                MAKELPARAM(endPt.x, endPt.y));
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;

    case WM_ERASEBKGND:
        // Return non-zero to indicate background has been erased
        // (We'll actually erase it in WM_PAINT)
        return 1;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool DXGIFullscreenCapture::SelectCaptureRegionByMouse() {
    // Store initial screen state
    bool wasFullscreen = !hasCustomRegion;
    UINT originalX = captureX;
    UINT originalY = captureY;
    UINT originalWidth = captureWidth;
    UINT originalHeight = captureHeight;

    // Register a window class for our overlay
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"RegionSelectClass";
    RegisterClassEx(&wc);

    // Create an overlay window
    HWND hwndOverlay = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED,
        L"RegionSelectClass", L"Region Select", WS_POPUP,
        0, 0, fullWidth, fullHeight, nullptr, nullptr, GetModuleHandle(NULL), nullptr);

    if (!hwndOverlay) {
        std::cerr << "Failed to create overlay window" << std::endl;
        return false;
    }

    // Make the window semi-transparent (80% opacity for grey overlay)
    SetLayeredWindowAttributes(hwndOverlay, 0, 204, LWA_ALPHA); // 204 is 80% of 255

    // Set the window procedure
    SetWindowLongPtr(hwndOverlay, GWLP_WNDPROC, (LONG_PTR)RegionSelectProc);

    // Show the window
    ShowWindow(hwndOverlay, SW_SHOW);
    UpdateWindow(hwndOverlay);

    // Instructions for user
    std::cout << "Click and drag to select a region. Press ESC to cancel." << std::endl;

    // Message loop
    bool selectionComplete = false;
    MSG msg;
    while (!selectionComplete && GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.hwnd == hwndOverlay && msg.message == WM_USER + 1) {
            // Our custom message with selection coordinates
            POINT start = { LOWORD(msg.wParam), HIWORD(msg.wParam) };
            POINT end = { LOWORD(msg.lParam), HIWORD(msg.lParam) };

            // Set the capture region
            captureX = min(start.x, end.x);
            captureY = min(start.y, end.y);
            captureWidth = abs(end.x - start.x);
            captureHeight = abs(end.y - start.y);

            // Validate minimum size
            if (captureWidth >= 10 && captureHeight >= 10) {
                hasCustomRegion = true;
                selectionComplete = true;

                // Save the selection to a file
                SaveCaptureRegion("capture_region.cfg");
            }
        }
        else if (msg.message == WM_CLOSE) {
            // Escape was pressed
            captureX = originalX;
            captureY = originalY;
            captureWidth = originalWidth;
            captureHeight = originalHeight;
            hasCustomRegion = wasFullscreen ? false : true;
            selectionComplete = true;
        }
        else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // Clean up
    DestroyWindow(hwndOverlay);

    return hasCustomRegion && captureWidth >= 10 && captureHeight >= 10;
}

bool DXGIFullscreenCapture::HandleKeyMessage(UINT message, WPARAM wParam) {
    if (message == WM_KEYDOWN && wParam == VK_DOWN) {
        // Down arrow key was pressed, trigger region selection
        return SelectCaptureRegionByMouse();
    }
    return false;
}

std::vector<uint8_t> DXGIFullscreenCapture::ExtractMinimap(const std::vector<uint8_t>& screenBuffer) {
    // Estimate minimap position (bottom right corner)
    // These values need to be calibrated for League of Legends
    int minimapSize = captureHeight / 5; // Example size
    int minimapX = captureWidth - minimapSize - 20;
    int minimapY = captureHeight - minimapSize - 20;

    std::vector<uint8_t> minimapBuffer(minimapSize * minimapSize * 4);

    // Extract minimap region from captured screen
    for (int y = 0; y < minimapSize; y++) {
        for (int x = 0; x < minimapSize; x++) {
            int screenPos = ((minimapY + y) * captureWidth + (minimapX + x)) * 4;
            int minimapPos = (y * minimapSize + x) * 4;

            // Copy RGBA values
            minimapBuffer[minimapPos] = screenBuffer[screenPos];
            minimapBuffer[minimapPos + 1] = screenBuffer[screenPos + 1];
            minimapBuffer[minimapPos + 2] = screenBuffer[screenPos + 2];
            minimapBuffer[minimapPos + 3] = screenBuffer[screenPos + 3];
        }
    }

    return minimapBuffer;
}

void DXGIFullscreenCapture::CleanUp() {
    if (acquiredDesktopImage) {
        acquiredDesktopImage->Release();
        acquiredDesktopImage = nullptr;
    }

    if (deskDupl) {
        deskDupl->Release();
        deskDupl = nullptr;
    }

    if (d3dContext) {
        d3dContext->Release();
        d3dContext = nullptr;
    }

    if (d3dDevice) {
        d3dDevice->Release();
        d3dDevice = nullptr;
    }
}

UINT DXGIFullscreenCapture::GetFullWidth() const { return fullWidth; }
UINT DXGIFullscreenCapture::GetFullHeight() const { return fullHeight; }

void DXGIFullscreenCapture::SetCaptureRegion(UINT x, UINT y, UINT width, UINT height) {
    UINT actualY = ((std::min))(captureY, fullHeight); // Use parentheses around (std::min) to avoid macro conflicts
    captureX = x;
    captureY = y;
    captureWidth = width;
    captureHeight = height;
    hasCustomRegion = true;
}

UINT DXGIFullscreenCapture::GetCaptureWidth() const { return captureWidth; }
UINT DXGIFullscreenCapture::GetCaptureHeight() const { return captureHeight; }
