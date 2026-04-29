import cv2

def test_camera():
    print("🚀 Project ASi: 啟動『抗干擾』修復模式...")
    cap = cv2.VideoCapture(0, cv2.CAP_V4L2)
    
    # 強制切換為 YUYV 格式，並降低解析度以確保封包能穿透
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'YUYV'))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
    
    # 增加讀取等待時間
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 2)

    if not cap.isOpened():
        print("❌ 無法開啟設備")
        return

    # 讀取多次，直到緩衝區填滿
    for i in range(5):
        ret, frame = cap.read()
        if ret:
            cv2.imwrite('/mnt/f/AI/MultiModelTest/wsl_survival_shot.jpg', frame)
            print(f"✅ 突破阻塞！成功擷取。")
            break
    else:
        print("❌ 依然超時。")
    cap.release()