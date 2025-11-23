#include <iostream>
#include <memory>
#include <mysql_connection.h>
#include <mysql_driver.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <cppconn/exception.h>

int main() {
    try {
        std::string url = "tcp://127.0.0.1:3306"; // �ĳ���� host:port
        std::string user = "chatuser";
        std::string pass = "123456";
        std::string schema = "chat_system";

        std::cout << "Test: get driver..." << std::endl;
        sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
        if (!driver) {
            std::cerr << "driver null" << std::endl;
            return 1;
        }
        std::cout << "Test: connect..." << std::endl;
        std::unique_ptr<sql::Connection> con(driver->connect(url, user, pass));
        std::cout << "Test: setSchema..." << std::endl;
        con->setSchema(schema);
        std::cout << "Connected OK" << std::endl;
    }
    catch (sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what() << " code=" << e.getErrorCode() << " state=" << e.getSQLState() << std::endl;
        return 1;
    }
    catch (std::exception& e) {
        std::cerr << "std::exception: " << e.what() << std::endl;
        return 2;
    }
    catch (...) {
        std::cerr << "unknown exception" << std::endl;
        return 3;
    }
    return 0;
}
