# 查找并配置 VS2022 CMake 到系统 PATH

Write-Host "正在查找 VS2022 安装的 CMake..." -ForegroundColor Cyan

# 方法1: 尝试常见路径
$possiblePaths = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
)

# 方法2: 搜索整个 VS2022 安装目录
Write-Host "正在搜索 Visual Studio 2022 安装目录..." -ForegroundColor Gray
$vsBasePaths = @(
    "C:\Program Files\Microsoft Visual Studio\2022",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022"
)

foreach ($vsBase in $vsBasePaths) {
    if (Test-Path $vsBase) {
        $foundPaths = Get-ChildItem -Path $vsBase -Recurse -Filter "cmake.exe" -ErrorAction SilentlyContinue | 
                      Where-Object { $_.DirectoryName -like "*CMake\bin*" } |
                      Select-Object -ExpandProperty DirectoryName -Unique
        if ($foundPaths) {
            $possiblePaths += $foundPaths
        }
    }
}

$cmakePath = $null

foreach ($path in $possiblePaths) {
    $cmakeExe = Join-Path $path "cmake.exe"
    if (Test-Path $cmakeExe) {
        $cmakePath = $path
        Write-Host "✓ 找到 CMake: $cmakeExe" -ForegroundColor Green
        
        # 测试版本
        $version = & $cmakeExe --version 2>&1 | Select-Object -First 1
        Write-Host "  版本: $version" -ForegroundColor Gray
        break
    }
}

if (-not $cmakePath) {
    Write-Host "✗ 未找到 VS2022 安装的 CMake" -ForegroundColor Red
    Write-Host "  请检查 Visual Studio 2022 是否已安装'用于 Windows 的 C++ CMake 工具'组件" -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "找到的 CMake 路径: $cmakePath" -ForegroundColor Green
Write-Host ""

# 检查当前用户 PATH
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -like "*$cmakePath*") {
    Write-Host "✓ CMake 路径已在用户 PATH 中" -ForegroundColor Green
} else {
    Write-Host "! CMake 路径不在用户 PATH 中" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "要添加到 PATH，请运行以下命令（或手动配置）：" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "# 方法 1: 添加到用户 PATH（推荐，无需管理员权限）" -ForegroundColor Green
    Write-Host "`$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')" -ForegroundColor White
    Write-Host "`$newPath = `$userPath + ';$cmakePath'" -ForegroundColor White
    Write-Host "[Environment]::SetEnvironmentVariable('Path', `$newPath, 'User')" -ForegroundColor White
    Write-Host ""
    Write-Host "# 方法 2: 或者手动配置" -ForegroundColor Green
    Write-Host "1. Win + R 输入: sysdm.cpl" -ForegroundColor White
    Write-Host "2. 高级 → 环境变量" -ForegroundColor White
    Write-Host "3. 用户变量 → Path → 编辑 → 新建" -ForegroundColor White
    Write-Host "4. 粘贴此路径: $cmakePath" -ForegroundColor White
    Write-Host ""
    
    # 提供一键添加选项
    $response = Read-Host "是否现在添加到用户 PATH？(y/n)"
    if ($response -eq 'y' -or $response -eq 'Y') {
        try {
            $newPath = $userPath + ";$cmakePath"
            [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
            Write-Host "✓ 已添加到用户 PATH" -ForegroundColor Green
            Write-Host ""
            Write-Host "⚠ 重要: 需要重启 PowerShell 和 VS Code 才能生效！" -ForegroundColor Yellow
            Write-Host ""
            Write-Host "请执行以下步骤:" -ForegroundColor Cyan
            Write-Host "1. 关闭所有 PowerShell 和 VS Code 窗口" -ForegroundColor White
            Write-Host "2. 重新打开 VS Code" -ForegroundColor White
            Write-Host "3. 在新的终端中运行: cmake --version" -ForegroundColor White
        } catch {
            Write-Host "✗ 添加失败: $_" -ForegroundColor Red
        }
    }
}

Write-Host ""
Write-Host "临时解决方案（当前会话有效）：" -ForegroundColor Cyan
Write-Host "`$env:Path += ';$cmakePath'" -ForegroundColor White
