# ASi Felis Origin — UI 模擬器實驗記錄

## 設計 Principle
橢圓極座標系統作為 ASi Felis 的 UI 設計語言核心：
- 所有 UI 元素沿橢圓弧分佈（極座標）
- 容器遵循橢圓語言，內容保持線性可讀
- 角落資訊以弧形徽章從橢圓錨點延伸（v4B 最佳）

## 2D 極座標 UI 模擬器

### polar_ui_v4.html — 統一橢圓幾何語言
四個設計概念即時對比：
- v3 矩形角落（對照）：直角與橢圓 principle 幾何衝突
- v4A pill 角落：消除直角，曲線一致
- **v4B 橢圓錨點延伸（最佳）**：資訊從橢圓邊緣錨點延伸，空間邏輯一致
- v4C 純極座標統一：零矩形，最激進

### polar_ui_v5.html — 應用程式視窗框架
三種視窗重新定義：
- A 弧形面板：頂底弧線消除直角，適合中等資訊量
- B 氣泡卡片：從橢圓錨點延伸，適合通知與快速操作
- C 透明疊層：無邊框漸層消散，適合環境標注

**設計結論：容器遵循橢圓語言，文字排版維持線性可讀性。**

## 3D 橢球 Texture Mapping 模擬器

### ellipsoid_v6.html — Three.js 真實渲染（最終版）
技術架構：
- 2D Canvas 繪製 UI → CanvasTexture → Three.js 橢球內表面（BackSide）
- 眼睛相機在原點 → PerspectiveCamera FOV=52°（VITURE Luma Ultra）
橢球參數：
- a（水平半徑）：控制左右曲率，小→強弧形，大→近平面
- b（垂直半徑）：控制上下曲率
- c（深度半徑）：控制前後深度感，小→視窗貼近，大→視窗退遠
- 視野角 FOV：UI 覆蓋範圍，建議 45°（VITURE 52° FOV 對應）

已驗證預設值：
| 模式 | a | b | c | 效果 |
|---|---|---|---|---|
| 極端弧形 | 0.5 | 0.35 | 0.4 | 強烈 VR 包覆感 |
| AR 最佳 | 1.4 | 1.0 | 1.2 | 自然空間彎曲 |
| 淺景深 | 1.4 | 1.0 | 0.5 | 視窗貼近 |
| 深景深 | 1.4 | 1.0 | 3.5 | 視窗退遠 |
| 近似平面 | 3.5 | 3.5 | 3.5 | 退化為平面螢幕 |

## 技術決策記錄
- Canvas 2D 無法做真正的 texture mapping（v1-v3 的根本問題）
- 必須用 WebGL/Three.js 才能實現真實橢球曲面效果
- BackSide + `repeat.set(-1,1)` + `offset.set(1,0)` 修正水平翻轉
- `SphereGeometry` phiStart/phiLength 限制部分球面，避免全球面覆蓋

## 對 ASi Felis M5 的設計價值
- 橢球 texture mapping 是 ASi Felis AR overlay 的候選渲染架構：
- UI 內容（2D） → CanvasTexture → 橢球內表面 → VITURE Luma Ultra 顯示
- 三軸半徑提供連續的「平面螢幕 ↔ VR 環繞」控制，
- c 參數可根據電致變色透光率動態調整沉浸感。