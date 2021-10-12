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
#include <spdlog/spdlog.h>

#ifdef MARIADB_VERSION_ID
	#define CONNECT_STRING "SET NAMES utf8mb4, @@SESSION.max_statement_time=3000"
#else
	#define CONNECT_STRING "SET NAMES utf8mb4, @@SESSION.max_execution_time=3000"
#endif

using namespace std::literals;

namespace db {

	const uint16_t INITIAL_THREADPOOL_SIZE = 20;

	uint64_t processed = 0;
	uint64_t errored = 0;

	std::mutex queue_mutex;
	std::vector<sqlconn*> all_conns;
	std::vector<sqlconn*> busy_conns;
	std::vector<sqlconn*> ready_conns;

	std::shared_ptr<spdlog::logger> log;

	/**
	 * Connect all connections to mysql database, returns false if there was an error on any connection.
	 */
	bool connect(std::shared_ptr<spdlog::logger> logger, const std::string &host, const std::string &user, const std::string &pass, const std::string &db, int port) {

		bool all_connected = true;
		all_conns.clear();
		ready_conns.clear();
		busy_conns.clear();

		log = logger;

		log->info("Creating {} database connections...", INITIAL_THREADPOOL_SIZE);
		for (uint16_t cid = 0; cid < INITIAL_THREADPOOL_SIZE; ++cid) {
			sqlconn* c = new sqlconn();
			std::lock_guard<std::mutex> db_lock(c->mutex);
			if (mysql_init(&(c->connection)) != nullptr) {
				mysql_options(&(c->connection), MYSQL_SET_CHARSET_NAME, "utf8mb4");
				mysql_options(&(c->connection), MYSQL_INIT_COMMAND, CONNECT_STRING);
				char reconnect = 1;
				if (mysql_options(&(c->connection), MYSQL_OPT_RECONNECT, &reconnect) == 0) {
					if (!mysql_real_connect(&(c->connection), host.c_str(), user.c_str(), pass.c_str(), db.c_str(), port, NULL, CLIENT_MULTI_RESULTS | CLIENT_MULTI_STATEMENTS)) {
						all_connected = false;
						std::string error = mysql_error(&(c->connection));
						log->error("SQL connection error: {}", error);

					} else {
						std::lock_guard<std::mutex> queue_lock(queue_mutex);
						all_conns.push_back(c);
						ready_conns.push_back(c);
						std::thread* thr = new std::thread([c] {
							c->handle();
						});
						c->thread = thr;
						thr->detach();
					}
				}
			}
		}
		return all_connected;
	}

	void sqlconn::handle() {
		while (true) {
			std::mutex mtx;
			std::unique_lock<std::mutex> lock{ mtx };			
			new_query_ready.wait(lock);

			std::string error;
			sqlquery qry;
			{
				std::lock_guard<std::mutex> db_lock(mutex);
				qry = *(queries.begin());
				queries.pop_front();
			}
				
			int result = mysql_query(&connection, qry.query.c_str());
			processed++;
			db::resultset rv;

			if (result == 0) {
				MYSQL_RES *a_res = mysql_use_result(&connection);
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
				error = mysql_error(&connection);
				errored++;
				log->error("SQL Error: {} on query {}", error, qry.query);
			}

			{
				/* Move this connection from busy back to ready */
				std::lock_guard<std::mutex> queue_lock(queue_mutex);
				auto i1 = std::find(busy_conns.begin(), busy_conns.end(), this);
				if (i1 != busy_conns.end()) {
					busy_conns.erase(i1);
				}
				auto i2 = std::find(ready_conns.begin(), ready_conns.end(), this);
				if (i2 == ready_conns.end()) {
					ready_conns.push_back(this);
				}
			}

			if (qry._callback) {
				qry._callback(rv, error);
			}
		}
	}

	/**
	 * Disconnect from mysql database, for now always returns true.
	 * If there's an error, there isn't much we can do about it anyway.
	 */
	sqlconn::~sqlconn() {
		std::lock_guard<std::mutex> db_lock(mutex);
		mysql_close(&connection);
	}

	static size_t last_conn = 0;

	sqlconn* find_free_conn() {
		if (ready_conns.empty()) {
			/* Round robin */
			sqlconn* available = all_conns[last_conn];
			auto rdy = std::find(ready_conns.begin(), ready_conns.end(), all_conns[last_conn]);
			if (rdy != ready_conns.end()) {
				ready_conns.erase(rdy);
			}
			auto bsy = std::find(busy_conns.begin(), busy_conns.end(), all_conns[last_conn]);
			if (bsy == busy_conns.end()) {
				busy_conns.push_back(all_conns[last_conn]);
			}
			last_conn++;
			if (last_conn == all_conns.size()) {
				last_conn = 0;
			}
			return available;
		} else {
			/* First non-busy connection */
			sqlconn* available = *(ready_conns.begin());
			ready_conns.erase(ready_conns.begin());
			busy_conns.push_back(available);
			return available;
		}
	}

	/**
	 * Run a mysql query, with automatic escaping of parameters to prevent SQL injection.
	 * The parameters given should be a vector of strings. You can instantiate this using "{}".
	 * For example: db::query("UPDATE foo SET bar = '?' WHERE id = '?'", {"baz", "3"});
	 * Returns a resultset of the results as rows. Avoid returning massive resultsets if you can.
	 */
	void query(const std::string &format, const paramlist &parameters, callback cb) {

		sqlconn* c = nullptr;
		{
			/* Returns null if there are no free connections */
			std::lock_guard<std::mutex> queue_lock(queue_mutex);
			c = find_free_conn();
		}

		if (!c) {
			/* There are no free threads available. Do something! */
			log->error("No SQL threads available");
			cb(db::resultset(), "No threads were available for the query");
			return;
		}

		std::vector<std::string> escaped_parameters;

		/**
		 * Escape all parameters properly from a vector of std::variant
		 */
		for (const auto& param : parameters) {
			/* Worst case scenario: Every character becomes two, plus NULL terminator*/
			std::visit([parameters, &escaped_parameters, c](const auto &p) {
				std::ostringstream v;
				v << p;
				std::string s_param(v.str());
				char out[s_param.length() * 2 + 1];
				/* Some moron thought it was a great idea for mysql_real_escape_string to return an unsigned but use -1 to indicate error.
				 * This stupid cast below is the actual recommended error check from the reference manual. Seriously stupid.
				 */
				if (mysql_real_escape_string(&(c->connection), out, s_param.c_str(), s_param.length()) != (unsigned long)-1) {
					escaped_parameters.push_back(out);
				}
			}, param);
		}

		if (parameters.size() != escaped_parameters.size()) {
			log->error("Parameter wasn't escaped; error: " + std::string(mysql_error(&(c->connection))));
			cb(db::resultset(), "Parameter wasn't escaped; error: " + std::string(mysql_error(&(c->connection))));
			return;
		}

		unsigned int param = 0;
		std::string querystring;

		/**
		 * Search and replace escaped parameters in the query string.
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

		sqlquery ready_query;
		ready_query.query = querystring;
		ready_query._callback = cb;
		{
			std::lock_guard<std::mutex> db_lock(c->mutex);
			c->queries.push_back(ready_query);
		}
		c->new_query_ready.notify_one();
	}

	statistics get_stats() {
		statistics stats;
		std::lock_guard<std::mutex> queue_lock(queue_mutex);
		for (auto c : all_conns) {
			connection_info ci;
			ci.ready = (std::find(ready_conns.begin(), ready_conns.end(), c) != ready_conns.end());
			{
				std::lock_guard<std::mutex> db_lock(c->mutex);
				ci.queue_length = c->queries.size();
			}
			stats.connections.push_back(ci);
		}
		stats.queries_processed = processed;
		stats.queries_errored = errored;
		return stats;
	}

};
