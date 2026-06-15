# OBS Advanced Multiview - 插件分发指南

本文档说明如何准备插件进行分发，包括构建产物位置、文件说明和部署方式。

当前推荐发布路径是 GitHub Actions release workflow：推送不带 `v` 前缀的 release tag（例如 `1.0.0`、`1.0.0-rc.1`）后，CI 自动生成 draft release 与平台 artifact。本文的手工命令仅用于本地调试、临时测试包或排查 CI 打包问题。

## 📦 构建产物位置

### 构建版本对比

| 构建类型 | DLL 大小 | PDB 大小 | 推荐用途 |
|---------|---------|---------|---------|
| **Debug** | 54 KB | 884 KB | 开发调试（包含调试符号） |
| **RelWithDebInfo** | 12 KB | 476 KB | 日常开发和分发（优化+符号） |
| **Release** | ~10 KB | — | 生产发布（完全优化，无符号） |

### 文件位置

#### Debug 版本
```
build_x64\Debug\
  ├── obs-advanced-multiview.dll    (54 KB)  ← 插件主文件
  ├── obs-advanced-multiview.pdb    (884 KB) ← 调试符号文件
  └── plugin-support.pdb            (60 KB)  ← 支持库符号（编译时使用）
```

#### RelWithDebInfo 版本（推荐分发）
```
build_x64\RelWithDebInfo\
  ├── obs-advanced-multiview.dll    (12 KB)  ← 插件主文件（优化版）
  ├── obs-advanced-multiview.pdb    (476 KB) ← 调试符号文件
  └── plugin-support.pdb            (60 KB)  ← 支持库符号（编译时使用）
```

## 📋 分发包准备

### GitHub Actions artifact（推荐公开发布）

推送 release tag 后，workflow 会生成 draft release 与平台 artifact。Windows 用户主要使用两种 zip：

- **portable/root-layout zip**：面向 OBS portable，解压到 OBS 根目录即可。
- **user-layout zip**：面向 OBS 用户插件目录或传统插件安装布局。

portable/root-layout zip 必须包含：

```text
obs-plugins/64bit/obs-advanced-multiview.dll
data/obs-plugins/obs-advanced-multiview/locale/en-US.ini
data/obs-plugins/obs-advanced-multiview/locale/zh-CN.ini
```

### 本地临时调试包（不推荐公开分发）

如果只是本机 smoke test，可以直接部署当前构建：

```powershell
.\docs\setup\deploy-plugin.ps1 RelWithDebInfo
```

如果必须手工创建临时 zip，请同时放入 DLL 和 data 文件；不要只打包 DLL：

```powershell
$version = "1.0.0-rc.1"
$distDir = "dist\OBS-Advanced-Multiview-$version-portable"

New-Item -Path "$distDir\obs-plugins\64bit" -ItemType Directory -Force | Out-Null
New-Item -Path "$distDir\data\obs-plugins\obs-advanced-multiview" -ItemType Directory -Force | Out-Null

Copy-Item "build_x64\RelWithDebInfo\obs-advanced-multiview.dll" "$distDir\obs-plugins\64bit\"
Copy-Item "data\*" "$distDir\data\obs-plugins\obs-advanced-multiview\" -Recurse -Force
Copy-Item "README.md", "README.cn.md", "LICENSE" "$distDir\"

Compress-Archive -Path "$distDir\*" -DestinationPath "OBS-Advanced-Multiview-$version-portable.zip" -Force
```

调试符号（`obs-advanced-multiview.pdb`）可以另行打包给崩溃问题排查使用；公开发布包默认不必包含 PDB。

## 🚀 部署方式

### 用户手动部署

推荐用户直接使用 release 中的 portable/root-layout zip，并解压到 OBS portable 根目录。

**OBS Portable 版本**：
```powershell
# 复制到 OBS Portable 目录
Copy-Item "obs-advanced-multiview.dll" `
          "C:\Downloads\OBS-Studio-31.1.1-Windows-x64\obs-plugins\64bit\"
Copy-Item "data\*" `
          "C:\Downloads\OBS-Studio-31.1.1-Windows-x64\data\obs-plugins\obs-advanced-multiview\" `
          -Recurse -Force
```

**OBS 已安装版本**（需管理员权限）：
```powershell
# 以管理员身份运行 PowerShell
Copy-Item "obs-advanced-multiview.dll" "C:\Program Files\obs-studio\obs-plugins\64bit\"
Copy-Item "data\*" "C:\Program Files\obs-studio\data\obs-plugins\obs-advanced-multiview\" -Recurse -Force
```

**通过 OBS Plugins 文件夹**（推荐）：
```powershell
# 复制到用户插件目录（无需管理员权限）
$pluginDir = "$env:APPDATA\obs-studio\obs-plugins\64bit"
$dataDir = "$env:APPDATA\obs-studio\data\obs-plugins\obs-advanced-multiview"
New-Item -Path $pluginDir -ItemType Directory -Force
New-Item -Path $dataDir -ItemType Directory -Force
Copy-Item "obs-advanced-multiview.dll" $pluginDir
Copy-Item "data\*" $dataDir -Recurse -Force
```

### 自动部署脚本

仓库内开发部署请使用 [deploy-plugin.ps1](deploy-plugin.ps1)。公开发布包暂不维护单独的交互式 `install.ps1`；如未来增加安装器，应确保同时安装 DLL 与 `data/obs-plugins/obs-advanced-multiview` 数据目录。

历史上使用过的 DLL-only 安装脚本已不再适合公开分发，因为它不会安装 locale 文件。

## ✅ 验证清单

分发前检查：
- [ ] 使用 RelWithDebInfo 或 Release 配置构建
- [ ] DLL 文件大小正常（约 12 KB）
- [ ] 在测试 OBS 环境中验证插件加载
- [ ] Windows portable 包包含 `obs-plugins/64bit` 与 `data/obs-plugins/obs-advanced-multiview`
- [ ] 包含 README / README.cn 安装说明
- [ ] 包含 LICENSE 许可证文件
- [ ] 版本号正确（文件名、README 等）
- [ ] GitHub Actions draft release 已创建，artifact 可下载并能解压到预期目录

## 📊 发布渠道

### GitHub Releases（推荐）

```bash
# 打标签
git tag 1.0.0-rc.1
git push origin 1.0.0-rc.1

# GitHub Actions 会创建 draft release 并上传 artifact
```

支持的 tag 格式包括 `1.0.0`、`1.0.0-rc1`、`1.0.0-rc.1`、`1.0.0-beta1`、`1.0.0-beta.1`。`buildspec.json` 可以保留完整 prerelease 版本号；CI build action 会临时把它规范化为 CMake 接受的 core semver（例如 `1.0.0`），打包与 release 命名再恢复完整版本号。

### OBS Resources/Project

将插件提交到 OBS Resources：
- 官网：https://obsproject.com/forum/resources/
- 需要注册账号并填写插件信息

## 相关文档

- [../../README.md](../../README.md) - 项目 README
- [SETUP.md](SETUP.md) - 完整配置指南
- [../ROADMAP.md](../ROADMAP.md) - 项目开发计划
