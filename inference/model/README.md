把你要部署的 YOLO 權重複製到這裡，命名 best.pt：

  Windows PowerShell（在 bike-assist-v3\ 下）：
  copy ml\runs\safeway_v3_pothole_dog_640-3\weights\best.pt inference\model\best.pt

docker-compose 會把這個目錄唯讀掛進 inference 容器的 /model，
worker 讀 /model/best.pt。權重本身不進 git（見 .gitignore）。
