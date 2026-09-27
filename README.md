# Mouse Forge

Panel tinh chỉnh BlueStacks (BlueStacks 5 nxt và BlueStacks MSI) viết bằng C++17 + Win32 API thuần, không phụ thuộc Qt/MFC/.NET.

## Cấu trúc repo
```
MouseForge.cpp          - toàn bộ mã nguồn ứng dụng
resource.rc              - resource: icon + version info
icon.ico                 - icon mặc định (placeholder, có thể thay)
build.md                 - hướng dẫn build chi tiết (MSVC / MinGW)
.github/workflows/build.yml - CI tự build .exe bằng MSVC trên GitHub Actions
```

## Build nhanh trên GitHub (không cần máy Windows)
1. Push repo này lên GitHub.
2. Vào tab **Actions**, workflow "Build Mouse Forge" sẽ tự chạy trên `windows-latest`.
3. Sau khi chạy xong, tải file `MouseForge.exe` trong mục **Artifacts** của run đó.

## Build local (Windows)
Xem chi tiết trong `build.md`. Tóm tắt:

```bat
rem MSVC (Developer Command Prompt)
rc /fo resource.res resource.rc
cl /std:c++17 /EHsc /utf-8 /O2 /DUNICODE /D_UNICODE MouseForge.cpp resource.res /Fe:MouseForge.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib shlwapi.lib psapi.lib dwmapi.lib advapi32.lib
```

```bash
# MinGW-w64
windres resource.rc -O coff -o resource.res
g++ -std=c++17 -O2 -municode -mwindows MouseForge.cpp resource.res -o MouseForge.exe -lshlwapi -lpsapi -lcomctl32 -lgdi32 -luser32 -ldwmapi -ladvapi32
```

## Mới trong v2
- Tab **Presets**: lưu/nạp nhiều bộ lựa chọn checkbox ở tab Tối ưu thành các preset đặt tên riêng (ví dụ "Game nhẹ", "Game nặng"), lưu dưới dạng file text trong thư mục `presets/` cạnh file .exe.
- Thêm 3 tuỳ chọn tối ưu mới (đều là key AOSP thật, có scope `system`):
  - **tắt haptic feedback** (`haptic_feedback_enabled=0`)
  - **không tự tắt màn hình** (`screen_off_timeout=2147483647`)
  - **khoá xoay màn hình tự động** (`accelerometer_rotation=0`)
- Mở rộng danh sách key an toàn hiển thị ở tab ADB (thêm `development_settings_enabled`, `wifi_sleep_policy`, `auto_time`,... để tham khảo).

## Lưu ý trung thực
Mouse Forge chỉ áp dụng các key Android/ADB có tác dụng thật đã được xác minh trong code (xem `SAFE_GLOBAL_KEYS` trong `MouseForge.cpp`). Các key vô dụng (`touch.pressure.scale`, `pointer_speed`, ...) được liệt kê rõ kèm lý do kỹ thuật, không được áp dụng. Ứng dụng không hứa hẹn chống rung tâm/lố tâm, không chèn sensi vào engine game, không exploit kernel.
