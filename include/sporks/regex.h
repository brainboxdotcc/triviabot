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
#include <string>
#include <vector>
#include <exception>

/**
 * An exception thrown by a regular expression
 */
class regex_exception : public std::exception {
public:
	std::string message;
	regex_exception(const std::string &_message);
};

/**
 * Class PCRE represents a perl compatible regular expression
 * This is internally managed by libpcre.
 * Regular expressions are compiled in the constructor and matched
 * by the Match() methods.
 */
class PCRE
{
	const char* pcre_error;
	int pcre_error_ofs;
	struct real_pcre* compiled_regex;
 public:
	/* Constructor */
	PCRE(const std::string &match, bool case_insensitive = false);
	~PCRE();
	/* Match methods */
	bool Match(const std::string &comparison);
	bool Match(const std::string &comparison, std::vector<std::string>& matches);
};

