@echo off
rem -----------------------------
rem 生成 Protobuf 和 gRPC C++ 代码
rem 放到：D:\cppworks\FullStackProject\ChatServer\ChatServer
rem -----------------------------

setlocal

rem vcpkg 安装路径（按你的环境填写）
set "VCPKG_TOOLS=D:\vcpkg\installed\x64-windows\tools"

rem proto 文件目录与文件名
set "PROTO_DIR=D:\cppworks\FullStackProject\ChatServer\ChatServer"
set "PROTO_FILE=message.proto"

rem 可选：如果想把生成文件放到子目录，取消下面两行注释并修改 OUT_DIR
rem set "OUT_DIR=%PROTO_DIR%\generated"
rem if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

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
pushd "%PROTO_DIR%"

echo Generating protobuf C++ files...
"%VCPKG_TOOLS%\protobuf\protoc.exe" -I=. --cpp_out=. "%PROTO_FILE%"
if errorlevel 1 (
  echo protoc --cpp_out failed.
  popd
  pause
  exit /b 1
)

echo Generating gRPC C++ files...
"%VCPKG_TOOLS%\protobuf\protoc.exe" -I=. --grpc_out=. --plugin=protoc-gen-grpc="%VCPKG_TOOLS%\grpc\grpc_cpp_plugin.exe" "%PROTO_FILE%"
if errorlevel 1 (
  echo protoc --grpc_out failed.
  popd
  pause
  exit /b 1
)

echo.
echo Generation finished successfully. Generated files are in "%PROTO_DIR%"
rem 列出生成的文件（可选）
dir /b *.pb.h *.pb.cc *.grpc.pb.h *.grpc.pb.cc 2>nul

pause
popd
endlocal
