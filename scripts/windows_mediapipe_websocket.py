"""
Project ASi Felis Origin - Windows 原生 MediaPipe + WebSocket 神經橋接
解決 WSL2 USB 硬體死鎖問題的穩定替代方案，並具備即時 3D UI 連動能力
"""

import math
import cv2
import mediapipe as mp
from mediapipe.tasks import python
from mediapipe.tasks.python import vision
import time
import asyncio
import websockets
import json
import threading

# --- 神經橋接：全域狀態暫存 ---
ws_state = {
    'relative_depth': 0.0,
    'is_trigger': False
}

async def broadcast_state(websocket):
    """WebSocket 廣播協程 (獨立執行緒運行)"""
    print("🟢 [神經網路] 3D UI 客戶端已成功連接！開始傳輸深度數據...")
    try:
        while True:
            # 將最新的狀態打包為 JSON 廣播
            payload = json.dumps(ws_state)
            await websocket.send(payload)
            await asyncio.sleep(0.03)  # 大約維持 30Hz 的發送頻率
    except websockets.exceptions.ConnectionClosed:
        print("🔴 [神經網路] 3D UI 客戶端已斷開連接")

async def run_ws_server():
    """新版 WebSocket 伺服器啟動協程"""
    print("📡 WebSocket 廣播伺服器啟動於 ws://localhost:8765")
    # 使用 async with 確保伺服器生命週期綁定在運行的 event loop 內
    async with websockets.serve(broadcast_state, "localhost", 8765):
        await asyncio.Future()  # 永遠運行，維持廣播狀態

def start_ws_server():
    """供執行緒呼叫的入口點"""
    # asyncio.run 會自動為這個子執行緒建立並管理乾淨的 Event Loop
    asyncio.run(run_ws_server())

class WindowsNeoWalkerDepthSensor:
    def __init__(self, model_path='hand_landmarker.task'):
        """Windows 原生環境 MediaPipe 初始化"""
        print("🪟 Windows 原生 NeoWalker 深度感知器啟動中...")
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
        
        # [新增] EMA (指數移動平均) 濾波器參數
        self.ema_depth = 0.0
        self.ema_alpha = 0.15  # 平滑強度：0~1，數值越小越抗抖動，但跟隨會微幅延遲
        print("✅ MediaPipe Tasks API 已初始化")
    
    def setup_camera(self):
        """配置攝像頭（Windows DirectShow 後端）"""
        print("🎥 設定攝像頭...")
        cap = cv2.VideoCapture(0, cv2.CAP_DSHOW)
        if not cap.isOpened():
            print("❌ 無法開啟攝像頭")
            return None
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
        cap.set(cv2.CAP_PROP_FPS, 30)
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc('M','J','P','G'))
        actual_width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        actual_height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        actual_fps = int(cap.get(cv2.CAP_PROP_FPS))
        print(f"✅ 攝像頭已就緒: {actual_width}x{actual_height} @ {actual_fps}fps")
        return cap
    
    def process_frame(self, image):
        rgb_image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb_image)
        detection_result = self.detector.detect(mp_image)
        
        depth_data = None
        if detection_result.hand_landmarks:
            landmarks = detection_result.hand_landmarks[0]
            wrist = landmarks[0]      # 手掌根部
            thumb_tip = landmarks[4]  # [新增] 拇指指尖
            index_tip = landmarks[8]  # 食指指尖
            
            # --- 1. Z軸平滑濾波 (負責 UI 的平滑呼吸/跟隨) ---
            raw_depth = index_tip.z - wrist.z
            # EMA 濾波：融合當前新數據與歷史數據，吸收突波噪聲
            self.ema_depth = (self.ema_alpha * raw_depth) + ((1 - self.ema_alpha) * self.ema_depth)
            
            # --- 2. 絕對捏合引擎 (負責 UI 的點擊觸發) ---
            # 計算拇指與食指在 3D 空間的歐幾里得距離
            pinch_distance = math.sqrt(
                (index_tip.x - thumb_tip.x)**2 +
                (index_tip.y - thumb_tip.y)**2 +
                (index_tip.z - thumb_tip.z)**2
            )
            
            # 當指尖距離小於 0.04 時，視為捏合 (Pinch)
            is_pinched = pinch_distance < 0.04
            
            depth_data = {
                'wrist_z': wrist.z,
                'index_tip_z': index_tip.z,
                'relative_depth': self.ema_depth, # 輸出已經過濾波的滑順 Z 值
                'is_trigger': self._check_depth_trigger(is_pinched), # 改由 Pinch 驅動
                'landmarks': landmarks,
                'pinch_dist': pinch_distance # 供除錯用
            }
            self._draw_enhanced_landmarks(image, landmarks, depth_data['is_trigger'])
        return depth_data
    
    def _check_depth_trigger(self, is_pinched):
        """捏合觸發檢測 (帶冷卻時間)"""
        current_time = time.time()
        # 改為只要偵測到捏合，且過了冷卻時間，就觸發
        if is_pinched and (current_time - self.last_trigger_time > self.trigger_cooldown):
            self.last_trigger_time = current_time
            return True
        return False
    
    def _draw_enhanced_landmarks(self, image, landmarks, is_triggered):
        h, w, c = image.shape
        wrist = landmarks[0]
        wrist_color = (0, 255, 255) if is_triggered else (255, 0, 255)
        cv2.circle(image, (int(wrist.x * w), int(wrist.y * h)), 12, wrist_color, -1)
        cv2.circle(image, (int(wrist.x * w), int(wrist.y * h)), 15, (255, 255, 255), 2)
        
        index_tip = landmarks[8]
        tip_color = (0, 255, 0) if is_triggered else (0, 128, 255)
        cv2.circle(image, (int(index_tip.x * w), int(index_tip.y * h)), 10, tip_color, -1)
        cv2.circle(image, (int(index_tip.x * w), int(index_tip.y * h)), 13, (255, 255, 255), 2)
        
        line_thickness = 4 if is_triggered else 2
        line_color = (0, 255, 0) if is_triggered else (0, 200, 255)
        cv2.line(image, 
                (int(wrist.x * w), int(wrist.y * h)),
                (int(index_tip.x * w), int(index_tip.y * h)),
                line_color, line_thickness)

def main():
    print("🚀 Project ASi Felis Origin - Windows 原生深度感知與橋接測試")
    print("-" * 50)
    
    # 啟動 WebSocket 執行緒 (Daemon 模式，主程式關閉時自動結束)
    threading.Thread(target=start_ws_server, daemon=True).start()
    
    sensor = WindowsNeoWalkerDepthSensor()
    cap = sensor.setup_camera()
    if cap is None: return
    
    frame_count = 0
    fps_start_time = time.time()
    
    try:
        while True:
            ret, frame = cap.read()
            if not ret: continue
            
            frame_count += 1
            frame = cv2.flip(frame, 1)
            depth_data = sensor.process_frame(frame)
            
            # --- 核心橋接：將數據更新至全域變數，供 WebSocket 執行緒讀取 ---
            global ws_state
            if depth_data:
                ws_state['relative_depth'] = depth_data['relative_depth']
                ws_state['is_trigger'] = depth_data['is_trigger']
            else:
                # 若無偵測到手部，可選擇歸零或保持最後狀態。這裡選擇微幅回歸待機
                ws_state['is_trigger'] = False
            
            # HUD 資訊與 FPS (保留您原本的優秀實作)
            if depth_data:
                status_text = "DEPTH TRIGGERED!" if depth_data['is_trigger'] else "Depth Sensing Active"
                status_color = (0, 255, 0) if depth_data['is_trigger'] else (255, 255, 255)
                cv2.putText(frame, status_text, (10, 40), cv2.FONT_HERSHEY_SIMPLEX, 1.2, status_color, 2)
                depth_text = f"Relative Depth: {depth_data['relative_depth']:+.3f}"
                cv2.putText(frame, depth_text, (10, 80), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 1)
            
            if frame_count % 60 == 0:
                fps = 60 / (time.time() - fps_start_time)
                fps_start_time = time.time()
                print(f"📊 FPS: {fps:.1f} | 狀態: {'觸發' if ws_state['is_trigger'] else '待機'} (Z: {ws_state['relative_depth']:+.3f})")
            
            cv2.imshow('NeoWalker Windows Native Depth Sensing', frame)
            
            key = cv2.waitKey(1) & 0xFF
            if key == ord('q') or key == 27:
                break
            elif key == ord('r'):
                sensor.last_trigger_time = 0
    except KeyboardInterrupt:
        pass
    finally:
        cap.release()
        cv2.destroyAllWindows()

if __name__ == "__main__":
    main()