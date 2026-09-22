# Codex Remote Bootstrap

Qt 6.8 + QML 的 Windows 辅助工具，用于给 Jetson、RK3588、x86_64 Linux 开发机快速准备 Codex SSH 远程环境。

当前版本：**v0.4.0**

- 应用自身版本由 `CMakeLists.txt` 的 `project(... VERSION ...)` 管理
- Codex CLI 包版本来自 OpenAI 官方 GitHub Release，由程序在线下载和缓存
- 这两套版本互不混用

## v0.4.0

- GitHub Actions 自动编译 Windows x64 绿色版
- `windeployqt` 收集 Qt 依赖，生成可直接运行的 ZIP
- `git tag v0.4.0` 后自动创建 GitHub Release
- 程序版本由 CMake 注入 UI（侧栏 / 窗口标题）
- Release Tag 必须与 CMake `VERSION` 一致，否则发布失败

## 两套版本

```text
Codex Remote Bootstrap   v0.4.0     ← 本仓库 GitHub Release
Codex CLI                0.xxx.x    ← openai/codex GitHub Release
```

绿色版 ZIP **不包含** Codex CLI 安装包：

```text
codex-aarch64-unknown-linux-musl.tar.gz
codex-x86_64-unknown-linux-musl.tar.gz
```

程序启动后自行管理：

```text
%LOCALAPPDATA%\CodexRemoteBootstrap\
├── packages\
├── cache\
└── logs\
```

## 下载

内部测试：GitHub → Actions → Build Windows x64 → Artifacts

```text
CodexRemoteBootstrap-Windows-x64.zip
```

正式发布：GitHub → Releases

```text
CodexRemoteBootstrap-v0.4.0-Windows-x64.zip
SHA256SUMS.txt
```

解压后直接运行 `CodexRemoteBootstrap.exe`，不需要安装 Qt。

校验：

```powershell
Get-FileHash .\CodexRemoteBootstrap-v0.4.0-Windows-x64.zip -Algorithm SHA256
```

## 发布

1. 把 `CMakeLists.txt` 里的版本改成目标版本，例如 `0.4.0`
2. 合并到 `main`
3. 打 tag 并推送：

```powershell
git tag v0.4.0
git push origin v0.4.0
```

GitHub Actions 会编译、打包，并创建 Release。若 tag `v0.4.0` 与 CMake `VERSION 0.4.0` 不一致，发布作业会失败。

版本规则使用 Semantic Versioning：`MAJOR.MINOR.PATCH`。

## SSH 设备管理

首页会枚举本机 `C:\Users\<user>\.ssh\config` 中的显式 `Host`，并调用：

```powershell
ssh -G <alias>
```

解析最终生效的 `HostName / User / Port / IdentityFile`。因此 `Host *` 默认项、`ProxyJump`、`Include` 等 OpenSSH 规则仍由 OpenSSH 自己解析。

选择已有 Host 时工具默认**不改写**该 Host；已有免密登录可直接留空密码。也可以选择“手动输入 / 新增设备”，由工具创建一个 Codex 托管 Host。

## Codex 安装包在线下载与本地管理

工具通过 OpenAI 官方仓库：

```text
https://github.com/openai/codex/releases
```

获取版本列表，并只展示包含 Linux Codex CLI 资产的 Release。

支持两种 Linux 架构：

```text
ARM64 / aarch64:  codex-aarch64-unknown-linux-musl.tar.gz
x86_64 / amd64:   codex-x86_64-unknown-linux-musl.tar.gz
```

界面支持刷新官方 Release、显示 Stable / Preview、按架构下载、进度显示、缓存管理、SHA256 校验。

配置远端时可选择 `Latest stable`、`Latest published`（含 Preview）或指定 Release Tag。若本地没有对应架构包且开启“缺失时自动下载”，Windows 会先下载再 SCP 上传，因此远端设备本身可以没有互联网。

## 远端配置流程

1. 从 `~/.ssh/config` 选择已有设备，或手动新增。
2. 优先测试现有免密 SSH。
3. 若尚未免密，则使用一次 SSH 密码安装公钥。
4. 自动检测 `uname -s` / `uname -m`。
5. 根据 `aarch64 / x86_64` 选择对应 Codex Linux MUSL 包。
6. 本地没有包时可自动在线下载。
7. SCP 上传到远端。
8. 优先安装到 `/usr/local/bin/codex`，没有 sudo 时回退 `~/.local/bin/codex`。
9. 执行 `codex --version` 最终验证。

手动新增设备默认使用 `~/.ssh/codex_remote_ed25519`，不存在时自动创建，不覆盖现有 `id_rsa`。从 `~/.ssh/config` 选择已有 Host 时，优先沿用 `ssh -G` 解析出的 IdentityFile。

## 无互联网开发板

对于手动新增的托管 Host，可以开启 Windows 本机代理反向转发，例如：

```ssh
RemoteForward 127.0.0.1:17890 127.0.0.1:7890
```

并在远端 `~/.profile` 配置：

```bash
export HTTP_PROXY=http://127.0.0.1:17890
export HTTPS_PROXY=http://127.0.0.1:17890
```

从已有 `~/.ssh/config` Host 进入时，工具默认不改写该 Host。需要代理时建议新建一个专门的 Codex 托管别名。

## 可选同步 Codex 登录态

可以把本机 `~/.codex/auth.json` 同步到可信远端 `~/.codex/auth.json`，并设置为 `0600`。该文件属于敏感认证凭据，功能默认关闭。

## 本地构建

Qt Creator 直接打开 `CMakeLists.txt`，选择 Qt 6.8+ Kit。

依赖组件：`Qt6::Quick`、`Qt6::QuickControls2`、`Qt6::Concurrent`、`Qt6::Network`。

命令行示例：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
./scripts/package-windows.ps1 -BuildDir ./build -SourceDir . -Config Release
```

CI 固定使用 **windows-2022 + Qt 6.8.3 + MSVC 2022 x64**（`windows-latest` 现已是 VS 2026）。本机可以使用更高的 6.8/6.9 Kit，但发布包以 CI 为准。

## Windows 前置条件

需要 Windows OpenSSH Client，并确保以下命令在 PATH：

```powershell
ssh -V
scp -V
ssh-keygen -?
```

## 安全设计

- SSH 密码仅存在于当前进程内存，不保存到文件。
- `SSH_ASKPASS` 使用当前 exe 自身作为 helper，不生成明文密码脚本。
- 不修改 Codex Desktop 的 `.codex-global-state.json`。
- 新增 SSH Host 使用 `StrictHostKeyChecking accept-new`。
- `~/.ssh/config` 仅对工具自己创建的管理区块做更新，并在写入前生成 `config.crb.bak`。
- 在线包只从 OpenAI 官方 `openai/codex` GitHub Release 下载。

## 后续计划

- Inno Setup 生成 `Setup.exe`
- 程序检查自身 GitHub Release 并提示更新
- Linux / ARM64 Windows 构建
- Code Signing
