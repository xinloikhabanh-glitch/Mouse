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

## v3 — sửa 2 lỗi nghiêm trọng + giao diện mới
**2 lỗi khiến chuột mất kiểm soát/siêu nhanh đã được sửa:**
1. **Vòng lặp tự khuếch đại**: bộ lọc nhận diện sự kiện do chính engine bơm ra (`SendInput`) dựa vào `hDevice != NULL`, nhưng giá trị này **không đáng tin cậy** — trên một số máy, sự kiện do `SendInput` tạo vẫn báo `hDevice` khác NULL, khiến engine đọc lại chính chuyển động nó vừa bơm ra rồi khuếch đại tiếp qua đường cong gia tốc → tăng theo cấp số nhân trong vài phần nghìn giây → chuột "bay" mất kiểm soát. Đã sửa: dùng `ulExtraInformation` (tag riêng gắn vào mọi sự kiện engine tự bơm) để nhận diện chính xác 100%.
2. **Chuột đứng hình sau khi bộ bảo vệ (watchdog) tự kích hoạt**: watchdog phát hiện đúng chuyển động bất thường và tắt cờ engine, nhưng thông điệp báo cho luồng giao diện lại **không có nơi xử lý**, nên `RIDEV_NOLEGACY` (chặn Windows xử lý chuột mặc định) không được gỡ — kết quả là chuột đứng yên hoàn toàn thay vì trả về bình thường. Đã thêm handler xử lý đúng: gỡ đăng ký raw input, khôi phục cài đặt chuột gốc, đồng bộ lại checkbox, và hiện hộp thoại báo cho người dùng biết chuột đã ổn định trở lại.

Phím cứu hộ **Ctrl+Alt+F7** luôn tắt engine ngay lập tức trong mọi trường hợp.

**Giao diện mới**: thanh tab tự vẽ (owner-drawn) hiện đại hơn control gốc của Windows, hỗ trợ dark mode, lưu cài đặt (`MouseForge.ini`) giữa các lần mở app.

**Tab mới, dễ dùng hơn cho người không rành kỹ thuật:**
- **Bắt đầu** — 5 nút chọn nhanh kiểu chuột dựng sẵn (Chính xác 1:1 / Mượt & ổn định / Cân bằng / Xoay nhanh / Về mặc định Windows), 2 bước là xong.
- **Test** — đo trực tiếp tốc độ chuột thật so với sau khi qua engine, có ô test riêng.
- **Hướng dẫn** — giải thích từng thông số bằng ngôn ngữ đơn giản.

## Tab "Engine"
Đăng ký raw mouse input (`RIDEV_NOLEGACY`) để tự xử lý delta chuột thô trước khi Windows di chuyển con trỏ, rồi bơm chuyển động đã xử lý bằng `SendInput` — đúng kỹ thuật RawAccel dùng, **không đọc bộ nhớ/pixel của bất kỳ tiến trình game nào**:
- **EMA Fast**: bộ lọc làm mượt rung tay (time-normalized exponential moving average)
- **Velocity Window**: số mẫu tốc độ dùng để tính trung bình trượt
- **Micro Threshold**: ngưỡng bỏ qua lọc/accel khi di chuyển siêu nhỏ (giữ độ chính xác lúc ngắm tĩnh)
- **Acceleration / Accel Offset / Accel Cap**: đường cong gia tốc tuỳ chỉnh
- **Ref DPI / Cur DPI**: quy đổi khi đổi DPI chuột thật
- Tham số áp dụng NGAY khi kéo thanh trượt; tắt tick "Bật Mouse Engine" để trả về mặc định Windows ngay lập tức. Tự động unregister khi app đóng (kể cả khi đóng đột ngột qua WM_DESTROY).

## Mới trong v2
- Tab **Presets**: lưu/nạp nhiều bộ lựa chọn checkbox ở tab Tối ưu thành các preset đặt tên riêng (ví dụ "Game nhẹ", "Game nặng"), lưu dưới dạng file text trong thư mục `presets/` cạnh file .exe.
- Thêm 3 tuỳ chọn tối ưu mới (đều là key AOSP thật, có scope `system`):
  - **tắt haptic feedback** (`haptic_feedback_enabled=0`)
  - **không tự tắt màn hình** (`screen_off_timeout=2147483647`)
  - **khoá xoay màn hình tự động** (`accelerometer_rotation=0`)
- Mở rộng danh sách key an toàn hiển thị ở tab ADB (thêm `development_settings_enabled`, `wifi_sleep_policy`, `auto_time`,... để tham khảo).

## Lưu ý trung thực
Mouse Forge chỉ áp dụng các key Android/ADB có tác dụng thật đã được xác minh trong code (xem `SAFE_GLOBAL_KEYS` trong `MouseForge.cpp`). Các key vô dụng (`touch.pressure.scale`, `pointer_speed`, ...) được liệt kê rõ kèm lý do kỹ thuật, không được áp dụng. Ứng dụng không hứa hẹn chống rung tâm/lố tâm, không chèn sensi vào engine game, không exploit kernel.
