#!/usr/bin/env python3
"""
ZASR - Streaming ASR Server
Copyright (C) 2025-2026 zighouse <zighouse@users.noreply.github.com>

Licensed under the MIT License

zasr-server 麦克风实时识别客户端
从麦克风采集音频并发送到 zasr-server 进行实时语音识别
"""

import asyncio
import json
import uuid
import argparse
import sys
import signal

try:
    import websockets
except ImportError:
    print("错误: 需要安装 websockets 库")
    print("请运行: pip install websockets")
    sys.exit(1)

try:
    import pyaudio
except ImportError:
    print("错误: 需要安装 pyaudio 库")
    print("请运行: pip install pyaudio")
    print("注意: 在 Ubuntu 上可能需要先运行: sudo apt-get install python3-pyaudio portaudio19-dev")
    sys.exit(1)


# 音频配置
SAMPLE_RATE = 16000
CHANNELS = 1
FORMAT = pyaudio.paInt16
CHUNK_SIZE = 512  # 每次从麦克风读取的样本数
CHUNK_MS = int(CHUNK_SIZE / SAMPLE_RATE * 1000)  # 32ms
SEND_BATCH_MS = 200  # 每200ms发送一次
SEND_BATCH_SIZE = int(SAMPLE_RATE * SEND_BATCH_MS / 1000)  # 3200样本 = 6400字节


class AudioRecorder:
    """音频录制器，使用 pyaudio 从麦克风采集音频"""

    def __init__(self, sample_rate=SAMPLE_RATE, channels=CHANNELS, chunk_size=CHUNK_SIZE):
        self.sample_rate = sample_rate
        self.channels = channels
        self.chunk_size = chunk_size
        self.audio = pyaudio.PyAudio()
        self.stream = None
        self.is_recording = False

    def start(self):
        """开始录制"""
        self.stream = self.audio.open(
            format=FORMAT,
            channels=self.channels,
            rate=self.sample_rate,
            input=True,
            frames_per_buffer=self.chunk_size
        )
        self.is_recording = True
        print(f"麦克风已启动: {self.sample_rate}Hz, {self.channels}声道, 16位")

    def read_chunk(self):
        """读取一块音频数据"""
        if self.stream and self.is_recording:
            return self.stream.read(self.chunk_size, exception_on_overflow=False)
        return None

    def stop(self):
        """停止录制"""
        self.is_recording = False
        if self.stream:
            self.stream.stop_stream()
            self.stream.close()
            self.stream = None
        print("麦克风已停止")

    def __del__(self):
        """清理资源"""
        if self.stream:
            self.stream.stop_stream()
            self.stream.close()
        self.audio.terminate()


async def receive_messages(ws, task_id):
    """
    接收并处理服务器消息

    Args:
        ws: WebSocket 连接
        task_id: 任务 ID
    """
    message_count = 0
    current_sentence = None
    timeout_count = 0
    MAX_TIMEOUTS = 15  # 最多允许连续超时次数（15秒）

    # npm 风格动画帧
    animation_frames = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]
    animation_idx = 0
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
        nonlocal animation_idx

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
            try:
                message = await asyncio.wait_for(ws.recv(), timeout=1.0)
                timeout_count = 0  # 重置超时计数
            except asyncio.TimeoutError:
                timeout_count += 1
                if timeout_count >= MAX_TIMEOUTS:
                    # 连续超时，认为没有更多消息了
                    break
                continue

            if isinstance(message, str):
                try:
                    data = json.loads(message)
                    header = data.get("header", {})
                    name = header.get("name", "UNKNOWN")
                    status = header.get("status", -1)
                    message_count += 1

                    if name == "Started":
                        session_id = data.get("payload", {}).get("sid", "")
                        # 清除蛇形动画并显示启动信息
                        if show_snake_animation:
                            show_snake_animation = False
                            sys.stdout.write("\r" + " " * 80 + "\r")
                            sys.stdout.flush()
                        print(f"✓ 转录会话已启动 (session_id: {session_id})")
                        # 恢复蛇形动画
                        show_snake_animation = True

                    elif name == "SentenceBegin":
                        payload = data.get("payload", {})
                        index = payload.get("idx", 0)
                        time_ms = payload.get("time", 0)
                        current_sentence = {"index": index, "begin_time": time_ms}

                    elif name == "Result":
                        payload = data.get("payload", {})
                        index = payload.get("idx", 0)
                        result = payload.get("text", "")
                        time_ms = payload.get("time", 0)

                        # 停止蛇形动画，显示识别结果
                        if show_snake_animation:
                            show_snake_animation = False
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
                        result = payload.get("text", "")
                        begin_time = payload.get("begin", 0)
                        end_time = payload.get("time", 0)

                        # 句子结束时，换行显示最终结果
                        sys.stdout.write("\r")
                        sys.stdout.flush()

                        duration_ms = end_time - begin_time
                        time_str = f"{format_time_hms(begin_time)} - {format_duration(duration_ms)}"
                        print(f"✓ {index}. {time_str} | {result}")

                        current_sentence = None
                        # 句子结束后，恢复蛇形动画
                        show_snake_animation = True

                    elif name == "Completed":
                        break

                    elif name == "Failed":
                        status_text = header.get("status_text", "Unknown error")
                        print(f"\n✗ 转录失败: {status_text}")
                        break

                except json.JSONDecodeError as e:
                    print(f"\nJSON 解析错误: {e}")

    except websockets.exceptions.ConnectionClosed:
        print("\n连接已关闭")
    finally:
        # 停止蛇形动画任务
        snake_stop_event.set()
        try:
            await asyncio.wait_for(snake_task, timeout=1.0)
        except (asyncio.TimeoutError, Exception):
            pass

    return message_count


async def send_audio_loop(ws, recorder, stop_event, silence_threshold=200, max_silence_seconds=5.0):
    """
    持续从麦克风读取音频并发送到服务器

    Args:
        ws: WebSocket 连接
        recorder: 音频录制器
        stop_event: 停止事件
        silence_threshold: 静音阈值（RMS值，低于此值视为静音）
        max_silence_seconds: 最大静音时长（秒），超过后自动停止发送
    """
    import audioop
    import math

    buffer = b''
    batch_count = 0
    silence_count = 0  # 连续静音的批次计数
    max_silence_batches = int(max_silence_seconds * 1000 / SEND_BATCH_MS)  # 将秒转换为批次数

    #print(f"[发送线程] 正在采集音频... (每 {SEND_BATCH_MS}ms 发送一次)")
    #print(f"[发送线程] 静音检测阈值: {silence_threshold}, 最大静音时长: {max_silence_seconds}秒")

    while not stop_event.is_set():
        # 从麦克风读取一块音频（使用线程池避免阻塞事件循环）
        chunk = await asyncio.to_thread(recorder.read_chunk)
        if chunk is None:
            break

        buffer += chunk

        # 当缓冲区达到一个批次大小时发送
        if len(buffer) >= SEND_BATCH_SIZE:
            batch_data = buffer[:SEND_BATCH_SIZE]
            buffer = buffer[SEND_BATCH_SIZE:]

            # 检测音量（RMS）
            rms = audioop.rms(batch_data, 2)  # 2 bytes per sample (16-bit)
            is_silence = rms < silence_threshold

            if is_silence:
                silence_count += 1
            else:
                silence_count = 0  # 重置静音计数

            # 发送音频数据
            try:
                await ws.send(batch_data)
                batch_count += 1

                # 每5秒显示一次状态
                if batch_count % (1000 // CHUNK_MS * 5) == 0:
                    elapsed = batch_count * SEND_BATCH_MS / 1000
                    silence_status = f"静音({silence_count}/{max_silence_batches})" if is_silence else f"音量({rms})"
                    #print(f"[发送线程] 已录制 {elapsed:.1f} 秒, 发送 {batch_count} 个批次 [{silence_status}]")

                # 检测到持续静音，自动停止
                if silence_count >= max_silence_batches:
                    #print(f"\n[发送线程] 检测到 {max_silence_seconds} 秒静音，自动停止发送")
                    break

            except Exception as e:
                print(f"\n[发送线程] 发送音频错误: {e}")
                break

    print(f"[发送线程] 音频发送完成，共发送 {batch_count} 个批次")


async def run_microphone_test(server_url: str, auto_stop: bool = False, max_silence: float = 3.0):
    """
    运行麦克风测试

    Args:
        server_url: WebSocket 服务器地址
        auto_stop: 是否自动停止（检测静音后停止）
        max_silence: 最大静音时长（秒），超过此时长后自动停止
    """

    print(f"连接到服务器: {server_url}")

    # 生成任务ID和消息ID
    task_id = str(uuid.uuid4())
    message_id = str(uuid.uuid4())

    # 创建音频录制器
    recorder = AudioRecorder()

    try:
        async with websockets.connect(server_url, ping_timeout=120) as ws:
            print("WebSocket 连接成功！\n")

            # 发送 Begin 请求
            print("[1] 发送 Begin 请求...")
            start_request = {
                "header": {
                    "name": "Begin",
                    "mid": message_id
                },
                "payload": {
                    "fmt": "pcm",
                    "rate": SAMPLE_RATE,
                    "itn": True,
                    "silence": 800
                }
            }

            await ws.send(json.dumps(start_request, ensure_ascii=False))
            print(f"请求已发送\n")

            # 等待 Started 响应
            print("[2] 等待服务器确认...")
            response_text = await asyncio.wait_for(ws.recv(), timeout=10)
            response = json.loads(response_text)

            if (response.get("header", {}).get("name") != "Started" or
                response.get("header", {}).get("status") != 20000000):
                print(f"错误: 服务器未确认启动")
                print(f"响应: {json.dumps(response, ensure_ascii=False, indent=2)}")
                return

            # 启动音频录制和消息接收
            recorder.start()
            stop_event = asyncio.Event()

            # 创建并发任务
            receive_task = asyncio.create_task(receive_messages(ws, task_id))
            send_task = asyncio.create_task(send_audio_loop(ws, recorder, stop_event,
                                                           silence_threshold=200,
                                                           max_silence_seconds=5.0))

            print("\n提示: 按 Ctrl+C 停止录制\n")
            print("=" * 60)

            # 等待用户中断
            try:
                await send_task
            except KeyboardInterrupt:
                print("\n\n检测到用户中断...")
                stop_event.set()

            # 取消发送任务
            if not send_task.done():
                send_task.cancel()
                try:
                    await send_task
                except asyncio.CancelledError:
                    pass

            # 停止录音
            recorder.stop()

            # 发送 End 消息
            print("\n[3] 发送 End 消息...")
            stop_request = {
                "header": {
                    "name": "End",
                    "mid": str(uuid.uuid4())
                },
                "payload": {}
            }

            await ws.send(json.dumps(stop_request, ensure_ascii=False))

            # 等待所有识别结果
            print("[4] 等待最终识别结果（最多10秒）...")
            try:
                await asyncio.wait_for(receive_task, timeout=10)
            except asyncio.TimeoutError:
                print("等待识别结果超时")
                # 取消接收任务
                if not receive_task.done():
                    receive_task.cancel()
                    try:
                        await receive_task
                    except asyncio.CancelledError:
                        pass

            print("\n" + "=" * 60)
            print("识别会话结束")

    except ConnectionRefusedError:
        print(f"错误: 无法连接到服务器 {server_url}")
        print("请确保 zasr-server 正在运行")
    except asyncio.TimeoutError:
        print("错误: 连接超时")
    except Exception as e:
        print(f"错误: {e}")
        import traceback
        traceback.print_exc()
    finally:
        recorder.stop()


def main():
    parser = argparse.ArgumentParser(
        description="zasr-server 麦克风实时语音识别客户端",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  # 使用默认服务器
  %(prog)s

  # 指定服务器地址
  %(prog)s --server ws://192.168.1.100:2026

  # 列出可用的麦克风设备
  %(prog)s --list-devices

使用说明:
  1. 运行程序后，对着麦克风说话
  2. 程序会实时显示识别结果
  3. 按 Ctrl+C 停止录制
  4. 程序会等待最终识别结果后退出
        """
    )

    parser.add_argument("--server", default="ws://localhost:2026",
                       help="WebSocket 服务器地址 (默认: ws://localhost:2026)")
    parser.add_argument("--list-devices", action="store_true",
                       help="列出可用的音频输入设备")

    args = parser.parse_args()

    # 列出设备
    if args.list_devices:
        print("可用的音频输入设备:\n")
        p = pyaudio.PyAudio()
        for i in range(p.get_device_count()):
            info = p.get_device_info_by_index(i)
            if info['maxInputChannels'] > 0:
                print(f"设备 {i}: {info['name']}")
                print(f"  采样率: {int(info['defaultSampleRate'])}Hz")
                print(f"  输入通道: {info['maxInputChannels']}")
                print()
        p.terminate()
        return

    # 运行麦克风测试
    try:
        asyncio.run(run_microphone_test(args.server))
    except KeyboardInterrupt:
        print("\n\n程序已退出")


if __name__ == "__main__":
    main()
