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

#include <string>
#include <locale>
#include <codecvt>
#include <algorithm>
#include "trivia.h"
#include "wlower.h"

int TriviaModule::min3(int x, int y, int z) 
{ 
	return std::min(std::min(x, y), z); 
} 

int TriviaModule::levenstein(std::string s1, std::string s2)
{
	// Create a table to store results of subproblems
	s1 = utf8lower(s1, false);
	s2 = utf8lower(s2, false);
	std::setlocale(LC_CTYPE, "en_US.UTF-8"); // the locale will be the UTF-8 enabled English
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	std::wstring str1 = converter.from_bytes(s1.c_str());
	std::wstring str2 = converter.from_bytes(s2.c_str());
	int m = str1.length();
	int n = str2.length();
	int dp[m + 1][n + 1];

	// Fill d[][] in bottom up manner
	for (int i = 0; i <= m; i++) {
		for (int j = 0; j <= n; j++) {
			// If first string is empty, only option is to
			// insert all characters of second string
			if (i == 0)
				dp[i][j] = j; // Min. operations = j

			// If second string is empty, only option is to
			// remove all characters of second string
			else if (j == 0)
				dp[i][j] = i; // Min. operations = i

			// If last characters are same, ignore last char
			// and recur for remaining string
			else if (str1[i - 1] == str2[j - 1])
				dp[i][j] = dp[i - 1][j - 1];

			// If the last character is different, consider all
			// possibilities and find the minimum
			else
				dp[i][j] = 1 + min3(dp[i][j - 1], // Insert
					   dp[i - 1][j], // Remove
					   dp[i - 1][j - 1]); // Replace
		}
	}
	return dp[m][n]; 
}

