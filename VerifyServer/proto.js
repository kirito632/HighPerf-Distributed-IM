// VerifyServer protobuf加载模块
// 
// 作用：
//   加载和解析message.proto文件，生成gRPC服务定义
// 
// 依赖：
//   - grpc: gRPC库
//   - proto-loader: protobuf加载器

const path = require('path')
const grpc = require('@grpc/grpc-js')
const protoLoader = require('@grpc/proto-loader')

// 获取proto文件的路径
const PROTO_PATH = path.join(__dirname, 'message.proto')

// 加载proto文件
// 
// 参数说明：
//   - keepCase: 保持字段名称的大小写
//   - longs: 将long类型转换为String
//   - enums: 将enum转换为String
//   - defaults: 使用默认值
//   - oneofs: 支持oneof字段
const packageDefinition = protoLoader.loadSync(PROTO_PATH,
    { keepCase: true, longs: String, enum: String, default: true, oneofs: true })

// 将packageDefinition加载为grpc对象
const protoDescriptor = grpc.loadPackageDefinition(packageDefinition)

// 提取message模块
const message_proto = protoDescriptor.message

// 导出message_proto供server.js使用
module.exports = message_proto
