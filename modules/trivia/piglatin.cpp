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

#include <string>
#include <cstdint>
#include <sporks/stringops.h>
#include "state.h"
#include "trivia.h"

bool TriviaModule::isVowel(char c) 
{ 
	return (c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U' || c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'); 
}

std::string TriviaModule::piglatinword(std::string s) 
{ 
	int len = s.length();
	int index = -1; 
	for (int i = 0; i < len; i++) { 
		if (isVowel(s[i])) { 
			index = i; 
			break; 
		} 
	} 
	if (index == -1) 
		return s;

	return s.substr(index) + s.substr(0, index) + "ay"; 
}

std::string TriviaModule::piglatin(const std::string &s)
{
	std::stringstream str(s);
	std::string word;
	std::string ret;
	while ((str >> word)) {
		ret.append(piglatinword(word)).append(" ");
	}
	/* No translation needed, Pig Latin is English only */
	return std::string("Pig Latin: ") + lowercase(ret);
}

