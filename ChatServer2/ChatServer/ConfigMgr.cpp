#include "ConfigMgr.h"

std::string ConfigMgr::GetValue(const std::string& section, const std::string& key)
{
	// 判断 section 是否存在
	if (_config_map.find(section) == _config_map.end()) {
		std::cerr << "[ConfigMgr] Section not found: " << section << std::endl;
		return "";
	}

	// 使用 SectionInfo 的 operator[] 获取 key 的值
	return _config_map[section][key];
}

ConfigMgr::ConfigMgr()
{
	boost::filesystem::path current_path = boost::filesystem::current_path();
	boost::filesystem::path config_path = current_path / "config_chat1.ini";
	std::cout << "Config path: " << config_path << std::endl;

	// 使用property_tree读取INI配置文件
	boost::property_tree::ptree pt;
	boost::property_tree::read_ini(config_path.string(), pt);

	// 遍历所有section配置节
	for (const auto& section_pair : pt) {
		const std::string& section_name = section_pair.first;
		const boost::property_tree::ptree& section_tree = section_pair.second;

		// 存储该section下的所有key-value对
		std::map<std::string, std::string> section_config;
		// 遍历该section下的所有key-value对
		for (const auto& key_value_pair : section_tree) {
			const std::string& key = key_value_pair.first;
			const std::string& value = key_value_pair.second.get_value<std::string>();
			section_config[key] = value;
		}

		SectionInfo sectionInfo;
		sectionInfo._section_datas = section_config;
		_config_map[section_name] = sectionInfo;
	}
	
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
