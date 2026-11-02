# 迅为开发板测试平台

BoardLab 的目标是一套源码适配不同 Rockchip SoC、内核和 Linux SDK，而不是把某个 RK3568 4.19 工程复制到其他 SDK。当前静态兼容矩阵覆盖 `/home/topeet/Linux` 下 17 套已集成 BoardLab 的 SDK：RK3562、RK3568、RK3576、RK3588、RK3588S，包含 2 套 4.19、6 套 5.10 和 9 套 6.1。程序、安装路径、服务和环境变量继续使用内部兼容标识 `boardlab`。

产品显示策略只有两条：所有 Buildroot SDK 不论版本都固定使用 Weston/Wayland；Ubuntu 和 Debian 不论版本都固定使用 X11/xcb。Ubuntu/Debian 的共享源码镜像固定放在 `/home/topeet/rootfs_build/ubuntu_debian/projects/boardlab`，不会混入 `/home/topeet/Linux` 下的 Buildroot SysV/Weston 文件。

“源码可构建”不等于“任意底板可直接操作硬件”。启动时先读取根设备树 `compatible` 和 `model`：已验证产品使用内置 profile；其他 Rockchip SoC 自动得到 `generic-<soc>` 身份；未知板只开放文件、Web、监控和诊断。GPIO、PWM、LED、存储、网络、看门狗和 NPU 等动作必须由匹配设备树的 profile 明确授权。

## RK3568 4.19 参考项目

参考目录 `/home/topeet/Linux/rk3568/rk356x_linux/app/boardlab` 与本仓库修改前的 `HEAD` 核心源码一致。tracked 文件只有 `S49weston-boardlab` 和 `boardlab-supervisor` 不同：参考项目增加了 Weston 进程/PID fallback、陈旧 Wayland socket 清理、启动失败日志和 `QT_QPA_PLATFORM_PLUGIN_PATH`。这些行为已经合并到 canonical Buildroot 模板；没有整树覆盖参考项目，也没有把 4.19 的固定路径写入业务代码。

## 通用性分层

- **构建层**：最低 CMake 3.8；只使用 Qt 5，并以各 Rockchip Linux SDK 共有的 Qt 5.12+ 为统一基线，不探测或引入 Qt 6。GStreamer 和 RKNN 分开探测，主机或无 NPU SDK 可以显式关闭。
- **SDK 集成层**：`packaging/buildroot/package/rockchip/boardlab` 是 canonical package。4.19 的 GStreamer 使用拆分 `videoconvert`/`videoscale`，较新 SDK 使用 `videoconvertscale`；V4L2、appsink、JPEG 和 multifile 插件均由 package 声明。
- **产品显示层**：Buildroot package 无条件依赖 Qt5Wayland 和 Weston，并由 SysV 启动 Weston 后再启动 BoardLab；Ubuntu/Debian 的 service、桌面入口和安装器固定传入 `QT_QPA_PLATFORM=xcb`。`scripts/run.sh auto` 保留给源码调试、诊断和非产品环境，不改变这两条产品策略。
- **板级策略层**：标准 Linux 接口通过 sysfs、`/proc`、`lsblk --json`、GPIO character device 和 evdev 动态发现；发现结果只表示端点存在，不会绕过 profile 授权。
- **NPU 层**：`AUTO|SYSTEM|BUNDLED|OFF` provider 隔离 SDK ABI。Buildroot 默认使用本 SDK 的 RKNPU2；模型、worker 数和 1 到 16 个遥测核心由 profile 描述。

## SDK 扫描与同步

兼容扫描全程只读，并把源码能力与 SDK 内旧副本分开报告：

```bash
scripts/scan-sdk-compatibility.py
scripts/scan-sdk-compatibility.py --json | jq '.summary'
```

当前扫描结果为 10 个 SDK `supported`、7 个 `degraded`、0 个
`unsupported`。17 个 SDK 的主板配置都满足 canonical Qt 5/Weston
契约；7 个 RK3588/RK3588S 系 SDK 还各自带有 IPC 与 multi-IPC 配置，
这些配置继承主配置后又关闭 Weston，因此共 14 个配置被标为
`ineligible`，不会错误地与主板配置合并成一个虚构结果。

配置证据中，3 个现存 `.config` 是 `resolved-supported`；另外 14 个主板
defconfig 只能标为 `declared-compatible`，仍需在对应 SDK 执行
`olddefconfig`/实际构建后才能成为 resolved 证据。`deployment_drift`
由扫描器实时比较当前源码和 SDK 副本，修改源码后需重新同步并检查。
这些是 CMake、Qt 5、
架构/libc、显示/init、GStreamer、RKNN/OpenCV 和 package 契约的静态结论，
不等于 17 套 SDK 都已经完成交叉编译、烧录和实机硬件验收。

同步工具默认只预览，不写外部 SDK：

```bash
./scripts/sync-buildroot-boardlab-runtime.sh --dry-run
./scripts/sync-sdk-boardlab.sh --dry-run
./scripts/sync-sdk-boardlab.sh --check
./install.sh --dry-run
```

确认目标后才使用 `--apply`。同步器动态发现带 BoardLab package 的 SDK，不依赖目录名或固定两层深度；`Config.in`/`.mk` 以 `0644` 安装，启动脚本以 `0755` 安装。
`sync-sdk-boardlab.sh` 和根目录 `install.sh` 还会单独维护
`/home/topeet/rootfs_build/ubuntu_debian/projects/boardlab`，该副本只用于
Ubuntu/Debian X11 rootfs 工程。

只更新一个部署目标时使用 `--rootfs-only` 或 `--sdk-only`，例如：

```bash
./scripts/sync-sdk-boardlab.sh --rootfs-only --apply
./scripts/sync-sdk-boardlab.sh --sdk-only --check
```

同步前会核对内容与文件权限，已经一致的副本不会替换。更新时先完成临时
副本验证，再替换目录；旧源码保留在目标旁的 `.boardlab.previous.*`，旧
Buildroot 运行文件保留在 SDK 根目录的 `.boardlab-runtime.previous.*`。
这些保留目录不参与下次同步，也不会进入应用源码包。

## 桌面操作与维护

Buildroot 通过 Weston 全屏启动；Ubuntu/Debian 桌面图标默认以普通窗口
打开，窗口初始大小适应当前屏幕工作区。需要常驻全屏时仍可使用 `--kiosk`
或显式启用已有的 systemd 服务。

首页支持分类、工具名搜索和清除筛选。只有一个搜索结果时可按回车打开；
Tab 焦点移动到屏幕外工具时会自动滚动。应用内使用 `Alt+Left` 返回目录。
首页的“已启用”表示板型配置允许该入口，硬件是否就绪仍以工具页探测结果
为准。小屏调整布局与行高，保留固定字号。

窗口关闭、退出按钮和 SIGTERM/SIGINT 进入统一的 Qt 清理流程。Buildroot
停止服务时先停止 supervisor 重启循环，再等待 Qt 退出，超时后仅清理
PID 文件对应的进程。可通过 `/etc/init.d/S60boardlab status` 查询状态；
服务启动失败会返回非零状态。

手工运行二进制需要 `--web` 才会开启 Web，产品启动器会传入该参数；
`--no-web` 优先于 `--web`。错误的 Web 地址、端口、窗口尺寸与截图延迟
会明确报错，不会静默替换成默认值。`--window-size 800x480` 也适用于
普通运行，不再依赖截图模式。

开发验证入口独立于产品构建和安装：

```bash
python3 scripts/qa/verify-runtime.py
python3 scripts/qa/verify-deployment.py
QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software \
  xvfb-run -a qmltestrunner -input scripts/qa/tst_home.qml
```

最后一项需要主机安装 Qt 5 的 `qml-module-qttest` 与 `xvfb`。这些检查
覆盖启动参数、Web 生命周期、首页操作和临时目录部署，不执行硬件动作，
也不能替代板端的摄像头、NPU、显示、触控及外设验收。

## 运行入口与显示后端

Buildroot 产品入口固定为 canonical package 提供的 Weston + BusyBox SysV 启动链；不为 Buildroot 选择 X11、EGLFS 或其他 compositor。Ubuntu/Debian 产品入口固定为 `boardlab-root`、桌面文件或可选的 `boardlab.service`，它们使用 PolicyKit/X11 集成并复用现有 X11 授权。`boardlab-run` 和 `scripts/run.sh auto` 仅作为手工调试入口，可根据真实 socket/device 选择可用后端，不改变产品镜像的显示后端。

摄像头方向修正同样使用一份跨发行版实现：程序从 Linux V4L2 sysfs
识别当前 MIPI 传感器，仅 OV5695 在 Qt 内嵌预览和 Web JPEG 预览中插入
`videoflip method=counterclockwise`（即 `90l`），OV13850、IMX415、USB
摄像头和 HDMI 输入保持原方向。
这里使用标准 GStreamer `videoflip`，不依赖某个 Rockchip 显示 sink 专有的
`rotate-method` 属性，因此适用于项目支持的 Buildroot、Ubuntu 和 Debian
镜像；`autorun.sh` 会安装所需的 GStreamer 开发库和 plugins-good 运行插件。

Buildroot 不能运行 `autorun.sh`；Ubuntu/Debian 也不能套用 Buildroot 的 SysV/Weston kiosk 配置。以任意已集成 SDK 的 Buildroot 输出目录为例：

```bash
cd /path/to/sdk/buildroot
O=output/<board>/<defconfig>
make O=$O boardlab-dirclean boardlab
make O=$O target-finalize
```

## Ubuntu/Debian 一键安装

项目根目录的 `autorun.sh` 只用于 Ubuntu/Debian X11 桌面。当前完成依赖与回归验证的版本为：

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

其他 Ubuntu/Debian 版本仍沿用同一 X11/xcb 路径；安装器会给出“需要复核”的提示并继续，而不会切换到 Wayland。发布前需要在对应 rootfs 中补做依赖检查和实机验收。

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
BOARDLAB_RKNN_PROVIDER=OFF BOARDLAB_ENABLE_NPU_YOLO=OFF ./scripts/build.sh
./scripts/run.sh auto --framed
```

交叉编译时由 SDK/Buildroot 传入 toolchain 和 sysroot；独立调用可使用
`BOARDLAB_CMAKE_TOOLCHAIN_FILE`、`BOARDLAB_SYSROOT` 与
`BOARDLAB_CMAKE_PREFIX_PATH`。构建缓存指纹包含这些 ABI 输入，切换 SDK
后不会复用上一套工具链的对象文件。需要 NPU 时选择：

```bash
BOARDLAB_RKNN_PROVIDER=SYSTEM BOARDLAB_REQUIRE_NPU_YOLO=ON ./scripts/build.sh
BOARDLAB_RKNN_PROVIDER=BUNDLED ./scripts/build.sh   # 仅用于明确的兼容部署
```

已安装版本可使用：

```bash
/opt/boardlab/bin/boardlab-desktop --framed
/opt/boardlab/bin/boardlab-run --kiosk
sudo /opt/boardlab/bin/boardlab-root --framed
```

## 新板 profile

新 SoC 无需修改识别代码即可进入安全的 generic profile。要开放板级操作，安装一份与设备树绑定的配置：

```bash
sudo install -m 0644 config/board-profile.example.json \
  /etc/boardlab/board-profile.json
boardlab --profile-json
```

完整格式见 `config/board-profile.schema.json`。`schemaVersion` 和非空
`match` 必填；`compatible` 与 `modelContains` 两组同时存在时按 AND
匹配，每组内部按 OR 匹配。配置可声明 capability、GPIO alias、LED
class 路径以及 NPU 模型/并发策略。`BOARDLAB_BOARD_PROFILE` 和旧的
`BOARDLAB_TARGET` 只能选择与当前设备树一致的内置 profile；仅测试 fixture
可配合 `BOARDLAB_TEST_MODE=1` 绕过该限制。

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
├── models/<profile-model>.rknn
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
`BOARDLAB_NPU_ASSET_ROOT` 明确指定资源根。内置产品 profile 使用对应的
`yolov8n_<soc>.rknn`；其他 SoC 必须在匹配的本地 profile 中给出
`modelFileName`，不会回退到旧 `yolov8.rknn`，也不会拿其他 SoC 的模型代替。
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
