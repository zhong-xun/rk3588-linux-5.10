#!/bin/bash
# BoardLab 普通用户权限修复脚本

if [ "$(id -u)" -eq 0 ]; then
    echo "请不要以root运行此脚本"
    echo "使用方法: ./fix_permissions.sh 或 bash fix_permissions.sh"
    exit 1
fi

USERNAME=$(whoami)

echo "=== BoardLab 权限修复工具 ==="
echo "当前用户: $USERNAME"
echo ""

# 添加必要的用户组
echo "正在添加用户到必要的组..."
sudo groupadd -f boardlab-wifi
sudo usermod -aG netdev $USERNAME
sudo usermod -aG input $USERNAME
sudo usermod -aG video $USERNAME
sudo usermod -aG render $USERNAME
sudo usermod -aG audio $USERNAME
sudo usermod -aG dialout $USERNAME
sudo usermod -aG gpio $USERNAME
sudo usermod -aG i2c $USERNAME
sudo usermod -aG boardlab-wifi $USERNAME

echo "✓ 已添加用户组权限"
echo ""

# 设置GPIO权限
if [ -d /sys/class/gpio ]; then
    echo "设置GPIO权限..."
    sudo chown -R root:gpio /sys/class/gpio 2>/dev/null
    sudo chmod -R g+rw /sys/class/gpio 2>/dev/null
    echo "✓ GPIO权限已设置"
fi

if [ -d /sys/class/leds ]; then
    echo "设置板载LED权限..."
    for led in work work1 work2 user1 user2; do
        brightness="/sys/class/leds/$led/brightness"
        if [ -e "$brightness" ]; then
            sudo chgrp gpio "$brightness" 2>/dev/null || true
            sudo chmod 0660 "$brightness" 2>/dev/null || true
        fi
    done
    echo "✓ 板载LED权限已设置"
fi

# The pwm-beeper kernel driver owns the PWM controller. Grant only its exact
# input-event node to the input group, independent of event or PWM numbering.
# Some kernel versions use the exact space-separated input name.
echo "检查 pwm-beeper 输入事件权限..."
buzzer_found=0
for name_file in /sys/class/input/event*/device/name; do
    [ -r "$name_file" ] || continue
    IFS= read -r input_name < "$name_file" || continue
    case "$input_name" in
        pwm-beeper|"pwm beeper") ;;
        *) continue ;;
    esac
    event_name=$(basename "$(dirname "$(dirname "$name_file")")")
    event_path="/dev/input/$event_name"
    [ -e "$event_path" ] || continue
    sudo chgrp input "$event_path"
    sudo chmod 0660 "$event_path"
    buzzer_found=1
done
if [ "$buzzer_found" -eq 1 ]; then
    echo "✓ pwm-beeper 输入事件权限已设置"
else
    echo "提示：当前系统未发现 pwm-beeper，客户底板无此外设时可忽略"
fi

echo ""
echo "=== 修复完成 ==="
echo "⚠️  请注销并重新登录以使组权限生效"
echo ""
echo "验证命令:"
echo "  groups                    # 查看用户组"
echo ""
