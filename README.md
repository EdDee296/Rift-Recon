# RiftRecon

[Read dis babyboo](https://eddee296.netlify.app/blog/rift-recon.html)

## Backend setup and run

1. Clone on Windows:

```bash
git clone https://github.com/EdDee296/Rift-Recon
cd Rift-Recon
```

2. Install all backend dependencies.

### 2.1 Install vcpkg

```powershell
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg integrate install
cd ..
```

### 2.2 Install C++ packages (x64)

```powershell
.\vcpkg\vcpkg install opencv4:x64-windows nlohmann-json:x64-windows cpp-httplib[openssl]:x64-windows boost-beast:x64-windows openssl:x64-windows
```

### 2.3 Install FFmpeg (required)

- Download FFmpeg shared build (Windows x64)
- Extract it to:

`D:\library\ffmpeg\ffmpeg-master-latest-win64-gpl-shared`

This is required because the current project file references that FFmpeg include/lib path directly.

3. Open the solution in Visual Studio 2022 and run.

## Frontend

This repo is backend only. For frontend setup/run, follow the instructions in the frontend repo.
