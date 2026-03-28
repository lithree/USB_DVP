import serial
import numpy as np
import cv2

# 配置参数
PORT = 'COM6'       # 修改为你的串口号
BAUD = 921600       # 确保与单片机配置一致
WIDTH = 128         # 与单片机保持一致
HEIGHT = 96         # 与单片机保持一致
FRAME_BYTES = WIDTH * HEIGHT * 2
HEADER = b'\xAA\xBB\xCC\xDD'

def stream_parser():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.1)
        ser.flushInput()
        print(f"已连接串口 {PORT}，等待接收 RGB565 数据...")
    except Exception as e:
        print(f"串口打开失败: {e}")
        return

    buffer = bytearray()
    frame_count = 0

    while True:
        try:
            data = ser.read(4096)
            if data:
                buffer.extend(data)
            
            header_index = buffer.find(HEADER)
            
            if header_index != -1:
                # 检查是否包含一整帧数据 (帧头 + 图像数据)
                if len(buffer) >= header_index + len(HEADER) + FRAME_BYTES:
                    start_idx = header_index + len(HEADER)
                    end_idx = start_idx + FRAME_BYTES
                    frame_data = buffer[start_idx:end_idx]
                    
                    # 截断缓冲区，保留后续数据
                    buffer = buffer[end_idx:]
                    
                    # 解析 RGB565 大端数据 (OV2640硬件默认输出为大端)
                    img_16 = np.frombuffer(frame_data, dtype='>u2').reshape((HEIGHT, WIDTH))
                    
                    # 提取 R, G, B 通道
                    R5 = (img_16 >> 11) & 0x1F
                    G6 = (img_16 >> 5) & 0x3F
                    B5 = img_16 & 0x1F
                    
                    # 映射到 8-bit (0-255) 范围
                    R = (R5 * 255 // 31).astype(np.uint8)
                    G = (G6 * 255 // 63).astype(np.uint8)
                    B = (B5 * 255 // 31).astype(np.uint8)
                    
                    # 合并为 OpenCV 默认的 BGR 格式
                    img_bgr = cv2.merge([B, G, R])
                    
                    # 放大图像以适应屏幕观察
                    img_display = cv2.resize(img_bgr, (WIDTH * 4, HEIGHT * 4), interpolation=cv2.INTER_NEAREST)
                    cv2.imshow('OV2640 RGB565 Stream', img_display)
                    
                    frame_count += 1
                    print(f"\r成功解析第 {frame_count} 帧", end="")
                    
            else:
                # 未找到帧头时，保留末尾的 3 个字节防止截断
                if len(buffer) > len(HEADER) - 1:
                    buffer = buffer[-(len(HEADER) - 1):]
            
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break
                
        except KeyboardInterrupt:
            print("\n解析停止。")
            break
        except Exception as e:
            print(f"\n解析异常: {e}")
            buffer.clear()
            continue

    ser.close()
    cv2.destroyAllWindows()

if __name__ == '__main__':
    stream_parser()