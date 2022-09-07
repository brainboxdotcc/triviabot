/************************************************************************************
 * 
 * TriviaBot, the Discord Quiz Bot with over 80,000 questions!
 *
 * Copyright 2004 Craig Edwards <support@sporks.gg>
 *
 * Core based on Sporks, the Learning Discord Bot, Craig Edwards (c) 2019.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ************************************************************************************/

#include <fmt/format.h>
#include <sporks/database.h>
#include <iostream>
#include <mutex>
#include <sstream>
#include <chrono>
#include <thread>
#include <queue>
#include <array>
#include <dpp/dpp.h>

#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <cppconn/prepared_statement.h>

/* Initial connection string for the database.
 * Note that mariadb and mysql have different syntax (this is one of the few sitations where they differ).
 * Both set the maximum query time to 3 seconds, which is far more than needed but acts as a safety net
 * against any bad programming or runaway queries.
 */
#ifdef MARIADB_VERSION_ID
	#define CONNECT_STRING "SET NAMES utf8mb4, @@SESSION.max_statement_time=3000"
#else
	#define CONNECT_STRING "SET NAMES utf8mb4, @@SESSION.max_execution_time=3000"
#endif

using namespace std::literals;

thread_local static bool is_stringlike = false;

/**
 * @brief Used for type debduction to determine if a value is string-like or not
 * within a std::visit.
 */
namespace std {
	std::string to_string(const std::string& s) {
		is_stringlike = true;
		return s;
	};
};

namespace db {

	/* Represents a stored background query for the queue */
	struct background_query {
		/* Format string */
		std::string format;
		/* Unescaped parameters */
		paramlist parameters;
	};

	/* Number of connections in the foreground thread pool.
	 * REMEMBER NOT TO GET TOO GREEDY!
	 * This will be multiplied up by how many clusters are running!
	 */
	const size_t POOL_SIZE = 10;

	/* Current connection index for the foreground pool,
	 * used for round-robin of the queries across connections.
	 */
	size_t curr_index = 0;

	/* Foreground connection pool */
	sqlconn connections[POOL_SIZE];

	/* Background connection */
	sqlconn bg_connection;

	/* Total processed query counter */
	uint64_t processed = 0;
	
	/* Total errored queries counter */
	uint64_t errored = 0;

	/* Protects the background_queries queue from concurrent access */
	std::mutex b_db_mutex;

	/* Queue of background queries to be executed */
	std::queue<background_query> background_queries;

	/* Thread upon which background queries will execute */
	std::thread* background_thread = nullptr;

	/* spdlog logger */
	dpp::cluster* log;

	value::value(const std::string& v) : stored_value(v)
	{
	}

	value::operator std::string() {
		return this->stored_value;
	}

	value& value::operator=(const std::string& v) {
		this->stored_value = v; return *this;
	}

	bool value::operator==(const std::string& v) const {
		return this->stored_value == v;
	}

	dpp::snowflake value::getSnowflake() const {
		return from_string<uint64_t>(this->stored_value, std::dec);
	}

	int64_t value::getInt() const {
		return from_string<int64_t>(this->stored_value, std::dec);
	}

	bool value::getBool() const {
		return from_string<int>(this->stored_value, std::dec);
	}

	std::string value::getString() const {
		return this->stored_value;
	}

	uint64_t value::getUInt() const {
		return from_string<uint64_t>(this->stored_value, std::dec);
	}

	resultset real_query(sqlconn &conn, const std::string &format, const paramlist &parameters);

	statistics get_stats() {
		statistics stats;
		for (size_t cc = 0; cc < POOL_SIZE; ++cc) {
			sqlconn& c = connections[cc];
			connection_info ci;
			ci.ready = !c.busy;
			ci.queries_errored = c.queries_errored;
			ci.queries_processed = c.queries_processed;
			ci.busy_time = c.busy_time;
			ci.avg_query_length = c.avg_query_length;
			ci.background = false;
			stats.connections.push_back(ci);
		}
		stats.queries_processed = processed;
		stats.queries_errored = errored;
		{
			std::lock_guard<std::mutex> db_lock(b_db_mutex);
			stats.bg_queue_length = background_queries.size();
		}

		connection_info ci;
		ci.ready = !bg_connection.busy;
		ci.queries_errored = bg_connection.queries_errored;
		ci.queries_processed = bg_connection.queries_processed;
		ci.busy_time = bg_connection.busy_time;
		ci.avg_query_length = bg_connection.avg_query_length;
		ci.background = true;
		stats.connections.push_back(ci);
		
		return stats;
	}

	/**
	 * @brief This thread monitors for new background queries in the queue, and
	 * if there are any, executes them in order.
	 */
	void bgthread() {
		dpp::utility::set_thread_name("db/background");
		while (true) {
			std::queue<background_query> bg_copy;
			{
				std::lock_guard<std::mutex> db_lock(b_db_mutex);
				bg_copy = {};
				while (background_queries.size()) {
					bg_copy.emplace(background_queries.front());
					background_queries.pop();
				}
			}
			if (bg_copy.empty()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(250));
				continue;
			}
			while (!bg_copy.empty()) {
				background_query q = bg_copy.front();
				processed++;
				bg_connection.queries_processed++;
				real_query(bg_connection, q.format, q.parameters);
				bg_copy.pop();
			}
		}
	}

	/**
	 * Connect to mysql database, returns false if there was an error.
	 */
	bool connect(dpp::cluster* logger, const std::string &host, const std::string &user, const std::string &pass, const std::string &db, int port) {
		std::lock_guard<std::mutex> db_lock2(b_db_mutex);

		sql::Driver * driver = get_driver_instance();
		sql::ConnectOptionsMap opts = {
			{ "hostName", fmt::format("tcp://{}", host).c_str() },
			{ "userName", user.c_str() },
			{ "password", pass.c_str() },
			{ "schema", db.c_str() },
			{ "port", 3306 },
			{ "SET_CHARSET_NAME", "utf8mb4" },
			{ "INIT_COMMAND", CONNECT_STRING },
		};

		log = logger;
		bool failed = false;
		try {
			for (size_t i = 0; i < POOL_SIZE; ++i) {
				connections[i].connection = driver->connect(opts);
			}
			bg_connection.connection = driver->connect(opts);
			background_thread = new std::thread(bgthread);
		}
		catch (const std::exception &e) {
			logger->log(dpp::ll_error, e.what());
			return false;
		}
		return !failed;
	}

	/**
	 * Disconnect from mysql database, for now always returns true.
	 * If there's an error, there isn't much we can do about it anyway.
	 */
	bool close() {
		for (size_t i = 0; i < POOL_SIZE; ++i) {
			std::lock_guard<std::mutex> db_lock(connections[i].mutex);
			connections[i].connection->close();
		}
		bg_connection.connection->close();
		return true;
	}

	void backgroundquery(const std::string &format, const paramlist &parameters) {
		std::lock_guard<std::mutex> db_lock(b_db_mutex);
		background_queries.emplace(background_query{ format, parameters });
	}

	/**
	 * Run a mysql query, with automatic escaping of parameters to prevent SQL injection.
	 * The parameters given should be a vector of strings. You can instantiate this using "{}".
	 * For example: db::query("UPDATE foo SET bar = ? WHERE id = ?", {"baz", "3"});
	 * Returns a resultset of the results as rows. Avoid returning massive resultsets if you can.
	 */
	resultset query(const std::string &format, const paramlist &parameters) {
		size_t c = 0;
		size_t tries = 0;
		if (curr_index >= POOL_SIZE) {
			curr_index = c = 0;
		} else {
			c = curr_index++;
		}
		while (tries < POOL_SIZE + 1 && connections[c].busy) {
			log->log(dpp::ll_debug, fmt::format("Skipped busy connection {}", c));
			c = ++curr_index;
			if (curr_index >= POOL_SIZE) {
				c = curr_index = 0;
			}
			tries++;
		}
		processed++;
		connections[c].queries_processed++;
		return real_query(connections[c], format, parameters);
	}

	resultset real_query(sqlconn& conn, const std::string &format, const paramlist &parameters) {

		std::vector<std::string> escaped_parameters;

		resultset rv;

		std::unique_ptr<sql::PreparedStatement> pstmt;
		std::unique_ptr<sql::ResultSet> res;

		double busy_start = dpp::utility::time_f();
		try
		{
			std::lock_guard<std::mutex> db_lock(conn.mutex);
			conn.busy = true;

			if (!conn.connection->isValid() || conn.connection->isClosed()) {
				conn.connection->reconnect();
			}

			pstmt.reset(conn.connection->prepareStatement(format.c_str()));

			/**
			 * Build prepared statement parameters via std::visit
			 */
			size_t index = 1;
			for (const auto& param : parameters) {
				std::visit([&pstmt, &index](const auto &p) {
					/**
					 * Type debduction
					 * We call std::to_string() on the auto value, and for anything numeric it
					 * passes through the C++11 implementations in <string>. For an actual string,
					 * it passes through our dummy implementation, which sets a thread_local bool
					 * is_stringlike. The bool can then be used by this loop to determine if we should
					 * add the value to the prepared statement as BigInt or string. For anything numeric,
					 * as a final check before adding it as BigInt we check if it contains a '.', if it
					 * does we set its type as String (not double) - this helps avoid any rounding issues.
					 */
					is_stringlike = false;
					std::string v = std::to_string(p);
					if (is_stringlike) {
						pstmt->setString(index++, v);
					} else {
						if (v.find('.') != std::string::npos) {
							pstmt->setString(index++, v);
						} else {
							pstmt->setBigInt(index++, v);
						}
					}
				}, param);
			}

			/**
			 * Execute the query
			 */
			bool results = pstmt->execute();
			if (!results) {
				return rv;
			}
			res.reset(pstmt->getResultSet());

			/**
			 * Get all column names for the query results
			 */
			sql::ResultSetMetaData * meta = res->getMetaData();
			std::vector<std::string> names(meta->getColumnCount());
			for (size_t n = 0; n < meta->getColumnCount(); ++n) {
				names[n] = meta->getColumnLabel(n + 1);
			}

			/**
			 * Fetch all rows for the query
			 */
			while (true) {
				while (res->next()) {
					row thisrow;
					for (const auto& n : names) {
						thisrow[n] = res->getString(n);
					}
					rv.push_back(thisrow);
				}
				if (pstmt->getMoreResults()) {
					res.reset(pstmt->getResultSet());
					continue;
				}
				break;
			}
		}
		catch (const std::exception &e) {
			/**
			 * In properly written code, this should never happen. Famous last words.
			 */
			log->log(dpp::ll_error, fmt::format("SQL Error: {} on query {}", e.what(), format));
			errored++;
			conn.queries_errored++;
		}
		conn.busy_time += (dpp::utility::time_f() - busy_start);
		conn.avg_query_length -= conn.avg_query_length / conn.queries_processed;
		conn.avg_query_length += (dpp::utility::time_f() - busy_start) / conn.queries_processed;
		conn.busy = false;

		return rv;
	}
};
