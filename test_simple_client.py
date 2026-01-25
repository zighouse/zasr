#!/usr/bin/env python3
"""
zasr-server 测试客户端
用于测试 WebSocket 识别功能
"""

import asyncio
import json
import uuid
import wave
import struct
import argparse
import sys
import time

try:
    import websockets
except ImportError:
    print("错误: 需要安装 websockets 库")
    print("请运行: pip install websockets")
    sys.exit(1)

async def test_zasr_server_new(audio_file: str, server_url: str = "ws://localhost:2026"):
    """
    测试 zasr-server 的识别功能
    
    Args:
        audio_file: 音频文件路径（WAV格式）
        server_url: WebSocket 服务器地址
    """
    
    print(f"连接到服务器: {server_url}")
    print(f"音频文件: {audio_file}")
    
    # 1. 生成任务ID和消息ID
    task_id = str(uuid.uuid4())
    message_id = str(uuid.uuid4())
    print(f"task_id: {task_id}")
    print(f"message_id: {message_id}")
    
    # 2. 读取 WAV 文件
    print("读取音频文件...")
    try:
        with wave.open(audio_file, 'rb') as wf:
            # 检查格式
            if wf.getnchannels() != 1:
                print(f"警告: 音频文件不是单声道（当前: {wf.getnchannels()}声道）")
                print("期望: 1声道（单声道）")
            
            if wf.getsampwidth() != 2:
                print(f"警告: 音频采样宽度不是16位（当前: {wf.getsampwidth()}字节）")
                print("期望: 2字节（16位）")
            
            if wf.getframerate() != 16000:
                print(f"警告: 音频采样率不是16000Hz（当前: {wf.getframerate()}Hz）")
                print("期望: 16000Hz")
            
            # 读取音频数据
            audio_data = wf.readframes(wf.getnframes())
            print(f"音频数据大小: {len(audio_data)} 字节")
            print(f"音频时长: {len(audio_data) / 2 / 16000:.2f} 秒")
    except Exception as e:
        print(f"读取音频文件失败: {e}")
        sys.exit(1)
    
    # 计算发送批次（每60ms发送一次）
    sample_rate = 16000
    batch_size_ms = 200 #60
    batch_size = int(sample_rate * batch_size_ms / 1000) * 2  # 字节数
    
    print(f"每次发送: {batch_size} 字节 ({batch_size_ms}ms)")
    
    # 3. 连接 WebSocket
    try:
        async with websockets.connect(server_url, ping_timeout=120) as ws:
            print("WebSocket 连接成功！")
            
            # 4. 发送 Begin 请求
            print("\n[1] 发送 Begin 请求...")
            start_request = {
                "header": {
                    "name": "Begin",
                    "mid": message_id
                },
                "payload": {
                    "fmt": "pcm",
                    "rate": 16000,
                    "itn": True,
                    "silence": 500
                }
            }

            await ws.send(json.dumps(start_request, ensure_ascii=False))
            print(f"发送: {json.dumps(start_request, ensure_ascii=False, indent=2)}")

            # 5. 接收 Started 响应
            print("\n[2] 等待 Started 响应...")
            response_text = await asyncio.wait_for(ws.recv(), timeout=10)
            response = json.loads(response_text)
            print(f"收到: {json.dumps(response, ensure_ascii=False, indent=2)}")

            if (response.get("header", {}).get("name") != "Started" or
                response.get("header", {}).get("status") != 20000000):
                print(f"错误: 期望 Started 响应")
                header = response.get("header", {})
                print(f"收到: name={header.get('name')}, status={header.get('status')}")
                return

            session_id = response.get("payload", {}).get("sid", "")
            print(f"转录开始成功！session_id={session_id}")

            # 6. 发送音频数据（使用独立的发送任务，与接收任务并发执行）
            print(f"\n[3] 开始发送音频数据...")

            # 同时启动发送和接收任务
            send_task = asyncio.create_task(send_audio_data(ws, audio_data, batch_size, batch_size_ms))
            receive_task = asyncio.create_task(receive_messages_new(ws, task_id))

            # 等待发送完成
            await send_task
            print(f"音频发送完成")

            # 7. 发送 End 消息
            print("\n[4] 发送 End 消息...")
            stop_request = {
                "header": {
                    "name": "End",
                    "mid": str(uuid.uuid4())
                },
                "payload": {}
            }

            await ws.send(json.dumps(stop_request, ensure_ascii=False))
            print(f"发送: {json.dumps(stop_request, ensure_ascii=False, indent=2)}")
            
            # 8. 等待 TranscriptionCompleted 响应和所有识别结果
            print("\n[5] 等待识别结果和转录完成...")
            try:
                await asyncio.wait_for(receive_task, timeout=30)
            except asyncio.TimeoutError:
                print("警告: 等待识别结果超时")
            
            print(f"\n所有识别结果接收完毕")
            
    except asyncio.TimeoutError:
        print(f"错误: 操作超时")
    except ConnectionRefusedError:
        print(f"错误: 无法连接到服务器 {server_url}")
        print("请确保 zasr-server 正在运行")
    except Exception as e:
        print(f"错误: {e}")
        import traceback
        traceback.print_exc()


async def send_audio_data(ws, audio_data, batch_size, batch_size_ms):
    """
    发送音频数据的独立协程

    Args:
        ws: WebSocket 连接
        audio_data: 音频数据（bytes）
        batch_size: 每批发送的音频字节数
        batch_size_ms: 每批音频的毫秒数
    """
    total_sent = 0
    batch_count = 0

    for offset in range(0, len(audio_data), batch_size):
        batch = audio_data[offset:offset+batch_size]

        # 如果最后一批数据不足，用 0 填充
        if len(batch) < batch_size:
            batch = batch + b'\x00' * (batch_size - len(batch))

        await ws.send(batch)
        total_sent += len(batch)
        batch_count += 1

        # 等待指定时间，模拟实时音频流
        await asyncio.sleep(batch_size_ms / 1000.0)


async def receive_messages_new(ws, task_id):
    """
    接收 WebSocket 消息
    """
    import sys

    message_count = 0
    all_results = []
    current_sentence = None
    # npm 风格动画帧
    animation_frames = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]
    animation_idx = 0
    last_display_time = -1
    # 控制蛇形动画的标志
    show_snake_animation = True
    snake_stop_event = asyncio.Event()

    def format_time_hms(ms):
        """将毫秒转换为 HH:MM:SS 格式（开始时间）"""
        seconds = ms // 1000
        hours = seconds // 3600
        minutes = (seconds % 3600) // 60
        seconds = seconds % 60
        return f"{hours:02d}:{minutes:02d}:{seconds:02d}"

    def format_duration(ms):
        """将毫秒转换为 MM:SS 格式（持续时间）"""
        seconds = ms // 1000
        minutes = seconds // 60
        seconds = seconds % 60
        return f"{minutes:02d}:{seconds:02d}"

    def update_display(time_ms, begin_time, result, sentence_idx=None):
        """在同一行动态更新显示"""
        nonlocal animation_idx, last_display_time

        last_display_time = time_ms

        # 计算持续时间
        duration_ms = time_ms - begin_time

        # 构建显示内容
        time_str = f"{format_time_hms(begin_time)} - {format_duration(duration_ms)}"
        animation = animation_frames[animation_idx % len(animation_frames)]
        animation_idx += 1

        # 清空当前行并显示新内容
        if sentence_idx is not None:
            display = f"\r{animation} {sentence_idx}. {time_str} | {result:<50}"
        else:
            display = f"\r{animation} {time_str} | {result:<50}"

        sys.stdout.write(display)
        sys.stdout.flush()

    async def snake_animation():
        """蛇形前进动画"""
        nonlocal show_snake_animation
        width = 30
        pos = 0
        direction = 1

        while not snake_stop_event.is_set():
            if not show_snake_animation:
                await asyncio.sleep(0.05)
                continue

            # 构建蛇形条
            snake_bar = ["·"] * width
            for i in range(max(0, pos - 2), min(width, pos + 1)):
                snake_bar[i] = "="
            if pos < width:
                snake_bar[pos] = "="

            output = f"\r  等待语音输入 [{''.join(snake_bar)}]"
            sys.stdout.write(output)
            sys.stdout.flush()

            # 更新位置
            pos += direction
            if pos >= width - 1:
                direction = -1
            elif pos <= 0:
                direction = 1

            await asyncio.sleep(0.1)

        # 清除蛇形动画行
        sys.stdout.write("\r" + " " * 80 + "\r")
        sys.stdout.flush()

    # 启动蛇形动画任务
    snake_task = asyncio.create_task(snake_animation())

    try:
        while True:
            # 接收消息（带超时）
            try:
                message = await asyncio.wait_for(ws.recv(), timeout=5)
            except asyncio.TimeoutError:
                # 如果没有更多消息，继续等待
                continue

            if isinstance(message, str):
                # 文本消息（JSON）
                try:
                    data = json.loads(message)
                    header = data.get("header", {})
                    name = header.get("name", "UNKNOWN")
                    status = header.get("status", -1)
                    message_count += 1

                    if name == "SentenceBegin":
                        payload = data.get("payload", {})
                        index = payload.get("idx", 0)
                        time_ms = payload.get("time", 0)
                        current_sentence = {"index": index, "begin_time": time_ms}

                    elif name == "Result":
                        payload = data.get("payload", {})
                        index = payload.get("idx", 0)
                        time_ms = payload.get("time", 0)
                        result = payload.get("text", "")

                        # 停止蛇形动画，显示识别结果
                        if show_snake_animation:
                            show_snake_animation = False
                            # 清除蛇形动画行
                            sys.stdout.write("\r" + " " * 80 + "\r")
                            sys.stdout.flush()

                        # 动态更新显示（需要获取 begin_time）
                        begin_time = current_sentence.get("begin_time", time_ms) if current_sentence else time_ms
                        update_display(time_ms, begin_time, result, index)

                        if current_sentence and current_sentence["index"] == index:
                            current_sentence["current_result"] = result
                            current_sentence["current_time"] = time_ms

                    elif name == "SentenceEnd":
                        payload = data.get("payload", {})
                        index = payload.get("idx", 0)
                        time_ms = payload.get("time", 0)
                        begin_time = payload.get("begin", 0)
                        result = payload.get("text", "")

                        # 句子结束时，换行显示最终结果
                        # 清除当前行并显示完成状态
                        sys.stdout.write("\r")  # 回到行首
                        sys.stdout.flush()

                        duration_ms = time_ms - begin_time
                        time_str = f"{format_time_hms(begin_time)} - {format_duration(duration_ms)}"
                        print(f"✓ {index}. {time_str} | {result}")

                        all_results.append({
                            "index": index,
                            "begin_time": begin_time,
                            "end_time": time_ms,
                            "result": result
                        })

                        current_sentence = None
                        # 句子结束后，恢复蛇形动画
                        show_snake_animation = True

                    elif name == "Completed":
                        print("转录完成")
                        break

                    elif name == "Failed":
                        status_text = header.get("status_text", "Unknown error")
                        print(f"转录失败: {status_text}")
                        break
                        
                except json.JSONDecodeError as e:
                    print(f"JSON 解析错误: {e}")
                    print(f"原始消息: {message}")
            else:
                # 二进制消息（不应该收到，因为服务器只发送文本）
                print(f"收到二进制消息 ({len(message)} 字节)")

    except websockets.exceptions.ConnectionClosed:
        print("连接已关闭")
    finally:
        # 停止蛇形动画任务
        snake_stop_event.set()
        try:
            await asyncio.wait_for(snake_task, timeout=1.0)
        except (asyncio.TimeoutError, Exception):
            pass

    #print(f"\n总共收到 {message_count} 条消息")
    #if all_results:
    #    print(f"\n所有句子识别结果:")
    #    for result in sorted(all_results, key=lambda x: x["index"]):
    #        print(f"  句子 {result['index']}: [{result['begin_time']}ms-{result['end_time']}ms] {result['result']}")


def main():
    parser = argparse.ArgumentParser(description="测试 zasr-server 识别功能")
    parser.add_argument("audio_file", help="音频文件路径（WAV格式）")
    parser.add_argument("--server", default="ws://localhost:2026", 
                       help="WebSocket 服务器地址（默认: ws://localhost:2026）")
    
    args = parser.parse_args()
    
    asyncio.run(test_zasr_server_new(args.audio_file, args.server))


if __name__ == "__main__":
    main()
