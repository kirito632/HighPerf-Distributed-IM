@echo off
rem -----------------------------
rem 生成 Protobuf 和 gRPC C++ 代码
rem proto 文件在：D:\cppworks\FullStackProject\GateServer\GateServer
rem 输出文件到： D:\cppworks\FullStackProject\GateServer\GateServer
rem -----------------------------

setlocal

rem vcpkg 安装路径（按你的环境填写）
set "VCPKG_TOOLS=D:\vcpkg\installed\x64-windows\tools"

rem proto 文件所在目录与目标输出目录
set "PROTO_DIR=D:\cppworks\FullStackProject\GateServer\GateServer"
set "OUT_DIR=D:\cppworks\FullStackProject\GateServer\GateServer"
set "PROTO_FILE=message.proto"

rem 检查工具是否存在
if not exist "%VCPKG_TOOLS%\protobuf\protoc.exe" (
  echo ERROR: protoc.exe not found at "%VCPKG_TOOLS%\protobuf\protoc.exe"
  pause
  exit /b 1
)
if not exist "%VCPKG_TOOLS%\grpc\grpc_cpp_plugin.exe" (
  echo ERROR: grpc_cpp_plugin.exe not found at "%VCPKG_TOOLS%\grpc\grpc_cpp_plugin.exe"
  pause
  exit /b 1
)

rem 进入 proto 目录
pushd "%PROTO_DIR%" || (
  echo ERROR: 无法进入目录 "%PROTO_DIR%"
  pause
  exit /b 1
)

echo Generating protobuf C++ files...
"%VCPKG_TOOLS%\protobuf\protoc.exe" -I=. --cpp_out="%OUT_DIR%" "%PROTO_FILE%"
if errorlevel 1 (
  echo protoc --cpp_out failed.
  popd
  pause
  exit /b 1
)

echo Generating gRPC C++ files...
"%VCPKG_TOOLS%\protobuf\protoc.exe" -I=. --grpc_out="%OUT_DIR%" --plugin=protoc-gen-grpc="%VCPKG_TOOLS%\grpc\grpc_cpp_plugin.exe" "%PROTO_FILE%"
if errorlevel 1 (
  echo protoc --grpc_out failed.
  popd
  pause
  exit /b 1
)

echo.
echo Generation finished successfully. Generated files are in "%OUT_DIR%"
dir /b "%OUT_DIR%\message.pb*" "%OUT_DIR%\message.grpc.pb*" 2>nul

pause
popd
endlocal
