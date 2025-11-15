#pragma once
#include"const.h"

// SectionInfo结构体：表示配置文件中的一个section及其键值对
// 
// 作用：
//   封装配置文件中一个section下的所有key-value对
struct SectionInfo {
    SectionInfo() {}
    ~SectionInfo() { _section_datas.clear(); }

    // 拷贝构造函数
    SectionInfo(const SectionInfo& src) {
        _section_datas = src._section_datas;
    }

    // 赋值运算符
    SectionInfo& operator=(const SectionInfo& src) {
        if (&src == this) {
            return *this;
        }

        this->_section_datas = src._section_datas;
        return *this;
    }

    // section下的键值对映射表
    std::map<std::string, std::string> _section_datas;

    // 重载[]操作符：通过key获取value
    // 参数：
    //   - key: 配置项名称
    // 返回值：
    //   配置项的值，如果不存在则返回空字符串
    std::string operator[](const std::string& key) {
        if (_section_datas.find(key) == _section_datas.end()) {
            return "";
        }
        return _section_datas[key];
    }
};

// 配置管理器类：管理程序配置信息
// 
// 作用：
//   1. 读取和解析config.ini配置文件
//   2. 提供统一配置访问接口
//   3. 使用单例模式确保全局唯一配置
// 
// 使用方式：
//   ConfigMgr::Inst()[section][key]
class ConfigMgr
{
public:
    // 析构函数：清理配置映射表
    ~ConfigMgr() {
        _config_map.clear();
    }

    // 重载[]操作符：通过section名称获取SectionInfo
    // 参数：
    //   - section: 配置节名称
    // 返回值：
    //   SectionInfo对象，包含该section下的所有键值对
    SectionInfo operator[](const std::string section) {
        if (_config_map.find(section) == _config_map.end()) {
            return SectionInfo();
        }

        return _config_map[section];
    }

    // 获取配置管理器单例实例
    // 
    // 返回值：
    //   ConfigMgr单例引用
    // 
    // 使用示例：
    //   auto& cfg = ConfigMgr::Inst();
    //   std::string host = cfg["Mysql"]["Host"];
    static ConfigMgr& Inst() {
        static ConfigMgr cfg_mgr;
        return cfg_mgr;
    }

    // 拷贝构造函数
    ConfigMgr(const ConfigMgr& src) {
        _config_map = src._config_map;
    }

    // 赋值运算符
    ConfigMgr& operator=(const ConfigMgr& src) {
        if (&src == this) {
            return *this;
        }

        _config_map = src._config_map;
    }

private:
    // 私有构造函数：单例模式
    ConfigMgr();

    // 配置映射表：以section名称为key，SectionInfo为value
    std::map<std::string, SectionInfo> _config_map;
};
