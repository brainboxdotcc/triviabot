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

#pragma once
#include <vector>
#include <map>
#include <string>
#include <variant>
#include <deque>
#include <thread>
#include <functional>
#include <condition_variable>
#include <queue>
#include <mysql/mysql.h>
#include <spdlog/fwd.h>

/* Database abstraction wrapper for MySQL/MariaDB */
namespace db {

	/* Definition of a row in a result set*/
	typedef std::map<std::string, std::string> row;

	/* Definition of a result set, a vector of maps */
	typedef std::vector<row> resultset;

	/* Represents a list of parameters to escape for an SQL query */
	typedef std::vector<std::variant<float, std::string, uint64_t, int64_t, bool, int32_t, uint32_t, double>> paramlist;

	/* An asyncronous callback to an SQL query, containing the query results */
	typedef std::function<void(resultset, std::string error)> callback;

	/* Represents a parsed SQL query and callback, ready to go */
	struct sqlquery {
		std::string query = "";
		callback _callback = {};
	};

	/* Represents a MySQL connection. Each connection has a queue of queries waiting
	 * to be executed. These are stored in three lists, the 'all' queue, the 'busy'
	 * queue and the 'ready' queue.
	 * All connections always exist in the 'all' queue, and move between the 'busy'
	 * and 'ready' queue while they are executing a query regardless of queue length
	 * on that connection.
	 * When a new query is added to the queue for the connection, the condition_variable
	 * is signalled from the calling thread to tell it to check for new queries at the
	 * head of the queue.
	 */
	struct sqlconn {
		MYSQL connection;
		std::mutex mutex;
		std::thread* thread = nullptr;
		std::condition_variable new_query_ready;
		std::deque<sqlquery> queries;
		void handle();
		~sqlconn();
	};

	/* Information on a connection for struct statistics */
	struct connection_info {
		bool ready;
		size_t queue_length;
	};

	/* Connection information */
	struct statistics {
		std::vector<connection_info> connections;
		uint64_t queries_processed = 0;
		uint64_t queries_errored = 0;
	};

	/* Get statistics */
	statistics get_stats();

	/* Connect all connections to the database */
	bool connect(std::shared_ptr<spdlog::logger> log, const std::string &host, const std::string &user, const std::string &pass, const std::string &db, int port);

	/* Issue a database query and return results later to the lambda callback */
	void query(const std::string &format, const paramlist &parameters, callback cb = {});
};
