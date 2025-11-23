#include "ConfigMgr.h"

// ���캯������ʼ�����ù�����
// 
// ���ã�
//   �����ȡ�ͽ���config.ini�����ļ�����������Ϣ���ص��ڴ��е�����ӳ���
// 
// ʵ���߼���
//   1. ��ȡ��ǰ��ִ���ļ���·��
//   2. ����config.ini�ļ�������·��
//   3. ʹ��property_tree��ȡINI�ļ�
//   4. ��������section��key-value��
//   5. ���������ݴ洢��_config_mapӳ�����
//   6. ��ӡ����������Ϣ�����ڵ��ԣ�
// 
// config.ini�ļ���ʽʾ����
//   [Section1]
//   key1=value1
//   key2=value2
//   [Section2]
//   key3=value3
ConfigMgr::ConfigMgr()
{
    // ��ȡ��ǰ��ִ���ļ����ڵ�Ŀ¼·��
    boost::filesystem::path current_path = boost::filesystem::current_path();
    // ����config.ini�ļ�������·��
    boost::filesystem::path config_path = current_path / "config_gate.ini";
    std::cout << "Config path: " << config_path << std::endl;

    // ʹ��property_tree��ȡINI�����ļ�
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(config_path.string(), pt);

    // ��������section�����ýڣ�
    for (const auto& section_pair : pt) {
        const ::std::string& section_name = section_pair.first;  // section����
        const boost::property_tree::ptree& section_tree = section_pair.second;  // section����

        // �洢��section�µ�����key-value��
        std::map<std::string, std::string> section_config;
        // ������section�µ�����key-value��
        for (const auto& key_value_pair : section_tree) {
            const std::string& key = key_value_pair.first;  // ����������
            const std::string& value = key_value_pair.second.get_value<std::string>();  // ������ֵ
            section_config[key] = value;  // �洢��map��
        }

        // ����SectionInfo���󲢴洢��������
        SectionInfo sectionInfo;
        sectionInfo._section_datas = section_config;
        // ��section�����������ݴ洢������ӳ���
        _config_map[section_name] = sectionInfo;

        // ��ӡ����section��key-value�����ڵ��ԣ�
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