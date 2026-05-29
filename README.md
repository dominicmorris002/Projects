## 🚀 QUICK START (COPY + PASTE)

### 1) Activate Zephyr environment
```powershell
cd C:\Projects\DripMonitor-feature-esp32plc
.venv\Scripts\activate
```

### 2) Build firmware
```powershell
west build -d build\esp32_plc_21
```

### 3) Flash ESP32 PLC
```powershell
west flash -d build\esp32_plc_21
```

### 4) Serial monitor
```powershell
python -m serial.tools.miniterm COM22 115200
```







### Update Changes From Computer to Github

```powershell
cd C:\Projects
git add .
git commit -m "Update project"
git push
```
