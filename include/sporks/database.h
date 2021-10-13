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
#include <spdlog/fwd.h>
#include <mysql/mysql.h>

namespace db {

	/* Definition of a row in a result set */
	typedef std::map<std::string, std::string> row;

	/* Definition of a result set, a vector of db::row */
	typedef std::vector<row> resultset;

	/* Contains a list of parameters for a query to escape prior to execution */
	typedef std::vector<std::variant<float, std::string, uint64_t, int64_t, bool, int32_t, uint32_t, double>> paramlist;


	/* Represents a MySQL connection.

	 * The system will usually spawn a set of these dictate dby the
	 * constant POOL_SIZE in database.cpp, plus one extra for background
	 * queries.
	 * 
	 * Each separate connections has a mutex which prevents concurrent
	 * calling of that connection (MySQL C api does not support this).
	 * The busy flag in this stucture is set and cleared independently
	 * of the mutex being claimed, set to true just before claiming it
	 * and cleared back to false just after releasing the mutex. This is
	 * then used to determine if the connection is immediately busy or not.
	 * Because these connections do not have queues, this is reliable,
	 * I am aware there is a tiny chance of race condition using a separate
	 * non-atomic variable like this, but the solution is the simpliest,
	 * and worst case scenario, if the race is triggered then the code
	 * just ends up waiting for a query to complete before it can run its
	 * own. As queries are short lived anyway this is not a big issue.
	 */
	struct sqlconn {
		/* Native MySQL connection struct */
		MYSQL connection;
		/* Safety mutex */
		std::mutex mutex;
		/* Queries processed (including errored) */
		uint64_t queries_processed = 0;
		/* Queries that caused an error */
		uint64_t queries_errored = 0;
		/* Average query duration (seconds) */
		double avg_query_length = 0.0;
		/* Total time spent waiting fo ror retrieving queries */
		double busy_time = 0.0;
		/* True if the connection is currently executing a query.
		 * If this is set to true attempting to call
		 * db::real_query() with this connection object will cause
		 * the mutex to wait for completion.
		 */
		bool busy = false;
	};

	/* Information on a connection for struct statistics */
	struct connection_info {
		/* Queries processed by this connection (including errors) */
		uint64_t queries_processed = 0;
		/* Queries that resulted in an error */
		uint64_t queries_errored = 0;
		/* Time spent waiting for and retrieving query results (in seconds) */
		double busy_time = 0.0;
		/* Average time taken for a query to execute (in seconds) */
		double avg_query_length = 0.0;
		/* True if this connection is ready now to serve a connection */
		bool ready = true;
		/* True if this is the background connection (there is usually one of these) */
		bool background = false;
	};

	/* Connection information */
	struct statistics {
		/* List of connections */
		std::vector<connection_info> connections;
		/* Total queries processed across all connections */
		uint64_t queries_processed = 0;
		/* Total erroring queries across all connections */
		uint64_t queries_errored = 0;
		/* Background thread queue length */
		uint64_t bg_queue_length = 0;
	};

	/* Get statistics */
	statistics get_stats();

	/* Connect all connections to the database */
	bool connect(std::shared_ptr<spdlog::logger> logger, const std::string &host, const std::string &user, const std::string &pass, const std::string &db, int port);

	/* Disconnect all connections from from the database */
	bool close();

	/* Issue a database query and return results.
	 * The query will be allocated to the first available free connection,
	 * or if no free connection is available the function will wait for one
	 * to become availabe (using a mutex).
	 */
	resultset query(const std::string &format, const paramlist &parameters);

	/* Issue a background query.
	 *
	 * When using this function we only care about two things:
	 * - It will be guaranteed to execute at some point in the near future
	 * - background queries will be executed in the order passed to this function
	 * 
	 * We do not care about:
	 * - Any kind of feedback from the function
	 * - If there is some short delay before the query gets ran
	 * 
	 * Queries will be placed into a queue and executed in-order in a separate thread,
	 * with its own connection.
	 */
	void backgroundquery(const std::string &format, const paramlist &parameters);

	/* Returns the last error string */
	const std::string& error();
};
