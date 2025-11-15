# ===============================================================
# 重新生成 GateServer 下的 .proto 文件 (Protobuf + gRPC)
# 适用于路径: D:\cppworks\FullStackProject\GateServer\GateServer
# ===============================================================

# -------------- 配置区（按需修改） --------------
$ProjectDir = "D:\cppworks\FullStackProject\GateServer\GateServer"
$VCPKG = "D:\vcpkg"
$Protoc = Join-Path $VCPKG "installed\x64-windows\tools\protobuf\protoc.exe"
$GrpcPlugin = Join-Path $VCPKG "installed\x64-windows\tools\grpc\grpc_cpp_plugin.exe"
$CMakeToolchain = Join-Path $VCPKG "scripts\buildsystems\vcpkg.cmake"
$BuildDir = Join-Path $ProjectDir "build"
# -------------- end 配置区 -----------------------

Set-Location $ProjectDir
Write-Host "当前目录: $ProjectDir"

# 检查工具
if (-Not (Test-Path $Protoc)) { throw "找不到 protoc：$Protoc" }
if (-Not (Test-Path $GrpcPlugin)) { throw "找不到 grpc_cpp_plugin.exe：$GrpcPlugin" }

# 清理旧生成文件
Write-Host "`n[1/4] 清理旧的生成文件..."
Get-ChildItem -Path $ProjectDir -Include *.pb.cc,*.pb.h,*.grpc.pb.cc,*.grpc.pb.h -Recurse | Remove-Item -Force -ErrorAction SilentlyContinue

# 找出 proto 文件
$protoFiles = Get-ChildItem -Path $ProjectDir -Filter *.proto -Recurse
if ($protoFiles.Count -eq 0) {
    Write-Host "未找到任何 .proto 文件，请检查路径。"
    exit
}

# 重新生成
Write-Host "`n[2/4] 使用 vcpkg 的 protoc 重新生成..."
foreach ($p in $protoFiles) {
    Write-Host "  -> 正在生成 $($p.Name)..."
    & "$Protoc" `
        --cpp_out="$ProjectDir" `
        --grpc_out="$ProjectDir" `
        --plugin="protoc-gen-grpc=$GrpcPlugin" `
        -I "$ProjectDir" `
        "$($p.FullName)"
    if ($LASTEXITCODE -ne 0) { throw "生成失败：$($p.Name)" }
}

# 清理旧构建缓存（可选）
Write-Host "`n[3/4] 清理旧的 CMake 构建缓存..."
Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $BuildDir | Out-Null

# 重新 CMake 配置
Write-Host "`n[4/4] 重新 CMake 配置..."
cmake -S $ProjectDir -B $BuildDir -DCMAKE_TOOLCHAIN_FILE="$CMakeToolchain" -DCMAKE_BUILD_TYPE=Release

Write-Host "`n 已完成 Protobuf & gRPC 文件重新生成！"
Write-Host "生成目录: $ProjectDir"
Write-Host "构建目录: $BuildDir"
