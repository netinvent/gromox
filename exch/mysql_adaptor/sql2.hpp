#pragma once
#include <cstring>
#include <mysql.h>
#include <string>
#include <vector>
#include <gromox/database_mysql.hpp>
#include <gromox/mysql_adaptor.hpp>
#include <gromox/resource_pool.hpp>

enum {
	/* For ADDRESS_TYPE_NORMAL */
	SUB_TYPE_USER = 0,
	SUB_TYPE_ROOM,
	SUB_TYPE_EQUIPMENT,
};

class sqlconn final {
	public:
	sqlconn() = default;
	sqlconn(MYSQL *m) : m_conn(m) {}
	sqlconn(sqlconn &&o) noexcept : m_conn(o.m_conn) { o.m_conn = nullptr; }
	~sqlconn() { mysql_close(m_conn); }
	sqlconn &operator=(sqlconn &&o) noexcept;
	operator bool() const { return m_conn; }
	bool operator==(std::nullptr_t) const { return m_conn == nullptr; }
	bool operator!=(std::nullptr_t) const { return m_conn != nullptr; }
	MYSQL *get() const { return m_conn; }
	std::string quote(std::string_view);
	bool query(std::string_view);
	gromox::DB_RESULT store_result() { return mysql_store_result(m_conn); }

	protected:
	MYSQL *m_conn = nullptr;
};

struct sqlconnpool final : public gromox::resource_pool<sqlconn> {
	resource_pool::token get_wait();
};

extern std::string sql_crypt_newhash(const char *);
extern bool sql_crypt_verify(const char *, const char *);
extern gromox::errno_t mysql_adaptor_scndstore_hints(unsigned int, std::vector<sql_user> &);
extern bool mysql_adaptor_reload_config(const char *path, const char *hostid, const char *progid);
extern bool db_upgrade_check();
extern MYSQL *sql_make_conn();

extern struct mysql_adaptor_init_param g_parm;
extern sqlconnpool g_sqlconn_pool;
