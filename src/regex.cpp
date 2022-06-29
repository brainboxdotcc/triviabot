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

#include <sporks/regex.h>
#include <pcre.h>
#include <string>
#include <vector>
#include <iostream>

/**
 * Constructor for an exception in a regex
 */
regex_exception::regex_exception(const std::string &_message) : std::exception(), message(_message) {
}

/**
 * Constructor for PCRE regular expression. Takes an expression to match against and optionally a boolean to
 * indicate if the expression should be treated as case sensitive (defaults to false).
 * Construction compiles the regex, which for a well formed regex may be more expensive than matching against a string.
 */
PCRE::PCRE(const std::string &match, bool case_insensitive) {
	compiled_regex = pcre_compile(match.c_str(), case_insensitive ? PCRE_CASELESS | PCRE_MULTILINE : PCRE_MULTILINE, &pcre_error, &pcre_error_ofs, NULL);
	if (!compiled_regex) {
		throw regex_exception(pcre_error);
	}
}

/**
 * Match regular expression against a string, returns true on match, false if no match.
 */
bool PCRE::Match(const std::string &comparison) {
	return (pcre_exec(compiled_regex, NULL, comparison.c_str(), comparison.length(), 0, 0, NULL, 0) > -1);
}

/**
 * Match regular expression against a string, and populate a referenced vector with an
 * array of matches, formatted pretty much like PHP's preg_match().
 * Returns true if at least one match was found, false if the string did not match.
 */
bool PCRE::Match(const std::string &comparison, std::vector<std::string>& matches) {
	/* Match twice: first to find out how many matches there are, and again to capture them all */
	matches.clear();
	int matcharr[90];
	int matchcount = pcre_exec(compiled_regex, NULL, comparison.c_str(), comparison.length(), 0, 0, matcharr, 90);
	if (matchcount == 0) {
		throw regex_exception("Not enough room in matcharr");
	}
	for (int i = 0; i < matchcount; ++i) {
		/* Ugly char ops */
		matches.push_back(std::string(comparison.c_str() + matcharr[2 * i], (size_t)(matcharr[2 * i + 1] - matcharr[2 * i])));
	}
	return matchcount > 0;
}

/**
 * Destructor to free compiled regular expression structure
 */
PCRE::~PCRE()
{
	/* Ugh, C libraries */
	free(compiled_regex);
}

