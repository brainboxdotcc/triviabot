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
#include <dpp/dpp.h>

/* Initial connection string for the database.
 * Note that mariadb and mysql have differnet syntax (this is one of the few sitations where they differ).
 * Both set the maximum query time to 3 seconds, which is far more than needed but acts as a safety net
 * against any bad programming or runaway queries.
 */
#ifdef MARIADB_VERSION_ID
	#define CONNECT_STRING "SET NAMES utf8mb4, @@SESSION.max_statement_time=3000"
#else
	#define CONNECT_STRING "SET NAMES utf8mb4, @@SESSION.max_execution_time=3000"
#endif

using namespace std::literals;

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

	void bgthread() {
		while (true) {
			std::queue<background_query> bg_copy;
			{
				std::lock_guard<std::mutex> db_lock(b_db_mutex);
				while (!background_queries.empty()) {
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
		log = logger;
		bool failed = false;
		for (auto & connection : connections) {
			if (mysql_init(&connection.connection) != nullptr) {
				mysql_options(&connection.connection, MYSQL_SET_CHARSET_NAME, "utf8mb4");
				mysql_options(&connection.connection, MYSQL_INIT_COMMAND, CONNECT_STRING);
				char reconnect = 1;
				if (mysql_options(&connection.connection, MYSQL_OPT_RECONNECT, &reconnect) == 0) {
					if (!mysql_real_connect(&connection.connection, host.c_str(), user.c_str(), pass.c_str(), db.c_str(), port, NULL, CLIENT_MULTI_RESULTS | CLIENT_MULTI_STATEMENTS)) {
						failed = true;
						std::cout << mysql_error(&connection.connection) << "\n";
						logger->log(dpp::ll_error, "Database connection failed " + std::string(mysql_error(&connection.connection)));
						break;
					}
				}
			}
		}

		if (mysql_init(&bg_connection.connection) != nullptr) {
			mysql_options(&bg_connection.connection, MYSQL_SET_CHARSET_NAME, "utf8mb4");
			mysql_options(&bg_connection.connection, MYSQL_INIT_COMMAND, CONNECT_STRING);
			char reconnect = 1;
			if (mysql_options(&bg_connection.connection, MYSQL_OPT_RECONNECT, &reconnect) == 0) {
				background_thread = new std::thread(bgthread);
				return !failed && mysql_real_connect(&bg_connection.connection, host.c_str(), user.c_str(), pass.c_str(), db.c_str(), port, NULL, CLIENT_MULTI_RESULTS | CLIENT_MULTI_STATEMENTS);
			}
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
			mysql_close(&connections[i].connection);
		}
		mysql_close(&bg_connection.connection);
		return true;
	}

	void backgroundquery(const std::string &format, const paramlist &parameters) {
		std::lock_guard<std::mutex> db_lock(b_db_mutex);
		background_queries.emplace(background_query{ format, parameters });
	}

	/**
	 * Run a mysql query, with automatic escaping of parameters to prevent SQL injection.
	 * The parameters given should be a vector of strings. You can instantiate this using "{}".
	 * For example: db::query("UPDATE foo SET bar = '?' WHERE id = '?'", {"baz", "3"});
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
				if (mysql_real_escape_string(&conn.connection, out, s_param.c_str(), s_param.length()) != (unsigned long)-1) {
					escaped_parameters.push_back(out);
				}
			}, param);
		}

		if (parameters.size() != escaped_parameters.size()) {
			log->log(dpp::ll_error, "Parameter wasn't escaped: " + std::string(mysql_error(&conn.connection)));
			errored++;
			conn.queries_errored++;
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
		for (char v : format) {
			if (v == '?' && escaped_parameters.size() >= param + 1) {
				querystring.append(escaped_parameters[param]);
				if (param != escaped_parameters.size() - 1) {
					param++;
				}
			} else {
				querystring += v;
			}
		}

		{
			/**
			 * One DB handle can't query the database from multiple threads at the same time.
			 * To prevent corruption of results, put a lock guard on queries.
			 */
			conn.busy = true;
			double busy_start = dpp::utility::time_f();
			std::lock_guard<std::mutex> db_lock(conn.mutex);
			int result = mysql_query(&conn.connection, querystring.c_str());
			/**
			 * On successful query collate results into a std::map
			 */
			if (result == 0) {
				MYSQL_RES *a_res = mysql_use_result(&conn.connection);
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
				log->log(dpp::ll_error, fmt::format("SQL Error: {} on query {}", mysql_error(&conn.connection), querystring));
				errored++;
				conn.queries_errored++;
			}
			conn.busy_time += (dpp::utility::time_f() - busy_start);
			conn.avg_query_length -= conn.avg_query_length / conn.queries_processed;
			conn.avg_query_length += (dpp::utility::time_f() - busy_start) / conn.queries_processed;
			conn.busy = false;
		}
		return rv;
	}
};
