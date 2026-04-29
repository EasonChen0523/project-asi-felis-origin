import cv2
import mediapipe as mp

# 初始化 MediaPipe Hands
mp_hands = mp.solutions.hands
hands = mp_hands.Hands(
    static_image_mode=False,
    max_num_hands=1,         # 為了節省算力，我們目前只追蹤單手
    min_detection_confidence=0.7,
    min_tracking_confidence=0.5
)
mp_drawing = mp.solutions.drawing_utils

# 連接 F16 內建相機 (若無法開啟，可嘗試將 0 改為 1 或 2)
cap = cv2.VideoCapture(0)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1920)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 1080)

print("黑足貓視覺管線已啟動。按下 'ESC' 鍵退出。")

while cap.isOpened():
    success, image = cap.read()
    if not success:
        print("警告：無法從相機獲取影像。")
        break

    # 由於 F16 鏡頭在 1080p 下有過曝傾向，這裡不做額外處理，直接轉為 RGB 供模型推論
    image_rgb = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
    results = hands.process(image_rgb)

    # 繪製結果並輸出深度數據
    if results.multi_hand_landmarks:
        for hand_landmarks in results.multi_hand_landmarks:
            # 繪製骨架在影像上以便視覺除錯
            mp_drawing.draw_landmarks(image, hand_landmarks, mp_hands.HAND_CONNECTIONS)
            
            # 擷取關鍵節點深度 (數值通常為負數，越靠近鏡頭絕對值越大)
            palm_z = hand_landmarks.landmark[0].z       # 手掌根部
            index_tip_z = hand_landmarks.landmark[8].z  # 食指指尖
            
            # 計算指尖相對於手掌的相對深度 (可用於判斷是否執行「點擊/捏合」)
            relative_depth = index_tip_z - palm_z
            
            print(f"手掌深度: {palm_z:.4f} | 食指深度: {index_tip_z:.4f} | 相對捏合: {relative_depth:.4f}")

    # 透過 WSLg 顯示影像視窗
    cv2.imshow('NeoWalker Hand Tracking (Plan B)', image)
    
    if cv2.waitKey(5) & 0xFF == 27: # 27 是 ESC 鍵的 ASCII 碼
        break

cap.release()
cv2.destroyAllWindows()