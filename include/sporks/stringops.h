/************************************************************************************
 * 
 * TriviaBot, the Discord Quiz Bot with over 80,000 questions!
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
#include <cstdint>
#include <string>
#include <iomanip>
#include <locale>
#include <algorithm>

/**
 * Convert a string to lowercase using tolower()
 */
template <typename T> std::basic_string<T> lowercase(const std::basic_string<T>& s)
{
    std::basic_string<T> s2 = s;
    std::transform(s2.begin(), s2.end(), s2.begin(), tolower);
    return std::move(s2);
}

/**
 * Convert a string to uppercase using toupper()
 */
template <typename T> std::basic_string<T> uppercase(const std::basic_string<T>& s)
{
    std::basic_string<T> s2 = s;
    std::transform(s2.begin(), s2.end(), s2.begin(), toupper);
    return std::move(s2);
}

/* Simple search and replace, case sensitive */
std::string ReplaceString(std::string subject, const std::string& search, const std::string& replace);

/**
 *  trim from end of string (right)
 */
inline std::string rtrim(std::string s)
{
	s.erase(s.find_last_not_of(" \t\n\r\f\v") + 1);
	return s;
}

/**
 * trim from beginning of string (left)
 */
inline std::string ltrim(std::string s)
{
	s.erase(0, s.find_first_not_of(" \t\n\r\f\v"));
	return s;
}

/**
 * trim from both ends of string (right then left)
 */
inline std::string trim(std::string s)
{
	return ltrim(rtrim(s));
}

/**
 * Add commas to a string (or dots) based on current locale server-side
 */
template<class T> std::string Comma(T value)
{
	std::string number_str = std::to_string(value);
	for (int64_t i = number_str.length() - 3; i > 0; i -= 3) {
		number_str.insert(i, ",");
	}
	return number_str;
}

/**
 * Convert any value from a string to another type using stringstream.
 * The optional second parameter indicates the format of the input string,
 * e.g. std::dec for decimal, std::hex for hex, std::oct for octal.
 */
template <typename T> T from_string(const std::string &s, std::ios_base & (*f)(std::ios_base&))
{
	T t;
	std::istringstream iss(s);
	iss >> f, iss >> t;
	return t;
}

template <uint64_t> uint64_t from_string(const std::string &s, std::ios_base & (*f)(std::ios_base&))
{
	return std::stoull(s, 0, 10);
}

template <uint32_t> uint32_t from_string(const std::string &s, std::ios_base & (*f)(std::ios_base&))
{
	return std::stoul(s, 0, 10);
}

template <int> int from_string(const std::string &s, std::ios_base & (*f)(std::ios_base&))
{
	return std::stoi(s, 0, 10);
}
