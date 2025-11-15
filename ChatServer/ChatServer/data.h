#pragma once
#include <string>

struct UserInfo {
	UserInfo() : name(""), pwd(""), uid(0), email(""), sex(0), nick(""), desc(""), back(""), icon("") {}

    std::string name;
    std::string pwd;
    int uid;
    std::string email;
    int sex;
    std::string nick;
    std::string desc;
    std::string back;
    std::string icon;
};

struct ApplyInfo {
    ApplyInfo(int uid, std::string name, std::string desc, std::string icon, std::string nick, int sex, int status)
        : _uid(uid), _name(name), _desc(desc), _icon(icon), _nick(nick), _sex(sex), _status(status) { }


    int _uid;
    std::string _name;
    std::string _desc;
    std::string _icon;
    std::string _nick;
    int _sex;
    int _status;
};