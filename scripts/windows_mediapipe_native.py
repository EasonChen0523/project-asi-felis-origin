"""
Project ASi Felis Origin - Windows 原生 MediaPipe 備用方案
解決 WSL2 USB 硬體死鎖問題的穩定替代方案

環境要求:
- Windows 11 原生 Python 3.10+
- pip install mediapipe opencv-python

使用方法:
1. 在 Windows Command Prompt 或 PowerShell 中執行
2. 確保 hand_landmarker.task 在同一目錄
"""

import cv2
import mediapipe as mp
from mediapipe.tasks import python
from mediapipe.tasks.python import vision
import time

class WindowsNeoWalkerDepthSensor:
    def __init__(self, model_path='hand_landmarker.task'):
        """Windows 原生環境 MediaPipe 初始化"""
        print("🪟 Windows 原生 NeoWalker 深度感知器啟動中...")
        
        # Tasks API 配置
        base_options = python.BaseOptions(model_asset_path=model_path)
        self.options = vision.HandLandmarkerOptions(
            base_options=base_options,
            num_hands=1,
            min_hand_detection_confidence=0.7,
            min_hand_presence_confidence=0.7,
            min_tracking_confidence=0.6
        )
        
        self.detector = vision.HandLandmarker.create_from_options(self.options)
        
        # 深度觸發參數
        self.DEPTH_TRIGGER_THRESHOLD = -0.15
        self.last_trigger_time = 0
        self.trigger_cooldown = 0.3
        
        print("✅ MediaPipe Tasks API 已初始化")
    
    def setup_camera(self):
        """配置攝像頭（Windows DirectShow 後端）"""
        print("🎥 設定攝像頭...")
        
        # 使用 DirectShow 後端避免 MMF 相關問題
        cap = cv2.VideoCapture(0, cv2.CAP_DSHOW)
        
        if not cap.isOpened():
            print("❌ 無法開啟攝像頭")
            return None
        
        # 最佳化設定
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
        cap.set(cv2.CAP_PROP_FPS, 30)
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc('M','J','P','G'))
        
        # 驗證設定
        actual_width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        actual_height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        actual_fps = int(cap.get(cv2.CAP_PROP_FPS))
        
        print(f"✅ 攝像頭已就緒: {actual_width}x{actual_height} @ {actual_fps}fps")
        return cap
    
    def process_frame(self, image):
        """處理單幀影像，返回深度數據"""
        # MediaPipe 格式轉換
        rgb_image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb_image)
        
        # 執行手部偵測
        detection_result = self.detector.detect(mp_image)
        
        depth_data = None
        if detection_result.hand_landmarks:
            landmarks = detection_result.hand_landmarks[0]
            
            # 關鍵點擷取
            wrist = landmarks[0]      # 手掌根部
            index_tip = landmarks[8]  # 食指指尖
            
            # Z軸深度計算
            relative_depth = index_tip.z - wrist.z
            
            depth_data = {
                'wrist_z': wrist.z,
                'index_tip_z': index_tip.z,
                'relative_depth': relative_depth,
                'is_trigger': self._check_depth_trigger(relative_depth),
                'landmarks': landmarks
            }
            
            # 視覺化
            self._draw_enhanced_landmarks(image, landmarks, depth_data['is_trigger'])
        
        return depth_data
    
    def _check_depth_trigger(self, relative_depth):
        """深度觸發檢測"""
        current_time = time.time()
        if (relative_depth < self.DEPTH_TRIGGER_THRESHOLD and 
            current_time - self.last_trigger_time > self.trigger_cooldown):
            self.last_trigger_time = current_time
            return True
        return False
    
    def _draw_enhanced_landmarks(self, image, landmarks, is_triggered):
        """增強版視覺化"""
        h, w, c = image.shape
        
        # 手掌根部（動態顏色）
        wrist = landmarks[0]
        wrist_color = (0, 255, 255) if is_triggered else (255, 0, 255)
        cv2.circle(image, (int(wrist.x * w), int(wrist.y * h)), 12, wrist_color, -1)
        cv2.circle(image, (int(wrist.x * w), int(wrist.y * h)), 15, (255, 255, 255), 2)
        
        # 食指指尖（動態顏色）
        index_tip = landmarks[8]
        tip_color = (0, 255, 0) if is_triggered else (0, 128, 255)
        cv2.circle(image, (int(index_tip.x * w), int(index_tip.y * h)), 10, tip_color, -1)
        cv2.circle(image, (int(index_tip.x * w), int(index_tip.y * h)), 13, (255, 255, 255), 2)
        
        # 深度指示線（動態粗細）
        line_thickness = 4 if is_triggered else 2
        line_color = (0, 255, 0) if is_triggered else (0, 200, 255)
        cv2.line(image, 
                (int(wrist.x * w), int(wrist.y * h)),
                (int(index_tip.x * w), int(index_tip.y * h)),
                line_color, line_thickness)

def main():
    """主程式入口"""
    print("🚀 Project ASi Felis Origin - Windows 原生深度感知測試")
    print("📋 操作說明:")
    print("   - 將手伸向攝像頭進行深度感知測試")
    print("   - 按 'q' 或 'ESC' 退出")
    print("   - 按 'r' 重置觸發器")
    print("-" * 50)
    
    # 初始化
    sensor = WindowsNeoWalkerDepthSensor()
    cap = sensor.setup_camera()
    
    if cap is None:
        print("❌ 攝像頭初始化失敗")
        return
    
    frame_count = 0
    fps_start_time = time.time()
    
    try:
        while True:
            ret, frame = cap.read()
            if not ret:
                print("⚠️ 無法讀取攝像頭畫面")
                continue
            
            frame_count += 1
            
            # 水平翻轉（鏡像效果）
            frame = cv2.flip(frame, 1)
            
            # 深度感知處理
            depth_data = sensor.process_frame(frame)
            
            # 數據輸出（每30幀一次）
            if depth_data and frame_count % 30 == 0:
                status_icon = "🎯 TRIGGERED!" if depth_data['is_trigger'] else "🤏 hover    "
                print(f"[{frame_count:05d}] {status_icon} | "
                      f"手掌: {depth_data['wrist_z']:+.4f} | "
                      f"食指: {depth_data['index_tip_z']:+.4f} | "
                      f"深度差: {depth_data['relative_depth']:+.4f}")
            
            # HUD 資訊顯示
            if depth_data:
                status_text = "DEPTH TRIGGERED!" if depth_data['is_trigger'] else "Depth Sensing Active"
                status_color = (0, 255, 0) if depth_data['is_trigger'] else (255, 255, 255)
                cv2.putText(frame, status_text, (10, 40), 
                           cv2.FONT_HERSHEY_SIMPLEX, 1.2, status_color, 2)
                
                # 深度數值顯示
                depth_text = f"Relative Depth: {depth_data['relative_depth']:+.3f}"
                cv2.putText(frame, depth_text, (10, 80), 
                           cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 1)
            
            # FPS 計算
            if frame_count % 60 == 0:
                fps = 60 / (time.time() - fps_start_time)
                fps_start_time = time.time()
                print(f"📊 FPS: {fps:.1f}")
            
            # 顯示畫面
            cv2.imshow('NeoWalker Windows Native Depth Sensing', frame)
            
            # 按鍵控制
            key = cv2.waitKey(1) & 0xFF
            if key == ord('q') or key == 27:  # 'q' 或 ESC
                break
            elif key == ord('r'):  # 重置觸發器
                sensor.last_trigger_time = 0
                print("🔄 觸發器已重置")
    
    except KeyboardInterrupt:
        print("\n⚠️ 使用者中斷")
    
    finally:
        # 清理資源
        cap.release()
        cv2.destroyAllWindows()
        print("✅ Windows 原生深度感知測試結束")

if __name__ == "__main__":
    main()
