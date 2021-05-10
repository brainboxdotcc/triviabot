/************************************************************************************
 * 
 * TriviaBot, The trivia bot for discord based on Fruitloopy Trivia for ChatSpike IRC
 *
 * Copyright 2004 Craig Edwards <support@brainbox.cc>
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

#include <dpp/dpp.h>
#include <fmt/format.h>
#include <dpp/nlohmann/json.hpp>
#include <sporks/modules.h>
#include <string>
#include <cstdint>
#include <fstream>
#include <streambuf>
#include <sporks/stringops.h>
#include "trivia.h"
#include <sys/stat.h>
#include <sys/types.h>

using json = nlohmann::json;

time_t get_mtime(const char *path)
{
	struct stat statbuf;
	if (stat(path, &statbuf) == -1) {
		return 0;
	}
	return statbuf.st_mtime;
}

// Check for updated lang.json and attempt to reload it. if reloading fails, dont try again until its 
// modified a second time. Log errors to log file.
void TriviaModule::CheckLangReload()
{
	if (get_mtime("../lang.json") > lastlang) {
		std::lock_guard<std::mutex> cmd_list_lock(lang_mutex);
		lastlang = get_mtime("../lang.json");
		std::ifstream langfile("../lang.json");
		json* newlang = new json();
		try {
			json* oldlang = this->lang;
	
			// Parse updated contents
			langfile >> *newlang;
	
			this->lang = newlang;
			delete oldlang;
		}
		catch (const std::exception &e) {
			bot->core->log(dpp::ll_error, fmt::format("Error in lang.json: ", e.what()));
			// Delete attempted new language file to prevent memory leaks
			delete newlang;
		}

	}
}

