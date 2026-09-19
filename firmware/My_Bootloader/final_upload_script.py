import serial
import time
import os
import struct
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler

def parse_bootloader_packets(data):
    """
    解析BootloaderPacket_t格式的数据
    格式: [magic_head][magic_end][cmd][BootloaderState][data(可选)]
    """
    packets = []
    magic_header = 0xA5
    magic_footer = 0x5A

    i = 0
    while i < len(data) - 3:
        if data[i] == magic_header and data[i+1] == magic_footer:

            if i + 3 < len(data):
                packet = {
                    'magic_head': data[i],
                    'magic_end': data[i+1],
                    'cmd': data[i+2],
                    'bootloader_state': data[i+3]
                }

                packets.append(packet)

                i += 4
            else:
                i += 1
        else:
            i += 1

    return packets

def print_bootloader_packet(packet):
    """
    按BootloaderPacket_t格式打印数据
    """
    cmd_names = {
        0x00: "Cmd_Which_State",
        0x01: "Cmd_Invalid / ERROR",
        0x02: "Cmd_ACK",
        0x03: "Cmd_NACK",
        0x04: "Cmd_Update_State",
        0x05: "Cmd_Jump_To_A",
        0x06: "Cmd_CopyB_To_A_Jump_To_A",
        0x07: "Cmd_Updata_A"
    }

    state_names = {
        0x00: "Run_Invalid",
        0x01: "Run_Waiting_Cmd",
        0x02: "Run_Receiving_Bin",
        0x03: "Run_Flash_Erasure",
        0x04: "Run_Flash_Write",
        0x05: "Run_Prepare_To_Jump_To_A",
        0x06: "Run_Prepare_To_Jump_To_B"
    }

    cmd_name = cmd_names.get(packet['cmd'], f"Unknown_Cmd(0x{packet['cmd']:02X})")
    state_name = state_names.get(packet['bootloader_state'], f"Unknown_State(0x{packet['bootloader_state']:02X})")

    print(f"下位机响应 -> 魔数头:0x{packet['magic_head']:02X}, "
          f"魔数尾:0x{packet['magic_end']:02X}, "
          f"命令:{cmd_name}, "
          f"状态:{state_name}")

class VersionFileHandler(FileSystemEventHandler):
    def __init__(self):
        self.last_version = self.read_version()
        print(f"初始版本号: {self.last_version}")

    def read_version(self):
        """读取版本号"""
        try:
            with open('D:\\code_D\\stm32_code_MX\\My_RemoteDownload\\My_Bootloader\\Test\\version.txt', 'r') as f:
                return f.read().strip()
        except Exception as e:
            print(f"读取版本号失败: {e}")
            return None

    def on_modified(self, event):
        """文件修改事件处理"""
        if event.src_path.endswith('version.txt') and not event.is_directory:
            current_version = self.read_version()
            if current_version != self.last_version:
                print(f"版本号发生变化: {self.last_version} -> {current_version}")

                self.update_firmware(current_version)

                self.last_version = current_version

    def update_firmware(self, version):
        """执行固件升级"""
        bin_file_path = 'D:\\code_D\\stm32_code_MX\\My_RemoteDownload\\My_Bootloader\\Test\\My_APP.bin'

        try:

            if not os.path.exists(bin_file_path):
                print(f"固件文件不存在: {bin_file_path}")
                return False

            com_port = 'COM23'
            ser = None

            try:
                print(f"尝试连接到 {com_port}...")
                ser = serial.Serial(com_port, 115200, timeout=5)

                print(f"已连接到 {com_port}")
            except serial.SerialException as e:
                print(f"无法连接到 {com_port}: {e}")
                print("请检查:")
                print("1. STM32是否正确连接到电脑")
                print("2. 是否处于Bootloader模式")
                print("3. USB驱动是否正确安装")
                print("4. 端口是否被其他程序占用")
                return False

            with open(bin_file_path, 'rb') as f:
                firmware_data = f.read()

            firmware_size = len(firmware_data)
            print(f"固件大小: {firmware_size} 字节")

            start_cmd = bytearray([0xA5, 0x5A, 0x07, 0x00])

            start_cmd.extend(struct.pack('<I', firmware_size))

            print(f"发送启动命令: {[hex(b) for b in start_cmd]}")
            ser.write(start_cmd)

            print("等待启动命令的ACK响应...")
            ack_received = False
            timeout_counter = 0
            max_timeout = 200

            while not ack_received and timeout_counter < max_timeout:
                response = ser.read_all()
                if response:

                    packets = parse_bootloader_packets(response)
                    for packet in packets:
                        print_bootloader_packet(packet)

                        if packet['cmd'] == 0x02:
                            print(f"收到启动命令的ACK响应")
                            ack_received = True
                            break
                        elif packet['cmd'] == 0x01:
                            print(f"收到错误响应，Flash擦除失败")
                            return False

                time.sleep(0.1)
                timeout_counter += 1

            if not ack_received:
                print("启动命令ACK响应超时，停止程序")
                return False

            chunk_size = 512
            total_sent = 0

            while total_sent < firmware_size:

                remaining = firmware_size - total_sent
                current_chunk_size = min(chunk_size, remaining)

                chunk_data = firmware_data[total_sent:total_sent + current_chunk_size]

                ser.write(chunk_data)
                total_sent += current_chunk_size

                print(f"已发送: {total_sent}/{firmware_size} 字节 ({total_sent/firmware_size*100:.1f}%)")

                ack_received = False
                timeout_counter = 0
                max_timeout = 100

                while not ack_received and timeout_counter < max_timeout:
                    response = ser.read_all()
                    if response:

                        packets = parse_bootloader_packets(response)
                        for packet in packets:
                            print_bootloader_packet(packet)

                            if packet['cmd'] == 0x02:
                                print(f"收到数据块ACK响应")
                                ack_received = True
                                break
                            elif packet['cmd'] == 0x01:
                                print(f"收到错误响应，固件下载失败")
                                return False

                    time.sleep(0.1)
                    timeout_counter += 1

                if not ack_received:
                    print(f"数据块ACK响应超时，已发送 {total_sent}/{firmware_size} 字节，停止程序")
                    return False

            print("固件发送完成")

            print("等待Bootloader处理完成...")
            time.sleep(5)

            while ser.in_waiting:
                final_response = ser.read(ser.in_waiting)
                if final_response:
                    print(f"最终响应: {[hex(b) for b in final_response]}")

            print(f"固件升级完成! 版本号: {version}")
            return True

        except serial.SerialException as e:
            print(f"串口通信错误: {e}")
            print("请检查设备连接和驱动程序")
            return False
        except Exception as e:
            print(f"固件升级过程中发生错误: {e}")
            return False
        finally:
            try:
                if ser and ser.is_open:
                    ser.close()
                    print(f"串口 {com_port} 已关闭")
            except:
                pass

def main():
    version_file_path = 'D:\\code_D\\stm32_code_MX\\My_RemoteDownload\\My_Bootloader\\Test\\version.txt'

    if not os.path.exists(version_file_path):
        print(f"版本文件不存在: {version_file_path}")
        return

    event_handler = VersionFileHandler()

    observer = Observer()
    observer.schedule(event_handler, os.path.dirname(version_file_path), recursive=False)

    observer.start()
    print(f"开始监控版本文件: {version_file_path}")
    print("等待版本号变化...")
    print("提示：修改 version.txt 文件中的版本号以触发固件升级")
    print("注意：确保STM32处于Bootloader模式并正确连接")
    print("使用端口: COM23")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        observer.stop()
        print("\n停止监控")

    observer.join()

if __name__ == "__main__":
    main()