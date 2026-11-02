# 迅为开发板测试平台

北京迅为电子的“迅为开发板测试平台”面向 iTOP-RK3562、RK3568、RK3576、RK3588 与 RK3588S Linux 开发板，提供温湿度检测、按键检测、GPIO、UART、以太网、CAN、4G/5G、摄像头、HDMI 输入、看门狗、音频、存储、背光、文件和局域网控制台等独立工具。程序、安装路径、服务和环境变量仍使用内部兼容标识 `boardlab`，以保持现有 Buildroot、Ubuntu/Debian 和升级脚本稳定。启动时优先以根设备树的精确 `compatible` 识别板型；没有已验证外设拓扑的板型会进入保守 profile，不会继承标准板的 PWM、LED 或 YOLO 配置。

RK3576 标准版与 LGA 版使用独立且优先级最高的板级身份和能力 profile：标准版为 `topeet,itop-rk3576-standard` / `topeet-rk3576`，LGA 版为 `topeet,itop-rk3576-lga` / `topeet-rk3576-lga`。旧的 `topeet,itop-rk3576` 兼容项仍映射到标准版；LGA 精确项必须排在它之前，且 LGA profile 不会继承标准版独有的 CAN、DHT11 温湿度或 PWM 蜂鸣器功能。

## 运行环境区分

同一份应用源码支持两条独立的运行路径，不能混用启动脚本、图形后端或 GPU 基准程序：

- **RK3588S Buildroot 产品镜像**：由 Buildroot package 和 BusyBox `S49weston` / `S60boardlab` 启动；固定为 `QT_QPA_PLATFORM=wayland`、`XDG_RUNTIME_DIR=/run`、`WAYLAND_DISPLAY=wayland-0`。GPU 基准固定选择 `glmark2-es2-wayland`，预览嵌入迅为开发板测试平台的 Qt Quick 页面，不创建独立 Weston 窗口。
- **Ubuntu/Debian 桌面镜像**：固定使用 X11/xcb，并使用本仓库的 `autorun.sh`、桌面启动器和 systemd/PolicyKit 配置。GPU 基准只使用 `glmark2-es2` / `glmark2-es`；Weston/Wayland 与 DRM kiosk 只属于 Buildroot 产品镜像及其救援模式。

摄像头方向修正同样使用一份跨发行版实现：程序从 Linux V4L2 sysfs
识别当前 MIPI 传感器，仅 OV5695 在 Qt 内嵌预览和 Web JPEG 预览中插入
`videoflip method=counterclockwise`（即 `90l`），OV13850、IMX415、USB
摄像头和 HDMI 输入保持原方向。
这里使用标准 GStreamer `videoflip`，不依赖某个 Rockchip 显示 sink 专有的
`rotate-method` 属性，因此适用于项目支持的 Buildroot、Ubuntu 和 Debian
镜像；`autorun.sh` 会安装所需的 GStreamer 开发库和 plugins-good 运行插件。

Buildroot 不能运行 `autorun.sh`；Ubuntu/Debian 也不能套用 Buildroot 的 SysV/Weston kiosk 配置。RK3588S Buildroot 开发重编命令为：

```bash
cd ~/rk3588s-project/rk3588s-linux-6.1/buildroot
O=output/topeet_rk3588s/rockchip_rk3588
make O=$O boardlab-dirclean boardlab
make O=$O target-finalize
```

## Ubuntu/Debian 一键安装

项目根目录的 `autorun.sh` 仅用于以下桌面系统：

- Debian 11 Bullseye
- Debian 12 Bookworm
- Ubuntu 20.04 Focal
- Ubuntu 22.04 Jammy
- Ubuntu 24.04 Noble

普通桌面用户直接运行：

```bash
./autorun.sh
```

脚本会自动提权并完成依赖安装、Release 构建、程序与配置部署、udev/PolicyKit 配置、图标缓存刷新，以及真实桌面用户的快捷方式创建。若项目迁移目录，构建步骤会检测并清理项目内的旧 CMake 缓存后重新配置。默认安装到 `/opt/boardlab`。可选的迅为开发板测试平台 systemd 服务会被安装，但默认保持禁用和停止，避免开机直接占用显示与 Web 端口。

安装流程只负责构建和部署，不会自动运行开发测试或硬件验收；需要验收时可单独执行已安装的 `boardlab-hardware-validate.sh`。

常用选项：

```bash
./autorun.sh --check                 # 只读检查系统、依赖和安装状态
./autorun.sh --no-apt                # 离线环境，跳过 apt
sudo ./autorun.sh --user topeet      # root/SSH 部署，默认只安装桌面入口，不启动常驻服务
./autorun.sh --enable-service        # 确实需要 kiosk 常驻时，显式启用并启动服务
./autorun.sh --disable-service       # 停止服务并关闭开机启动
```

安装内容包括：

- `/opt/boardlab/bin/boardlab`：Qt Quick 主程序
- `/opt/boardlab/bin/boardlab-desktop`：桌面会话兼容启动器
- `/opt/boardlab/bin/boardlab-run`：自动后端/kiosk 运行入口
- `/opt/boardlab/bin/boardlab-root`：桌面默认使用的受控 PolicyKit root 入口
- `/usr/share/applications/boardlab.desktop`：应用菜单入口
- `/usr/share/icons/hicolor/scalable/apps/boardlab.svg`：迅为开发板测试平台图标
- `~/Desktop/boardlab.desktop` 或对应 XDG Desktop：可点击桌面入口
- `/etc/udev/rules.d/99-boardlab.rules`：板端设备权限规则

桌面图标通过受控的 PolicyKit 入口以 root 启动 BoardLab，因此系统诊断可读取 root-only 的 RTC 设备和 RKNPU debugfs 节点。仅 `sudo` 组允许调用该固定启动器；它保留当前 X11/VNC 图形会话的显示授权，不使用 setuid 或开放式 root shell。普通用户可继续手动运行 `boardlab-desktop` 进行无特权调试。常见 Rockchip X11 镜像会使用软件 Qt Quick 渲染，避免不完整的 `xcb_egl` 导致点击图标后立即退出；Buildroot 的 Weston/Wayland 路径保持独立，不受影响。

蜂鸣器统一由内核 `pwm-beeper` 驱动提供 input-event 接口。BoardLab 只接受精确的 `pwm-beeper` 或 `pwm beeper` 输入设备名，并检查 `EV_SND`/`SND_TONE` 能力后选择数值最小的 `eventN`；不依赖 PWM 控制器编号或探测顺序，也不会访问 PWM sysfs。没有该外设的客户底板会自动禁用全部蜂鸣器操作，不影响其他测试。

## 手工构建与运行

```bash
./scripts/build.sh
./scripts/run.sh x11 --framed
```

已安装版本可使用：

```bash
/opt/boardlab/bin/boardlab-desktop --framed
/opt/boardlab/bin/boardlab-run --kiosk
sudo /opt/boardlab/bin/boardlab-root --framed
```

## YOLOv8 资源安装契约

BoardLab 和历史兼容命令
`/topeet_test/16_yolo/topeet_yolo` 共用同一套 YOLOv8 资源；模型、标签和示例视频
不复制到 `/topeet_test`，也不放进某个 SoC 的 overlay。所有系统的相对目录固定为：

```text
PREFIX/share/boardlab/npu/
├── models/yolov8n_rk3562.rknn
├── models/yolov8n_rk3568.rknn
├── models/yolov8n_rk3576.rknn
├── models/yolov8n_rk3588.rknn
├── videos/person-bicycle-car-detection.mp4
└── coco_80_labels.txt
```

安装前缀可以因 rootfs 而不同，但资源相对路径、文件名和 SoC 对应关系不变：

| Rootfs / 安装方式 | `PREFIX` | 实际资源根 |
| --- | --- | --- |
| 迅为 Ubuntu / Debian 安装器 | `/opt/boardlab` | `/opt/boardlab/share/boardlab/npu` |
| 普通 CMake 安装 | `/usr/local` | `/usr/local/share/boardlab/npu` |
| Buildroot package | `/usr` | `/usr/share/boardlab/npu` |

两个程序按相同优先级寻找这些安装根；需要非标准部署时，均可通过
`BOARDLAB_NPU_ASSET_ROOT` 明确指定资源根。两者都会由设备树选择对应的
`yolov8n_<soc>.rknn`，不会回退到旧 `yolov8.rknn`，也不会拿其他 SoC 的模型代替。
Buildroot 要实际运行 BoardLab 的 RKNN YOLO 页面时，产品配置还必须启用
`BR2_PACKAGE_BOARDLAB_NPU=y`；该开关负责引入 RKNPU2 和 OpenCV，不能仅靠资源
文件存在来代替。

## 硬件与 Web

`autorun.sh` 默认不启动 `boardlab.service`。客户点击桌面上的“迅为开发板测试平台”图标后，Qt 与监听 `0.0.0.0:17999` 的局域网 Web 控制台会由同一进程一起启动；在 Qt 首页进入“扫码控制”，即可使用手机或电脑扫码访问。关闭 Qt 后 Web 入口也随之关闭。首次安装的出厂默认 Token 为 `topeet`，保存到仅 root 和运行用户可读的 `/etc/boardlab/web.token`。登录页与 `/healthz` 保持公开，其余 `/api/*` 状态及控制接口都要求 Bearer Token；浏览器只在当前页面内存中保存 Token，刷新后需要重新输入。

命令行 Web 示例和可选的 Web 资源校验器使用 `curl`；它不再是安装器的必需依赖，需要时再单独安装即可。

```bash
sudo cat /etc/boardlab/web.token
curl -H "Authorization: Bearer $(sudo cat /etc/boardlab/web.token)" \
  http://127.0.0.1:17999/api/system
BOARDLAB_WEB_TOKEN='customer-token' sudo -E ./autorun.sh
```

客户可以通过 `BOARDLAB_WEB_TOKEN` 自定义 Token；长度支持 1-256 个字符，可使用字母、数字、点、下划线、波浪号和连字符。重复安装会保留现有 Token，除非显式传入 `BOARDLAB_WEB_TOKEN`。由于出厂默认值公开且当前服务使用 HTTP，Web 应只在可信局域网使用。直接运行主二进制时 Web 默认关闭，桌面启动器会显式开启 Web；如确需开机常驻，可执行 `sudo ./autorun.sh --enable-service`。板端验收可执行：

```bash
/opt/boardlab/bin/boardlab-hardware-validate.sh
```

该报告会记录设备树证据与选中的平台 profile。历史命令
`rk3588-hardware-validate.sh` 仍可用，但新部署应使用通用命令。

## 温湿度与按键检测

Qt 首页和已认证的 Web 控制台都将“温湿度检测”和“按键检测”作为独立应用入口；它们不再作为系统诊断首页的实时摘要，客户可在各自页面完成发现、开始测试、查看结果和返回。

温湿度检测只识别同一 IIO 设备中同时存在温度与相对湿度输入通道的探头。进入页面只显示已发现探头和上次结果，不会隐式读取传感器。客户点击“开始采样”或手动开启自动刷新后，才会执行只读的安全序列：读取温度，等待至少 3 秒，再读取相对湿度。自动刷新默认关闭，并且运行中的采样会拒绝新的请求；页面不会修改 IIO 配置、阈值或设备树。

按键检测只接纳设备树与运行时输入设备均能确认的 `adc-keys` 或 `gpio-keys` 板载按键。电源键、USB 输入、盖子/霍尔开关及其他非板载测试输入会被排除，不会因为某个 `eventN` 节点存在而误纳入测试。客户选择已识别设备后显式开始监听；事件记录仅在该页面显示，离开页面会停止监听。
