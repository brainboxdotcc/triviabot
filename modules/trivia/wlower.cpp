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

#include <iostream>
#include <string>
#include <locale>
#include <codecvt>
#include "wlower.h"

/* Lowercases a utf8 string using unicode case-folding rules.
 * Note the special flag 'spanish_hack' that allows for lazy grammar in spanish only.
 * Lazy grammar lets a Spanish speaker answer 'papa' when they meant 'papá'
 * (The first means 'potatoes' and the second means 'father'!) This happens a lot amongst
 * younger players, so we allow this as a way to make things friendlier.
 */
std::string utf8lower(const std::string &input, bool spanish_hack)
{
	std::setlocale(LC_CTYPE, "en_US.UTF-8"); // the locale will be the UTF-8 enabled English

	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;

	std::wstring str = converter.from_bytes(input.c_str());
	for (std::wstring::iterator it = str.begin(); it != str.end(); ++it) {
		*it = towlower(*it);
		if (spanish_hack) {
			/* spanish_hack grammar rules on vowels with accents only, AEIOU! */
			if (*it == L'á') {
				*it = L'a';
			} else if (*it == L'é') {
				*it = L'e';
			} else if (*it == L'ó') {
				*it = L'o';
			} else if (*it == L'ú' || *it == L'ü') {
				*it = L'u';
			}
		}
	}
	return converter.to_bytes(str);
}
