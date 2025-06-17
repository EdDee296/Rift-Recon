#pragma once
#ifndef DXGIFULLSCREENCAPTURE_H
#define DXGIFULLSCREENCAPTURE_H

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <vector>
#include <windowsx.h>
#include <fstream> 
#include <string>
#include <filesystem>

class DXGIFullscreenCapture {
private:
    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dContext = nullptr;
    IDXGIOutputDuplication* deskDupl = nullptr;
    ID3D11Texture2D* acquiredDesktopImage = nullptr;

    UINT fullWidth, fullHeight;
    UINT captureX = 0, captureY = 0;
    UINT captureWidth, captureHeight;
    bool hasCustomRegion = false;
public:
    DXGIFullscreenCapture();
    ~DXGIFullscreenCapture();

    bool SelectCaptureRegionByMouse();
    bool SaveCaptureRegion(const std::string& filename);
    bool LoadCaptureRegion(const std::string& filename);
    bool HandleKeyMessage(UINT message, WPARAM wParam);

    bool Initialize();
    bool CaptureScreen(std::vector<uint8_t>& buffer);
    std::vector<uint8_t> ExtractMinimap(const std::vector<uint8_t>& screenBuffer);
    void CleanUp();

    UINT GetFullWidth() const;
    UINT GetFullHeight() const;

    void SetCaptureRegion(UINT x, UINT y, UINT width, UINT height);
    UINT GetCaptureWidth() const;
    UINT GetCaptureHeight() const;
};

#endif
