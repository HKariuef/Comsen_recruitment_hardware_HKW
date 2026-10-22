# 在 Windows 下直接编译：把工程路径转成 WSL 路径后调用 build.sh
param(
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$wslDir = (wsl.exe wslpath -a "$PSScriptRoot").Trim()
if ([string]::IsNullOrWhiteSpace($wslDir)) {
    throw "wslpath 转换失败，请确认已安装 WSL。"
}

wsl.exe -e bash -lc "cd `"$wslDir`" && chmod +x build.sh && ./build.sh $Config"
if ($LASTEXITCODE -ne 0) {
    throw "编译失败，退出码 $LASTEXITCODE"
}

Write-Host ""
Write-Host "固件：$PSScriptRoot\build\$Config\HCalculate.hex"
