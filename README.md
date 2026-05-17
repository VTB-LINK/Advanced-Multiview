# OBS Advanced Multiview

高度自定义的多画面监看工具插件，用于 OBS Studio。

## 项目文档

### 开发相关
- **[开发工作流](docs/DEVELOPMENT.md)** ⭐ — **开发完成后必读**：构建配置、提交前检查、发布流程
- **[开发环境配置](docs/setup/README.md)** — 完整的开发环境搭建指南
  - [详细配置步骤](docs/setup/SETUP.md)
  - [故障排除](docs/setup/TROUBLESHOOTING.md)
  - [插件分发](docs/setup/DISTRIBUTION.md)
  
### 项目规划
- **[项目计划](plan.md)** — 完整的项目开发计划和路线图
- **[UI 设计](docs/ui-ascii-wireframes.md)** — UI 界面设计线框图
- **[第一阶段开发任务](docs/phase-1-development-breakdown.md)** — 详细的开发任务分解

## 快速开始

### 环境要求

- Windows 10/11
- Visual Studio 2022/2026（含 C++ 桌面开发工作负载）
- CMake 3.28+（随 VS 安装）
- OBS Studio 31.1.1+（用于测试）

详细要求请查看 [开发环境配置文档](docs/setup/README.md)。

### 构建步骤

```powershell
# 1. 配置项目（首次运行）
cmake --preset windows-x64

# 2. 构建（日常开发推荐使用 RelWithDebInfo）
cmake --build build_x64 --config Debug           # 调试用
cmake --build build_x64 --config RelWithDebInfo  # 日常开发和分发

# 3. 部署到 OBS
.\docs\setup\deploy-plugin.ps1 RelWithDebInfo

# 4. 启动 OBS 测试
C:\Downloads\OBS-Studio-31.1.1-Windows-x64\bin\64bit\obs64.exe
```

**重要**：功能开发完成后，请阅读 **[开发工作流指南](docs/DEVELOPMENT.md)** 了解完整的构建、测试和提交流程。

完整的配置和构建指南请查看 [SETUP.md](docs/setup/SETUP.md)。

---

## 原始 OBS 插件模板信息

本项目基于 OBS 官方插件模板。以下是原始模板说明：

## Introduction

The plugin template is meant to be used as a starting point for OBS Studio plugin development. It includes:

* Boilerplate plugin source code
* A CMake project file
* GitHub Actions workflows and repository actions

## Supported Build Environments

| Platform  | Tool   |
|-----------|--------|
| Windows   | Visual Studio 17 2022 |
| macOS     | XCode 16.0 |
| Windows, macOS  | CMake 3.30.5 |
| Ubuntu 24.04 | CMake 3.28.3 |
| Ubuntu 24.04 | `ninja-build` |
| Ubuntu 24.04 | `pkg-config`
| Ubuntu 24.04 | `build-essential` |

## Quick Start

An absolute bare-bones [Quick Start Guide](https://github.com/obsproject/obs-plugintemplate/wiki/Quick-Start-Guide) is available in the wiki.

## Documentation

All documentation can be found in the [Plugin Template Wiki](https://github.com/obsproject/obs-plugintemplate/wiki).

Suggested reading to get up and running:

* [Getting started](https://github.com/obsproject/obs-plugintemplate/wiki/Getting-Started)
* [Build system requirements](https://github.com/obsproject/obs-plugintemplate/wiki/Build-System-Requirements)
* [Build system options](https://github.com/obsproject/obs-plugintemplate/wiki/CMake-Build-System-Options)

## GitHub Actions & CI

Default GitHub Actions workflows are available for the following repository actions:

* `push`: Run for commits or tags pushed to `master` or `main` branches.
* `pr-pull`: Run when a Pull Request has been pushed or synchronized.
* `dispatch`: Run when triggered by the workflow dispatch in GitHub's user interface.
* `build-project`: Builds the actual project and is triggered by other workflows.
* `check-format`: Checks CMake and plugin source code formatting and is triggered by other workflows.

The workflows make use of GitHub repository actions (contained in `.github/actions`) and build scripts (contained in `.github/scripts`) which are not needed for local development, but might need to be adjusted if additional/different steps are required to build the plugin.

### Retrieving build artifacts

Successful builds on GitHub Actions will produce build artifacts that can be downloaded for testing. These artifacts are commonly simple archives and will not contain package installers or installation programs.

### Building a Release

To create a release, an appropriately named tag needs to be pushed to the `main`/`master` branch using semantic versioning (e.g., `12.3.4`, `23.4.5-beta2`). A draft release will be created on the associated repository with generated installer packages or installation programs attached as release artifacts.

## Signing and Notarizing on macOS

Basic concepts of codesigning and notarization on macOS are explained in the correspodning [Wiki article](https://github.com/obsproject/obs-plugintemplate/wiki/Codesigning-On-macOS) which has a specific section for the [GitHub Actions setup](https://github.com/obsproject/obs-plugintemplate/wiki/Codesigning-On-macOS#setting-up-code-signing-for-github-actions).
