import serial
import time
import os
import struct
import sys

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

                remaining_bytes = len(data) - (i + 4)
                if remaining_bytes > 0:
                    packet['raw_data'] = data[i+4:i+4+remaining_bytes]

                packets.append(packet)

                i += 4

                if remaining_bytes > 0:
                    i += remaining_bytes
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

    output_str = f"下位机响应 -> 魔数头:0x{packet['magic_head']:02X}, "
    output_str += f"魔数尾:0x{packet['magic_end']:02X}, "
    output_str += f"命令:{cmd_name}, "
    output_str += f"状态:{state_name}"

    if packet['cmd'] == 0x01:

        data_bytes = packet.get('raw_data', [])
        if len(data_bytes) >= 4:
            crc_value = int.from_bytes(data_bytes[:4], byteorder='little')
            output_str += f", Data:0x{crc_value:08X}"

    print(output_str)

def send_command_and_wait_ack(ser, command, timeout=200):
    """
    发送命令并等待ACK响应
    """
    print(f"发送命令: {[hex(b) for b in command]}")
    ser.write(command)

    print("等待ACK响应...")
    ack_received = False
    timeout_counter = 0

    while not ack_received and timeout_counter < timeout:
        response = ser.read_all()
        if response:

            packets = parse_bootloader_packets(response)
            for packet in packets:
                print_bootloader_packet(packet)

                if packet['cmd'] == 0x02:
                    print(f"收到ACK响应")
                    ack_received = True
                    break
                elif packet['cmd'] == 0x01:
                    print(f"收到错误响应")
                    return False

        time.sleep(0.1)
        timeout_counter += 1

    if not ack_received:
        print("ACK响应超时，停止程序")
        return False

    return True

def calculate_crc32(data):
    """
    计算CRC32校验值 (使用CRC32-CRC算法)
    """

    crc = 0xFFFFFFFF
    polynomial = 0xEDB88320

    for byte in data:

        crc ^= byte

        for j in range(8):

            if crc & 1:
                crc = (crc >> 1) ^ polynomial
            else:
                crc >>= 1

    return crc ^ 0xFFFFFFFF

def upgrade_firmware(ser, bin_file_path):
    """
    升级固件
    """
    try:

        if not os.path.exists(bin_file_path):
            print(f"固件文件不存在: {bin_file_path}")
            return False

        with open(bin_file_path, 'rb') as f:
            firmware_data = f.read()

        firmware_size = len(firmware_data)
        print(f"固件大小: {firmware_size} 字节")

        calculated_crc = calculate_crc32(firmware_data)
        print(f"计算得到的CRC32值: 0x{calculated_crc:08X}")

        start_cmd = bytearray([0xA5, 0x5A, 0x07, 0x00])

        start_cmd.extend(struct.pack('<I', firmware_size))

        start_cmd.extend(struct.pack('<I', calculated_crc))

        if not send_command_and_wait_ack(ser, start_cmd):
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

        print(f"固件升级完成!")
        return True

    except Exception as e:
        print(f"固件升级过程中发生错误: {e}")
        return False

def direct_jump_to_a(ser):
    """
    直接跳转到A区
    """

    jump_cmd = bytearray([0xA5, 0x5A, 0x05, 0x00])

    print(f"发送跳转到A区命令: {[hex(b) for b in jump_cmd]}")
    ser.write(jump_cmd)

    print("等待下位机响应...")
    response_received = False
    timeout_counter = 0
    max_timeout = 50

    while not response_received and timeout_counter < max_timeout:
        response = ser.read_all()
        if response:

            packets = parse_bootloader_packets(response)
            for packet in packets:
                print_bootloader_packet(packet)
                response_received = True

        time.sleep(0.1)
        timeout_counter += 1

    print("跳转命令已发送，单片机应该已跳转到A区应用程序")
    return True

def copy_b_to_a_and_jump(ser):
    """
    用B区覆盖A区并跳转
    """

    copy_and_jump_cmd = bytearray([0xA5, 0x5A, 0x06, 0x00])

    print(f"发送用B区覆盖A区并跳转命令: {[hex(b) for b in copy_and_jump_cmd]}")
    ser.write(copy_and_jump_cmd)

    print("等待下位机响应...")
    response_received = False
    timeout_counter = 0
    max_timeout = 50

    while not response_received and timeout_counter < max_timeout:
        response = ser.read_all()
        if response:

            packets = parse_bootloader_packets(response)
            for packet in packets:
                print_bootloader_packet(packet)

                if packet['cmd'] == 0x01:
                    print(f"收到错误响应，操作失败")
                    return False
                else:
                    response_received = True

        time.sleep(0.1)
        timeout_counter += 1

    print("复制B区到A区并跳转命令已发送，单片机应该已完成操作")
    return True

def connect_serial(port, baudrate=115200, timeout=5):
    """
    连接串口，带重试机制
    """
    try:
        ser = serial.Serial(port, baudrate, timeout=timeout)
        return ser
    except serial.SerialException:
        return None

def reconnect_serial(port, baudrate=115200, timeout=5, max_wait_time=5):
    """
    断线重连串口
    """
    print(f"尝试重连 {port}...")
    start_time = time.time()

    while time.time() - start_time < max_wait_time:
        ser = connect_serial(port, baudrate, timeout)
        if ser:
            print(f"重连成功，已连接到 {port}")
            return ser
        time.sleep(0.5)

    print(f"重连失败，在 {max_wait_time} 秒内未能连接到 {port}")
    return None

def show_main_menu(ser):
    """
    显示主菜单并处理用户选择
    """
    print("\n请选择开始模式:")
    print("1. 升级A区固件")
    print("2. 直接跳转到A区")
    print("3. 用B区覆盖A区并跳转")

    try:
        choice = int(input("请输入选择 (1-3): "))
        if choice in [1, 2, 3]:
            if choice == 1:

                bin_file_path = r"D:\code_D\stm32_code_MX\gitcode\Single-USB-burning-for-AB-partition\firmware\My_APP\MDK-ARM\My_APP\My_APP.bin"
                success = upgrade_firmware(ser, bin_file_path)
                if success:
                    print("固件升级成功完成！")
                else:
                    print("固件升级失败！")

            elif choice == 2:

                success = direct_jump_to_a(ser)
                if success:
                    print("跳转到A区命令发送成功！")

            elif choice == 3:

                success = copy_b_to_a_and_jump(ser)
                if success:
                    print("用B区覆盖A区并跳转命令发送成功！")
        else:
            print("选择无效")
    except ValueError:
        print("输入无效")

def is_serial_connected(ser):
    """
    检查串口连接是否仍然有效
    """
    try:

        if hasattr(ser, 'in_waiting'):

            return ser.is_open
        return False
    except:
        return False

def main():
    print("STM32 Bootloader 通信测试脚本")
    print("="*50)

    print("请选择开始模式:")
    print("1. 升级A区固件")
    print("2. 直接跳转到A区")
    print("3. 用B区覆盖A区并跳转")

    try:
        choice = int(input("请输入选择 (1-3): "))
    except ValueError:
        print("输入无效，程序退出")
        return

    if choice not in [1, 2, 3]:
        print("选择无效，程序退出")
        return

    com_port = 'COM23'
    ser = None

    try:
        print(f"尝试连接到 {com_port}...")
        ser = connect_serial(com_port, 115200, timeout=5)
        if not ser:
            raise serial.SerialException(f"无法连接到 {com_port}")
        print(f"已连接到 {com_port}")

        if choice == 1:

            bin_file_path = r"D:\code_D\stm32_code_MX\gitcode\Single-USB-burning-for-AB-partition\firmware\My_APP\MDK-ARM\My_APP\My_APP.bin"
            success = upgrade_firmware(ser, bin_file_path)
            if success:
                print("固件升级成功完成！")
            else:
                print("固件升级失败！")

        elif choice == 2:

            success = direct_jump_to_a(ser)
            if success:
                print("跳转到A区命令发送成功！")

        elif choice == 3:

            success = copy_b_to_a_and_jump(ser)
            if success:
                print("用B区覆盖A区并跳转命令发送成功！")

    except serial.SerialException as e:
        print(f"无法连接到 {com_port}: {e}")
        print("请检查:")
        print("1. STM32是否正确连接到电脑")
        print("2. 是否处于Bootloader模式")
        print("3. USB驱动是否正确安装")
        print("4. 端口是否被其他程序占用")
    except Exception as e:
        print(f"程序执行过程中发生错误: {e}")
    finally:
        try:
            if ser and ser.is_open:
                print("任务完成，继续监视串口回传信息... (按 Ctrl+C 退出)")

                while True:
                    try:

                        if not is_serial_connected(ser):
                            raise serial.SerialException("串口连接已断开")

                        response = ser.read_all()
                        if response:

                            packets = parse_bootloader_packets(response)
                            for packet in packets:
                                print_bootloader_packet(packet)

                        time.sleep(0.1)

                    except serial.SerialException:

                        print("检测到串口断开，尝试重连...")
                        try:
                            ser.close()
                        except:
                            pass

                        new_ser = reconnect_serial(com_port)
                        if new_ser:
                            ser = new_ser

                            print("\n重连成功！请确认单片机当前运行在哪个区：")
                            print("1. A区")
                            print("2. B区")

                            try:
                                region_choice = int(input("请选择 (1-2): "))

                                if region_choice == 1:

                                    print("\n当前在A区运行，是否要升级程序？")
                                    print("1. 是，升级程序")
                                    print("2. 否，返回主菜单")

                                    upgrade_choice = int(input("请选择 (1-2): "))

                                    if upgrade_choice == 1:

                                        start_cmd = bytearray([0xA5, 0x5A, 0x07, 0x00])

                                        print(f"发送开始指令: {[hex(b) for b in start_cmd]}")
                                        ser.write(start_cmd)

                                        print("等待ACK响应...")
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
                                                        print(f"收到ACK响应")
                                                        ack_received = True
                                                        break

                                            time.sleep(0.1)
                                            timeout_counter += 1

                                        if not ack_received:
                                            print("ACK响应超时")
                                        else:

                                            show_main_menu(ser)
                                    elif upgrade_choice == 2:

                                        show_main_menu(ser)
                                elif region_choice == 2:

                                    show_main_menu(ser)
                                else:
                                    print("选择无效，继续监视...")

                            except ValueError:
                                print("输入无效，继续监视...")
                        else:

                            print("重连失败，程序退出")
                            sys.exit(1)
                    except KeyboardInterrupt:
                        print("\n用户中断，程序退出")
                        break
                    except Exception as e:
                        print(f"监视过程中发生错误: {e}")
                        break
        except KeyboardInterrupt:
            print("\n用户中断，程序退出")
        except Exception as e:
            print(f"监视过程中发生错误: {e}")
        finally:
            try:
                if ser and ser.is_open:
                    ser.close()
                    print(f"串口 {com_port} 已关闭")
            except:
                pass

    print("程序结束")

if __name__ == "__main__":
    main()