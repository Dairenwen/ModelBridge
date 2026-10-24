#include <iostream>
#include <sqlite3.h>

using namespace std;

struct StudentInfo
{
    StudentInfo(const std::string &name, const std::string &gender, int age,
                double gap)
        : name(name), gender(gender), age(age), gap(gap)
    {
    }
    std::string name;
    std::string gender;
    int age;
    double gap;
};

class StudentDB
{
public:
    StudentDB(const std::string &dbName)
    {
        // 创建数据库
        if (SQLITE_OK != sqlite3_open("student.db", &_db))
        {
            std::cout << "open student.db failed" << std::endl;
            exit(-1);
        }
        // 对数据库初始化 - 创建学生表
        if (!initDataBase())
        {
            std::cout << "init database failed" << std::endl;
            exit(-1);
        }
    }
    ~StudentDB()
    {
        if (_db)
        {
            sqlite3_close(_db);
            _db = nullptr;
        }
    }
    bool insertStudentInfo(const StudentInfo &info)
    {
        // 构建sql语句
        std::string sql = "INSERT INTO student(name, gender, age, gap)"
                          "VALUES(?, ?, ?, ?);";
        // 准备SQL语句
        sqlite3_stmt *stmt = nullptr;
        if (SQLITE_OK != sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr))
        {
            std::cout << "prepare sql failed: " << sqlite3_errmsg(_db) << std::endl;
            return false;
        }
        // 绑定参数
        sqlite3_bind_text(stmt, 1, info.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, info.gender.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, info.age);
        sqlite3_bind_double(stmt, 4, info.gap);

        // 执行SQL语句，检查是否执行成功
        if (SQLITE_DONE != sqlite3_step(stmt))
        {
            std::cout << "execute sql failed: " << sqlite3_errmsg(_db) << std::endl;
            sqlite3_finalize(stmt);
            return false;
        }

        // 执行完sql，释放stmt
        sqlite3_finalize(stmt);
        std::cout << "insert student info success" << std::endl;
        return true;
    }
    bool queryStudentInfo(const std::string &name)
    {
        // 构建sql语句
        std::string sql = "SELECT * FROM student WHERE name = ?";

        // 准备SQL语句
        sqlite3_stmt *stmt = nullptr;
        if (SQLITE_OK != sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr))
        {
            std::cout << "prepare sql failed: " << sqlite3_errmsg(_db) << std::endl;
            return false;
        }

        // 绑定参数
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);

        // 执行SQL语句，检查是否执行成功
        int result = sqlite3_step(stmt);
        if (SQLITE_ROW != result && SQLITE_DONE != result)
        {
            std::cout << "execute sql failed: " << sqlite3_errmsg(_db) << std::endl;
            sqlite3_finalize(stmt);
            return false;
        }

        // 执行完sql，释放stmt
        sqlite3_finalize(stmt);
        std::cout << "query student info success" << std::endl;
        return true;
    }

    bool queryAllStudentInfo()
    {
        // 构建sql语句
        std::string sql = "SELECT * FROM student";

        // 准备SQL语句
        sqlite3_stmt *stmt = nullptr;
        if (SQLITE_OK != sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr))
        {
            std::cout << "prepare sql failed: " << sqlite3_errmsg(_db) << std::endl;
            return false;
        }

        // 执行SQL语句，检查是否执行成功
        int result = sqlite3_step(stmt);
        if (SQLITE_ROW != result && SQLITE_DONE != result)
        {
            std::cout << "execute sql failed: " << sqlite3_errmsg(_db) << std::endl;
            sqlite3_finalize(stmt);
            return false;
        }

        std::cout << "-------------------student table info:-----------------" << std::endl;
        // 遍历结果集
        while (SQLITE_ROW == result)
        {
            // 表结构是 id, name, gender, age, gap，所以列索引从1开始获取我们需要的值
            std::string name(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)));
            std::string gender(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2)));
            int age = sqlite3_column_int(stmt, 3);
            double gap = sqlite3_column_double(stmt, 4);
            std::cout << "name: " << name << ", gender: " << gender << ", age: " << age << ", gap: " << gap << std::endl;
            result = sqlite3_step(stmt);
        }
        // 检查是否还有更多的行
        if (SQLITE_DONE != result)
        {
            std::cout << "execute sql failed: " << sqlite3_errmsg(_db) << std::endl;
            return false;
        }
        std::cout << "-------------------student table info end-----------------" << std::endl;

        // 执行完sql，释放stmt
        sqlite3_finalize(stmt);
        std::cout << "query all student info success" << std::endl;
        return true;
    }

    // 修改学生信息
    bool updateStudentInfo(const std::string &name, const StudentInfo &info)
    {
        // 构建sql语句
        std::string sql = "UPDATE student SET name = ?, gender = ?, age = ?, gap = ? WHERE name = ?";
        // 准备SQL语句
        sqlite3_stmt *stmt = nullptr;
        if (SQLITE_OK != sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr))
        {
            std::cout << "prepare sql failed: " << sqlite3_errmsg(_db) << std::endl;
            return false;
        }

        // 绑定参数
        sqlite3_bind_text(stmt, 1, info.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, info.gender.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, info.age);
        sqlite3_bind_double(stmt, 4, info.gap);
        sqlite3_bind_text(stmt, 5, name.c_str(), -1, SQLITE_TRANSIENT);
        // 执行SQL语句，检查是否执行成功
        if (SQLITE_DONE != sqlite3_step(stmt))
        {
            std::cout << "execute sql failed: " << sqlite3_errmsg(_db) << std::endl;
            sqlite3_finalize(stmt);
            return false;
        }

        // 执行完sql，释放stmt
        sqlite3_finalize(stmt);
        std::cout << "update student info success" << std::endl;
        return true;
    }

    // 删除学生信息
    bool deleteStudentInfo(const std::string &name)
    {
        // 构建sql语句
        std::string sql = "DELETE FROM student WHERE name = ?";
        // 准备SQL语句
        sqlite3_stmt *stmt = nullptr;
        if (SQLITE_OK != sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr))
        {
            std::cout << "prepare sql failed: " << sqlite3_errmsg(_db) << std::endl;
            return false;
        }
        // 绑定参数
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        // 执行SQL语句，检查是否执行成功
        if (SQLITE_DONE != sqlite3_step(stmt))
        {
            std::cout << "execute sql failed: " << sqlite3_errmsg(_db) << std::endl;
            sqlite3_finalize(stmt);
            return false;
        }

        // 执行完sql，释放stmt
        sqlite3_finalize(stmt);
        std::cout << "delete student info success" << std::endl;
        return true;
    }

private:
    bool initDataBase()
    {
        // 创建数据库表
        const std::string studentTab = "CREATE TABLE IF NOT EXISTS student( "
                                       "id INTEGER PRIMARY KEY, "
                                       "name TEXT NOT NULL, "
                                       "gender TEXT NOT NULL, "
                                       "age INTEGER NOT NULL, "
                                       "gap REAL NOT NULL)";
        char *errMsg = nullptr;
        // 执行单条语句
        if (SQLITE_OK != sqlite3_exec(_db, studentTab.c_str(), nullptr, nullptr, &errMsg))
        {
            std::cout << "create table failed: " << errMsg << std::endl;
            sqlite3_free(errMsg);
            return false;
        }
        std::cout << "create studentTab success" << std::endl;
        return true;
    }

private:
    sqlite3 *_db;
};

int main()
{
    StudentInfo info1("张三", "男", 18, 3.5);
    StudentInfo info2("李四", "男", 19, 3.8);
    StudentInfo info3("王五", "男", 20, 2.9);
    StudentInfo info4("赵六", "女", 21, 3.0);
    StudentDB db("student.db");
    db.insertStudentInfo(info1);
    db.insertStudentInfo(info2);
    db.insertStudentInfo(info3);
    db.insertStudentInfo(info4);

    db.queryAllStudentInfo();
    info1.age = 20;
    db.updateStudentInfo("张三", info1);
    db.queryAllStudentInfo();
    db.deleteStudentInfo("张三");
    db.queryAllStudentInfo();

    return 0;
}
