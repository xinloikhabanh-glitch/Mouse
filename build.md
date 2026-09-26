# Build Mouse Forge

## Yêu cầu môi trường
- Windows 10/11 (build và chạy).
- `icon.ico` đã có sẵn trong repo (placeholder). Có thể thay bằng icon riêng của bạn, giữ nguyên tên file.
- Chưa cần cài BlueStacks/ADB để build; chỉ cần khi chạy thử tính năng ADB/Root.

## Biên dịch resource
### MSVC (rc.exe, thường có sẵn trong "Developer Command Prompt for VS")
```
rc /fo resource.res resource.rc
```

### MinGW-w64 (windres)
```
windres resource.rc -O coff -o resource.res
```

## Build với MSVC
Mở "Developer Command Prompt for VS" (x64 hoặc x86 tuỳ mục tiêu):
```
cl /std:c++17 /EHsc /utf-8 /O2 /DUNICODE /D_UNICODE MouseForge.cpp resource.res /Fe:MouseForge.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib shlwapi.lib psapi.lib dwmapi.lib
```

## Build với MinGW-w64
```
g++ -std=c++17 -O2 -municode -mwindows MouseForge.cpp resource.res -o MouseForge.exe -lshlwapi -lpsapi -lcomctl32 -lgdi32 -luser32 -ldwmapi
```

## Chạy
- Chạy MouseForge.exe với quyền Administrator để dùng đầy đủ chức năng (sửa config, bật root, đặt priority).
- Nếu không phải admin, title bar sẽ hiện `[không phải admin]` và một số nút sẽ báo lỗi khi bấm thay vì crash.

## Ghi chú
- MSVC: cần Windows SDK chuẩn đi kèm Visual Studio (đã có `shlwapi.h`, `psapi.h`, `dwmapi.h`, `tlhelp32.h`).
- MinGW: nếu link lỗi `-ldwmapi`, dùng bản MinGW-w64 mới hơn (ví dụ WinLibs).
- `resource.res` phải build **trước** khi biên dịch/link `MouseForge.cpp`.
- Có thể build tự động bằng GitHub Actions, xem `.github/workflows/build.yml` trong repo này — mỗi lần push sẽ tự build ra `MouseForge.exe` bằng MSVC trên `windows-latest` và đăng làm artifact để tải về.
