#include "ConfigMgr.h"

// 构造函数：初始化配置管理器
// 
// 作用：
//   读取并解析config.ini配置文件，将配置信息加载到内存中的映射表中
// 
// 实现逻辑：
//   1. 获取当前可执行文件所在路径
//   2. 构建config.ini文件的完整路径
//   3. 使用property_tree读取INI文件
//   4. 遍历所有section和key-value对
//   5. 将配置数据存储到_config_map映射表中
//   6. 打印所有配置信息（用于调试）
// 
// config.ini文件格式示例：
//   [Section1]
//   key1=value1
//   key2=value2
//   [Section2]
//   key3=value3
ConfigMgr::ConfigMgr()
{
	// 获取当前可执行文件所在的目录路径
	boost::filesystem::path current_path = boost::filesystem::current_path();
	// 构建config.ini文件的完整路径
	boost::filesystem::path config_path = current_path / "config_gate.ini";
	std::cout << "Config path: " << config_path << std::endl;

	// 使用property_tree读取INI配置文件
	boost::property_tree::ptree pt;
	boost::property_tree::read_ini(config_path.string(), pt);

	// 遍历所有section配置节
	for (const auto& section_pair : pt) {
		const std::string& section_name = section_pair.first;  // section名称
		const boost::property_tree::ptree& section_tree = section_pair.second;  // section内容

		// 存储该section下的所有key-value对
		std::map<std::string, std::string> section_config;
		// 遍历该section下的所有key-value对
		for (const auto& key_value_pair : section_tree) {
			const std::string& key = key_value_pair.first;  // 键名
			const std::string& value = key_value_pair.second.get_value<std::string>();  // 键值
			section_config[key] = value;  // 存储到map中
		}

		// 创建SectionInfo对象并存储配置数据
		SectionInfo sectionInfo;
		sectionInfo._section_datas = section_config;
		// 将section配置数据存储到全局映射表
		_config_map[section_name] = sectionInfo;

		// 打印所有配置信息（用于调试）
		for (const auto& section_entry : _config_map) {
			const std::string& section_name = section_entry.first;
			SectionInfo section_config = section_entry.second;
			std::cout << "[" << section_name << "]" << std::endl;
			for (const auto& key_value_pair : section_config._section_datas) {
				std::cout << key_value_pair.first << "=" << key_value_pair.second << std::endl;
			}
		}
	}
}