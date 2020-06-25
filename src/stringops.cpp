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

#include <sporks/stringops.h>
#include <locale>
#include <iostream>
#include <algorithm>

/**
 * Search and replace a string within another string, case sensitive.
 */
std::string ReplaceString(std::string subject, const std::string& search, const std::string& replace) {
	size_t pos = 0;

	std::string subject_lc = lowercase(subject);
	std::string search_lc = lowercase(search);
	std::string replace_lc = lowercase(replace);

	while((pos = subject_lc.find(search_lc, pos)) != std::string::npos) {

		 subject.replace(pos, search.length(), replace);
		 subject_lc.replace(pos, search_lc.length(), replace_lc);

		 pos += replace.length();
	}
	return subject;
}

