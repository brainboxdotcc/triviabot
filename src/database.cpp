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
#include <mysql/mysql.h>
#include <iostream>
#include <mutex>
#include <sstream>
#include <chrono>
#include <thread>
#include <queue>

#ifdef MARIADB_VERSION_ID
	#define CONNECT_STRING "SET NAMES utf8mb4, @@SESSION.max_statement_time=3000"
#else
	#define CONNECT_STRING "SET NAMES utf8mb4, @@SESSION.max_execution_time=3000"
#endif

using namespace std::literals;

namespace db {

	double time_f() {
		using namespace std::chrono;
		auto tp = system_clock::now() + 0ns;
		return tp.time_since_epoch().count() / 1000000000.0;
	}

	struct background_query {
		std::string format;
		paramlist parameters;
	};

	const size_t POOL_SIZE = 10;
	size_t curr_index = 0;

	MYSQL connection[POOL_SIZE];
	std::mutex db_mutex[POOL_SIZE];

	MYSQL background_connection;
	std::mutex b_db_mutex;
	std::mutex b_db_query_mutex;

	std::string _error;
	std::queue<background_query> background_queries;
	std::thread* background_thread;

	resultset real_query(std::mutex& mutex, MYSQL &conn, const std::string &format, const paramlist &parameters);

	void bgthread() {
		while (true) {
			std::queue<background_query> bg_copy;
			{
				std::lock_guard<std::mutex> db_lock(b_db_mutex);
				while (background_queries.size()) {
					bg_copy.push(background_queries.front());
					background_queries.pop();
				}
			}
			if (bg_copy.empty()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(250));
				continue;
			}
			while (!bg_copy.empty()) {
				background_query q = bg_copy.front();
				real_query(b_db_query_mutex, background_connection, q.format, q.parameters);
				bg_copy.pop();
			}
		}
	}

	/**
	 * Connect to mysql database, returns false if there was an error.
	 */
	bool connect(const std::string &host, const std::string &user, const std::string &pass, const std::string &db, int port) {
		std::lock_guard<std::mutex> db_lock2(b_db_mutex);
		bool failed = false;
		for (size_t i = 0; i < POOL_SIZE; ++i) {
			if (mysql_init(&connection[i]) != nullptr) {
				mysql_options(&connection[i], MYSQL_SET_CHARSET_NAME, "utf8mb4");
				mysql_options(&connection[i], MYSQL_INIT_COMMAND, CONNECT_STRING);
				char reconnect = 1;
				if (mysql_options(&connection[i], MYSQL_OPT_RECONNECT, &reconnect) == 0) {
					if (!mysql_real_connect(&connection[i], host.c_str(), user.c_str(), pass.c_str(), db.c_str(), port, NULL, CLIENT_MULTI_RESULTS | CLIENT_MULTI_STATEMENTS)) {
						failed = true;
						break;
					}
				}
			}
		}

		if (mysql_init(&background_connection) != nullptr) {
			mysql_options(&background_connection, MYSQL_SET_CHARSET_NAME, "utf8mb4");
			mysql_options(&background_connection, MYSQL_INIT_COMMAND, CONNECT_STRING);
			char reconnect = 1;
			if (mysql_options(&background_connection, MYSQL_OPT_RECONNECT, &reconnect) == 0) {
				background_thread = new std::thread(bgthread);
				return !failed && mysql_real_connect(&background_connection, host.c_str(), user.c_str(), pass.c_str(), db.c_str(), port, NULL, CLIENT_MULTI_RESULTS | CLIENT_MULTI_STATEMENTS);
			}
		}

		_error = "db::connect() failed";
		return !failed;
	}

	/**
	 * Disconnect from mysql database, for now always returns true.
	 * If there's an error, there isn't much we can do about it anyway.
	 */
	bool close() {
		for (size_t i = 0; i < POOL_SIZE; ++i) {
			std::lock_guard<std::mutex> db_lock(db_mutex[i]);
			mysql_close(&connection[i]);
		}
		mysql_close(&background_connection);
		return true;
	}

	const std::string& error() {
		return _error;
	}

	void backgroundquery(const std::string &format, const paramlist &parameters) {
		std::lock_guard<std::mutex> db_lock(b_db_mutex);
		background_query bq;
		bq.format = format;
		bq.parameters = parameters;
		background_queries.push(bq);
	}

	/**
	 * Run a mysql query, with automatic escaping of parameters to prevent SQL injection.
	 * The parameters given should be a vector of strings. You can instantiate this using "{}".
	 * For example: db::query("UPDATE foo SET bar = '?' WHERE id = '?'", {"baz", "3"});
	 * Returns a resultset of the results as rows. Avoid returning massive resultsets if you can.
	 */
	resultset query(const std::string &format, const paramlist &parameters) {
		size_t c = 0;
		if (curr_index >= POOL_SIZE) {
			curr_index = 0;
			c = 0;
		} else {
			c = curr_index++;
		}
		return real_query(db_mutex[c], connection[c], format, parameters);
	}

	resultset real_query(std::mutex& mutex, MYSQL &conn, const std::string &format, const paramlist &parameters) {

		std::vector<std::string> escaped_parameters;

		resultset rv;

		/**
		 * Escape all parameters properly from a vector of std::variant
		 */
		for (const auto& param : parameters) {
			/* Worst case scenario: Every character becomes two, plus NULL terminator*/
			std::visit([parameters, &escaped_parameters, &conn](const auto &p) {
				std::ostringstream v;
				v << p;
				std::string s_param(v.str());
				char out[s_param.length() * 2 + 1];
				/* Some moron thought it was a great idea for mysql_real_escape_string to return an unsigned but use -1 to indicate error.
				 * This stupid cast below is the actual recommended error check from the reference manual. Seriously stupid.
				 */
				if (mysql_real_escape_string(&conn, out, s_param.c_str(), s_param.length()) != (unsigned long)-1) {
					escaped_parameters.push_back(out);
				}
			}, param);
		}

		if (parameters.size() != escaped_parameters.size()) {
			_error = "Parameter wasn't escaped; error: " + std::string(mysql_error(&conn));
			return rv;
		}

		unsigned int param = 0;
		std::string querystring;

		/**
		 * Search and replace escaped parameters in the query string.
		 *
		 * TODO: Really, I should use a cached query and the built in parameterisation for this.
		 *       It would scale a lot better.
		 */
		for (auto v = format.begin(); v != format.end(); ++v) {
			if (*v == '?' && escaped_parameters.size() >= param + 1) {
				querystring.append(escaped_parameters[param]);
				if (param != escaped_parameters.size() - 1) {
					param++;
				}
			} else {
				querystring += *v;
			}
		}

		{
			/**
			 * One DB handle can't query the database from multiple threads at the same time.
			 * To prevent corruption of results, put a lock guard on queries.
			 */
			std::lock_guard<std::mutex> db_lock(mutex);
			int result = mysql_query(&conn, querystring.c_str());

			/**
			 * On successful query collate results into a std::map
			 */
			if (result == 0) {
				MYSQL_RES *a_res = mysql_use_result(&conn);
				if (a_res) {
					MYSQL_ROW a_row;
					while ((a_row = mysql_fetch_row(a_res))) {
						MYSQL_FIELD *fields = mysql_fetch_fields(a_res);
						row thisrow;
						if (mysql_num_fields(a_res) == 0) {
							break;
						}
						if (fields && mysql_num_fields(a_res)) {
							unsigned int field_count = 0;
							while (field_count < mysql_num_fields(a_res)) {
								std::string a = (fields[field_count].name ? fields[field_count].name : "");
								std::string b = (a_row[field_count] ? a_row[field_count] : "");
								thisrow[a] = b;
								field_count++;
							}
							rv.push_back(thisrow);
						}
					}
					mysql_free_result(a_res);
				}
			} else {
				/**
				 * In properly written code, this should never happen. Famous last words.
				 */
				_error = mysql_error(&conn);
				std::cout << fmt::format("SQL Error: {} on query {}", _error, querystring) << "\n";
			}
		}
		return rv;
	}
};
