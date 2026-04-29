#!/bin/bash
# wsl2_camera_diagnostic.sh
# Project ASi Felis Origin - WSL2 攝像頭問題診斷與修復腳本

echo "🔍 WSL2 攝像頭診斷開始..."

# 1. USB 設備檢查
echo "=== USB 設備狀態 ==="
lsusb | grep -i camera
ls -la /dev/video*

# 2. 權限檢查
echo "=== 權限狀態 ==="
ls -la /dev/video* | grep video
whoami
groups

# 3. V4L2 設備詳細資訊
echo "=== V4L2 設備能力 ==="
for device in /dev/video*; do
    echo "檢查設備: $device"
    v4l2-ctl --list-formats-ext --device=$device 2>/dev/null || echo "設備 $device 無法存取"
done

# 4. 嘗試修復步驟
echo "=== 嘗試修復 ==="

# 修復權限
echo "修復權限..."
sudo chmod 666 /dev/video*

# 重置 USB 設備
echo "嘗試重置 USB 設備..."
sudo modprobe -r uvcvideo
sudo modprobe uvcvideo

# 強制設定格式
echo "強制設定 MJPEG 格式..."
v4l2-ctl --set-fmt-video=width=640,height=480,pixelformat=MJPG --device=/dev/video0 2>/dev/null

# 5. 測試 OpenCV 連接
echo "=== OpenCV 連接測試 ==="
python3 -c "
import cv2
print('測試 /dev/video0...')
cap = cv2.VideoCapture(0)
if cap.isOpened():
    print('✅ 攝像頭連接成功')
    ret, frame = cap.read()
    if ret:
        print(f'✅ 幀讀取成功，解析度: {frame.shape}')
    else:
        print('❌ 無法讀取幀')
    cap.release()
else:
    print('❌ 攝像頭連接失敗')
"

echo "🔧 診斷完成。如果問題持續，建議使用 Windows 原生 MediaPipe。"
