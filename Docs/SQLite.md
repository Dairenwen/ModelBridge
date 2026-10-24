# SQLite介绍

## 1. SQLite介绍

SQLite官网：https://sqlite.org/draft/index.html

SQLite菜鸟教程：https://www.runoob.com/sqlite/sqlite-data-types.html

SQLite是一个开源、轻量级、零配置、无服务器、自包含、事务性、支持标准SQL、跨平台的数据库管理系统。

- 开源：免费使用，源码透明
- 轻量级：整个库非常小，编译之后仅需几百KB
- 零配置：无需安装、启动、停止或配置任何数据库服务器进程
- 无服务器：SQLite没有独立的服务器进程，应用程序直接通过函数调用读写磁盘上的数据库文件
- 自包含：整个数据库就是一个独立的、跨平台的单个文件(通常是.db后缀)，文件中包含所有表、索引、数据和元信息
- 事务性：完全支持ACID(原子性、一致性、隔离性、持久性)事务，即使系统崩溃或断电也能保证数据完整性
- 支持标准SQL：它使用SQL语句来管理和查询数据，最多支持128TB
- 跨平台：数据库文件跨平台
- 简单易用：只需包含一个 `sqlite3.h` 和一个库文件即可

### 1.1 基本概念

| 名称               | 含义                                                    |
| ------------------ | ------------------------------------------------------- |
| 数据库（Database） | 保存表、索引等数据结构的集合，通常对应一个 `.db` 文件 |
| 表（Table）        | 按行和列组织数据                                        |
| 行（Row）          | 一条完整记录                                            |
| 列（Column）       | 记录中的一个字段                                        |
| SQL                | 操作数据库的语言，常用于建表、增删改查                  |

### 1.2 SQLite 数据类型

SQLite 采用**动态类型**：每个值具有自己的存储类型，列声明提供推荐的类型。

| 存储类型    | 含义                                                      | 示例               |
| ----------- | --------------------------------------------------------- | ------------------ |
| `NULL`    | 空值，表示缺失或未知的数据                                | `NULL`           |
| `INTEGER` | 有符号整数，按数值大小使用 0、1、2、3、4、6 或 8 字节存储 | `18`、`1000`   |
| `REAL`    | 8 字节浮点数（IEEE 754）                                  | `92.5`、`3.14` |
| `TEXT`    | 文本字符串，采用 UTF-8 或 UTF-16 编码                     | `'张三'`         |
| `BLOB`    | 原样保存的二进制数据                                      | `X'4142'`        |

### 1.3 类型亲和性

**类型亲和性**是列优先采用的存储类型。SQLite 根据列声明识别以下五类亲和性：

| 类型亲和性  | 常见列声明                              | 作用                                   |
| ----------- | --------------------------------------- | -------------------------------------- |
| `INTEGER` | `INT`、`INTEGER`、`BIGINT`        | 优先保存整数                           |
| `TEXT`    | `TEXT`、`VARCHAR(20)`、`CHAR(10)` | 优先保存文本                           |
| `REAL`    | `REAL`、`DOUBLE`、`FLOAT`         | 优先保存浮点数                         |
| `NUMERIC` | `NUMERIC`、`DECIMAL`、`BOOLEAN`   | 优先将数字形式的文本转换为整数或浮点数 |
| `BLOB`    | `BLOB`、省略类型声明                  | 按输入值的存储类型保存                 |

- `INTEGER PRIMARY KEY`：在普通行号表中，对应记录的 `rowid`。
- `AUTOINCREMENT`：配合 `INTEGER PRIMARY KEY`，按历史已使用行号生成新的递增编号。
- `VARCHAR(20)`：`20` 属于类型声明参数，实际文本长度由 SQLite 的全局长度限制控制。
- `STRICT`：为表开启严格类型检查（SQLite 3.37.0 起支持）。

```sql
CREATE TABLE example_types (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    score REAL
) STRICT;
```

### 1.4 特殊数据的保存方式

| 数据     | 保存方式                                | 示例                      |
| -------- | --------------------------------------- | ------------------------- |
| 布尔值   | `INTEGER`：`1` 表示真，`0` 表示假 | `TRUE`、`FALSE`       |
| 日期时间 | `TEXT`：ISO 8601 格式                 | `'2026-10-23 10:30:00'` |
| 日期时间 | `INTEGER`：Unix 时间戳（秒）          | `1761200000`            |
| 日期时间 | `REAL`：儒略日数                      | `2460000.5`             |
| 空值     | `NULL`，通过 `IS NULL` 判断         | `WHERE name IS NULL`    |

`typeof()` 返回值的实际存储类型；`CAST()` 按指定类型转换表达式。

```sql
SELECT typeof(123), typeof(3.14), typeof('hello'), typeof(NULL);
SELECT CAST('123' AS INTEGER);
SELECT datetime('now');
```

### 1.5 常用字段约束

| 约束            | 作用                       |
| --------------- | -------------------------- |
| `PRIMARY KEY` | 标识记录的主键             |
| `NOT NULL`    | 要求字段填写有效值         |
| `UNIQUE`      | 要求字段值满足唯一性       |
| `DEFAULT`     | 为省略赋值的字段提供默认值 |
| `CHECK`       | 按条件校验字段值           |
| `FOREIGN KEY` | 建立表之间的引用关系       |

```sql
CREATE TABLE IF NOT EXISTS courses (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    credits INTEGER NOT NULL DEFAULT 0 CHECK (credits >= 0)
);
```

### 1.6 事务与 ACID

事务将多条 SQL 操作组成一个整体。

- **原子性（Atomicity）**：事务内操作整体提交或整体回滚。
- **一致性（Consistency）**：事务执行前后满足数据库约束。
- **隔离性（Isolation）**：并发事务按隔离规则读取和修改数据。
- **持久性（Durability）**：事务提交后的数据持久保存。

```sql
BEGIN TRANSACTION;
INSERT INTO courses(name, credits) VALUES ('数据库基础', 3);
COMMIT;
```

`BEGIN` 开始事务，`COMMIT` 提交修改，`ROLLBACK` 撤销当前事务中的修改。

SQLite 数据类型参考：https://sqlite.org/datatype3.html

## 2. ubuntu命令行和开发库安装

```bash
sudo apt install sqlite3         # 安装 sqlite3 命令行工具
sudo apt install libsqlite3-dev  # 安装 sqlite 开发包
dpkg -L libsqlite3-dev           # 列出已安装的 libsqlite3-dev 软件包所包含的所有文件及其安装路径
```

```text
/.
/usr
/usr/include
/usr/include/sqlite3.h
/usr/include/sqlite3ext.h
/usr/lib
/usr/lib/x86_64-linux-gnu
/usr/lib/x86_64-linux-gnu/libsqlite3.a
/usr/lib/x86_64-linux-gnu/pkgconfig
/usr/lib/x86_64-linux-gnu/pkgconfig/sqlite3.pc
/usr/share
/usr/share/doc
/usr/share/doc/libsqlite3-dev
/usr/share/doc/libsqlite3-dev/copyright
/usr/lib/x86_64-linux-gnu/libsqlite3.so
/usr/share/doc/libsqlite3-dev/changelog.Debian.gz
```

编译时链接 sqlite3 库

makefile文件：

```bash
g++ main.cpp -o my_program -lsqlite3
```

CMakelists.txt：

```cmake
target_link_libraries(${SDK_NAME} sqlite3)
```

## 3. 终端使用

### 3.1 常用命令

数据库操作命令

| 命令                     | 说明                 | 示例                              |
| ------------------------ | -------------------- | --------------------------------- |
| `.open 文件名`         | 打开或创建数据库文件 | `.open student.db`              |
| `.databases`           | 显示所有连接的数据库 | `.databases`                    |
| `.attach 文件 as 别名` | 附加另一个数据库     | `.attach archive.db as archive` |
| `.backup 文件名`       | 备份当前数据库       | `.backup backup.db`             |
| `.restore 文件名`      | 从备份恢复数据库     | `.restore recovery.db`          |
| `.quit`                | 退出SQLite           | `.quit`                         |
| `.exit`                | 退出SQLite           | `.exit`                         |
| `.help`                | 显示帮助信息         | `.help`                         |

表和结构命令

| 命令                | 说明       | 示例                  |
| ------------------- | ---------- | --------------------- |
| `.tables`         | 显示所有表 | `.tables`           |
| `.schema [表名]`  | 显示表结构 | `.schema students`  |
| `.indexes [表名]` | 显示索引   | `.indexes students` |

### 3.1.1 常用 SQL 语句

| 语句             | 作用               |
| ---------------- | ------------------ |
| `CREATE TABLE` | 创建数据表         |
| `INSERT INTO`  | 添加记录           |
| `SELECT`       | 查询记录           |
| `UPDATE`       | 更新记录           |
| `DELETE FROM`  | 删除记录           |
| `WHERE`        | 筛选记录           |
| `ORDER BY`     | 排序查询结果       |
| `LIMIT`        | 限定结果条数       |
| `CREATE INDEX` | 为查询字段建立索引 |

```sql
SELECT name, score FROM students WHERE score >= 80 ORDER BY score DESC LIMIT 5;
UPDATE students SET score = 95.0 WHERE name = '张三';
DELETE FROM students WHERE name = '王五';
CREATE INDEX IF NOT EXISTS idx_students_score ON students(score);
```

### 3.2 使用演示

```text
> sqlite3                                # 打开 sqlite 命令行工具
sqlite> .help                            # 查看帮助信息
sqlite> .open student.db                 # 创建student数据库，数据库文件名称为student.db
sqlite> .databases                       # 验证数据库。检查当前连接了哪些数据库，以及数据库的详细状态信息
sqlite> .tables                          # 查看所有表
sqlite> CREATE TABLE students(           # 创建学生表
(x1...> id INTEGER PRIMARY KEY AUTOINCREMENT,
(x1...> name TEXT NOT NULL,
(x1...> age INTEGER,
(x1...> score REAL
(x1...> );
sqlite> .schema students                 # 查看表结构
sqlite> INSERT INTO students(name, age, score) VALUES  # students中插入数据
   ...> ('张三', 18, 92.0),
   ...> ('李四', 19, 93.5),
   ...> ('王五', 18, 76.5);
sqlite> SELECT * FROM students;          # 查看学生信息
1|张三|18|92.0
2|李四|19|93.5
3|王五|18|76.5
sqlite> DELETE FROM students WHERE score < 80; # 删除成绩小于80分同学信息
sqlite> SELECT * FROM students;
1|张三|18|92.0
2|李四|19|93.5
```

## 4. 常用api

数据库连接管理：

```cpp
int sqlite3_open(const char *filename, sqlite3 **ppDb);
```

功能：打开一个数据库连接。如果数据库文件不存在，则创建它

参数：

- filename - 数据库文件的路径，
- ppDb：一个指向sqlite3*的指针。函数成功后会在这个指针里填入数据库连接句柄

返回值：SQLITE_OK 表示创建成功，否则为错误码

```cpp
int sqlite3_close(sqlite3* db);
```

功能：关闭打开的数据库连接，并释放所有资源

参数：要关闭的数据库连接句柄

返回值：SQLITE_OK表示成功，如果还有未完成的预处理语句

```cpp
const char *sqlite3_errmsg(sqlite3* db);
```

功能：获取与数据库连接相关的最后一次错误操作的英文描述字符串。在操作失败后调用它来获取错误原因

参数：db数据库连接句柄

返回值：只想错误信息字符串的指针。如果还有查询没有完成，将返回SQLITE_BUSY禁止关闭的错误消息

执行SQL语句

```cpp
int sqlite3_prepare_v2(sqlite3 *db, const char *zSql, int nByte,
                       sqlite3_stmt **ppStmt, const char **pzTail);
```

功能：将一条SQL语句编译成一个预处理语句对象，为执行和绑定参数做准备

参数：

- db - 数据库连接句柄
- zSql：要编译的SQL语句文本
- nByte：SQL文本的长度(字节)，-1表示自动计算到\0结束
- ppStmt：指向sqlite3_stmt*的指针，表示输出预处理结果语句的句柄
- pzTail：指向未使用的SQL部分指针，通常置为NULL

返回值：编译成功返回SQLITE_K，否则返回错误码

```cpp
int sqlite3_bind_int64(sqlite3_stmt*, int index, sqlite3_int64 value);
```

功能：向预处理语句中的指定参数位置绑定一个64位的整数值

参数：

- index - 参数的序号，从1开始
- value - 要绑定的整数值

返回值：SQLITE_OK表示绑定成功，否则为错误码

```cpp
int sqlite3_bind_text(sqlite3_stmt*, int index, const char *value, int len,
                      void(*destructor)(void*));
```

功能：向预处理语句的指定参数位置绑定一个文本字符串

参数：

- index - 参数的序号，从1开始
- value - 要绑定的文本字符串
- len - 字符串的长度(字节)，-1表示自动计算到\0结束
- destructor：用于指定SQLite不会尝试释放该字符串
  - SQLITE_STATIC：SQLite不会尝试释放该字符串
  - SQLITE_TRANSIENT：SQLite会制作该字符串的副本

返回值：SQLITE_OK表示绑定成功，否则为错误码

```cpp
int sqlite3_exec(sqlite3* db, const char *sql,
                 int (*callback)(void*, int, char**, char**), void *arg, char **errmsg);
```

功能：执行一条或多条SQL语句，适合不返回数据的语句，如CREATE、INSERT

参数：

- db - 数据库连接句柄
- sql - 要执行的SQL语句字符串
- callback：回调函数，每有一条结果记录就产生一次调用，可以设置为NULL
- arg：传递给回调函数的第一个参数
- errmsg：只想错误信息的指针。必须用sqlite3_free释放

返回值：执行成功返回SQLITE_OK，否则返回错误码

```cpp
int sqlite3_step(sqlite3_stmt *pStmt);
```

功能：用于逐步执行预处理语句。一般用于执行查询和更新操作，并返回执行结果。对于查询操作，sqlite3_step 需要多次调用，直到返回 SQLITE_DONE

参数：pstmt - 预处理语句句柄

返回值：

- SQLITE_ROW - 表示有一行结果数据就绪
- SQLITE_DONE - 表示语句执行完成
- 否则表示有其他错误

```cpp
const unsigned char *sqlite3_column_text(sqlite3_stmt*, int iCol);
```

功能：从当前结果行中，以const unsigned cht*的形式获取制定文本列的值

参数：iCol表示列号，从0开始

返回值：指向文本数据的指针。

```cpp
int sqlite3_finalize(sqlite3_stmt *pStmt);
```

功能：销毁预处理语句对象，释放所有关联资源

参数：pstmt - 要销毁的预处理语句句柄

返回值：成功返回SQLITE_OK，否则为错误码

### 4.1 C/C++ 数据库操作流程

1. `sqlite3_open()`：打开数据库，获得连接句柄 `sqlite3*`。
2. `sqlite3_prepare_v2()`：编译 SQL，获得预处理语句 `sqlite3_stmt*`。
3. `sqlite3_bind_*()`：为 SQL 占位符 `?` 绑定参数，参数编号从 **1** 开始。
4. `sqlite3_step()`：执行语句；`SQLITE_ROW` 表示获得一行，`SQLITE_DONE` 表示执行完成。
5. `sqlite3_column_*()`：在 `SQLITE_ROW` 状态读取结果行，列编号从 **0** 开始。
6. `sqlite3_finalize()`：释放预处理语句。
7. `sqlite3_close()`：关闭数据库连接。

- `sqlite3_exec()`：适合直接执行 SQL；可通过回调接收查询结果。
- `sqlite3_prepare_v2()` + `sqlite3_bind_*()`：分离 SQL 结构与参数数据，适合带参数的增删改查。

## 5. 使用案例

下面持久化存储学生信息，学生基本信息包括学号、姓名、性别、年龄、学分

| 字段       | SQLite 类型             | 含义     |
| ---------- | ----------------------- | -------- |
| `id`     | `INTEGER PRIMARY KEY` | 学号标识 |
| `name`   | `TEXT NOT NULL`       | 姓名     |
| `gender` | `TEXT NOT NULL`       | 性别     |
| `age`    | `INTEGER NOT NULL`    | 年龄     |
| `gap`    | `REAL NOT NULL`       | 学分     |

`StudentDB` 封装数据库连接及增删改查操作；`initDataBase()` 负责建表，`sqlite3_finalize()` 负责释放语句资源。

```cpp
#include <iostream>
using namespace std;
#include <sqlite3.h>

struct StudentInfo{
    StudentInfo(const std::string& name, const std::string& gender, int age,
                double gap)
        :name(name), gender(gender), age(age), gap(gap)
    {}
    std::string name;
    std::string gender;
    int age;
    double gap;
};

class StudentDB{
public:
    StudentDB(const std::string& dbName){
        // 创建数据库
        if(SQLITE_OK != sqlite3_open("student.db", &_db)){
            std::cout << "open student.db failed" <<std::endl;
            exit(-1);
        }
        // 对数据库初始化 - 创建学生表
        if(!initDataBase()){
            std::cout << "init database failed" << std::endl;
            exit(-1);
        }
    }
    ~StudentDB(){
        if(_db){
            sqlite3_close(_db);
            _db = nullptr;
        }
    }
    bool insertStudentInfo(const StudentInfo& info){
        // 构建sql语句
        std::string sql = "INSERT INTO student(name, gender, age, gap)"
                          "VALUES(?, ?, ?, ?);";
        // 准备SQL语句
        sqlite3_stmt* stmt = nullptr;
        if(SQLITE_OK != sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt,
                                           nullptr)){
            std::cout << "prepare sql failed: " << sqlite3_errmsg(_db) <<
                std::endl;
            return false;
        }
        // 绑定参数
        sqlite3_bind_text(stmt, 1, info.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, info.gender.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, info.age);
        sqlite3_bind_double(stmt, 4, info.gap);

        // 执行SQL语句，检查是否执行成功
        if(SQLITE_DONE != sqlite3_step(stmt)){
            std::cout << "execute sql failed: " << sqlite3_errmsg(_db) <<
                std::endl;
            sqlite3_finalize(stmt);
            return false;
        }

        // 执行完sql，释放stmt
        sqlite3_finalize(stmt);
        std::cout<<"insert student info success"<<std::endl;
        return true;
    }
    bool queryStudentInfo(const std::string& name){
        // 构建sql语句
        std::string sql = "SELECT * FROM student WHERE name = ?";

        // 准备SQL语句
        sqlite3_stmt* stmt = nullptr;
        if(SQLITE_OK != sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt,
                                           nullptr)){
            std::cout << "prepare sql failed: " << sqlite3_errmsg(_db) <<
                std::endl;
            return false;
        }

        // 绑定参数
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);

        // 执行SQL语句，检查是否执行成功
        int result = sqlite3_step(stmt);
        if(SQLITE_ROW != result && SQLITE_DONE != result){
            std::cout << "execute sql failed: " << sqlite3_errmsg(_db) <<
                std::endl;
            sqlite3_finalize(stmt);
            return false;
        }

        // 执行完sql，释放stmt
        sqlite3_finalize(stmt);
        std::cout<<"query student info success"<<std::endl;
        return true;
    }

    bool queryAllStudentInfo(){
        // 构建sql语句
        std::string sql = "SELECT * FROM student";

        // 准备SQL语句
        sqlite3_stmt* stmt = nullptr;
        if(SQLITE_OK != sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt,
                                           nullptr)){
            std::cout << "prepare sql failed: " << sqlite3_errmsg(_db) <<
                std::endl;
            return false;
        }

        // 执行SQL语句，检查是否执行成功
        int result = sqlite3_step(stmt);
        if(SQLITE_ROW != result && SQLITE_DONE != result){
            std::cout << "execute sql failed: " << sqlite3_errmsg(_db) <<
                std::endl;
            sqlite3_finalize(stmt);
            return false;
        }

        std::cout<<"-------------------student table info:-----------------"
                 <<std::endl;
        // 遍历结果集
        while(SQLITE_ROW == result){
            // 表结构是 id, name, gender, age, gap，所以列索引从1开始获取我们需要的值
            std::string name(reinterpret_cast<const char*>
                             (sqlite3_column_text(stmt, 1)));
            std::string gender(reinterpret_cast<const char*>
                               (sqlite3_column_text(stmt, 2)));
            int age = sqlite3_column_int(stmt, 3);
            double gap = sqlite3_column_double(stmt, 4);
            std::cout << "name: " << name << ", gender: " << gender << ", age: "
                      << age << ", gap: " << gap << std::endl;
            result = sqlite3_step(stmt);
        }
        // 检查是否还有更多的行
        if(SQLITE_DONE != result){
            std::cout << "execute sql failed: " << sqlite3_errmsg(_db) <<
                std::endl;
            return false;
        }
        std::cout<<"-------------------student table info end-----------------"
                 <<std::endl;

        // 执行完sql，释放stmt
        sqlite3_finalize(stmt);
        std::cout<<"query all student info success"<<std::endl;
        return true;
    }

    // 修改学生信息
    bool updateStudentInfo(const std::string& name, const StudentInfo& info){
        // 构建sql语句
        std::string sql = "UPDATE student SET name = ?, gender = ?, age = ?, gap = ? WHERE name = ?";
        // 准备SQL语句
        sqlite3_stmt* stmt = nullptr;
        if(SQLITE_OK != sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt,
                                           nullptr)){
            std::cout << "prepare sql failed: " << sqlite3_errmsg(_db) <<
                std::endl;
            return false;
        }

        // 绑定参数
        sqlite3_bind_text(stmt, 1, info.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, info.gender.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, info.age);
        sqlite3_bind_double(stmt, 4, info.gap);
        sqlite3_bind_text(stmt, 5, name.c_str(), -1, SQLITE_TRANSIENT);
        // 执行SQL语句，检查是否执行成功
        if(SQLITE_DONE != sqlite3_step(stmt)){
            std::cout << "execute sql failed: " << sqlite3_errmsg(_db) <<
                std::endl;
            sqlite3_finalize(stmt);
            return false;
        }

        // 执行完sql，释放stmt
        sqlite3_finalize(stmt);
        std::cout<<"update student info success"<<std::endl;
        return true;
    }

    // 删除学生信息
    bool deleteStudentInfo(const std::string& name){
        // 构建sql语句
        std::string sql = "DELETE FROM student WHERE name = ?";
        // 准备SQL语句
        sqlite3_stmt* stmt = nullptr;
        if(SQLITE_OK != sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt,
                                           nullptr)){
            std::cout << "prepare sql failed: " << sqlite3_errmsg(_db) <<
                std::endl;
            return false;
        }
        // 绑定参数
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        // 执行SQL语句，检查是否执行成功
        if(SQLITE_DONE != sqlite3_step(stmt)){
            std::cout << "execute sql failed: " << sqlite3_errmsg(_db) <<
                std::endl;
            sqlite3_finalize(stmt);
            return false;
        }

        // 执行完sql，释放stmt
        sqlite3_finalize(stmt);
        std::cout<<"delete student info success"<<std::endl;
        return true;
    }

private:
    bool initDataBase(){
        // 创建数据库表
        const std::string studentTab = "CREATE TABLE IF NOT EXISTS student( "
                                       "id INTEGER PRIMARY KEY, "
                                       "name TEXT NOT NULL, "
                                       "gender TEXT NOT NULL, "
                                       "age INTEGER NOT NULL, "
                                       "gap REAL NOT NULL)";
        char* errMsg = nullptr;
        if(SQLITE_OK != sqlite3_exec(_db, studentTab.c_str(), nullptr,
                                     nullptr, &errMsg)){
            std::cout << "create table failed: " << errMsg << std::endl;
            sqlite3_free(errMsg);
            return false;
        }
        std::cout<<"create studentTab success"<<std::endl;
        return true;
    }

private:
    sqlite3* _db;
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
```

运行程序，会在当前目录下生成一个student.db的数据库文件，里面存储表结构和数据信息。

```bash
g++ testSQLite.cpp -o testsqlite3 -lsqlite3
./testsqlite3
```

```text
create studentTab success
insert student info success
insert student info success
insert student info success
insert student info success
-------------------student table info:-----------------
name: 张三, gender: 男, age: 18, gap: 3.5
name: 李四, gender: 男, age: 19, gap: 3.8
name: 王五, gender: 男, age: 20, gap: 2.9
name: 赵六, gender: 女, age: 21, gap: 3
-------------------student table info end-----------------
query all student info success
update student info success
-------------------student table info:-----------------
name: 张三, gender: 男, age: 20, gap: 3.5
name: 李四, gender: 男, age: 19, gap: 3.8
name: 王五, gender: 男, age: 20, gap: 2.9
name: 赵六, gender: 女, age: 21, gap: 3
-------------------student table info end-----------------
query all student info success
delete student info success
-------------------student table info:-----------------
name: 李四, gender: 男, age: 19, gap: 3.8
name: 王五, gender: 男, age: 20, gap: 2.9
name: 赵六, gender: 女, age: 21, gap: 3
-------------------student table info end-----------------
query all student info success
```
