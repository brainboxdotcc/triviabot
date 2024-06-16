/************************************************************************************
 *
 * TriviaBot, The trivia bot for discord based on Fruitloopy Trivia for ChatSpike IRC
 *
 * Copyright 2004,2005,2020,2021,2024 Craig Edwards <support@brainbox.cc>
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

/* Make a utf8 string lower case, with a special adjustment for spanish accented vowels */
std::string utf8lower(const std::string &input, bool spanish_hack);

/* Return the number of vowels and words in a string */
std::pair<int, int> countvowel(const std::string &input);

/* Shuffle a utf8 string */
std::string utf8shuffle(const std::string &input);

/* Replace letters in a string with homoglyphs */
std::string homoglyph(const std::string &input);

/* Remove punctuation and spaces from a string */
std::string removepunct(const std::string &input);

/* Length in symbols of a utf-8 string, e.g. wlength("Riñón") == 5 */
size_t wlength(const std::string &input);

std::string wfirst(const std::string &input);

std::string wlast(const std::string &input);
