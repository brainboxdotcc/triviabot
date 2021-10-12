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

/*
 * db::resultset r = db::query("SELECT * FROM infobot WHERE setby = '?'", {"SKIPDX00"});
 * int t = 0;
 * for (auto q = r.begin(); q != r.end(); ++q) {
 *	 std::cout << (t++) << ": " << (*q)["key_word"] << std::endl;
 * }
 */

namespace db {

	/* Definition of a row in a result set*/
	typedef std::map<std::string, std::string> row;
	/* Definition of a result set, a vector of maps */
	typedef std::vector<row> resultset;

	typedef std::vector<std::variant<float, std::string, uint64_t, int64_t, bool, int32_t, uint32_t, double>> paramlist;

	typedef std::function<void(resultset)> callback;

	struct sqlquery {
		std::string query = "";
		callback _callback = {};
	};

	struct sqlconn {
		MYSQL connection;
		std::mutex mutex;
		std::string error;
		std::thread* thread = nullptr;
		std::condition_variable new_query_ready;
		std::deque<sqlquery> queries;
		void handle();
		~sqlconn();
	};

	/* Connect to database */
	bool connect(const std::string &host, const std::string &user, const std::string &pass, const std::string &db, int port);
	/* Issue a database query and return results */
	void query(const std::string &format, const paramlist &parameters, callback cb = {});
};
