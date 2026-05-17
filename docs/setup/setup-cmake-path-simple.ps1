# 配置 CMake PATH 环境变量 - 简化版
# 请在 VS2022 Developer PowerShell 中运行此脚本

Write-Host "=== CMake PATH 配置助手 ===" -ForegroundColor Cyan
Write-Host ""

# 检查当前终端是否能找到 cmake
$cmakeCmd = Get-Command cmake -ErrorAction SilentlyContinue

if ($cmakeCmd) {
    $cmakePath = Split-Path -Parent $cmakeCmd.Source
    Write-Host "✓ 当前终端中找到 CMake:" -ForegroundColor Green
    Write-Host "  路径: $($cmakeCmd.Source)" -ForegroundColor White
    Write-Host "  目录: $cmakePath" -ForegroundColor White
    
    # 检查版本
    $version = cmake --version 2>&1 | Select-Object -First 1
    Write-Host "  版本: $version" -ForegroundColor Gray
    Write-Host ""
    
    # 检查是否已在用户 PATH 中
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    
    if ($userPath -like "*$cmakePath*") {
        Write-Host "✓ 此 CMake 路径已在用户 PATH 中" -ForegroundColor Green
        Write-Host ""
        Write-Host "如果其他 PowerShell 窗口仍无法使用 cmake 命令，请：" -ForegroundColor Yellow
        Write-Host "1. 关闭所有 PowerShell 和 VS Code 窗口" -ForegroundColor White
        Write-Host "2. 重新打开即可" -ForegroundColor White
    } else {
        Write-Host "! CMake 路径不在用户 PATH 中" -ForegroundColor Yellow
        Write-Host ""
        
        $response = Read-Host "是否添加到用户 PATH？这样所有 PowerShell 都能使用 cmake (y/n)"
        
        if ($response -eq 'y' -or $response -eq 'Y') {
            try {
                # 添加到用户 PATH
                $newPath = if ($userPath) { "$userPath;$cmakePath" } else { $cmakePath }
                [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
                
                Write-Host ""
                Write-Host "✓ 成功添加 CMake 到用户 PATH！" -ForegroundColor Green
                Write-Host ""
                Write-Host "⚠ 重要提示:" -ForegroundColor Yellow
                Write-Host "1. 关闭所有 PowerShell 窗口和 VS Code" -ForegroundColor White
                Write-Host "2. 重新打开 VS Code" -ForegroundColor White
                Write-Host "3. 在新终端中运行: cmake --version" -ForegroundColor White
                Write-Host "4. 如果显示版本号，说明配置成功！" -ForegroundColor White
                
            } catch {
                Write-Host ""
                Write-Host "✗ 自动添加失败: $_" -ForegroundColor Red
                Write-Host ""
                Write-Host "请手动添加:" -ForegroundColor Yellow
                Write-Host "1. Win + R → 输入: sysdm.cpl" -ForegroundColor White
                Write-Host "2. 高级 → 环境变量" -ForegroundColor White
                Write-Host "3. 用户变量 → Path → 编辑 → 新建" -ForegroundColor White
                Write-Host "4. 粘贴: $cmakePath" -ForegroundColor White
            }
        } else {
            Write-Host ""
            Write-Host "未添加到 PATH。如需手动添加:" -ForegroundColor Yellow
            Write-Host "复制此路径: $cmakePath" -ForegroundColor White
        }
    }
    
} else {
    Write-Host "✗ 当前终端中未找到 cmake 命令" -ForegroundColor Red
    Write-Host ""
    Write-Host "请按以下步骤操作:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "1. 打开 'Developer PowerShell for VS 2022'" -ForegroundColor Cyan
    Write-Host "   (从开始菜单 → Visual Studio 2022 文件夹中找到)" -ForegroundColor Gray
    Write-Host ""
    Write-Host "2. 在 Developer PowerShell 中，切换到项目目录:" -ForegroundColor Cyan
    Write-Host "   cd '$PSScriptRoot'" -ForegroundColor White
    Write-Host ""
    Write-Host "3. 运行此脚本:" -ForegroundColor Cyan
    Write-Host "   .\setup-cmake-path-simple.ps1" -ForegroundColor White
    Write-Host ""
    Write-Host "4. 脚本会自动检测并添加 CMake 到 PATH" -ForegroundColor Cyan
}

Write-Host ""
Write-Host "=== 配置完成 ===" -ForegroundColor Cyan
