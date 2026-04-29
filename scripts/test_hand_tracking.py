import cv2
import mediapipe as mp
from mediapipe.tasks import python
from mediapipe.tasks.python import vision

print("黑足貓系統：正在載入新世代 Tasks API 視覺管線...")

# 1. 配置 Tasks API 參數
base_options = python.BaseOptions(model_asset_path='hand_landmarker.task')
options = vision.HandLandmarkerOptions(
    base_options=base_options,
    num_hands=1,
    min_hand_detection_confidence=0.6,
    min_hand_presence_confidence=0.6,
    min_tracking_confidence=0.5
)

# 2. 啟動偵測器
detector = vision.HandLandmarker.create_from_options(options)

# 3. 戰術修改：讀取實戰模擬數據檔 (MP4)
video_path = 'hand_test.mp4'
cap = cv2.VideoCapture(video_path)

if not cap.isOpened():
    print(f"致命錯誤：無法讀取模擬數據檔 {video_path}。請確認檔案是否存在於專案根目錄。")
    exit()

print(f"視覺管線已上線 (讀取模擬數據：{video_path})。按下 'ESC' 鍵退出。")

while cap.isOpened():
    success, image = cap.read()
    if not success:
        print("警告：無法獲取影像流。")
        break

    # Tasks API 需要專屬的 mp.Image 格式
    rgb_image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
    mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb_image)

    # 執行推論
    detection_result = detector.detect(mp_image)

    # 解析數據與 Z 軸映射
    if detection_result.hand_landmarks:
        # 取第一隻手 (我們設定 num_hands=1)
        landmarks = detection_result.hand_landmarks[0]
        
        # Landmark 0: 手掌根部 (Wrist) | Landmark 8: 食指指尖 (Index Finger Tip)
        wrist = landmarks[0]
        index_tip = landmarks[8]
        
        # 計算相對深度 (可用於 Z 軸 UI 的「捏合點擊」判斷)
        relative_depth = index_tip.z - wrist.z
        
        print(f"[Z軸深度偵測] 手掌根部: {wrist.z:.4f} | 食指指尖: {index_tip.z:.4f} | 相對捏合: {relative_depth:.4f}")

        # 視覺化：由於舊版繪圖工具失效，我們手動在畫面上標記這兩個關鍵點
        h, w, c = image.shape
        cv2.circle(image, (int(index_tip.x * w), int(index_tip.y * h)), 10, (0, 255, 0), -1) # 綠點：食指
        cv2.circle(image, (int(wrist.x * w), int(wrist.y * h)), 10, (255, 0, 255), -1)       # 紫點：手掌

        # 畫一條連線模擬「射線深度」
        cv2.line(image, 
                 (int(wrist.x * w), int(wrist.y * h)), 
                 (int(index_tip.x * w), int(index_tip.y * h)), 
                 (0, 200, 255), 2)

    # 顯示畫面
    cv2.imshow('NeoWalker Vision Pipeline (Tasks API)', image)
    
    if cv2.waitKey(5) & 0xFF == 27:
        break

cap.release()
cv2.destroyAllWindows()